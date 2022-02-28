#!/usr/local/anaconda/envs/swarm/bin/python
import argparse
import logging
from collections import OrderedDict
import sys, time

import pyopmess
from swarm import Swarm
from swarm.defines import query_yes_no, SWARM_MAPPING_CHUNKS, SWARM_MAPPINGS


# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to program new bitcode and reload_plugins SWARM quadrants. ENGINEERING/TESTING ONLY!!!')
parser.add_argument('integers', metavar='N', type=int, nargs='+',
                    help='Enter a space delimited list of integers representing SWARM quadrants. '
                         'ex: swarm_idle 5 6')
args = parser.parse_args()

# Do a little error checking to make sure the input params are at least valid swarm quadrants.
idle_quad_mappings = OrderedDict()
for num in args.integers:
    if (num - 1) in SWARM_MAPPING_CHUNKS:
        idle_quad_mappings[num] = (SWARM_MAPPINGS[num - 1])
    else:
        print((str(num) + " is not a valid SWARM quadrant and will be ignored."))

# Present the request and ask to proceed to IDLE quadrants.
idle_quad_string = " ".join(map(str, list(idle_quad_mappings.keys())))
if idle_quad_mappings and query_yes_no("Proceed to load bitcode and reload plugins for quadrant(s) " + idle_quad_string + "?"):

    # Instantiate a Swarm object using the disabled quadrant mappings.
    swarm = Swarm(mappings_dict=idle_quad_mappings)
    pyopmess.send(1, 1, 100, "SWARM quadrant(s) " + idle_quad_string + " loading bitcode")
    swarm.members_do(lambda fid, mbr: mbr.load_bitcode())
    swarm.members_do(lambda fid, mbr: mbr.set_source(2, 2))
    swarm.members_do(lambda fid, mbr: mbr.reload_plugins())
    time.sleep(600)

    pyopmess.send(1, 1, 100, "SWARM quadrant(s) " + idle_quad_string + " now being reloading plugins")
    swarm.members_do(lambda fid, mbr: mbr.send_katcp_cmd('stop-adc-monitor'))
    for  fid, member in swarm.get_valid_members():
         print(fid)
         member.set_scope(3, 0, 6)
         member.warm_calibrate_adc()
    swarm.members_do(lambda fid, mbr: mbr.send_katcp_cmd('start-adc-monitor'))
