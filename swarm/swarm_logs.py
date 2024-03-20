import time, sys, pickle, logging, logging.handlers, argparse
from signal import signal, SIGQUIT, SIGTERM, SIGINT
from redis import Redis, ConnectionError
from threading import Event
from swarm.defines import *

# Global variables
SHORTLOG_NAME = '/global/logs/swarm/all.short.log'
LOGFILE_NAME = '/global/logs/swarm/all.log'
RETRY_PERIOD = 60
RUNNING = Event()

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Also, log to rotating file handler
logfile = logging.handlers.TimedRotatingFileHandler(LOGFILE_NAME, when='midnight', interval=1, backupCount=90)
logfile.setLevel(logging.DEBUG)
logger.addHandler(logfile)

# Create and set formatting
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
stdout.setFormatter(formatter)
logfile.setFormatter(formatter)

# And a short version of the log handler
shortlog = logging.handlers.RotatingFileHandler(SHORTLOG_NAME, maxBytes=5*1024*1024, backupCount=1)
shortlog.setLevel(logging.DEBUG)
logger.addHandler(shortlog)

# Use shorter width formatting for short log
shortform = logging.Formatter('%(asctime)s:%(levelname)s:%(message)s', '%m-%d %H:%M:%S')
shortlog.setFormatter(shortform)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Read various SWARM registers and memory and write to Redis server')
parser.add_argument('-v', dest='verbose', action='store_true', help='Display debugging logs')
parser.add_argument('--redis-host', dest='redis_host', help='Redis host name (default="localhost")', default='localhost')
parser.add_argument('--redis-port', dest='redis_port', help='Redis port number (default=6379)', type=int, default=6379)
args = parser.parse_args()

# Set logging level given verbosity
if args.verbose:
    stdout.setLevel(logging.DEBUG)
else:
    stdout.setLevel(logging.INFO)

# Create our Redis client instance
redis = Redis(args.redis_host, args.redis_port)

# Exit signal handler
def quit_handler(signum, frame):
    logger.info("Received signal #{0}; Quitting...".format(signum))
    RUNNING.clear() # clear first, then send message to cause a check
    redis.publish('swarm.logs.kill', '')

# Register the exit handler
EXIT_ON = (SIGQUIT, SIGTERM, SIGINT)
for sig in EXIT_ON:
    signal(sig, quit_handler)

# Loop continously, in case of errors
RUNNING.set()
while RUNNING.is_set():

    try: # Unless exception is caught

        # Create our PubSub object and subscribe to swarm channels
        pubsub = redis.pubsub()
        pubsub.psubscribe('swarm.logs.*')

        # Loop over every published message
        logger.info('Starting redis->file logging')
        for msg in pubsub.listen():

            # Check again if we should run
            if not RUNNING.is_set():
                break

            # Only respond to actual messages
            if msg['type'] == 'pmessage':

                # De-pickle the message
                try:
                    record = pickle.loads(msg['data'])
                except:
                    logger.error('Message cannot be unpickled!')

                # Then handle the message
                logger.handle(record)

    except (RuntimeError, ConnectionError) as err:
        logger.error("Exception caught: {0}".format(err))
        time.sleep(RETRY_PERIOD)
        continue

logger.info("Exiting normally")
sys.exit(SMAINIT_QUIT_RTN)
