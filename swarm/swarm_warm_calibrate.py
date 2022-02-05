#!/usr/local/anaconda/envs/swarm/bin/python
import argparse
import logging
from collections import OrderedDict
import sys
import time
from queue import Queue
from traceback import format_exception

import pyopmess
from swarm import Swarm
from swarm.core import ExceptingThread
from swarm.defines import query_yes_no, SWARM_MAPPING_CHUNKS, SWARM_MAPPINGS


# Setup root logger
logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(name)-30s: %(asctime)s : %(levelname)-8s %(message).140s')

# Stream to stdout
stdout = logging.StreamHandler(sys.stdout)
stdout.setLevel(logging.INFO)
logger.addHandler(stdout)

# Set up command line parameter parsing.
parser = argparse.ArgumentParser(description='Script to load bitcode, plugins and warm calibrate the roach2s')
parser.add_argument('integers', metavar='N', type=int, nargs='+',
                    help='Enter a space delimited list of integers representing SWARM quadrants. '
                         'ex: swarm_warm_calibrate 5 6')
args = parser.parse_args()

# Do a little error checking to make sure the input params are at least valid swarm quadrants.
quad_mappings = OrderedDict()
for num in args.integers:
    if (num - 1) in SWARM_MAPPING_CHUNKS:
        quad_mappings[num] = (SWARM_MAPPINGS[num - 1])
    else:
        print((str(num) + " is not a valid SWARM quadrant and will be ignored."))

# Present the request and ask to proceed to IDLE quadrants.
quad_string = " ".join(map(str, list(quad_mappings.keys())))
if quad_mappings and query_yes_no("Proceed to load bitcode and reload plugins for quadrant(s) " + quad_string + "?"):

    # Instantiate a Swarm object using the disabled quadrant mappings.
    pyopmess.send(1, 1, 100, "SWARM quadrant(s) " + quad_string + " starting ADC warm calibration")
    swarm = Swarm(mappings_dict=quad_mappings)
    swarm.members_do(lambda fid, mbr: mbr.load_bitcode())
    swarm.members_do(lambda fid, mbr: mbr.set_source(2, 2))
    swarm.members_do(lambda fid, mbr: mbr.reload_plugins())

    # Wait some time for things to warm up
    logger.info('Waiting 2 minutes for FPGAs to warm up; please be patient')
    time.sleep(120)

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
            target=member.warm_calibrate_adc,
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
