#!/usr/bin/env python2.7

import time, sys, pickle, logging, logging.handlers, argparse
from redis import StrictRedis, ConnectionError

# Global variables
LOGFILE_NAME = '/global/logs/swarm/all.log'
RETRY_PERIOD = 60

# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

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

# Parse the user's command line arguments
parser = argparse.ArgumentParser(description='Read various SWARM registers and memory and write to Redis server')
parser.add_argument('-v', dest='verbose', action='store_true', help='Display debugging logs')
parser.add_argument('--redis-host', dest='redis_host', help='Redis host name (defailt="localhost")', default='localhost')
parser.add_argument('--redis-port', dest='redis_port', help='Redis port number (defailt=6379)', type=int, default=6379)
args = parser.parse_args()

# Set logging level given verbosity
if args.verbose:
    stdout.setLevel(logging.DEBUG)
else:
    stdout.setLevel(logging.INFO)

# Create our Redis client instance
redis = StrictRedis(args.redis_host, args.redis_port)

# Loop continously, in case of errors
while True:

    try: # Unless exception is caught

        # Create our PubSub object and subscribe to swarm channels
        pubsub = redis.pubsub()
        pubsub.psubscribe('swarm.logs.*')

        # Loop over every published message
        logger.info('Starting redis->file logging')
        for msg in pubsub.listen():

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

    except (KeyboardInterrupt, SystemExit):
        logger.info("Exiting normally")
        break
