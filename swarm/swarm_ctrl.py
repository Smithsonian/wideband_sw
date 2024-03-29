import pickle
import traceback
import argparse
import logging
from time import sleep
from threading import Event
from queue import Queue
from collections import OrderedDict
from traceback import format_exception
from redis import ConnectionError
from signal import (
    signal,
    SIGTERM,
    SIGINT,
    SIGHUP,
    SIGURG,
    SIGCONT,
    SIGALRM,
    )
import pyopmess

from swarm.core import ExceptingThread
from swarm.defines import *
from swarm import (
    SwarmDataCatcher,
    SwarmDataHandler,
    SwarmInput,
    Swarm,
    )
from rawbacks.check_ramp import CheckRamp
from rawbacks.save_rawdata import SaveRawData
from callbacks.calibrate_vlbi import CalibrateVLBI
from callbacks.log_stats import LogStats
from callbacks.sma_data import SMAData
from smax import SmaxRedisClient
from numpy import all

# Global variables
RETURN_VALUE = SMAINIT_QUIT_RTN
LOG_CHANNEL = "swarm.logs.ctrl"
RUNNING = Event()


# Custom Redis logging handler
class RedisHandler(logging.Handler):

    def __init__(self, channel, host='localhost', port=6379):
        self.redis = Redis(host=host, port=port)
        self.channel = channel
        super(RedisHandler, self).__init__()

    def emit(self, record):
        if record.exc_info:  # this is an exception
            record.msg += '\n' + traceback.format_exc(record.exc_info)
            record.exc_info = None  # clear the traceback
        try:
            self.redis.publish(self.channel, pickle.dumps(record))
        except ConnectionError:
            pass


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
stdout.setLevel(logging.DEBUG)
logger.addHandler(stdout)

# Also, log to a Redis channel
logredis = RedisHandler(LOG_CHANNEL)
logredis.setLevel(logging.INFO)
logger.addHandler(logredis)

# Connect to smax
smax = SmaxRedisClient()

# Use the logger redis instance to check vlbi status, and use defaults if they aren't set.
VLBI_MODE = logredis.redis.get("vlbi").decode()
if not VLBI_MODE:
    VLBI_MODE = "off"
REFERENCE_ANT_POL_CHUNK = logredis.redis.get("reference").decode()
if not REFERENCE_ANT_POL_CHUNK:
    REFERENCE_ANT_POL_CHUNK = "2,0,0"
VLBI_CALIBRATE = logredis.redis.get("calibrate").decode()
if not VLBI_CALIBRATE:
    VLBI_CALIBRATE = None

logger.info("VLBI Calibrate mode: %s" % VLBI_CALIBRATE)
# Create and set formatting
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')
stdout.setFormatter(formatter)
logredis.setFormatter(formatter)

# Silence all katcp messages
katcp_logger = logging.getLogger('katcp')
katcp_logger.setLevel(logging.CRITICAL)

# Parse the user's command line arguments
parser = argparse.ArgumentParser(
    description='Idle, setup, or catch and process visibility data from a set of SWARM ROACH2s')
parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
parser.add_argument('-m', '--swarm-mappings', dest='swarm_mappings', metavar='SWARM_MAPPINGS', nargs='+', default=[],
                    help='Use files SWARM_MAPPINGS to determine the SWARM input to IF mapping (default="{0}")'.format(
                        SWARM_MAPPINGS))
parser.add_argument('-i', '--interfaces', dest='interfaces', metavar='INTERFACES', nargs='+',
                    default=SWARM_LISTENER_INTERFACES,
                    help='listen for UDP data on INTERFACES (default="{0}")'.format(SWARM_LISTENER_INTERFACES))
parser.add_argument('-t', '--integrate-for', dest='itime', metavar='INTEGRATION-TIME', type=float, default=10.0,
                    help='integrate for approximately INTEGRATION-TIME seconds (default=30)')
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
parser.add_argument('--log-file', dest='log_file', metavar='LOGFILE',
                    help='Write logger output to LOGFILE')
parser.add_argument('--silence-loggers', nargs='+', default=[],
                    help='silence the output from C extensions such as pysendint')
parser.add_argument('--no-thread-setup', dest='thread_setup', action='store_false',
                    help='multi-thread the setup to make it faster')
args = parser.parse_args()

# Add file handler, if requested
if args.log_file:
    fh = logging.FileHandler(args.log_file)
    fh.setFormatter(formatter)
    logger.addHandler(fh)

# Set logging level given verbosity
if args.verbose:
    stdout.setLevel(logging.DEBUG)
