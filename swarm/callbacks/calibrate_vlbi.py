from functools import partial
from signal import signal, SIGINT, SIG_IGN
from multiprocessing import Pool, cpu_count
from itertools import combinations_with_replacement as combos
from numpy.linalg import eig as eig
from numpy.fft import fft, ifft, fftshift
from numpy import (
    nan_to_num,
    complex128,
    linspace,
    vstack,
    array,
    zeros,
    empty,
    arange,
    around,
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

    # Now find the phase at the interpolated lag peak
    phases = angle(interp_lags.take(interp_peaks, axis=chan_axis)).diagonal()
    return delays, phases

def complex_nan_to_num(arr):
    out_arr = arr.copy()
    out_arr[isnan(out_arr)] = 0.0j
    return out_arr

class CalibrateVLBI(SwarmDataCallback):

    def __init__(self, swarm, reference=None):
        self.reference = reference if reference is not None else swarm[0].get_input(0)
        super(CalibrateVLBI, self).__init__(swarm)
        self.init_pool()
        self.accums = 0

    def init_history(self, first, length=8):
        hist_shape = [length,] + list(first.shape)
        self.logger.info("Initializing history to shape {0}".format(hist_shape))
        self.history = empty(hist_shape, dtype=first.dtype)
        self.history[:] = nan
        self.history[0] = first

    def append_history(self, point):
        self.history = roll(self.history, 1)
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

    def map(self, function, iterable):
        async_reply = self.process_pool.map_async(function, iterable)
        return async_reply.get(0xffff)

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
            baseline_data = data[baseline][solve_chunk][solve_sideband]
            complex_data = baseline_data[0::2] + 1j * baseline_data[1::2]
            corr_matrix[:, left_i, right_i] = complex_data
            corr_matrix[:, right_i, left_i] = complex_data.conj()
        referenced_solver = partial(solve_cgains, ref=inputs.index(self.reference))
        full_spec_gains = array(self.map(referenced_solver, complex_nan_to_num(corr_matrix)))
        delays, phases = solve_delay_phase(full_spec_gains)
        amplitudes = abs(full_spec_gains).mean(axis=0)
        cal_solution = vstack([amplitudes, delays, phases])
        for i in range(len(inputs)):
            self.logger.info('{} : Amp={:>12.2e}, Delay={:>8.2f} ns, Phase={:>8.2f} deg'.format(inputs[i], amplitudes[i], delays[i], (180.0/pi)*phases[i]))
        if self.accums == 0:
            self.init_history(cal_solution)
        else:
            self.append_history(cal_solution)
        self.accums += 1
        hist_amplitudes, hist_delays, hist_phases = self.history.mean(axis=0)
        for i in range(len(inputs)):
            self.logger.info('{} : Hist. amp={:>12.2e}, Hist. delay={:>8.2f} ns, Hist. phase={:>8.2f} deg'.format(inputs[i], hist_amplitudes[i], hist_delays[i], (180.0/pi)*hist_phases[i]))
