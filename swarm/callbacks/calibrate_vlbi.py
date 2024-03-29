from copy import copy
from datetime import datetime
from functools import partial
from signal import signal, SIGINT, SIG_IGN
from multiprocessing import Pool, cpu_count
from numpy.linalg import eig as eig
from numpy.fft import fft, ifft, fftshift
from numpy import (
    nan_to_num,
    any,
    all,
    complex128,
    errstate,
    linspace,
    float64,
    newaxis,
    hstack,
    vstack,
    array,
    zeros,
    ones,
    empty,
    arange,
    around,
    nanmean,
    nanstd,
    angle,
    isnan,
    isfinite,
    sqrt,
    roll,
    sum,
    nan,
    pi,
    exp,
    )
from swarm import (
    SwarmDataCallback,
    SwarmBaseline,
    SwarmInput,
    SWARM_CHANNELS,
    SWARM_CLOCK_RATE,
    SWARM_MAPPING_POLS,
    SWARM_MAPPING_CHUNKS,
    SWARM_BENGINE_SIDEBANDS,
)
from .json_file import JSONListFile
from redis import Redis
from smax import SmaxRedisClient

TODAY = datetime.utcnow()
CALFILE = TODAY.strftime('/global/logs/vlbi_cal/vlbi_cal.%j-%Y.json')
REDIS_PREFIX = 'swarm.calibrate_vlbi'


def solve_cgains(mat, ref=0):
    if mat.shape[0] == 1:
        # If we only have one antenna, then this whole thing can be bypassed.
        return ones([1], dtype=mat.dtype)

    eig_vals, eig_vecs = eig(mat)
    soln_idx = eig_vals.real.argmax()
    gain_soln = (eig_vecs[:, soln_idx] * sqrt(eig_vals[soln_idx].real)).squeeze()

    if isfinite(gain_soln[ref]):
        # Apply reference antenna phase to the solutions
        gain_soln *= exp(-1j*angle(gain_soln[ref]))

        # Normalize to account for the fact the autos have been removed -- more
        # specifically, the real components (since the imag should be zero).
        gain_soln *= sqrt(2 * mat.size / ((2 * mat.size) - gain_soln.size))

        # Avoid numerical precision issues, explicitly zero out phaes for refant
        gain_soln.imag[ref] = 0

    return gain_soln


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
    # Modify the array in-situ
    arr[isnan(arr)] = 0.0
    return arr


def wrap_phase(in_phase):
    return (in_phase + 180.0) % 360.0 - 180.0


