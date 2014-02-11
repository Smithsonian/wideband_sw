#!/usr/bin/env python

import sys, logging
from itertools import chain
from time import time, sleep
from struct import pack, unpack
from Queue import Queue, Empty
from threading import Thread, Event, active_count
from socket import (
    socket, timeout, error,
    AF_INET, SOCK_DGRAM, SOCK_STREAM,
    SOL_SOCKET, SO_RCVBUF, SO_SNDBUF,
    )
from numpy import (
    pi, sin, sqrt, abs, angle, log10, real, imag, nan,
    array, linspace, arange, empty, ones,
    savetxt, 
    )
import xeng_order
import pysendint

dc_roaches = ['192.168.10.64', '192.168.10.72']

logging.basicConfig()
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
logger = logging.getLogger()
logger.setLevel(logging.INFO)
logger.handlers[0].setFormatter(formatter)


LISTEN_HOST = "0.0.0.0" #"192.168.10.17"
LISTEN_PORT = 4100
class DataCatcher(Thread):

    def __init__(self, queue, stopevent):
        self.queue = queue
        self.stopevent = stopevent
        self.logger = logging.getLogger('root.DataCatcher')
        Thread.__init__(self)

    def _create_socket(self):
        self.udp_sock = socket(AF_INET, SOCK_DGRAM)
        self.udp_sock.bind((LISTEN_HOST, LISTEN_PORT))
        self.udp_sock.settimeout(2.0)

    def _close_socket(self):
        self.udp_sock.close()

    def run(self):
        self.logger.info('DataCatcher has started')
        self._create_socket()

        data = {}
        while not self.stopevent.isSet():

            # Receive a packet and get host info
            try:
                datar, addr = self.udp_sock.recvfrom(1025*8)
            except timeout:
                continue
            addr = addr[0]

	    # Check if packet is wrong size
            if len(datar) <> 1025*8:
                self.logger.error("received packet %d:#%d is of wrong size, %d bytes" %(acc_n, pkt_n, len(datar)))
		continue

	    # Unpack it to get packet # and accum #
            pkt_n, acc_n = unpack('>II', datar[:8])

	    # Initialize data buffer, if necessary
            if not data.has_key(acc_n):
                data[acc_n] = {dc_roaches[0]: [None]*256, dc_roaches[1]: [None]*256}

            # Then store data in it
            data[acc_n][addr][pkt_n] = datar[8:]

	    # If we've gotten all pkts for acc_n for both direct-connect ROACH2's
            if data[acc_n][dc_roaches[0]].count(None)==0 and \
			data[acc_n][dc_roaches[1]].count(None)==0:
                self.logger.info("received full accumulation #%d" %acc_n)
                acc_data = data.pop(acc_n)
                temp_data = ''.join(acc_data[dc_roaches[0]])
                temp_data += ''.join(acc_data[dc_roaches[1]])

		# Put data onto queue
		self.queue.put(temp_data)

        self.logger.info('DataCatcher has stopped')
        self._close_socket()


data_queue = Queue()
data_stopper = Event()
data_catcher = DataCatcher(data_queue, data_stopper)
data_catcher.setDaemon(True)
data_catcher.start()


# Function for sorting baselines
def order_baselines(baseline):
    left, right = baseline
    if left == right:
        return left
    else:
        return (left + right) * 100

valid_inputs = range(8)
baselines = sorted(xeng_order.visib_order.keys(), key=order_baselines)
ind = array(list(j + i for i in range(0, 256*64*2, 64*2) for j in range(0, 8*2, 2)))

n = 0
while active_count() > 0:

    try:
        datas = data_queue.get_nowait()
	sleep(0.5)
    except Empty:
	continue
    except KeyboardInterrupt:
        data_stopper.set()
	data_catcher.join()
	break

    # Unpack data
    data = unpack('>%di' % (524288*2), datas)
    data_dc0 = data[:len(data)/2]
    data_dc1 = data[len(data)/2:]

    # Scan time
    scan_time = time()

    for baseline in baselines:

        # Is this one an auto?
        left, right = baseline
	if left == right:
            auto = True
        else:
            auto = False

        # Initialize empty output data
        lsb_data = empty(2**15)
        lsb_data.fill(nan)

        # And fill it in 
        offset = xeng_order.visib_order[baseline]

        # Chan [0-7] * n
        lsb_data[ind]  = data_dc0[offset::256]
        if not auto: lsb_data[ind+1]  = data_dc0[offset+1::256]

        # Chan [8-15] * n
        lsb_data[ind+16] = data_dc1[offset::256]
        if not auto: lsb_data[ind+17] = data_dc1[offset+1::256]

        if (left in valid_inputs) and (right in valid_inputs): 
            pysendint.send_integration(scan_time, 32 * ((2**25)/(52e6)), 0, left+1, 1, right+1, 1, lsb_data, lsb_data, 0)
            pysendint.send_integration(scan_time, 32 * ((2**25)/(52e6)), 1, left+1, 1, right+1, 1, lsb_data, lsb_data, 0)
            # savetxt("lsb_%dx%d.dat" % (left, right), lsb_data)
            logger.info("processed baseline: %dx%d" % (left+1, right+1))
    
    logger.info("processed all baselines")

    n = n + 1
