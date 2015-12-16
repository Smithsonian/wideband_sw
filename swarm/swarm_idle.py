#!/usr/bin/env python2.7

import logging
from swarm import *

# Setup some basic logging
logging.basicConfig()
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
logger = logging.getLogger()
logger.handlers[0].setFormatter(formatter)
logger.setLevel(logging.INFO)

def idlize(fid, member):
    member.roach2.progdev('sma_idle.bof')
    logger.info("Idling {} = FID #{}".format(member.roach2_host, fid))

s = SwarmQuadrant(0, map_filename=SWARM_MAPPING)
s.unload_plugins()
s.members_do(idlize)
