#!/usr/local/anaconda/envs/swarm/bin/python
import argparse
import logging
from collections import OrderedDict
import time
import sys

import pyopmess
import subprocess
from swarm import Swarm
from swarm.defines import *


def compare_with_active_quadrants(requested_active_quads):
    with open(ACTIVE_QUADRANTS_FILE_PATH) as qfile:
        line = qfile.readline().strip()
    if line:
        active_quads = [int(x) for x in line.split(" ")]
        if sorted(active_quads) == sorted(requested_active_quads):
            return True
    return False


def query_yes_no(question, default="yes"):
    """Ask a yes/no question via raw_input() and return their answer.

    "question" is a string that is presented to the user.
    "default" is the presumed answer if the user just hits <Enter>.
        It must be "yes" (the default), "no" or None (meaning
        an answer is required of the user).

    The "answer" return value is True for "yes" or False for "no".
    """
    valid = {"yes": True, "y": True, "ye": True,
             "no": False, "n": False}
    if default is None:
        prompt = " [y/n] "
    elif default == "yes":
        prompt = " [Y/n] "
    elif default == "no":
        prompt = " [y/N] "
    else:
        raise ValueError("invalid default answer: '%s'" % default)

    while True:
        sys.stdout.write(question + prompt)
        choice = raw_input().lower()
        if default is not None and choice == '':
            return valid[default]
        elif choice in valid:
            return valid[choice]
        else:
            sys.stdout.write("Please respond with 'yes' or 'no' "
                             "(or 'y' or 'n').\n")


# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to set the active SWARM quadrants.')
parser.add_argument('integers', metavar='N', type=int, nargs='+',
                    help='Enter a space delimited list of integers representing active SWARM quadrants. '
                         'ex: set_active_quadrants 1 2 3')
args = parser.parse_args()

# Build two dictionaries one for active quadrant mappings and one for disabled quadrants.
active_quad_mappings = OrderedDict()
disabled_quad_mappings = OrderedDict()
for num in range(1, SWARM_MAX_NUM_QUADRANTS + 1):
    if num in args.integers:
        active_quad_mappings[num] = (SWARM_MAPPINGS[num - 1])
    else:
        disabled_quad_mappings[num] = (SWARM_MAPPINGS[num - 1])

# Compare to swarmquadrants file.
if compare_with_active_quadrants(active_quad_mappings.keys()):
    if not query_yes_no("Requested quadrants are the same as current configuration, proceed anyway?"):
        sys.exit()

# Present the request and ask to proceed to IDLE quadrants.
if disabled_quad_mappings:
    disabled_quad_string = " ".join(map(str, disabled_quad_mappings.keys()))
    if query_yes_no("Proceed to IDLE quadrant(s) " + disabled_quad_string + "?"):

        # Instantiate a Swarm object using the disabled quadrant mappings.
        swarm = Swarm(mappings_dict=disabled_quad_mappings)

        # IDLE the disabled quadrants.
        disabled_quad_string = " ".join(map(str, disabled_quad_mappings.keys()))
        pyopmess.send(1, 1, 100, "SWARM quadrant(s) " + disabled_quad_string + " now being idled")
        swarm.members_do(lambda fid, mbr: mbr.idle())

# Update the SWARMQuadrantsInArray file with the active quadrant list.
active_quad_string = " ".join(map(str, active_quad_mappings.keys()))
if query_yes_no("Proceed to change SWARMQuadrantsInArray to " + active_quad_string + "?"):
    with open(ACTIVE_QUADRANTS_FILE_PATH, "w") as quadrants_file:
        quadrants_file.write(active_quad_string)

if query_yes_no("Restart corrSaver and SWARM processes?"):
    # Restart corrSaver on obscon.
    out = subprocess.check_output(["/global/bin/killdaemon", "obscon", "corrSaver", "restart"])
    logger.debug(out)
    logger.info("")

    # Restart SWARM processes on Tenzing.

    out = subprocess.check_output(["/global/bin/killdaemon", "tenzing", "swarm_ctrl", "restart"])
    logger.debug(out)

    out = subprocess.check_output(["/global/bin/killdaemon", "tenzing", "swarm_checks", "restart"])
    logger.debug(out)

    # Somehow wait for swarm and corrsaver to come back to life.
    wait_time = 20
    logger.info("Waiting {0} seconds for corrSaver and Swarm python processes to restart".format(wait_time))
    time.sleep(wait_time)


if query_yes_no("Trigger cold start?"):
    # Trigger a cold start by opening the smainit URG file in write mode
    with open(SWARM_COLDSTART_PATH, "w") as smainit_file:
        logger.info("Triggered a cold start to initialize new SWARM quadrant configuration")

        # Since the smainit file opened successfully, a cold start should begin.
        # Update the last cold start value with C-style timestamp.
        with open(SWARM_LAST_COLDSTART_PATH, "w") as lastcoldstart_file:
            lastcoldstart_file.write(str(int(time.time())))
