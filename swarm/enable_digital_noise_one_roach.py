#!/usr/local/anaconda/envs/swarm/bin/python
import logging
import sys

import pyopmess
from swarm import Swarm
import argparse
from swarm.defines import query_yes_no

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to enable internal FPGA noise on a single roach2.')
parser.add_argument('-q', '--quad', dest='quad', metavar='QUADRANT', type=int)
parser.add_argument('-r', '--roach', dest='roach', metavar='ROACH-ID', type=int)
args = parser.parse_args()

swarm = Swarm()
for q in swarm.quads:
    if q.qid == args.quad:
        swarm_quad = q

for fid, member in swarm_quad.get_valid_members():
    if fid == args.roach - 1:
        member.set_source(3, 3)
