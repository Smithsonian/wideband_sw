from copy import copy
from datetime import datetime
from functools import partial
from signal import signal, SIGINT, SIG_IGN
from multiprocessing import Pool, cpu_count
from numpy.linalg import eig as eig
from numpy.fft import fft, ifft, fftshift
from numpy import (
    nan_to_num,
    complex128,
    errstate,
    linspace,
    float64,
    newaxis,
    hstack,
    vstack,
    array,
    zeros,
    empty,
    arange,
    around,
    nanmean,
    nanstd,
    angle,
    isnan,
    sqrt,
    roll,
    sum,
    nan,
    pi,
    )
from swarm import (
    SwarmDataCallback,
    SwarmBaseline,
    SwarmInput,
    SWARM_CHANNELS,
    SWARM_CLOCK_RATE,
    SWARM_MAPPING_POLS,
    SWARM_MAPPING_CHUNKS,
)
from .json_file import JSONListFile
from redis import StrictRedis

TODAY = datetime.utcnow()
CALFILE = TODAY.strftime('/global/logs/vlbi_cal/vlbi_cal.%j-%Y.json')
REDIS_PREFIX = 'swarm.calibrate_vlbi'

def solve_cgains(mat, ref=0):
    vals, vecs = eig(mat)
    max_val = vals.real.max()
    max_vec = vecs[:, vals.real.argmax()]
    raw_gains = max_vec * sqrt(max_val).squeeze()
    ref_gain = raw_gains[ref]
    factor = ref_gain.conj() / abs(ref_gain + 1.0)
    return raw_gains * nan_to_num(factor)

def slice_sub_lags(lags, peaks, axis, max_lags=16):
    out_shape = array(lags.shape)
    out_shape[axis] = max_lags * 2
    out_array = empty(out_shape, dtype=lags.dtype)
    for i, peak in enumerate(peaks):
        indices = fftshift(list(range(peak-max_lags, peak+max_lags)))
        out_array.T[i] = lags.T[i].take(indices, mode='wrap')
    return out_array

def solve_delay_phase(gains, chan_axis=0, sub_max_lags=16):

    # Get our nominal FFT size
    fft_size = gains.shape[chan_axis]

    # First stage, find peak in lag-space
    lags = ifft(gains, axis=chan_axis)
    peaks = abs(lags).argmax(axis=chan_axis)

    # Second stage, find peak in interpolated lag-space
    sub_lags = slice_sub_lags(lags, peaks, axis=chan_axis, max_lags=sub_max_lags)
    sub_fft = fft(sub_lags, axis=chan_axis)
    interp_lags = ifft(sub_fft, axis=chan_axis, n=fft_size)
    interp_peaks = abs(interp_lags).argmax(axis=chan_axis)

    # Translate the two peaks to a delay
    samp_time_ns = 1e9 / (SWARM_CLOCK_RATE * 8.0)
    bins = fftshift(arange(-fft_size/2, fft_size/2))
    interp_bins = fftshift(linspace(-sub_max_lags, sub_max_lags, fft_size, endpoint=False))
    delays = -samp_time_ns * (bins[peaks] + interp_bins[interp_peaks])

    # Now find the phase at the interpolated lag peak (in degrees)
    phases = (180.0/pi) * angle(interp_lags.take(interp_peaks, axis=chan_axis)).diagonal()
    return delays, phases

def complex_nan_to_num(arr):
    out_arr = arr.copy()
    out_arr[isnan(out_arr)] = 0.0j
    return out_arr

def wrap_phase(in_phase):
        return (in_phase + 180.0) % 360.0 - 180.0