else:
    stdout.setLevel(logging.DEBUG)

# Silence user-defined loggers
for logger_name in args.silence_loggers:
    logging.getLogger(logger_name).setLevel(logging.CRITICAL)

# Construct our reference input
reference_args = [['antenna', 2], ['chunk', 0], ['polarization', 0]]
for i, a in enumerate(REFERENCE_ANT_POL_CHUNK.split(',')):
    reference_args[i][1] = int(a)
reference = SwarmInput(**dict(reference_args))

# Create our SWARM instance
swarm = Swarm(map_filenames=args.swarm_mappings)

# Setup the data catcher class
swarm_catcher = SwarmDataCatcher(swarm)

# Create the data handler
swarm_handler = SwarmDataHandler(swarm, swarm_catcher.get_queue(), swarm_catcher.get_catch_queue())


# Signal handler for idling SWARM
def idle_handler(signum, frame):
    logger.info('Received signal #{0}; idling SWARM...'.format(signum))
    pyopmess.send(1, 1, 100, 'SWARM is now being idled')
    swarm_catcher.stop()  # stop waiting on data
    while not all(swarm.members_do(lambda fid, mbr: mbr.idle())):
        logger.info('SWARM idle unsuccessful, retrying...')
        sleep(5)


# Register it to SIGHUP
signal(SIGHUP, idle_handler)


# Signal handler for "cold-starting" SWARM
def cold_start_handler(signum, frame, adc_cal=None):

    idle_handler(signum, frame)

    if adc_cal is None:
        logger.info('Received signal #{0}; cold-starting SWARM...'.format(signum))
        pyopmess.send(1, 2, 100, 'SWARM cold-start beginning')
    else:
        logger.info('Received signal #{0}; warm-starting SWARM...'.format(signum))
        pyopmess.send(1, 2, 100, 'SWARM warm-start beginning')

    sleep(5)
    swarm.members_do(lambda fid, mbr: mbr._program('sma_corr.bof.gz'))

    # Wait some more time, cuz that's how we roll
    sleep(5)

    logger.info(
        "Beginning ROACH2 setup on all quadrants ({0} threading)...".format(
            "with" if args.thread_setup else "without"
        )
    )
    # Do the intial setup
    swarm.setup(
        args.itime,
        args.interfaces,
        delay_test=args.visibs_test,
        raise_qdr_err=args.raise_qdr_err,
        threaded=args.thread_setup,
        adc_cal=adc_cal,
        )

    # Switch to IF input
    swarm.members_do(lambda fid, mbr: mbr.set_source(2, 2))

    # Load all plugins
    swarm.members_do(lambda fid, mbr: mbr.reload_plugins())

    # Enable Walshing and fringe rotation
    swarm.quadrants_do(lambda qid, quad: quad.set_walsh_patterns())
    swarm.quadrants_do(lambda qid, quad: quad.set_sideband_states())
    swarm.quadrants_do(lambda qid, quad: quad.fringe_stopping(True))

    if adc_cal is None:
        # Wait some time for things to warm up
        logger.info('Waiting 2 minutes for FPGAs to warm up; please be patient')
        sleep(120)

        # Disable the ADC monitor
        swarm.members_do(lambda fid, mbr: mbr.send_katcp_cmd('stop-adc-monitor'))

        # Do a threaded calibration
        pyopmess.send(1, 3, 100, 'SWARM warm-calibrating the ADCs')
        exceptions_queue = Queue()
        adccal_threads = OrderedDict()
        for fid, member in swarm.get_valid_members():
            thread = ExceptingThread(
                exceptions_queue,
                logger=logger,
                target=member.calibrate_adc,
                )
            adccal_threads[thread] = member

        # Now start them all
        for thread in adccal_threads.keys():
            thread.start()

        # ...and immediately join them
        for thread in adccal_threads.keys():
            thread.join()

        # If there were exceptions log them
        exceptions = 0
        while not exceptions_queue.empty():
            exceptions += 1
            thread, exc = exceptions_queue.get()
            fmt_exc = format_exception(*exc)
            tb_str = ''.join(fmt_exc[0:-1])
            exc_str = ''.join(fmt_exc[-1])
            for line in tb_str.splitlines():
                adccal_threads[thread].logger.debug('<{0}> {1}'.format(exceptions, line))
            for line in exc_str.splitlines():
                adccal_threads[thread].logger.error('<{0}> {1}'.format(exceptions, line))

        # If any exception occurred raise error
        if exceptions > 0:
            pyopmess(1, 1, 100, 'Error occurred during ADC warm cal')
            raise RuntimeError('{0} member(s) had an error during ADC warm-calibration!'.format(exceptions))

        # Re-enable the ADC monitor
        swarm.members_do(lambda fid, mbr: mbr.send_katcp_cmd('start-adc-monitor'))

        # Finally, update the SMA-X database with the new values
        update_dict = swarm.members_do(lambda fid, mbr: {mbr.roach2_host: mbr.adc_cal})
        update_dict = {
            key: value for item in update_dict for jtem in item for key, value in jtem.items()
        }

        logger.info('Recording ADC calibration solutions')
        for roach2_host, value in update_dict.items():
            smax.smax_share("correlator:swarm:roach2:%s" % roach2_host, "mmcm_cal", value)

    # Start up catcher again
    swarm.reset_xengines_and_sync()
    swarm_catcher.start()
    if adc_cal is None:
        pyopmess.send(1, 4, 100, 'SWARM cold-start is finished')
    else:
        pyopmess.send(1, 4, 100, 'SWARM warm-start is finished')


