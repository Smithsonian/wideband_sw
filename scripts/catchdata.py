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
    pi, sin, sqrt, abs, angle, log10, real, imag,
    array, linspace, arange, zeros, ones,
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

    # Re-order X-engine output
    zero_pkt = (0,) * 2048

    # Function for sorting baselines
    def order_baselines(baseline):
        left, right = baseline
        if left == right:
            return left
        else:
            return left  * 100

    baselines = sorted(xeng_order.visib_order.keys(), key=order_baselines)
    for baseline in baselines:

        # Is this one an auto?
        left, right = baseline
	if left == right:
            auto = True
        else:
            auto = False

        # Initialize output data
        lsb_data = zeros(2**15)

        # And fill it in 
        offsets = xeng_order.visib_order[baseline]

        for chan in range(4):

            # Chan 0 * n
            lsb_data [chan*32+0::16*8] = data_dc0[256*0+offsets[chan]  ::256*8]
            if not auto: lsb_data [chan*32+1::16*8] = data_dc0[256*0+offsets[chan]+1::256*8]

            # Chan 1 * n
            lsb_data [chan*32+2::16*8] = data_dc0[256*1+offsets[chan]  ::256*8]
            if not auto: lsb_data [chan*32+3::16*8] = data_dc0[256*1+offsets[chan]+1::256*8]
    
            # Chan 2 * n
            lsb_data [chan*32+4::16*8] = data_dc1[256*0+offsets[chan]  ::256*8]
            if not auto: lsb_data [chan*32+5::16*8] = data_dc1[256*0+offsets[chan]+1::256*8]
    
            # Chan 3 * n
            lsb_data [chan*32+6::16*8] = data_dc1[256*1+offsets[chan]  ::256*8]
            if not auto: lsb_data [chan*32+7::16*8] = data_dc1[256*1+offsets[chan]+1::256*8]
    
            # Chan 4 * n
            lsb_data [chan*32+8::16*8] = data_dc0[256*4+offsets[chan]  ::256*8]
            if not auto: lsb_data [chan*32+9::16*8] = data_dc0[256*4+offsets[chan]+1::256*8]
    
            # Chan 5 * n
            lsb_data[chan*32+10::16*8] = data_dc0[256*5+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+11::16*8] = data_dc0[256*5+offsets[chan]+1::256*8]
    
            # Chan 6 * n
            lsb_data[chan*32+12::16*8] = data_dc1[256*4+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+13::16*8] = data_dc1[256*4+offsets[chan]+1::256*8]
    
            # Chan 7 * n
            lsb_data[chan*32+14::16*8] = data_dc1[256*5+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+15::16*8] = data_dc1[256*5+offsets[chan]+1::256*8]
    
            ########
    
            # Chan 8 * n
            lsb_data[chan*32+16::16*8] = data_dc0[256*2+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+17::16*8] = data_dc0[256*2+offsets[chan]+1::256*8]
    
            # Chan 9 * n
            lsb_data[chan*32+18::16*8] = data_dc0[256*3+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+19::16*8] = data_dc0[256*3+offsets[chan]+1::256*8]
    
            # Chan 10 * n
            lsb_data[chan*32+20::16*8] = data_dc1[256*2+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+21::16*8] = data_dc1[256*2+offsets[chan]+1::256*8]
    
            # Chan 11 * n
            lsb_data[chan*32+22::16*8] = data_dc1[256*3+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+23::16*8] = data_dc1[256*3+offsets[chan]+1::256*8]
    
            # Chan 12 * n
            lsb_data[chan*32+24::16*8] = data_dc0[256*6+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+25::16*8] = data_dc0[256*6+offsets[chan]+1::256*8]
    
            # Chan 13 * n
            lsb_data[chan*32+26::16*8] = data_dc0[256*7+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+27::16*8] = data_dc0[256*7+offsets[chan]+1::256*8]
    
            # Chan 14 * n
            lsb_data[chan*32+28::16*8] = data_dc1[256*6+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+29::16*8] = data_dc1[256*6+offsets[chan]+1::256*8]
    
            # Chan 15 * n
            lsb_data[chan*32+30::16*8] = data_dc1[256*7+offsets[chan]  ::256*8]
            if not auto: lsb_data[chan*32+31::16*8] = data_dc1[256*7+offsets[chan]+1::256*8]

        pysendint.send_integration(time(), 32 * ((2**25)/(52e6)), 0, left, 1, right, 1, lsb_data, lsb_data, 0)
        pysendint.send_integration(time(), 32 * ((2**25)/(52e6)), 1, left, 1, right, 1, lsb_data, lsb_data, 0)
