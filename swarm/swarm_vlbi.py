#!/usr/local/anaconda/envs/swarm/bin/python
import argparse
import logging
import sys
import subprocess
import pyopmess
from swarm.defines import query_yes_no
from redis import Redis

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to configure SWARM to support VLBI.')

parser.add_argument('mode', metavar='MODE', type=str, choices=['4to8', '5to9', 'off'],
                    help='vlbi mode can be set to 4 to 8 GHz, 5 to 9 Ghz, or off. {4to8,5to9,off}')

parser.add_argument('-r', '--reference', dest='reference', type=str, default='2,0,0',
                    help='use ANT,POL,CHUNK as a reference; POL and CHUNK are either 0 or 1 (default=2,0,0)')

parser.add_argument('-c', '--calibrate-vlbi', dest='calibrate_vlbi', choices=['low', 'high', 'off'], default='off',
                    help='Solve for complex gains (and possibly delay) to calibrate the phased sum '
                    '(optionally append either "high" or "low" for different SNR algorithms')

args = parser.parse_args()

redis_client = Redis(host='localhost', port=6379)

# Print out choices and ask to proceed.
print(("The following will be set:\n mode={}, reference={}, calibrate-vlbi={}".format(
    args.mode, args.reference, args.calibrate_vlbi)))

if query_yes_no("Continue to set values in redis and restart swarm_ctrl?"):

    redis_client = Redis(host='localhost', port=6379)
    redis_client.set("vlbi", args.mode)
    redis_client.set("reference", args.reference)
    redis_client.set("calibrate", args.calibrate_vlbi)

    # Restart SWARM processes on Tenzing.
    out = subprocess.check_output(["/global/bin/killdaemon", "tenzing", "swarm_ctrl", "restart"])
    logger.debug(out)

    pyopmess.send(1, 1, 100, "SWARM VLBI set to " + args.mode)
