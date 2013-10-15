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

roach     = sys.argv[1] #'roach2-02'

if roach=='roach2-01':
	final_hex = 10
elif roach=='roach2-02':
	final_hex = 12
elif roach=='roach2-03':
	final_hex = 14
elif roach=='roach2-04':
	final_hex = 16
elif roach=='roach2-05':
	final_hex = 18
elif roach=='roach2-07':
	final_hex = 20
elif roach=='roach2-08':
	final_hex = 22
elif roach=='roach2-09':
	final_hex = 24
else:
	raise Exception("ROACH2 not supported!")


logging.basicConfig()
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
logger = logging.getLogger()
logger.setLevel(logging.INFO)
logger.handlers[0].setFormatter(formatter)


RECEIVE_FROM = "192.168.10." + str(final_hex+50)
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
        data = {}
        last_pkt_n = -1
        self._create_socket()
        while not self.stopevent.isSet():
            try:
                datar, addr = self.udp_sock.recvfrom(1025*8)
            except timeout:
                continue
	    #self.logger.info("received a packet!")
            pkt_n, acc_n = unpack('>II', datar[:8])
            if not data.has_key(addr):
                data[addr] = {}
            if not data[addr].has_key(acc_n):
                data[addr][acc_n] = [None]*256
            data[addr][acc_n][pkt_n] = datar[8:]
            if data[addr][acc_n].count(None)==0:
                self.logger.info("received accumulation %d from %r" %(acc_n, addr))
                temp_data = ''.join(data[addr].pop(acc_n))
                if addr[0]==RECEIVE_FROM:
                    self.queue.put(temp_data)
            # if (pkt_n <> last_pkt_n+1) and (pkt_n <> 0):
            #     self.logger.error("received packet %d:#%d out of order" %(acc_n, pkt_n))
            if len(datar) <> 1025*8:
                self.logger.error("received packet %d:#%d is of wrong size, %d bytes" %(acc_n, pkt_n, len(datar)))
            last_pkt_n = pkt_n
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

    subfile = open('/common/bin/subscribers', 'r')
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
