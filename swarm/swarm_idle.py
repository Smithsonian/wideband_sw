#!/usr/bin/env python2.7

import logging, argparse
from swarm import *


def idlize(fid, member):
    member.roach2.progdev('sma_idle.bof')
    member.logger.info("Idling {} = FID #{}".format(member.roach2_host, fid))


# Setup some basic logging
logging.basicConfig()
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
logger = logging.getLogger()
logger.handlers[0].setFormatter(formatter)
logger.setLevel(logging.INFO)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Idle the SWARM using the so-called "idle" bitcode')
parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=SWARM_MAPPINGS,
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(SWARM_MAPPINGS))
args = parser.parse_args()

# Do the actual idling
s = Swarm(map_filenames=args.swarm_mappings)
s.quadrants_do(lambda qid, quad: quad.unload_plugins())
s.members_do(idlize)
