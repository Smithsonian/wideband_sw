import sys, pickle, traceback, logging, argparse
from time import sleep
from threading import Event
from queue import Queue, Empty
from collections import OrderedDict
from traceback import format_exception
from redis import StrictRedis, ConnectionError
from signal import (
    signal,
    SIGQUIT,
    SIGTERM,
    SIGINT,
    SIGHUP,
    SIGURG,
    SIGCONT,
    )
import pyopmess

from swarm.core import ExceptingThread
from swarm.defines import *
from swarm import (
    SwarmDataCatcher,
    SwarmDataHandler,
    SwarmQuadrant,
    SwarmInput,
    Swarm,
    )
from rawbacks.check_ramp import CheckRamp
from rawbacks.save_rawdata import SaveRawData
from callbacks.calibrate_vlbi import CalibrateVLBI
from callbacks.log_stats import LogStats
from callbacks.sma_data import SMAData

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

# Create our SWARM instance
DISABLE_SWARM_MAPPINGS = [
    # '/global/configFiles/swarmMapping.quad1',
    # '/global/configFiles/swarmMapping.quad2',
    # '/global/configFiles/swarmMapping.quad3',
    '/global/configFiles/swarmMapping.quad4',
    ]

swarm = Swarm(map_filenames=DISABLE_SWARM_MAPPINGS)


# Signal handler for idling SWARM
def idle_quadrant():
    pyopmess.send(1, 1, 100, 'SWARM quadrant is now being idled')
    swarm.members_do(lambda fid, mbr: mbr.idle())


idle_quadrant()

# Finish up and get out
logger.info("disable_quadrant complete")
