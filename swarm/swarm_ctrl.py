#!/usr/bin/env python2.7

import sys, pickle, traceback, logging, argparse
from time import sleep
from threading import Event
from redis import StrictRedis, ConnectionError
import pyopmess

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


# Global variables
LOG_CHANNEL = "swarm.logs.ctrl"
RUNNING = Event()

# Custom Redis logging handler
class RedisHandler(logging.Handler):

    def __init__(self, channel, host='localhost', port=6379):
        self.redis = StrictRedis(host=host, port=port)
        self.channel = channel
        super(RedisHandler, self).__init__()

    def emit(self, record):
        if record.exc_info: # this is an exception
            record.msg += '\n' + traceback.format_exc(record.exc_info)
            record.exc_info = None # clear the traceback
        try:
            self.redis.publish(self.channel, pickle.dumps(record))
        except ConnectionError:
            pass

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Also, log to a Redis channel
logredis = RedisHandler(LOG_CHANNEL)
logredis.setLevel(logging.INFO)
logger.addHandler(logredis)

# Create and set formatting
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')
stdout.setFormatter(formatter)
logredis.setFormatter(formatter)

# Silence all katcp messages
katcp_logger = logging.getLogger('katcp')
katcp_logger.setLevel(logging.CRITICAL)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Idle, setup, or catch and process visibility data from a set of SWARM ROACH2s')
parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=SWARM_MAPPINGS,
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(SWARM_MAPPINGS))
parser.add_argument('-i', '--interfaces', dest='interfaces', metavar='INTERFACES', nargs='+', default=SWARM_LISTENER_INTERFACES,
                    help='listen for UDP data on INTERFACES (default="{0}")'.format(SWARM_LISTENER_INTERFACES))
parser.add_argument('-t', '--integrate-for', dest='itime', metavar='INTEGRATION-TIME', type=float, default=28.45,
                    help='integrate for approximately INTEGRATION-TIME seconds (default=30)')
parser.add_argument('-r', '--reference', dest='reference', metavar='REFERENCE', type=str, default='2,0,0',
                    help='use ANT,POL,CHUNK as a REFERENCE; POL and CHUNK are either 0 or 1 (default=2,0,0')
parser.add_argument('--idle-only', dest='idle_only', action='store_true',
                    help='only program with the idle bitcode; do not do full setup')
parser.add_argument('--setup-only', dest='setup_only', action='store_true',
                    help='only program and setup the board; do not wait for data')
parser.add_argument('--listen-only', dest='listen_only', action='store_true',
                    help='do NOT setup the board; only wait for data')
parser.add_argument('--continue-on-qdr-error', dest='raise_qdr_err', action='store_false',
                    help='do NOT terminate program on a QDR calibration error.')
parser.add_argument('--no-data-catcher', dest='disable_data_catcher', action='store_true',
                    help='do NOT send data to the SMAs dataCatcher and corrSaver servers')
parser.add_argument('--visibs-test', dest='visibs_test', action='store_true',
                    help='enable the DDR3 visibility ramp test')
parser.add_argument('--save-raw-data', dest='save_rawdata', action='store_true',
                    help='Save raw data from each FID to file')
parser.add_argument('--log-stats', dest='log_stats', action='store_true',
                    help='Print out some baselines statistics (NOTE: very slow!)')
parser.add_argument('--calibrate-vlbi', dest='calibrate_vlbi', nargs='?', const='low', default=None,
                    help='Solve for complex gains (and possibly delay) to calibrate the phased sum '
                    '(optionally append either "high" or "low" for different SNR algorithms')
parser.add_argument('--log-file', dest='log_file', metavar='LOGFILE',
                    help='Write logger output to LOGFILE')
parser.add_argument('--silence-loggers', nargs='+', default=[],
                    help='silence the output from C extensions such as pysendint')
parser.add_argument('--thread-setup', dest='thread_setup', action='store_true',
                    help='multi-thread the setup to make it faster')
args = parser.parse_args()

# Require either setup, listen, or idle
if not (args.idle_only or args.setup_only or args.listen_only):
    parser.error('No action requested; either use --setup-only, --listen-only, or --idle-only')

# Add file handler, if requested
if args.log_file:
    fh = logging.FileHandler(args.log_file)
    fh.setFormatter(formatter)
    logger.addHandler(fh)

# Set logging level given verbosity
if args.verbose:
    stdout.setLevel(logging.DEBUG)
else:
    stdout.setLevel(logging.INFO)

# Silence user-defined loggers
for logger_name in args.silence_loggers:
    logging.getLogger(logger_name).setLevel(logging.CRITICAL)

# Construct our reference input
reference_args = [['antenna', 2], ['chunk', 0], ['polarization', 0]]
for i, a in enumerate(args.reference.split(',')):
    reference_args[i][1] = int(a)
reference = SwarmInput(**dict(reference_args))

# Create our SWARM instance
swarm = Swarm(map_filenames=args.swarm_mappings)

if not args.listen_only:

    # Idle the SWARM
    swarm.idle()

    if not args.idle_only:

        # Wait before starting setup
        sleep(5)

        # Setup using the Swarm class and our parameters
        swarm.setup(
            args.itime,
            args.interfaces,
            delay_test=args.visibs_test,
            raise_qdr_err=args.raise_qdr_err,
            threaded=args.thread_setup,
            )

else:

    # Setup the data catcher class
    swarm_catcher = SwarmDataCatcher(swarm)

    # Create the data handler
    swarm_handler = SwarmDataHandler(swarm, swarm_catcher.get_queue())

    if not args.disable_data_catcher:

        # Use a callback to send data to dataCatcher/corrSaver
        swarm_handler.add_callback(SMAData)

    if args.log_stats:

        # Use a callback to show visibility stats
        swarm_handler.add_callback(LogStats, reference=reference)

    if args.calibrate_vlbi:

        # Use a callback to calibrate fringes for VLBI

        if args.calibrate_vlbi == 'low':

            # Low SNR. Use single-channel solver and normalize corr. matrix
            swarm_handler.add_callback(CalibrateVLBI, single_chan=True, normed=True, reference=reference)

        elif args.calibrate_vlbi == 'high':

            # High SNR. Use full spectrum solver and do not normalize corr. matrix
            swarm_handler.add_callback(CalibrateVLBI, single_chan=False, normed=False, reference=reference)

        else:
            raise argparse.ArugmentError('--calibrate-vlbi must be either "low" or "high"!')

    if args.save_rawdata:

        # Give a rawback that saves the raw data
        swarm_catcher.add_rawback(SaveRawData)

    if args.visibs_test:

        # Give a rawback that checks for ramp errors
        swarm_catcher.add_rawback(CheckRamp)

    # Start the data catcher
    swarm_catcher.start()

    try:

        # Start the main loop
        RUNNING.set()
        swarm_handler.loop(RUNNING)

    except KeyboardInterrupt:

        # User wants to quit
        logger.info("Ctrl-C detected. Quitting loop.")

    except:

        # Some other exception detected
        logger.error("Exception caught, logging it and cleaning up")
        pyopmess.send(1, 1, 100, 'Listener crashed; try swarm.reset_xengines() in swarm_shell')

    # Stop the data catcher
    swarm_catcher.stop()
