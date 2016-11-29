#!/usr/bin/env python2.7

import logging, argparse

from swarm import (
    SWARM_LISTENER_INTERFACES,
    SWARM_MAPPINGS,
    SwarmDataCatcher,
    SwarmDataHandler,
    SwarmQuadrant,
    SwarmInput,
    Swarm,
    )
from rawbacks.check_ramp import *
from rawbacks.save_rawdata import *
from callbacks.calibrate_vlbi import *
from callbacks.log_stats import *
from callbacks.sma_data import *


# Setup some basic logging
logging.basicConfig()
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')
logger = logging.getLogger()
logger.handlers[0].setFormatter(formatter)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Catch and process visibility data from a set of SWARM ROACH2s')
parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=SWARM_MAPPINGS,
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(SWARM_MAPPINGS))
parser.add_argument('-i', '--interfaces', dest='interfaces', metavar='INTERFACES', nargs='+', default=SWARM_LISTENER_INTERFACES,
                    help='listen for UDP data on INTERFACES (default="{0}")'.format(SWARM_LISTENER_INTERFACES))
parser.add_argument('-t', '--integrate-for', dest='itime', metavar='INTEGRATION-TIME', type=float, default=28.45,
                    help='integrate for approximately INTEGRATION-TIME seconds (default=30)')
parser.add_argument('-r', '--reference', dest='reference', metavar='REFERENCE', type=str, default='2,0,0',
                    help='use ANT,POL,CHUNK as a REFERENCE; POL and CHUNK are either 0 or 1 (default=2,0,0')
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

# Add file handler, if requested
if args.log_file:
    fh = logging.FileHandler(args.log_file)
    fh.setFormatter(formatter)
    logger.addHandler(fh)

# Set logging level given verbosity
if args.verbose:
    logger.setLevel(logging.DEBUG)
else:
    logger.setLevel(logging.INFO)

# Silence all katcp messages
katcp_logger = logging.getLogger('katcp')
katcp_logger.setLevel(logging.CRITICAL)

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

    # Setup using the Swarm class and our parameters
    swarm.setup(args.itime, args.interfaces, delay_test=args.visibs_test, raise_qdr_err=args.raise_qdr_err, threaded=args.thread_setup)

# Setup the data catcher class
swarm_catcher = SwarmDataCatcher(swarm)

if not args.setup_only:

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

    # Start the main loop
    swarm_handler.loop()

    # Stop the data catcher
    swarm_catcher.stop()
