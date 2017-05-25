#!/usr/bin/env python2.7

import time, sys, logging, logging.handlers, argparse
from swarm import *
import pydsm

# Global variables
LOGFILE_NAME = '/global/logs/swarm/dsm.log'
PERIOD = 1

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Also, log to rotating file handler
logfile = logging.handlers.TimedRotatingFileHandler(LOGFILE_NAME, when='midnight', interval=1, backupCount=10)
logfile.setLevel(logging.DEBUG)
logger.addHandler(logfile)

# Create and set formatting
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
stdout.setFormatter(formatter)
logfile.setFormatter(formatter)

# Silence all katcp messages
katcp_logger = logging.getLogger('katcp')
katcp_logger.setLevel(logging.CRITICAL)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Read source information from newdds via DSM and copy to the ROACH2s')
parser.add_argument('-v', dest='verbose', action='store_true', help='Display debugging logs')
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=SWARM_MAPPINGS,
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(SWARM_MAPPINGS))
args = parser.parse_args()

# Create our SWARM instance
swarm = Swarm(map_filenames=args.swarm_mappings)

# This function takes the newdds dictionary and 
# sets the ROACH-specific DSM data
def copy_source_geom(source_geom, member):

    # Create our dict first (copied from dsm_allocation)
    ant0 = member[0].ant
    ant1 = member[1].ant
    geom_dict = {
        'SOURCE_RA_D'       : source_geom['SOURCE_RA_D'][0],
        'SOURCE_NAME_C24'   : source_geom['SOURCE_NAME_C24'][0],
        'UT1MUTC_D'         : source_geom['UT1MUTC_D'][0],
        'GEOM_DELAY_A_V2_D' : (
            source_geom['GEOM_DELAY_A_V9_D'][0][ant0],
            source_geom['GEOM_DELAY_A_V9_D'][0][ant1],
            ),
        'GEOM_DELAY_B_V2_D' : (
            source_geom['GEOM_DELAY_B_V9_D'][0][ant0],
            source_geom['GEOM_DELAY_B_V9_D'][0][ant1],
            ),
        'GEOM_DELAY_C_V2_D' : (
            source_geom['GEOM_DELAY_C_V9_D'][0][ant0],
            source_geom['GEOM_DELAY_C_V9_D'][0][ant1],
            ),
        }

    # Finally write it to our ROACH2 DSM host
    pydsm.write(member.roach2_host, 'SWARM_SOURCE_GEOM_X', geom_dict)

# Loop continously
logger.info('Starting DSM copy loop')
while True:

    try: # Unless exception is caught

        # Sleep first, in case we're in exception loop
        time.sleep(PERIOD)

        # Read source info from newdds
        source_geom = pydsm.read("newdds", "DDS_TO_TENZING_X")

        # Finally copy them over to every ROACH
        swarm.members_do(lambda fid, member: copy_source_geom(source_geom, member))

    except RuntimeError as err:
        logger.error("Exception caught: {0}".format(err))
        continue

    except (KeyboardInterrupt, SystemExit):
        logger.info("Exiting normally")
        break
