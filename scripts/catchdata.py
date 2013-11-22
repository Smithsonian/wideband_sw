#!/usr/bin/env python

import sys, logging
from time import sleep
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

    # Swap endiannes
    data = array(unpack('>524288i', datas))
    datas = pack('<524288i', *data)
    
    # # Ant 0 autos
    # ant0pol0_auto = (2**-6)*data[0::256]
    # ant0pol1_auto = (2**-6)*data[1::256]
    # ant0pol0_x_ant0pol1 = (2**-6)*(data[2::256] + 1j*data[3::256])

    # # Ant 1 autos
    # ant1pol0_auto = (2**-6)*data[12::256]
    # ant1pol1_auto = (2**-6)*data[13::256]
    # ant1pol0_x_ant1pol1 = (2**-6)*(data[14::256] + 1j*data[15::256])

    # # Ant 0 cross Ant 1
    # ant0pol0_x_ant1pol0 = (2**-6)*(data[4::256] + 1j*data[5::256])
    # ant0pol1_x_ant1pol1 = (2**-6)*(data[6::256] + 1j*data[7::256])
    # ant0pol0_x_ant1pol1 = (2**-6)*(data[8::256] + 1j*data[9::256])
    # ant0pol1_x_ant1pol0 = (2**-6)*(data[10::256] + 1j*data[11::256])

    # savetxt('data.txt', cross.view(float).reshape(-1, 2))

    # with open('data.txt', 'w') as file_:
    #     for chan in range(2048):
    #         line = "{:d} ".format(chan)
    # 	    for base in range(16):
    #             line += "{:.6f} ".format((2**-6)*data[chan*256 + base])
    # 	    file_.write(line + "\n")

    subfile = open('/application/bin/subscribers', 'r')
    subs = list(sub.rstrip() for sub in subfile.readlines())

    for sub in subs:
        sock = socket(AF_INET, SOCK_STREAM)
	try:
            sock.connect((sub, 54321))
        except error:
            logger.error("%s: subscriber not ready" % sub)
	    continue
	sock.setsockopt(SOL_SOCKET, SO_SNDBUF, 536870912)
	sock.send(datas)
	sock.close()
	logger.info("Sent [%d, %d, ..., %d] to %s" % (data[0], data[1], data[-1], sub))