class CalibrateVLBI(SwarmDataCallback):

    def __init__(
        self,
        swarm,
        reference=None,
        normed=False,
        single_chan=True,
        history_size=8,
        PID_coeffs=(0.75, 0.05, 0.01),
        outfilename=CALFILE,
    ):
        if reference is None:
            self.reference = dict(list(zip(SWARM_BENGINE_SIDEBANDS,[None]*len(SWARM_BENGINE_SIDEBANDS))))
            for beam_per_quad in swarm.get_beamformer_inputs():
                for sb in SWARM_BENGINE_SIDEBANDS:
                    if self.reference[sb] == None and beam_per_quad[sb] is not None:
                        self.reference[sb] = beam_per_quad[sb][0]
        else:
            self.reference = dict(list(zip(SWARM_BENGINE_SIDEBANDS,[reference]*len(SWARM_BENGINE_SIDEBANDS))))
        self.redis = Redis(host='localhost', port=6379)
        super(CalibrateVLBI, self).__init__(swarm)
        self.skip_next = len(SWARM_BENGINE_SIDEBANDS)*[zeros(2, dtype=bool)]
        self.history_size = history_size
        self.outfilename = outfilename
        self.single_chan = single_chan
        self.PID_coeffs = PID_coeffs
        self.normed = normed
        self.init_pool()
        self.inputs = len(SWARM_BENGINE_SIDEBANDS)*[[]]
        self.history = len(SWARM_BENGINE_SIDEBANDS)*[[]]
        self.smax = SmaxRedisClient()
        self.armed = False

    def init_history(self, first, sb_idx):
        hist_shape = [self.history_size,] + list(first.shape)
        self.logger.info("Initializing history to shape {0}".format(hist_shape))
        self.history[sb_idx] = empty(hist_shape, dtype=first.dtype)
        self.history[sb_idx][:] = nan
        self.history[sb_idx][0] = first

    def append_history(self, point, sb_idx):
        self.history[sb_idx] = roll(self.history[sb_idx], 1, axis=0)
        self.history[sb_idx][0] = point

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

    def feedback_delay_usb(self, this_input, feedback_delay):
        current_delay = self.swarm.get_delay(this_input)
        updated_delay = current_delay + feedback_delay
        if feedback_delay > 1.0:
            self.skip_next[:] = True
        if not this_input==self.reference['USB']:
            self.swarm.set_delay(this_input, updated_delay)
            self.logger.debug(
                '{0} : Old delay={1:>8.2f} ns,  New delay={2:>8.2f} ns,  Diff. delay={3:>8.2f} ns'.format(
                    this_input, current_delay, updated_delay, feedback_delay
                )
            )

    def feedback_phase_usb(self, this_input, feedback_phase):
        current_phase = self.swarm.get_phase(this_input)
        updated_phase = current_phase + feedback_phase
        if not this_input==self.reference['USB']:
            self.swarm.set_phase(this_input, wrap_phase(updated_phase))
            self.logger.debug(
                '{0}:USB : Old phase={1:>8.2f} deg, New phase={2:>8.2f} deg, Diff. phase={3:>8.2f} deg'.format(
                    this_input, current_phase, updated_phase, feedback_phase
                )
            )

    def feedback_phase_lsb(self, inputs, feedback_phases):
        # Do the phase update per quadrant
        chunk_list = set([inp.chk for inp in inputs])
        for ichunk, chunk in enumerate(chunk_list):
            # First filter the inputs that belongs to this quadrant
            these_inputs = list(inp for inp in inputs if inp.chk==chunk)
            # Get the current phases for this quadrant
            current_phases = self.swarm.get_beamformer_second_sideband_phase(these_inputs)
            # Then extract the phases for those inputs
            updated_phases = []
            for ii, inp in enumerate(these_inputs):
                this_current_phase = current_phases[ii]
                this_updated_phase = this_current_phase + feedback_phases[inputs.index(inp)]
                updated_phases.append(this_updated_phase)
            # And set the phases
            self.swarm.set_beamformer_second_sideband_phase(these_inputs, updated_phases)
            for ii, this_input in enumerate(these_inputs):
                self.logger.debug(
                    '{0}:LSB : Old phase={1:>8.2f} deg, New phase={2:>8.2f} deg, Diff. phase={3:>8.2f} deg'.format(
                        this_input, current_phases[ii], updated_phases[ii], feedback_phases[inputs.index(this_input)])
                    )

    def pid_servo(self, inputs, sb_idx):
        p, i, d = self.PID_coeffs
        p_amplitudes, p_delays, p_phases = self.history[sb_idx][0]
        i_amplitudes, i_delays, i_phases = self.history[sb_idx].sum(axis=0)
        d_amplitudes, d_delays, d_phases = self.history[sb_idx][0] - self.history[sb_idx][1]
        pid_delays = p * p_delays + i * i_delays + d * d_delays
        pid_phases = p * p_phases + i * i_phases + d * d_phases
        if SWARM_BENGINE_SIDEBANDS[sb_idx] == 'USB':
            for idx, (inp, delay_val, phase_val) in enumerate(
                zip(inputs, pid_delays, pid_phases)
            ):
                if isfinite(delay_val) and bool(delay_val):
                    self.feedback_delay_usb(inp, delay_val)
                if isfinite(phase_val) and bool(phase_val):
                    self.feedback_phase_usb(inp, phase_val)
        elif SWARM_BENGINE_SIDEBANDS[sb_idx] == 'LSB':
            if all(isfinite(pid_phases)) and any(pid_phases):
                self.feedback_phase_lsb(inputs, pid_phases)

    def solve_for(self, data, inputs, chunk, pol, sideband='USB', edge_chan=1024):
        # Check if reference is in beam
        ref_in_beam = True
        this_reference = SwarmInput(self.reference[sideband].ant, chunk, pol)
        ref_input = inputs.index(this_reference)

        if this_reference not in inputs:
            self.logger.warn(
                'Reference {ref!r} not in beam, adding it for the purpose of phase solution'.format(
                    ref=this_reference
                )
            )
            inputs.append(this_reference)
            ref_in_beam = False

        baselines = list(
            baseline for baseline in data.baselines if (
                (baseline.left in inputs) and (baseline.right in inputs)
            )
        )

        corr_matrix = zeros([len(inputs), len(inputs), SWARM_CHANNELS], dtype=complex128)
        for baseline in baselines:
            left_i = inputs.index(baseline.left)
            right_i = inputs.index(baseline.right)
            if left_i == right_i:
                # We don't want to consider the autos, since they are positively biased.
                continue
            baseline_data = data[baseline, sideband].view('<c8')
            corr_matrix[left_i, right_i] = baseline_data
            corr_matrix[right_i, left_i] = baseline_data.conj()
        if self.normed:
            with errstate(invalid='ignore'):
                corr_matrix = complex_nan_to_num(corr_matrix / abs(corr_matrix))
        if self.single_chan:
            gains_soln = solve_cgains(
                corr_matrix[:, :, edge_chan:-edge_chan].mean(axis=-1), ref=ref_input
            )
            phases = (180.0/pi) * angle(gains_soln)
            delays = zeros(len(inputs))
            gains_soln = gains_soln.reshape(1, -1)
        else:
            referenced_solver = partial(solve_cgains, ref=ref_input)
            gains_soln = array(self.map(referenced_solver, corr_matrix.transpose((2, 0, 1))))
            delays, phases = solve_delay_phase(gains_soln)
        amplitudes = abs(gains_soln).mean(axis=0)

        # Remove reference solution if it is not in beam
        if not ref_in_beam:
            delays = delays[:-1]
            phases = phases[:-1]
            amplitudes = amplitudes[:-1]

        with errstate(invalid='ignore'):
            efficiency = (abs(gains_soln.sum(axis=1)) / abs(gains_soln).sum(axis=1)).real
        return efficiency, vstack([amplitudes, delays, phases])

    def __call__(self, data):
        try:
            if not self.smax.smax_pull("correlator:swarm", "phasing").data:
                self.logger.info(
                    "calibrate_vlbi callback skipped because phasing is disabled."
                )
                self.armed = False
                return
            elif not self.armed:
                self.logger.info("Phasing just enabled, skipping first integration.")
                self.armed = True
                return
        except Exception as err:
            try:
                self.logger.error("Error determining phase status: %s" % str(err))
            except Exception:
                self.logger.error("Error determining phase status, reason unknown.")

        # First apply 2nd sideband phase data
        if data.is_phase_applied():
            self.logger.debug("Beamformer phases already applied to data, skipping.")
        else:
            phases = self.swarm.calc_baseline_second_sideband_phase(data.baselines)
            count = data.apply_phase(data.baselines, phases, sb="LSB")

            # Debug log that second sideband phases applied
            self.logger.debug(
                "Applied 2nd sideband beamformer phases on %d of %d baselines." % (
                    count, len(phases)
                )
            )

        """ Callback for VLBI calibration """
        for sb_idx, sb_str in enumerate(SWARM_BENGINE_SIDEBANDS):
            beam = [inp for quad in self.swarm.quads for inp in quad.get_beamformer_inputs()[sb_str]]
            inputs = sorted(beam, key=lambda b: b.ant + b.chk*10 + b.pol*100)
            if not inputs:
                continue

            listed_chunks = set([inp.chk for inp in inputs])
            listed_pols = set([inp.pol for inp in inputs])
            efficiencies = list([None for chunk in listed_chunks] for pol in listed_pols)
            cal_solutions = list([None for chunk in listed_chunks] for pol in listed_pols)
            for ichunk, chunk in enumerate(listed_chunks):
                for ipol, pol in enumerate(listed_pols):
                    these_inputs = list(inp for inp in inputs if (inp.chk==chunk) and (inp.pol==pol))
                    if not these_inputs:
                        continue
                    eff, cal = self.solve_for(data, these_inputs, chunk, pol, sideband=sb_str)
                    cal_solutions[ipol][ichunk] = cal
                    efficiencies[ipol][ichunk] = eff
            cal_solution_tmp = vstack([cs for cs in cal_solutions])
            cal_solution = hstack(cal_solution_tmp)
            amplitudes, delays, phases = cal_solution

            for i in range(len(inputs)):
                self.logger.debug(
                    '{}:{} : Amp={:>12.2e}, Delay={:>8.2f} ns, Phase={:>8.2f} deg'.format(
                        inputs[i], sb_str, amplitudes[i], delays[i], phases[i]
                    )
                )
            for ichunk, chunk in enumerate(listed_chunks):
                for ipol, pol in enumerate(listed_pols):
                    self.logger.info(
                        'Avg. phasing efficiency across chunk {}, pol {}, sideband {}={:>8.2f} +/- {:.2f}'.format(
                            chunk,
                            pol,
                            sb_str,
                            nanmean(efficiencies[ipol][ichunk]),
                            nanstd(efficiencies[ipol][ichunk]),
                        )
                    )
                    self.redis.setex(
                        '{0}.efficiency.chk{1}.pol{2}.{3}'.format(
                            REDIS_PREFIX, chunk, pol, sb_str
                        ),
                        int(data.int_length*2),
                        efficiencies[ipol][ichunk][0],
                    )

            if self.inputs[sb_idx] != inputs:
                self.init_history(cal_solution,sb_idx)
                self.inputs[sb_idx] = inputs
            elif self.skip_next[sb_idx][0]:
                self.logger.info("Ignoring this integration for historical purposes")
                self.skip_next[sb_idx][0] = False
                self.skip_next[sb_idx] = roll(self.skip_next[sb_idx], 1)
            else:
                self.append_history(cal_solution,sb_idx)
                self.pid_servo(inputs,sb_idx)

            if sb_str == "USB":
                new_delays_usb = [self.swarm.get_delay(inp) for inp in inputs]
                new_phases_usb = [self.swarm.get_phase(inp) for inp in inputs]
                efficiencies_usb = efficiencies
                inputs_usb = inputs
                cal_solution_usb = cal_solution
            elif sb_str == "LSB":
                new_phases_lsb = self.swarm.get_beamformer_second_sideband_phase(inputs)
                efficiencies_lsb = efficiencies
                inputs_lsb = inputs
                cal_solution_lsb = cal_solution
        with JSONListFile(self.outfilename) as jfile:
            jfile.append({
                    'int_time': data.int_time,
                    'int_length': data.int_length,
                    'inputs_usb': list((inp.ant, inp.chk, inp.pol) for inp in inputs_usb),
                    'inputs_lsb': list((inp.ant, inp.chk, inp.pol) for inp in inputs_lsb),
                    'efficiencies_usb': list(nanmean(eff) for pol_eff in efficiencies_usb for eff in pol_eff),
                    'efficiencies_lsb': list(nanmean(eff) for pol_eff in efficiencies_lsb for eff in pol_eff),
                    'delays_usb': new_delays_usb,
                    'phases_usb': new_phases_usb,
                    'phases_lsb': new_phases_lsb,
                    'cal_solution_usb': cal_solution_usb.tolist(),
                    'cal_solution_lsb': cal_solution_lsb.tolist(),
                    })
