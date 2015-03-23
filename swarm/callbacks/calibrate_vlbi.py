from functools import partial
from signal import signal, SIGINT, SIG_IGN
from multiprocessing import Pool, cpu_count
from itertools import combinations_with_replacement as combos
from numpy.linalg import eig as eig
from numpy.fft import fft, ifft, fftshift
from numpy import (
    nan_to_num,
    complex128,
    errstate,
    linspace,
    float64,
    newaxis,
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
    SWARM_CHANNELS,
    SWARM_CLOCK_RATE,
)
from json_file import JSONListFile

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
        indices = fftshift(range(peak-max_lags, peak+max_lags))
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

    def __init__(self, swarm, reference=None, history_size=8, PID_coeffs=(0.75, 0.05, 0.01), outfilename="vlbi_cal.json"):
        self.reference = reference if reference is not None else swarm[0].get_input(0)
        super(CalibrateVLBI, self).__init__(swarm)
        self.skip_next = zeros(2, dtype=bool)
        self.history_size = history_size
        self.outfilename = outfilename
        self.PID_coeffs = PID_coeffs
        self.init_pool()
        self.accums = 0

    def init_history(self, first, length=8):
        hist_shape = [length,] + list(first.shape)
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
            self.logger.info('{0} : Old delay={1:>8.2f} ns,  New delay={2:>8.2f} ns,  Diff. delay={3:>8.2f} ns'.format(this_input, current_delay, updated_delay, feedback_delay))

    def feedback_phase(self, this_input, feedback_phase):
        current_phase = self.swarm.get_phase(this_input)
        updated_phase = current_phase + feedback_phase
        if not this_input==self.reference:
            self.swarm.set_phase(this_input, wrap_phase(updated_phase))
            self.logger.info('{0} : Old phase={1:>8.2f} deg, New phase={2:>8.2f} deg, Diff. phase={3:>8.2f} deg'.format(this_input, current_phase, updated_phase, feedback_phase))

    def pid_servo(self, inputs):
        p, i, d = self.PID_coeffs
        p_amplitudes, p_delays, p_phases = self.history[0]
        i_amplitudes, i_delays, i_phases = self.history.sum(axis=0)
        d_amplitudes, d_delays, d_phases = self.history[0] - self.history[1]
        pid_delays = p * p_delays + i * i_delays + d * d_delays
        pid_phases = p * p_phases + i * i_phases + d * d_phases
        if not isnan(pid_delays).any():
            map(self.feedback_delay, inputs, pid_delays)
        if not isnan(pid_phases).any():
            map(self.feedback_phase, inputs, pid_phases)

    def __call__(self, data):
        """ Callback for VLBI calibration """
        solve_chunk = 0
        solve_sideband = 'USB'
        inputs = list(inp for inp in data.inputs if inp._chk==solve_chunk)
        baselines = list(SwarmBaseline(i, j) for i, j in combos(inputs, r=2))
        corr_matrix = zeros([SWARM_CHANNELS, len(inputs), len(inputs)], dtype=complex128)
        for baseline in baselines:
            left_i = inputs.index(baseline.left)
            right_i = inputs.index(baseline.right)
            baseline_data = data[baseline][sideband]
            complex_data = baseline_data[0::2] + 1j * baseline_data[1::2]
            corr_matrix[:, left_i, right_i] = complex_data
            corr_matrix[:, right_i, left_i] = complex_data.conj()
        referenced_solver = partial(solve_cgains, ref=inputs.index(self.reference))
        norm_corr_matrix = complex_nan_to_num(corr_matrix / abs(corr_matrix))
        full_spec_gains = array(self.map(referenced_solver, norm_corr_matrix))
        delays, phases = solve_delay_phase(full_spec_gains)
        amplitudes = abs(full_spec_gains).mean(axis=0)
        cal_solution = vstack([amplitudes, delays, phases])
        for i in range(len(inputs)):
            self.logger.info('{} : Amp={:>12.2e}, Delay={:>8.2f} ns, Phase={:>8.2f} deg'.format(inputs[i], amplitudes[i], delays[i], phases[i]))
        with errstate(invalid='ignore'):
            efficiency = (abs(full_spec_gains.sum(axis=1)) / abs(full_spec_gains).sum(axis=1)).real
        self.logger.info('Avg. phasing efficiency across band={:>8.2f} +/- {:.2f}'.format(nanmean(efficiency), nanstd(efficiency)))
        if self.accums == 0:
            self.init_history(cal_solution, length=self.history_size)
        elif self.skip_next[0]:
            self.logger.info("Ignoring this integration for historical purposes")
            self.skip_next[0] = False
            self.skip_next = roll(self.skip_next, 1)
        else:
            self.append_history(cal_solution)
            self.pid_servo(inputs)
        corr_matrix_ca = nanmean(corr_matrix.reshape([64, SWARM_CHANNELS/64, len(inputs), len(inputs)]), axis=1)
        complex_gains_ca = nanmean(full_spec_gains.reshape([64, SWARM_CHANNELS/64, len(inputs)]), axis=1)
        with JSONListFile(self.outfilename) as jfile:
            jfile.append({
                    'int_time': data.int_time,
                    'int_length': data.int_length,
                    'inputs': list((inp._ant, inp._chk, inp._pol) for inp in inputs),
                    'corr_matrix_ca': corr_matrix_ca.view(float64).tolist(),
                    'complex_gains_ca': complex_gains_ca.view(float64).tolist(),
                    'cal_solution': cal_solution.tolist(),
                    'efficiency': nanmean(efficiency),
                    })
        self.accums += 1
