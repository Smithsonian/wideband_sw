import argparse
import logging
from collections import OrderedDict
from signal import SIGURG
import os

import pyopmess
import subprocess
from swarm import Swarm
from swarm.defines import *

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to set the active SWARM quadrants.')
parser.add_argument('integers', metavar='N', type=int, nargs='+',
                    help='Enter a space delimited list of integers representing active SWARM quadrants.')
args = parser.parse_args()

# Build two dictionaries one for active quadrant mappings and one for disabled quadrants.
active_quad_mappings = OrderedDict()
disabled_quad_mappings = OrderedDict()
for num in range(1, SWARM_MAX_NUM_QUADRANTS + 1):
    if num in args.integers:
        active_quad_mappings[num] = (SWARM_MAPPINGS[num - 1])
    else:
        disabled_quad_mappings[num] = (SWARM_MAPPINGS[num - 1])

# Instantiate a Swarm object using the disabled quadrant mappings.
swarm = Swarm(mappings_dict=disabled_quad_mappings)

# IDLE the disabled quadrants.
disabled_quad_string = " ".join(map(str, disabled_quad_mappings.keys()))
pyopmess.send(1, 1, 100, "SWARM quadrant(s) " + disabled_quad_string + " now being idled")
swarm.members_do(lambda fid, mbr: mbr.idle())

# Update the SWARMQuadrantsInArray file with the active quadrant list.
active_quad_string = " ".join(map(str, active_quad_mappings.keys()))
with open(ACTIVE_QUADRANTS_FILE_PATH, "w") as quadrants_file:
    line = quadrants_file.write(active_quad_string)

# Restart corrSaver on obscon.
out = subprocess.check_output(["/global/bin/killdaemon", "obscon", "corrSaver" "restart"])
logger.info(out)

# Restart SWARM processes on Tenzing.
out = subprocess.check_output(["/global/bin/killdaemon", "tenzing", "smainit", "restart"])
logger.info(out)

# Begin Cold Start.
