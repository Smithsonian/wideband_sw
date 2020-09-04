#!/usr/local/anaconda/envs/swarm/bin/python
import logging
import sys

# import pyopmess
from swarm import Swarm
from swarm.defines import query_yes_no

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

if query_yes_no("Proceed to disable internal digital noise on active swarm quadrants?"):
    swarm = Swarm()
    swarm.members_do(lambda fid, member: member.set_source(2, 2))
    # pyopmess.send(1, 1, 100, "SWARM internal digital noise disabled")