class CalibrateVLBI(SwarmDataCallback):

    def __init__(self, swarm, reference=None, normed=False, single_chan=True, history_size=8, PID_coeffs=(0.75, 0.05, 0.01), outfilename=CALFILE):
        self.reference = reference if reference is not None else swarm[0][0].get_input(0)
        self.redis = StrictRedis(host='localhost', port=6379)
        super(CalibrateVLBI, self).__init__(swarm)
        self.skip_next = zeros(2, dtype=bool)
        self.history_size = history_size
        self.outfilename = outfilename
        self.single_chan = single_chan
        self.PID_coeffs = PID_coeffs
        self.normed = normed
        self.init_pool()
        self.inputs = []

    def init_history(self, first):
        hist_shape = [self.history_size,] + list(first.shape)
        self.logger.info("Initializing history to shape {0}".format(hist_shape))
        self.history = empty(hist_shape, dtype=first.dtype)
        self.history[:] = nan
        self.history[0] = first

    def append_history(self, point):
        self.history = roll(self.history, 1, axis=0)
        self.history[0] = point

    def __del__(self):
        self.term_pool()

    def init_pool(self, cpus = cpu_count()):
        self.logger.info("Starting pool of {0} workers".format(cpus))
        ignore_sigint = lambda: signal(SIGINT, SIG_IGN)
        self.process_pool = Pool(cpus, ignore_sigint)

    def term_pool(self):
        self.logger.info("Terminating the process pool")
        self.process_pool.close()
        self.process_pool.join()

    def map(self, function, iterable, *args, **kwargs):
        async_reply = self.process_pool.map_async(function, iterable, *args, **kwargs)
        return async_reply.get(0xffff)

    def feedback_delay(self, this_input, feedback_delay):
        current_delay = self.swarm.get_delay(this_input)
        updated_delay = current_delay + feedback_delay
        if feedback_delay > 1.0:
            self.skip_next[:] = True
        if not this_input==self.reference:
            self.swarm.set_delay(this_input, updated_delay)
            self.logger.debug('{0} : Old delay={1:>8.2f} ns,  New delay={2:>8.2f} ns,  Diff. delay={3:>8.2f} ns'.format(this_input, current_delay, updated_delay, feedback_delay))

    def feedback_phase(self, this_input, feedback_phase):
        current_phase = self.swarm.get_phase(this_input)
        updated_phase = current_phase + feedback_phase
        if not this_input==self.reference:
            self.swarm.set_phase(this_input, wrap_phase(updated_phase))
            self.logger.debug('{0} : Old phase={1:>8.2f} deg, New phase={2:>8.2f} deg, Diff. phase={3:>8.2f} deg'.format(this_input, current_phase, updated_phase, feedback_phase))

    def pid_servo(self, inputs):
        p, i, d = self.PID_coeffs
        p_amplitudes, p_delays, p_phases = self.history[0]
        i_amplitudes, i_delays, i_phases = self.history.sum(axis=0)
        d_amplitudes, d_delays, d_phases = self.history[0] - self.history[1]
        pid_delays = p * p_delays + i * i_delays + d * d_delays
        pid_phases = p * p_phases + i * i_phases + d * d_phases
        if not isnan(pid_delays).any():
            list(map(self.feedback_delay, inputs, pid_delays))
        if not isnan(pid_phases).any():
            list(map(self.feedback_phase, inputs, pid_phases))

    def solve_for(self, data, inputs, chunk, pol, sideband='USB'):
        baselines = list(baseline for baseline in data.baselines if ((baseline.left in inputs) and (baseline.right in inputs)))
        corr_matrix = zeros([SWARM_CHANNELS, len(inputs), len(inputs)], dtype=complex128)
        for baseline in baselines:
            left_i = inputs.index(baseline.left)
            right_i = inputs.index(baseline.right)
            baseline_data = data[baseline, sideband]
            complex_data = baseline_data[0::2] + 1j * baseline_data[1::2]
            corr_matrix[:, left_i, right_i] = complex_data
            corr_matrix[:, right_i, left_i] = complex_data.conj()
        this_reference = SwarmInput(self.reference.ant, chunk, pol)
        referenced_solver = partial(solve_cgains, ref=inputs.index(this_reference))
        if self.normed:
            with errstate(invalid='ignore'):
                corr_matrix = complex_nan_to_num(corr_matrix / abs(corr_matrix))
        if not self.single_chan:
            full_spec_gains = array(self.map(referenced_solver, corr_matrix))
            delays, phases = solve_delay_phase(full_spec_gains)
        else:
            full_spec_gains = array(self.map(referenced_solver, [corr_matrix.mean(axis=0),]))
            phases = (180.0/pi) * angle(full_spec_gains.mean(axis=0))
            delays = zeros(len(inputs))
        amplitudes = abs(full_spec_gains).mean(axis=0)
        with errstate(invalid='ignore'):
            efficiency = (abs(full_spec_gains.sum(axis=1)) / abs(full_spec_gains).sum(axis=1)).real
        return efficiency, vstack([amplitudes, delays, phases])

    def __call__(self, data):
        """ Callback for VLBI calibration """
        beam = [inp for quad in self.swarm.quads for inp in quad.get_beamformer_inputs()]
        inputs = sorted(beam, key=lambda b: b.ant + b.chk*10 + b.pol*100)
        if not inputs:
            return

        efficiencies = list([None for chunk in SWARM_MAPPING_CHUNKS] for pol in SWARM_MAPPING_POLS)
        cal_solutions = list([None for chunk in SWARM_MAPPING_CHUNKS] for pol in SWARM_MAPPING_POLS)
        for chunk in SWARM_MAPPING_CHUNKS:
            for pol in SWARM_MAPPING_POLS:
                these_inputs = list(inp for inp in inputs if (inp.chk==chunk) and (inp.pol==pol))
                eff, cal = self.solve_for(data, these_inputs, chunk, pol)
                cal_solutions[pol][chunk] = cal
                efficiencies[pol][chunk] = eff
        cal_solution_tmp = vstack([cs for cs in cal_solutions])
        cal_solution = hstack(cal_solution_tmp)
        amplitudes, delays, phases = cal_solution

        for i in range(len(inputs)):
            self.logger.debug('{} : Amp={:>12.2e}, Delay={:>8.2f} ns, Phase={:>8.2f} deg'.format(inputs[i], amplitudes[i], delays[i], phases[i]))
        for chunk in SWARM_MAPPING_CHUNKS:
            for pol in SWARM_MAPPING_POLS:
                self.logger.info('Avg. phasing efficiency across chunk {}, pol {}={:>8.2f} +/- {:.2f}'.format(chunk, pol, nanmean(efficiencies[pol][chunk]), nanstd(efficiencies[pol][chunk])))
                self.redis.setex('{0}.efficiency.chk{1}.pol{2}'.format(REDIS_PREFIX, chunk, pol), int(data.int_length*2), efficiencies[pol][chunk][0])

        if self.inputs != inputs:
            self.init_history(cal_solution)
            self.inputs = inputs
        elif self.skip_next[0]:
            self.logger.info("Ignoring this integration for historical purposes")
            self.skip_next[0] = False
            self.skip_next = roll(self.skip_next, 1)
        else:
            self.append_history(cal_solution)
            self.pid_servo(inputs)
        new_delays = list(map(self.swarm.get_delay, inputs))
        new_phases = list(map(self.swarm.get_phase, inputs))
        with JSONListFile(self.outfilename) as jfile:
            jfile.append({
                    'int_time': data.int_time,
                    'int_length': data.int_length,
                    'inputs': list((inp.ant, inp.chk, inp.pol) for inp in inputs),
                    'efficiencies': list(nanmean(eff) for pol_eff in efficiencies for eff in pol_eff),
                    'delays': new_delays, 'phases': new_phases,
                    'cal_solution': cal_solution.tolist(),
                    })
