#!/usr/local/anaconda/envs/swarm/bin/python
import argparse
import logging
import sys
import subprocess
import platform
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
parser = argparse.ArgumentParser(description='Script to check the health and initialize the Mark6 recorders.')

parser.add_argument('recorders', metavar='N', type=int, nargs='+',
                    help="Enter a space delimited integer list of recorders to check ex: mark6_init 1 2 3")

args = parser.parse_args()

mark6_hosts = ["Mark6-4052", "Mark6-4054", "Mark6-4055", "Mark6-4089"]


def ping(host):
    """
    Returns True if host (str) responds to a ping request.
    Remember that a host may not respond to a ping (ICMP) request even if the host name is valid.
    """

    # Option for the number of packets as a function of
    param = '-n' if platform.system().lower() =='windows' else '-c'

    # Building the command. Ex: "ping -c 1 google.com"
    command = ['ping', param, '1', host]

    return subprocess.call(command) == 0

# Ping Test.

# Disk Count.

# Diskpack Status Check.

# Interactively attempt to fix Mark6s reporting known statuses (other than open:ready).

# Mount for unmounted disks

# Group for ungrouped disks

# Prime for missing input streams.
