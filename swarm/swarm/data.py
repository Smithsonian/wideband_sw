import gc, math, logging, fcntl
from time import time, sleep
from struct import calcsize, pack, unpack
from queue import Queue, Empty
from threading import Thread, Event, active_count
from itertools import combinations
from socket import (
    socket, timeout, error,
    inet_ntoa, inet_aton,
    AF_INET, SOCK_DGRAM, SOCK_STREAM,
    SOL_SOCKET, SO_RCVBUF, SO_SNDBUF,
)

from numpy import array, frombuffer, full, empty, nan, reshape, vstack, zeros
from numba import njit, prange
from numba import config as nbconfig

from . import core
from .defines import *
from .xeng import (
    SwarmBaseline,
    SwarmXengine,
    )

import pydsm

nbconfig.THREADING_LAYER = 'safe'

SIOCGIFADDR = 0x8915
SIOCSIFHWADDR = 0x8927
SIOCGIFNETMASK = 0x891b

INNER_RANGE = list(range(0, SWARM_XENG_PARALLEL_CHAN * 4, 2))
OUTER_RANGE = list(range(0, SWARM_CHANNELS * 2, SWARM_XENG_TOTAL * 4))
DATA_FID_IND = array(list(j + i for i in OUTER_RANGE for j in INNER_RANGE))

ARRIVAL_THRESHOLD = 3
XNUM_TO_LENGTH = SWARM_WALSH_PERIOD / (SWARM_ELEVENTHS * (SWARM_EXT_HB_PER_WCYCLE / SWARM_WALSH_SKIP))


class SwarmDataPackage(object):
    header_prefix_fmt = '<IIIdd'

    def __init__(self, baselines, int_time, int_length):

        # Set all initial members
        self.int_time = int_time
        self.int_length = int_length
        self.baselines = baselines
        self.baselines_i = dict((b, i) for i, b in enumerate(self.baselines))

    @classmethod
    def from_swarm(cls, swarm, int_time=0.0, int_length=0.0):

        # Generate baselines list
        inputs = list(quad[i][j] for quad in swarm for i in range(quad.fids_expected) for j in SWARM_MAPPING_INPUTS)
        cross = list(SwarmBaseline(i, j) for i, j in combinations(inputs, r=2) if SwarmBaseline(i, j).is_valid())
        autos = list(SwarmBaseline(i, i) for i in inputs)
        baselines = autos + cross

        # Create an instance, populate header & data, and return it
        inst = cls(baselines, int_time, int_length)
        inst.init_header()
        inst.init_data()
        return inst

    @classmethod
    def from_bytes(cls, bytearr):

        # Generate baselines list
        header_prefix_size = calcsize(cls.header_prefix_fmt)
        n_baselines, n_sidebands, n_channels, int_time, int_length = unpack(
            cls.header_prefix_fmt, bytearr[0:header_prefix_size])
        header_size = header_prefix_size + 6 * n_baselines
        baselines_s = reshape(unpack('BBBBBB' * n_baselines, bytearr[header_prefix_size:header_size]), (n_baselines, 6))
        baselines = list(
            SwarmBaseline(core.SwarmInput(a, b, c), core.SwarmInput(d, e, f)) for a, b, c, d, e, f in baselines_s)

        # Create an instance, populate header & data, and return it
        data_shape = (n_baselines, n_sidebands, SWARM_CHANNELS * 2)
        inst = cls(baselines, int_time, int_length)
        inst.header = bytearr[:header_size]
        inst.array = frombuffer(bytearr[header_size:], dtype='<f4').reshape(data_shape)
        return inst

    def __getitem__(self, item):
        return self.get(*item)

    def get(self, *item):
        try:
            baseline, sideband = item
            i = self.baselines_i[baseline]
            j = SWARM_XENG_SIDEBANDS.index(sideband)
            return self.array[i, j]
        except:
            raise KeyError("Please only index data package using [baseline, sideband]!")

    def __setitem__(self, item, value):
        self.set(item, value)

    def set(self, item, value):
        try:
            baseline, sideband = item
            i = self.baselines_i[baseline]
            j = SWARM_XENG_SIDEBANDS.index(sideband)
            self.array[i, j] = value
        except:
            raise KeyError("Please only index data package using [baseline, sideband]!")

    def __bytes__(self):
        return b''.join([self.header, self.array.data])

    def init_header(self):
        hdr_fmt = self.header_prefix_fmt + 'BBBBBB' * len(self.baselines)
        self.header = pack(
            hdr_fmt,
            len(self.baselines),
            len(SWARM_XENG_SIDEBANDS),
            SWARM_CHANNELS,
            self.int_time, self.int_length,
            *list(x for z in self.baselines for y in (z.left, z.right) for x in (y.ant, y.chk, y.pol))
        )

    def update_header(self, int_time=None, int_length=None):
        self.int_time = int_time
        self.int_length = int_length
        self.init_header()

    def init_data(self):

        # Initialize our data array
        data_shape = (len(self.baselines), len(SWARM_XENG_SIDEBANDS), SWARM_CHANNELS * 2)
        self.array = empty(shape=data_shape, dtype='<f4')
        self.array[:, :, 0::2] = nan
        self.array[:, :, 1::2] = 0.0

    def set_data(self, xeng_word, data):

        # Get info from the data word
        imag = xeng_word.imag
        imag_off = int(imag)
        baseline = xeng_word.baseline
        sideband = xeng_word.sideband

        # Fill this baseline
        self.get(baseline, sideband)[imag_off::2] = data


