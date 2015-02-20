import logging
from struct import unpack
from numpy import (
    array,
    empty,
)
from swarm import (
    SWARM_VISIBS_ACC_SIZE,
)

# Our callback's logger
logger = logging.getLogger('Callback:check_ramp')

# Callback for checking ramp
def check_ramp(rawdata):
    ramp = empty(SWARM_VISIBS_ACC_SIZE)
    for fid, datas in enumerate(rawdata):
        raw = array(unpack('>%dI'%SWARM_VISIBS_ACC_SIZE, datas))
        ramp[0::2] = raw[1::2]
        ramp[1::2] = raw[0::2]
        errors_ind = list(i for i, p in enumerate(ramp) if p !=i)
        logger.info('Ramp errors for FID #%d: %d' % (fid, len(errors_ind)))
