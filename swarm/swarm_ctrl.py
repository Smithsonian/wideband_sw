#!/usr/bin/env python2.7

import logging, argparse

from swarm import (
    SWARM_XENG_SIDEBANDS,
    SWARM_MAPPING_CHUNKS,
    SWARM_MAPPING,
    SwarmDataHandler,
    SwarmListener,
    Swarm,
    )
from rawbacks.save_rawdata import *
from callbacks.calibrate_vlbi import *
from callbacks.check_ramp import *
from callbacks.log_stats import *


DEFAULT_BITCODE = 'sma_corr_2015_Feb_10_1113.bof.gz'


def main():

    # Setup some basic logging
    logging.basicConfig()
    formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
    logger = logging.getLogger()
    logger.handlers[0].setFormatter(formatter)

    # Parse the user's command line arguments
    parser = argparse.ArgumentParser(description='Catch and process visibility data from a set of SWARM ROACH2s')
    parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
    parser.add_argument('-m', '--swarm-mapping', dest='swarm_mapping', metavar='SWARM_MAPPING', type=str, default=SWARM_MAPPING,
                        help='Use file SWARM_MAPPING to determine the SWARM input to IF mapping (default="%s")' % SWARM_MAPPING)
    parser.add_argument('-i', '--interface', dest='interface', metavar='INTERFACE', type=str, default='eth2',
                        help='listen for UDP data on INTERFACE (default="eth2")')
    parser.add_argument('-b', '--bitcode', dest='bitcode', metavar='BITCODE', type=str, default=DEFAULT_BITCODE,
                        help='program ROACH2s with BITCODE (default="%s")' % DEFAULT_BITCODE)
    parser.add_argument('-t', '--integrate-for', dest='itime', metavar='INTEGRATION-TIME', type=float, default=28.45,
                        help='integrate for approximately INTEGRATION-TIME seconds (default=30)')
    parser.add_argument('--setup-only', dest='setup_only', action='store_true',
                        help='only program and setup the board; do not wait for data')
    parser.add_argument('--listen-only', dest='listen_only', action='store_true',
                        help='do NOT setup the board; only wait for data')
    parser.add_argument('--visibs-test', dest='visibs_test', action='store_true',
                        help='enable the DDR3 visibility ramp test')
    parser.add_argument('--save-raw-data', dest='save_rawdata', action='store_true',
                        help='Save raw data from each FID to file')
    parser.add_argument('--log-stats', dest='log_stats', action='store_true',
                        help='Print out some baselines statistics (NOTE: very slow!)')
    args = parser.parse_args()

    # Set logging level given verbosity
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    # Silence katcp INFO messages
    katcp_logger = logging.getLogger('katcp')
    katcp_logger.setLevel(logging.WARNING)

    # Setup the listener class
    listener = SwarmListener(args.interface)

    # Create our SWARM instance
    swarm = Swarm()

    if not args.listen_only:

        # Setup using the Swarm class and our parameters
        swarm.setup(args.bitcode, args.itime, listener)

    if args.visibs_test:

        # Enable the visibs test
        swarm.members_do(lambda fid, member: member.visibs_delay(delay_test=True))

    else:

        # Disable the visibs test
        swarm.members_do(lambda fid, member: member.visibs_delay(delay_test=False))

    if not args.setup_only:

        # Create the data handler 
        swarm_handler = SwarmDataHandler(swarm, listener)

        if args.log_stats:

            # Use a callback to show visibility stats
            swarm_handler.add_callback(log_stats)
            swarm_handler.add_callback(calibrate_vlbi)

        if args.save_rawdata:

            # Give a rawback that saves the raw data
            swarm_handler.add_rawback(save_rawdata)

        if args.visibs_test:

            # Give a rawback that checks for ramp errors
            swarm_handler.add_rawback(check_ramp)

        # Start the main loop
        swarm_handler.loop()



# Do main if not imported
if __name__=="__main__": main()
