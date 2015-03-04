from scipy.sparse.linalg import eigs
from numpy import (
    array,
    zeros,
    around,
    angle,
    isnan,
    sqrt,
    pi,
    )
from swarm import SwarmDataCallback

class CalibrateVLBI(SwarmDataCallback):

    def __init__(self, swarm, reference=None):
        self.reference = reference if reference is not None else swarm[0].get_input(0)
        super(CalibrateVLBI, self).__init__(swarm)

    def __call__(self, data):
        """ Callback for VLBI calibration """
        chunk = 0
        sideband = 'USB'
        n_inputs = len(data.inputs)
        corr_matrix = zeros([n_inputs, n_inputs], dtype=complex)
        for baseline in data.baselines:
            left_i = data.inputs.index(baseline.left)
            right_i = data.inputs.index(baseline.right)
            interleaved = array(list(p for p in data[baseline][chunk][sideband] if not isnan(p)))
            complex_data = interleaved[0::2] + 1j * interleaved[1::2]
            p_visib = complex_data.mean()
            corr_matrix[left_i][right_i] = p_visib
            corr_matrix[right_i][left_i] = p_visib.conjugate()
        corr_eig_val, corr_eig_vec = eigs(corr_matrix, k=1, which='LM')
        gains = around(corr_eig_vec * sqrt(corr_eig_val[0]), 8).squeeze()
        reference_gain = gains[data.inputs.index(self.reference)]
        factor = reference_gain.conj() / abs(reference_gain)
        gains *= factor
        for i in range(n_inputs):
            self.logger.info('{} : Amp={:>12.2e}, Phase={:>8.2f} deg'.format(data.inputs[i], abs(gains[i]), (180.0/pi)*angle(gains[i])))