# Register it to SIGURG
signal(SIGURG, cold_start_handler)


def warm_start_handler(signum, frame):
    # Grab the most recent metadata from SMA-X
    adc_cal = smax.smax_pull("correlator", "swarm")["correlator"]["swarm"]["roach2"]

    # Start a cold-start w/ adc-cal supplied, which will skip the ADC cal step
    cold_start_handler(signum, frame, adc_cal=adc_cal)


signal(SIGALRM, warm_start_handler)


# Signal handler to do a re-sync
def sync_handler(signum, frame):
    logger.info('Received signal #{0}; syncing SWARM...'.format(signum))
    swarm_catcher.stop()  # stop waiting on data
    swarm.sync()  # do the sync
    swarm_catcher.start()  # start waiting on data
    pyopmess.send(1, 1, 100, 'SWARM has been re-synced')


# Register it to SIGCONT
signal(SIGCONT, sync_handler)


# Signal handler to do a X-engine reset
def xeng_reset_handler(signum, frame):
    logger.info('Received signal #{0}; resetting X-eninges...'.format(signum))
    swarm.reset_xengines()
    pyopmess.send(1, 1, 100, 'SWARM X-engines reset')


# Register it to #24
signal(24, xeng_reset_handler)

if not args.disable_data_catcher:

    # If VLBI calibration is running, 2nd sideband phases need to be applied to correlator data
    rephase_2nd_sideband_data = False
    if VLBI_CALIBRATE == "low" or VLBI_CALIBRATE == "high":
        rephase_2nd_sideband_data = True

    # Use a callback to send data to dataCatcher/corrSaver
    swarm_handler.add_callback(SMAData, rephase_2nd_sideband_data=rephase_2nd_sideband_data)

if args.log_stats:

    # Use a callback to show visibility stats
    swarm_handler.add_callback(LogStats, reference=reference)

if VLBI_CALIBRATE == "low" or VLBI_CALIBRATE == "high":
    logger.info('VLBI callback added')
    # Use a callback to calibrate fringes for VLBI
    if VLBI_CALIBRATE == 'low':

        # Low SNR. Use single-channel solver and normalize corr. matrix
        swarm_handler.add_callback(CalibrateVLBI, single_chan=True, normed=True, reference=reference)

    elif VLBI_CALIBRATE == 'high':

        # High SNR. Use full spectrum solver and do not normalize corr. matrix
        swarm_handler.add_callback(CalibrateVLBI, single_chan=False, normed=False, reference=reference)

if args.save_rawdata:

    # Give a rawback that saves the raw data
    swarm_catcher.add_rawback(SaveRawData)

if args.visibs_test:

    # Give a rawback that checks for ramp errors
    swarm_catcher.add_rawback(CheckRamp)

# Reset the xengines until window counters to by in sync
swarm.reset_xengines_and_sync()

# test to set visibilities.
# swarm.set_chunk_delay()

# Start the data catcher
swarm_catcher.start()

try:

    # Start the main loop
    RUNNING.set()
    swarm_handler.loop(RUNNING)

except Exception as err:

    # Some other exception detected
    RETURN_VALUE = SMAINIT_SYSERR_RTN
    logger.error("Exception caught: {0}".format(err))
    pyopmess.send(1, 1, 100, 'Listener crashed; should restart but check smainit status')

# Stop the data catcher
swarm_catcher.stop()

# Finish up and get out
logger.info("Exiting normally")
sys.exit(RETURN_VALUE)
