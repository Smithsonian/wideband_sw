import time, sys, logging, logging.handlers, argparse
from signal import signal, SIGQUIT, SIGTERM, SIGINT
from redis import Redis, ConnectionError
from threading import Event
from swarm import *
import pyopmess

# Global variables
LOGFILE_NAME = '/global/logs/swarm/checks.log'
ERROR_THRESHOLD = 500
RUNNING = Event()
PERIOD = 600

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

# Exit signal handler
def quit_handler(signum, frame):
    logger.info("Received signal #{0}; Quitting...".format(signum))
    RUNNING.clear()

# Register the exit handler
EXIT_ON = (SIGQUIT, SIGTERM, SIGINT)
for sig in EXIT_ON:
    signal(sig, quit_handler)

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Also, log to rotating file handler
logfile = logging.handlers.TimedRotatingFileHandler(LOGFILE_NAME, when='midnight', interval=1, backupCount=90)
logfile.setLevel(logging.INFO)
logger.addHandler(logfile)

# Create and set formatting
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
stdout.setFormatter(formatter)
logfile.setFormatter(formatter)

# Silence all katcp messages
katcp_logger = logging.getLogger('katcp')
katcp_logger.setLevel(logging.CRITICAL)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Read various SWARM registers and memory and write to Redis server')
parser.add_argument('-v', dest='verbose', action='store_true', help='Display debugging logs')
parser.add_argument('--redis-host', dest='redis_host', help='Redis host name (defailt="localhost")', default='localhost')
parser.add_argument('--redis-port', dest='redis_port', help='Redis port number (defailt=6379)', type=int, default=6379)
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=[],
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(SWARM_MAPPINGS))
args = parser.parse_args()

# Set logging level given verbosity
if args.verbose:
    stdout.setLevel(logging.DEBUG)
else:
    stdout.setLevel(logging.INFO)

# Create our SWARM instance
swarm = Swarm(map_filenames=args.swarm_mappings)
reg_fmt = 'network_eth_{0}_{1}_pkt_cnt'

# Create our Redis client instance
redis = Redis(args.redis_host, args.redis_port)
key_fmt = 'swarm.monitor.corner-turn.{3}.qid{0}.fid{1}.core{2}'

# Loop continously
RUNNING.set()
logger.info('Starting checks loop')
while RUNNING.is_set():

    try: # Unless exception is caught

        # Log the wait period
        logger.debug('Waiting {0} seconds'.format(PERIOD))

        # Sleep first, in case we're in exception loop
        time.sleep(PERIOD)

        # Check again if we should run
        if not RUNNING.is_set():
            break

        # Log that the iteration is starting
        logger.debug("Checks starting")

        # Track total errors
        errors = 0

        # Cycle through all quadrants
        for quad in swarm.quads:

            # Cycle through all (valid) members
            for fid, member in quad.get_valid_members():

                # Cycle through all four corner-turn ports
                for core in range(4):

                    # Cycle through missing/extra
                    for regtype in ['missing', 'extra']:

                        # Read current register values
                        reg = reg_fmt.format(core, regtype)
                        new = member.fpga.read_int(reg)
                        errors += abs(new)

                        # Get the old value from redis
                        key = key_fmt.format(quad.qid, fid, core, regtype)
                        old = redis.get(key)

                        # Spew error if the error count changed
                        if (old is not None) and (int(old) != new):
                            member.logger.warning('{0} changed from {1} to {2}'.format(reg, old, new))

                        # Write to redis with expiration of 10 seconds
                        redis.setex(key, PERIOD*2, new)

        # If error threshold is exceeded, generate operator message
        if errors > ERROR_THRESHOLD:
            logger.warning('Total corner-turn errors, {0}, exceed threshold'.format(errors))
            pyopmess.send(1, 1, PERIOD, 'Corner-turn errors exceed threshold; check autos')

        # Log that the iteration is done
        logger.debug("Checks done")

    except (RuntimeError, ConnectionError) as err:
        logger.error("Exception caught: {0}".format(err))
        continue

logger.info("Exiting normally")
sys.exit(SMAINIT_QUIT_RTN)
