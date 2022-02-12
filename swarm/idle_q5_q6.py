#!/usr/local/anaconda/envs/swarm/bin/python
from collections import OrderedDict
#import pyopmess
import logging
import sys
from swarm import Swarm

Q5_Q6_MAPPINGS = [
    '/global/configFiles/swarmMapping.quad5',
    '/global/configFiles/swarmMapping.quad6',
    ]

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Build two dictionaries one for active quadrant mappings and one for disabled quadrants.
quad_mappings = OrderedDict()
quad_mappings[5] = Q5_Q6_MAPPINGS[0]
quad_mappings[6] = Q5_Q6_MAPPINGS[1]

# Instantiate a Swarm object using the disabled quadrant mappings.
swarm = Swarm(mappings_dict=quad_mappings)

# IDLE the disabled quadrants.
disabled_quad_string = " ".join(map(str, list(quad_mappings.keys())))
# pyopmess.send(1, 1, 100, "SWARM quadrant(s) " + disabled_quad_string + " now being idled")
# swarm.members_do(lambda fid, mbr: mbr.idle())
