#!/usr/local/anaconda/envs/swarm/bin/python

import sys
import logging
import argparse
from swarm import *

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Run an interactive SWARM shell')
parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=[],
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(SWARM_MAPPINGS))
args = parser.parse_args()

# Setup logging
logging.basicConfig()
logging.getLogger('katcp').setLevel(logging.CRITICAL)
logging.getLogger('').setLevel(logging.DEBUG if args.verbose else logging.INFO)

# Set up SWARM 
swarm = Swarm(map_filenames=args.swarm_mappings)

# Enable noise source
swarm.members_do(lambda i,m: m.dewalsh(False,False))
swarm.send_katcp_cmd("sma-astro-fstop-set","7.85","-12.15","-2.71359","0","0")
sys.exit(0)
