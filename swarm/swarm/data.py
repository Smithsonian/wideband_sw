import math, logging, fcntl
from time import time, sleep
from struct import pack, unpack
from Queue import Queue, Empty
from threading import Thread, Event, active_count
from itertools import combinations
from socket import (
    socket, timeout, error, inet_ntoa,
    AF_INET, SOCK_DGRAM, SOCK_STREAM,
    SOL_SOCKET, SO_RCVBUF, SO_SNDBUF,
    )

from numpy import array, nan

from pysendint import send_integration
from defines import *
from xeng import (
    SwarmBaseline, 
    SwarmXengine,
    )


SIOCGIFADDR = 0x8915
SIOCSIFHWADDR  = 0x8927

INNER_RANGE = range(0, SWARM_XENG_PARALLEL_CHAN * 2, 2)
OUTER_RANGE = range(0, SWARM_CHANNELS * 2, SWARM_XENG_TOTAL * 2)
DATA_FID_IND = array(list(j + i for i in OUTER_RANGE for j in INNER_RANGE))

EMPTY_DATA_ARRAY = array([nan,] * SWARM_CHANNELS * 2)

class SwarmDataPackage:

    def __init__(self, swarm):

        # Set all initial members
        self.swarm = swarm
        self._inputs = list(self.swarm[i][j] for i in range(self.swarm.fids_expected) for j in (0, 1))
        self._cross = list(SwarmBaseline(i, j) for i, j in combinations(self._inputs, r=2) if i._chk==j._chk)
        self._autos = list(SwarmBaseline(i, i) for i in self._inputs)
        self.baselines = self._autos + self._cross
        self._init_data()

    def __getitem__(self, baseline):
        return self.data[baseline]

    def _init_data(self):

        # Initialize our data array
        self.data = {}

        # Initialize baselines
        for baseline in self.baselines:
            self.data[baseline] = {}
            
            # Initialize chunk
            for chk in SWARM_MAPPING_CHUNKS:
                self.data[baseline][chk] = {}

                # Initialize sidebands
                for sb in SWARM_XENG_SIDEBANDS:
                    self.data[baseline][chk][sb] = EMPTY_DATA_ARRAY.copy()

    def set_data(self, xeng_word, fid, data):

        # Get info from the data word
        imag = xeng_word.imag
        imag_off = int(imag)
        baseline = xeng_word.baseline
        sideband = xeng_word.sideband
        chunk = xeng_word.left._chk

        slice_ = DATA_FID_IND + fid * SWARM_XENG_PARALLEL_CHAN * 2 + imag_off
        try: # normal conjugation first

            # Fill this baseline
            self.data[baseline][chunk][sideband][slice_] = data.copy()

            # Special case for autos, fill imag with zeros
            if baseline.is_auto():
                self.data[baseline][chunk][sideband][slice_+1] = 0.0

        except KeyError:

            # Conjugate the baseline
            conj_baseline = SwarmBaseline(baseline.right, baseline.left)

            # Try the conjugated baseline
            self.data[conj_baseline][chunk][sideband][slice_] = data.copy()


class SwarmListener:

    def __init__(self, interface, port=4100):
        self.logger = logging.getLogger('SwarmListener')
        self.interface = interface
        self._set_netinfo(port)

    def _set_netinfo(self, port=4100):
        s = socket(AF_INET, SOCK_DGRAM)
        info_adr = fcntl.ioctl(s.fileno(), SIOCGIFADDR, pack('256s', self.interface[:15]))
        info_mac = fcntl.ioctl(s.fileno(), SIOCSIFHWADDR, pack('256s', self.interface[:15]))
        self.mac = unpack('>Q', '\x00'*2 + info_mac[18:24])[0]
        self.ip = unpack(SWARM_REG_FMT, info_adr[20:24])[0]
        self.host = inet_ntoa(pack(SWARM_REG_FMT, self.ip))
        self.port = port
        s.close()


class SwarmDataCatcher(Thread):

    def __init__(self, queue, stopevent,
                 listen_host, listen_port):
        self.queue = queue
        self.stopevent = stopevent
        self.listen_on = (listen_host, listen_port)
        self.logger = logging.getLogger('SwarmDataCatcher')
        Thread.__init__(self)

    def _create_socket(self):
        self.udp_sock = socket(AF_INET, SOCK_DGRAM)
        self.udp_sock.bind(self.listen_on)
        self.udp_sock.settimeout(2.0)

    def _close_socket(self):
        self.udp_sock.close()

    def run(self):
        self.logger.info('SwarmDataCatcher has started')
        self._create_socket()

        data = {}
        last_acc = {}
        while not self.stopevent.isSet():

            # Receive a packet and get host info
            try:
                datar, addr = self.udp_sock.recvfrom(SWARM_VISIBS_PKT_SIZE)
            except timeout:
                continue

            #self.logger.info("Received packet: %s" %datar)
            
            # Check if packet is wrong size
            if len(datar) <> SWARM_VISIBS_PKT_SIZE:
                self.logger.error("Received packet %d:#%d is of wrong size, %d bytes" %(acc_n, pkt_n, len(datar)))
		continue

	    # Unpack it to get packet # and accum #
            pkt_n, acc_n = unpack(SWARM_VISIBS_HEADER_FMT, datar[:SWARM_VISIBS_HEADER_SIZE])

            # Initialize addr data buffer, if necessary 
            if not data.has_key(addr):
                data[addr] = {}

	    # Initialize acc_n data buffer, if necessary
            if not data[addr].has_key(acc_n):
                data[addr][acc_n] = [None]*SWARM_VISIBS_N_PKTS

            # Initialize last_acc tracker, if necessary 
            if not last_acc.has_key(addr):
                last_acc[addr] = float('nan')

            # Then store data in it
            data[addr][acc_n][pkt_n] = datar[8:]

	    # If we've gotten all pkts for acc_n for this addr
            if data[addr][acc_n].count(None)==0:

		# Put data onto queue
                temp_data = ''.join(data[addr].pop(acc_n))
		self.queue.put([addr, acc_n, time() - last_acc[addr], temp_data])

                # Set the last acc time
                last_acc[addr] = time()

        self.logger.info('SwarmDataCatcher has stopped')
        self._close_socket()