class SwarmDataCallback(object):

    def __init__(self, swarm):
        self.__log_name = "Callback:{0}".format(self.__class__.__name__)
        self.logger = logging.getLogger(self.__log_name)
        self.swarm = swarm

    def __call__(self, data):
        pass


class SwarmListener(object):

    def __init__(self, interface, port=4100):
        self.logger = logging.getLogger(self.__class__.__name__)
        self.interface = interface
        self._set_netinfo(port)

    def _set_netinfo(self, port=4100):
        s = socket(AF_INET, SOCK_DGRAM)
        info_adr = fcntl.ioctl(s.fileno(), SIOCGIFADDR, pack('256s', self.interface.encode()))
        info_mac = fcntl.ioctl(s.fileno(), SIOCSIFHWADDR, pack('256s', self.interface.encode()))
        info_mask = fcntl.ioctl(s.fileno(), SIOCGIFNETMASK, pack('256s', self.interface.encode()))
        self.netmask = unpack(SWARM_REG_FMT, info_mask[20:24])[0]
        self.mac = unpack('>Q', b'\x00' * 2 + info_mac[18:24])[0]
        self.ip = unpack(SWARM_REG_FMT, info_adr[20:24])[0]
        self.host = inet_ntoa(pack(SWARM_REG_FMT, self.ip))
        self.port = port
        s.close()


def unpack_ip(addr):
    # Parse the IP address
    return unpack('BBBB', inet_aton(addr))


@njit()
def determine_qid(ip):
    # Determine the QID
    return (ip >> 4) & 0x7


@njit()
def determine_fid(ip):
    # Determine the FID
    return ip & 0x7


@njit()
def determine_xnum(xnum_mb, xnum_lh):
    return ((xnum_mb << 16) | xnum_lh) << 5


@njit()
def determine_acc_n(acc_n_mb, acc_n_lh):
    return (acc_n_mb << 16) | acc_n_lh


def reorder_packets(data_list):
    data_arr = empty(
        (SWARM_VISIBS_N_PKTS * SWARM_N_FIDS, SWARM_VISIBS_CHANNELS), dtype='i4'
    )
    for idx in range(SWARM_VISIBS_N_PKTS >> 2):
        for jdx in range(8):
            sub_list = data_list[jdx]
            pos_idx = (idx << 5) + (jdx << 2)
            pkt_idx = (idx << 2)

            data_arr[pos_idx] = frombuffer(sub_list[pkt_idx], dtype='>i4')
            data_arr[pos_idx + 1] = frombuffer(sub_list[pkt_idx + 1], dtype='>i4')
            data_arr[pos_idx + 2] = frombuffer(sub_list[pkt_idx + 2], dtype='>i4')
            data_arr[pos_idx + 3] = frombuffer(sub_list[pkt_idx + 3], dtype='>i4')

    return data_arr


@njit(parallel=True)
def fast_sort_data(data_arr, ordered_packets, order_array):
    for idx in prange(SWARM_VISIBS_N_PKTS * SWARM_N_FIDS):
        packet_data = ordered_packets[idx]
        for jdx in range(SWARM_VISIBS_N_PKTS):
            pos_idx = order_array[jdx]
            if pos_idx < 0:
                continue
            sub_data = data_arr[pos_idx >> 1]
            chan_idx = (idx << 3) + (pos_idx & 0x1)
            sub_data[chan_idx:chan_idx + 8:2] = packet_data[(jdx & 0x1ff):2048:512]
    return data_arr


