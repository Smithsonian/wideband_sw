#!/usr/local/anaconda/envs/swarm/bin/python
import logging
import sys
import argparse
from time import sleep
from swarm import Swarm
from swarm.defines import *
from numpy import array


# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to directly set the integration time for SWARM')
parser.add_argument('time', type=float, default=30,
                    help='Enter a time in seconds to apply to SWARM for integration time, can be floating point')
args = parser.parse_args()

if args.time < 0 or args.time == 0:
    logger.error("Invalid integration time, must be greater than zero.")
    sys.exit()

swarm = Swarm()

# Set the itime and wait for it to register
logger.info('Setting integration time and resetting x-engines...')
for fid, member in swarm.get_valid_members():
    print(("fid: " + str(fid) + " member: " + str(member)))
    member.set_itime(args.time)

# Reset the xengines until window counters to by in sync
win_period = SWARM_ELEVENTHS * (SWARM_EXT_HB_PER_WCYCLE // SWARM_WALSH_SKIP)
win_sync = False
while not win_sync:
    swarm.reset_xengines()
    sleep(0.5)
    win_count = array([m.fpga.read_uint('xeng_status') for f, m in swarm.get_valid_members()])
    win_sync = len(set(c // win_period for c in win_count)) == 1
    logger.info('Window sync: {0}'.format(win_sync))