class SwarmDataHandler:

    def __init__(self, swarm, listener):

        # Create initial member variables
        self.logger = logging.getLogger('SwarmDataHandler')
        self.xengine = SwarmXengine(swarm)
        self.stopper = Event()
        self.queue = Queue()
        self.callbacks = []
        self.rawbacks = []
        self.swarm = swarm

        # Print out receive side (i.e. listener) network information
        self.logger.info('Listening for data on %s:%d' % (listener.host, listener.port))

        # Start the listening queue
        self.catcher = SwarmDataCatcher(
            self.queue, self.stopper, 
            listener.host, listener.port)
        self.catcher.setDaemon(True)
        self.catcher.start()

    def add_rawback(self, func):
        self.rawbacks.append(func)

    def add_callback(self, func):
        self.callbacks.append(func)

    def _reorder_data(self, datas_list):

        # Get the xengine order
        order = list(self.xengine.output_order())

        # Create data package to hold baseline data
        data_pkg = SwarmDataPackage(self.swarm)

        # Unpack and reorder each FID's data
        for fid, datas in enumerate(datas_list):

            # Unpack this FID's data
            data = array(unpack('>%di' % SWARM_VISIBS_ACC_SIZE, datas))

            # Reorder by Xengine word (per channel)
            for offset, word in enumerate(order):

                if word.baseline.is_valid():
                    sub_data = data[offset::len(order)]
                    data_pkg.set_data(word, fid, sub_data)

        # Return the (hopefully) complete data packages
        return data_pkg

    def _handle_data(self, data):

        # Do user callbacks first
        for callback in self.callbacks:
            callback(data)

        # Send data to dataCatcher/corrSaver
        for baseline in data.baselines:

            # Get our data arrays
            baseline_data = data[baseline]

            # Send every chunk
            for chunk in SWARM_MAPPING_CHUNKS:

                # Get baseline antennas
                ant_left = baseline.left._ant
                ant_right = baseline.right._ant

                # Get baseline polarizations
                pol_left = baseline.left._pol
                pol_right = baseline.right._pol

                # Get each sidebands data
                lsb_data = baseline_data[chunk]['LSB']
                if baseline.is_auto():
                    usb_data = lsb_data.copy()
                else:
                    usb_data = baseline_data[chunk]['USB']

                # Send our integration
                send_integration(0.0, 0.0, chunk, 
                                 ant_left, pol_left, 
                                 ant_right, pol_right, 
                                 lsb_data, usb_data, 0)

            # Debug log this baseline
            self.logger.debug("processed baseline: {!s}".format(baseline))

        # Info log the set
        self.logger.info("processed all baselines")

    def loop(self):

        try:

            acc = {}

            # Loop until user quits
            while True:

                try: # to check for data
                    addr, acc_n, last_acc, datas = self.queue.get_nowait()
                    sleep(0.1)
                except Empty: # none available
                    continue

                # Get the member/fid this set is from
                recv_fid, recv_member = self.swarm.get_member(addr[0])

                # Get the host name from member
                recv_host = recv_member.roach2_host

                # Log this accumulation
                if not math.isnan(last_acc):
                    self.logger.debug("Received full accumulation #{:<4} from fid #{}: {} ({:.4f} secs since last)".format(acc_n, recv_fid, recv_member, last_acc))
                else:
                    self.logger.debug("Received full accumulation #{:<4} from fid #{}: {}".format(acc_n, recv_fid, recv_member))

                # New accumulation, track it
                if not acc.has_key(acc_n):
                    acc[acc_n] = [None,] * self.swarm.fids_expected

                # Add this FID's data to accumulation
                acc[acc_n][recv_fid] = datas

                if acc[acc_n].count(None) == 0:

                    # We have all data for this accumulation, log it
                    self.logger.info("Received full accumulation #{:<4}".format(acc_n))

                    # Do user rawbacks first
                    rawdata = acc.pop(acc_n)
                    for rawback in self.rawbacks:
                        rawback(rawdata)

                    # Reorder the xengine data
                    data = self._reorder_data(rawdata)

                    # Handle the baseline data
                    self._handle_data(data)
                
        except KeyboardInterrupt:

            # User wants to quit
            self.stopper.set()
            self.catcher.join()