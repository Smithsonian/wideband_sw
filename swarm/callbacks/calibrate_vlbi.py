from functools import partial
from itertools import combinations_with_replacement as combos
from numpy.linalg import eig as eig
from numpy import (
    complex128,
    array,
    zeros,
    around,
    angle,
    isnan,
    sqrt,
    sum,
    pi,
    )
from swarm import (
    SwarmDataCallback,
    SwarmBaseline,
    SWARM_CHANNELS,
)

def solve_cgains(mat, ref=0):
    vals, vecs = eig(mat)
    max_val = vals.real.max()
    max_vec = vecs[:, vals.real.argmax()]
    raw_gains = max_vec * sqrt(max_val).squeeze()
    ref_gain = raw_gains[ref]
    factor = ref_gain.conj() / abs(ref_gain)
    return raw_gains * factor

class CalibrateVLBI(SwarmDataCallback):

    def __init__(self, swarm, reference=None):
        self.reference = reference if reference is not None else swarm[0].get_input(0)
        super(CalibrateVLBI, self).__init__(swarm)

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
            interleaved = baseline_data[~isnan(baseline_data)]
            complex_data = interleaved[0::2] + 1j * interleaved[1::2]
            corr_matrix[:, left_i, right_i] = complex_data
            corr_matrix[:, right_i, left_i] = complex_data.conj()
        referenced_solver = partial(solve_cgains, ref=inputs.index(self.reference))
        full_spec_gains = array(map(referenced_solver, corr_matrix))
        gains = full_spec_gains.mean(axis=0)
        for i in range(len(inputs)):
            self.logger.info('{} : Amp={:>12.2e}, Phase={:>8.2f} deg'.format(inputs[i], abs(gains[i]), (180.0/pi)*angle(gains[i])))