class SwarmDataCatcher:

    def __init__(self, swarm, host='0.0.0.0', port=4100):
        self.logger = logging.getLogger(self.__class__.__name__)
        self.xengines = list(SwarmXengine(quad) for quad in swarm)
        self.swarm = swarm
        self.rawbacks = []
        self.host = host
        self.port = port

        # Catch thread objects
        self.catch_thread = None
        self.catch_queue = Queue()
        self.catch_stop = Event()

        # Ordering thread objects
        self.order_thread = None
        self.order_queue = Queue()
        self.order_stop = Event()

    def get_queue(self):
        return self.order_queue

    def get_catch_queue(self):
        return self.catch_queue

    def _create_socket(self):
        udp_sock = socket(AF_INET, SOCK_DGRAM)
        udp_sock.bind((self.host, self.port))
        udp_sock.settimeout(30.0)
        return udp_sock

    def start_catch(self):
        if not self.catch_thread:
            self.catch_stop.clear()
            self.catch_thread = Thread(target=self.catch,
                                       args=(self.catch_stop,
                                             None,
                                             self.catch_queue))
            self.catch_thread.start()
            self.logger.info('Catching thread has started')
        else:
            self.logger.error('Catch thread has not been stopped!')

    def start_order(self):
        if not self.order_thread:
            self.order_stop.clear()
            self.order_thread = Thread(target=self.order,
                                       args=(self.order_stop,
                                             self.catch_queue,
                                             self.order_queue))
            self.order_thread.start()
            self.logger.info('Ordering thread has started')
        else:
            self.logger.error('Order thread has not been stopped!')

    def start(self):
        self.catch_queue.queue.clear()
        self.order_queue.queue.clear()
        self.start_catch()
        self.start_order()

    def stop_catch(self):
        if self.catch_thread:
            self.catch_stop.set()
            self.catch_thread.join()
            self.catch_thread = None
            self.logger.info('Catch thread has stopped')
        else:
            self.logger.error('Catch thread has not been started!')

    def stop_order(self):
        if self.order_thread:
            self.order_stop.set()
            self.order_thread.join()
            self.order_thread = None
            self.logger.info('Order thread has stopped')
        else:
            self.logger.error('Order thread has not been started!')

    def stop(self):
        self.stop_catch()
        self.stop_order()

    def catch(self, stop, in_queue, out_queue):

        data = {}
        mask = {}
        meta = {}
        udp_sock = self._create_socket()
        while not stop.is_set():
            # Receive a packet and get host info
            try:
                datar, addr = udp_sock.recvfrom(SWARM_VISIBS_PKT_SIZE)
            except timeout:
                continue

            # Parse the IP address
            ip_host_addr = unpack('BBBB', inet_aton(addr[0]))[3]

            # Determine the QID
            qid = (ip_host_addr >> 4) & 0x7

            # Determine the FID / ROACH2
            fid = ip_host_addr & 0x7

            # Unpack it to get packet #, accum #, and scan length
            pkt_n, acc_n_mb, acc_n_lh, xnum_mb, xnum_lh = unpack(
                SWARM_VISIBS_HEADER_FMT, datar[:SWARM_VISIBS_HEADER_SIZE]
            )
            xnum = ((xnum_mb << 16) | xnum_lh) << 5
            acc_n = (acc_n_mb << 16) | acc_n_lh

            try:
                # Then store data in it
                data[qid][fid][acc_n][pkt_n] = datar[SWARM_VISIBS_HEADER_SIZE:]
                mask[qid][fid][acc_n] |= (1 << pkt_n)
            except KeyError:
                # Initialize qid data buffer, if necessary
                if qid not in data:
                    data[qid] = {fid: {}}
                    mask[qid] = {fid: {}}
                    meta[qid] = {fid: {}}
                elif fid not in data[qid]:
                    data[qid][fid] = {}
                    mask[qid][fid] = {}
                    meta[qid][fid] = {}

                # First packet of new accumulation, initalize data buffers
                data[qid][fid][acc_n] = [None] * SWARM_VISIBS_N_PKTS
                data[qid][fid][acc_n][pkt_n] = datar[SWARM_VISIBS_HEADER_SIZE:]
                mask[qid][fid][acc_n] = (1 << pkt_n)
                meta[qid][fid][acc_n] = {
                    'time': time(),  # these values correspond to those
                    'xnum': xnum,  # of the first packet of this acc
                }

            # If we've gotten all pkts for this acc_n from this FID
            if mask[qid][fid][acc_n] == SWARM_VISIBS_TOTAL:
                # Put data onto the queue
                mask[qid][fid].pop(acc_n)
                out_queue.put(
                    (
                        qid,
                        fid,
                        acc_n,
                        meta[qid][fid].pop(acc_n),
                        data[qid][fid].pop(acc_n),
                    )
                )

        udp_sock.close()

    def add_rawback(self, callback, *args, **kwargs):
        inst = callback(self.swarm, *args, **kwargs)
        self.rawbacks.append(inst)

    def _sort_data(self, data_array, packet_list, data_order):
        return fast_sort_data(data_array, reorder_packets(packet_list), data_order)

    def _reorder_data(self, data_list, data_pkg, packet_order):
        # Note this is the old method, kept here for testing purposes.
        data_arr = frombuffer(
            b''.join(
                [
                    data_list[jdx][kdx]
                    for idx in range(0, SWARM_VISIBS_N_PKTS, 4)
                    for jdx in range(SWARM_N_FIDS)
                    for kdx in range(idx, idx + 4)
                ]
            ),
        )
        # Create two views for the data -- note that these have little
        # overhead, since they are just modifying the stride on the array.
        cross_data = data_arr.view("S8").reshape(SWARM_CHANNELS, SWARM_VISIBS_N_PKTS // 2).T
        auto_data = data_arr.view(">i4").reshape(SWARM_CHANNELS, SWARM_VISIBS_N_PKTS).T

        for idx, (word1, word2) in enumerate(
            zip(packet_order[0::2], packet_order[1::2])
        ):
            if (
                (word1.baseline == word2.baseline)
                and (word1.sideband == word2.sideband)
                and (not word1.imag and word2.imag)
            ):
                if not (word1.is_valid() and word2.is_valid()):
                    continue
                # Select out the relevant subarray here, to make this
                # operation as fast as possible.
                sb_i = SWARM_XENG_SIDEBANDS.index(word1.sideband)
                bl_i = data_pkg.baselines_i[word1.baseline]
                sub_arr = data_pkg.array[bl_i, sb_i]
                sub_arr.view('S8')[:] = cross_data[idx]
                sub_arr[:] = sub_arr.view('>i4')
            elif word1.is_auto() and word2.is_auto():
                if word1.is_valid():
                    data_pkg[word1.baseline, word1.sideband][::2] = auto_data[(2 * idx)]
                if word2.is_valid():
                    data_pkg[word2.baseline, word2.sideband][::2] = auto_data[(2 * idx) + 1]
            else:
                if word1.is_valid():
                    data_pkg.set_data(word1, auto_data[(2 * idx)])
                if word2.is_valid():
                    data_pkg.set_data(word2, auto_data[(2 * idx) + 1])

        return data_pkg

    def order(self, stop, in_queue, out_queue):
        # Initialize the data object to plug things into.
        data_pkg = SwarmDataPackage.from_swarm(self.swarm)
        header_size = len(data_pkg.header)
        # Also grab packet ordering, since it should remain static while
        # collection thread is running.
        packet_order = list(
            list(xengine.packet_order()) for xengine in self.xengines
        )

        last_acc = []
        current_acc = None
        for quad in self.swarm.quads:
            last_acc.append(list(None for fid in range(quad.fids_expected)))

        # Figure out what baseline order the packetized data contain, where -1
        # means that the position does not match a position in data_array
        data_order = full((len(packet_order), SWARM_VISIBS_N_PKTS), -1)

        for idx, packet_list in enumerate(packet_order):
            for jdx, word in enumerate(packet_list):
                if word.is_valid():
                     data_order[idx, jdx] = (
                         (SWARM_XENG_SIDEBANDS.index(word.sideband) << 1)
                         + (data_pkg.baselines_i[word.baseline] << 2)
                         + word.imag
                     )


        while not stop.is_set():
            # Receive a set of data
            try:
                message = in_queue.get_nowait()
            except Empty:
                sleep(0.001)
                continue

            # Check if we received an exception
            if isinstance(message, Exception):
                out_queue.put(message)
                continue  # pass on exemption and move on

            # Otherwise, continue and parse message
            qid, fid, acc_n, meta, data_list = message
            this_length = meta['xnum'] * XNUM_TO_LENGTH
            this_time = meta['time']

            # Check if we've started a new scan
            if current_acc is None:
                self.logger.info("First data of accumulation #{0} received".format(acc_n))
                current_acc = acc_n

                # Establish meta data for new scan
                int_length = this_length
                int_time = this_time

                # Create a dict for checking what needs to be caught
                check_dict = {
                    qid: list(range(quad.fids_expected))
                    for qid, quad in enumerate(self.swarm.quads)
                }

                # Update the existing package w/ header information
                data_pkg.update_header(int_time=int_time, int_length=int_length)

                # Initiate the data buffers
                data = list(
                    [None] * quad.fids_expected for quad in self.swarm.quads
                )

            elif current_acc != acc_n:  # not done with scan but scan #'s don't match
                err_msg = "Haven't finished acc. #{0} but received data for acc #{1} from qid={2}, fid={3}".format(
                    current_acc, acc_n, qid, fid)
                self.logger.error(err_msg)

                if (current_acc + 1) == acc_n:
                    self.logger.info("Skipping acc " + str(current_acc))
                    current_acc += 1
                else:
                    exception = ValueError(err_msg)
                    out_queue.put(exception)
                    continue

            # Make sure that all scan lengths match
            if this_length != int_length:
                err_msg = "Data from qid #{0}, fid #{1} has mis-matching scan length: {2:.2f}!".format(qid, fid,
                                                                                                       this_length)
                exception = ValueError(err_msg)
                self.logger.error(err_msg)
                out_queue.put(exception)
                continue

            # Make sure data arrives within a reasonable time since the first data
            # NOTE: this alone does not enforce any order
            if (this_time - int_time) > ARRIVAL_THRESHOLD:
                err_msg = "Arrival time of qid={0}, fid={1} is too late (>{2:.1f} s from first data)".format(qid, fid,
                                                                                                             ARRIVAL_THRESHOLD)
                exception = ValueError(err_msg)
                self.logger.error(err_msg)
                out_queue.put(exception)
                continue

            # Check if the data has already been populated
            try:
                check_dict[qid].remove(fid)
            except ValueError:
                self.logger.info(
                    "Ignoring duplicate data from quadrant (qid #{}) or F-engine (fid #{})".format(qid, fid)
                )
                continue  # ignore and move on
            except KeyError:
                self.logger.info(
                    "Ignoring duplicate data from quadrant (qid #{}) or F-engine (fid #{})".format(qid, fid)
                )
                continue  # ignore and move on

            # Populate this data
            try:
                data[qid][fid] = data_list
            except IndexError:
                self.logger.info(
                    "Ignoring data from unexpected quadrant (qid #{}) or F-engine (fid #{})".format(qid, fid)
                )
                continue  # ignore and move on

            # Get the member/fid this set is from
            member = self.swarm[qid][fid]
            time_stamp = time()

            # Log the fact
            suffix = "({:.4f} secs since last)".format(time_stamp - last_acc[qid][fid]) if last_acc[qid][fid] else ""
            self.logger.debug(
                "Received full accumulation #{:<4} from qid #{}: {} {}".format(acc_n, qid, member, suffix)
            )

            # Set the last acc time
            last_acc[qid][fid] = time_stamp

            # If a list inside of check_dict is empty, that means that we have a full
            # accumulation of a quadrant, and can proceed with reordering.
            if not check_dict[qid]:
                del check_dict[qid]

            # If we've gotten all pkts for this acc_n from all QIDs & FIDs
            if not check_dict:
                self.logger.info(
                    "Beginning reordering of data for accumulation #{:<4}".format(acc_n)
                )

                # Create a continuous array to pass back to other data handlers
                data_bytes = zeros(header_size + data_pkg.array.nbytes, dtype='B')
                data_bytes[:header_size] = frombuffer(data_pkg.header, dtype='B')
                data_array = data_bytes[header_size:].view('<f4').reshape(
                    -1, SWARM_CHANNELS*2
                )

                for idx in range(len(self.swarm.quads)):
                    self.logger.debug(
                        "Beginning reordering of data for accumulation #{:<4} from qid #{}".format(acc_n, idx)
                    )

                    # Reorder the xengine data, plug it into the data_pkg
                    # data_pkg = self._reorder_data(data[idx], data_pkg, packet_order[idx])
                    data_array = self._sort_data(data_array, data[idx], data_order[idx])

                    self.logger.debug(
                        "Reorderded full accumulation #{:<4} from qid #{}".format(acc_n, idx)
                    )
                # We have all data for this accumulation, log it
                self.logger.info(
                    "Reordered full accumulation #{:<4} with scan length {:.2f} s".format(acc_n, int_length)
                )

                # Do user rawbacks first
                for rawback in self.rawbacks:
                    try:  # catch callback error
                        rawback(data)
                    except Exception as exception:  # and log if needed
                        self.logger.error("Exception from rawback: {}".format(rawback))
                        out_queue.put(exception)
                        continue

                # Log that we're done with rawbacks
                self.logger.info("Processed all rawbacks for accumulation #{:<4}".format(acc_n))

                # Put data onto queue
                out_queue.put((acc_n, int_time, data_bytes.data.cast("B")))

                self.logger.debug(
                    "Full accumulation #{:<4} queued for callbacks".format(acc_n)
                )

                # Done with this accumulation
                current_acc = None


class SwarmDataHandler:

    def __init__(self, swarm, queue, catch_queue):

        # Create initial member variables
        self.logger = logging.getLogger('SwarmDataHandler')
        self.callbacks = []
        self.swarm = swarm
        self.queue = queue
        self.catch_queue = catch_queue

    def add_callback(self, callback, *args, **kwargs):
        inst = callback(self.swarm, *args, **kwargs)
        self.callbacks.append(inst)

    def update_itime_from_dsm(self, last_dsm_num_walsh_cycles=None, check_fpga_itime=False):

        # Check DSM for updated scan length.
        dsm_num_walsh_cycles = pydsm.read('hal9000', 'SWARM_SCAN_LENGTH_L')[0]

        # Error checking, ignore crap values from DSM.
        if dsm_num_walsh_cycles < 1 or dsm_num_walsh_cycles > 1000.0:
            self.logger.warning(
                "DSM returned a SWARM_SCAN_LENGTH_L value not between 0-1000, ignoring..." + str(dsm_num_walsh_cycles))
            return None

        # Return if last_dsm_num_walsh_cycles param was used, and its the same as the new value.
        if last_dsm_num_walsh_cycles == dsm_num_walsh_cycles:
            return dsm_num_walsh_cycles

        # DSM Stores the number of walsh cycles as a long, convert back to seconds.
        dsm_integration_time = dsm_num_walsh_cycles * SWARM_WALSH_PERIOD

        try:
            # If check_fpga_itime is set, check the fpga times before setting them.
            dsm_time_secs = round(dsm_integration_time, 2)
            if check_fpga_itime:
                fpga_time_secs = round(self.swarm.get_itime(), 2)
                if fpga_time_secs == dsm_time_secs:
                    return dsm_num_walsh_cycles

            self.logger.info("Setting integration time to " + str(dsm_time_secs) + "s...")

            # Do a threaded set_itime.
            swarm_member_threads = list(Thread(target=m.set_itime, args=(dsm_integration_time,)) for f, m in self.swarm.get_valid_members())
            for thread in swarm_member_threads:
                thread.start()

            # Finally join all threads
            for thread in swarm_member_threads:
                thread.join()

            with self.queue.mutex:
                self.queue.queue.clear()
            with self.catch_queue.mutex:
                self.catch_queue.queue.clear()

        except Exception as err:
            self.logger.error("Error setting integration time, exception caught {0}".format(err))

        return dsm_num_walsh_cycles

    def loop(self, running):

        # Set the integration time from DSM (function will be a noop if lengths are the same).
        current_scan_length = self.update_itime_from_dsm()

        # Loop until user quits
        while running.is_set():
            try:  # to check for data
                message = self.queue.get_nowait()
            except Empty:  # none available
                sleep(0.01)
                continue

            # Check if we received an exception
            if isinstance(message, Exception):
                raise message

            # Otherwise, continue and parse message
            acc_n, int_time, data_bytes = message

            # Finally, do user callbacks
            for callback in self.callbacks:
                try:  # catch callback error
                    callback(data_bytes)
                except:  # and log if needed
                    self.logger.error("Exception from callback: {}".format(callback))
                    raise

            # Log that we're done with callbacks
            self.logger.info("Processed all callbacks for accumulation #{:<4}".format(acc_n))

            gc.collect()  # Force garbage collection
            self.logger.info("Garbage collected. Processing took {:.4f} secs".format(time() - int_time))

            # Check dsm for updates
            current_scan_length = self.update_itime_from_dsm(last_dsm_num_walsh_cycles=current_scan_length)
