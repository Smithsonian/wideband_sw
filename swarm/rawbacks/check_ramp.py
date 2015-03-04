import logging
from struct import unpack
from numpy import (
    array,
    empty,
)
from swarm import (
    SWARM_VISIBS_ACC_SIZE,
    SwarmDataCallback,
)


class CheckRamp(SwarmDataCallback):

    def __call__(self, rawdata):
        """ Callback for checking ramp """
        ramp = empty(SWARM_VISIBS_ACC_SIZE)
        for fid, datas in enumerate(rawdata):
            raw = array(unpack('>%dI'%SWARM_VISIBS_ACC_SIZE, datas))
            ramp[0::2] = raw[1::2]
            ramp[1::2] = raw[0::2]
            errors_ind = list(i for i, p in enumerate(ramp) if p !=i)
            self.logger.info('Ramp errors for FID #%d: %d' % (fid, len(errors_ind)))
