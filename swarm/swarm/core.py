import logging
from struct import pack
from random import randint
from socket import inet_ntoa
from threading import Thread, Event
from collections import OrderedDict
from corr.katcp_wrapper import FpgaClient
from katcp import Message

from defines import *


class SwarmInput:

    def __init__(self, antenna=None, chunk=None, polarization=None):

        # Set all initial members
        self.logger = logging.getLogger('SwarmMember')
        self._ant = antenna
        self._chk = chunk
        self._pol = polarization

    def __repr__(self):
        repr_str = 'SwarmInput(antenna={ant}, chunk={chk}, polarization={pol})' 
        return repr_str.format(ant=self._ant, chk=self._chk, pol=self._pol)

    def __str__(self):
        repr_str = 'ant{ant}:chk{chk}:pol{pol}'
        return repr_str.format(ant=self._ant, chk=self._chk, pol=self._pol)

    def __hash__(self):
        return hash((self._ant, self._chk, self._pol))

    def __eq__(self, other):
        if other is not None:
            return (self._ant, self._chk, self._pol) == (other._ant, other._chk, other._pol)
        else:
            return not self.is_valid()

    def __ne__(self, other):
        return not self.__eq__(other)

    def is_valid(self):
        return (self._ant != None) and (self._chk != None) and (self._pol != None)


class SwarmMember:

    def __init__(self, roach2_host):

        # Set all initial members
        self.logger = logging.getLogger('SwarmMember')
        self._inputs = [SwarmInput(),] * len(SWARM_MAPPING_INPUTS)
        self.roach2_host = roach2_host

        # Connect to our ROACH2
        if self.roach2_host:
            self._connect(roach2_host)

    def __eq__(self, other):
        if other is not None:
            return self.roach2_host == other.roach2_host
        else:
            return not self.is_valid()

    def __ne__(self, other):
        return not self.__eq__(other)

    def is_valid(self):
        return self.roach2_host is not None

    def __repr__(self):
        repr_str = 'SwarmMember(roach2_host={host})[{inputs[0]!r}][{inputs[1]!r}]' 
        return repr_str.format(host=self.roach2_host, inputs=self._inputs)

    def __str__(self):
        repr_str = '{host} [{inputs[0]!s}] [{inputs[1]!s}]' 
        return repr_str.format(host=self.roach2_host, inputs=self._inputs)

    def __getitem__(self, input_n):
        return self._inputs[input_n]

    def set_input(self, input_n, input_inst):
        self._inputs[input_n] = input_inst

    def setup(self, fid, fids_expected, bitcode, itime_sec, listener, noise=randint(0, 15)):

        # Reset logger for current setup
        self.logger = logging.getLogger('SwarmMember[%d]' % fid)

        # Program the board
        self._program(bitcode)

        # Setup our digital noise
        self.set_digital_seed(0, (randint(0, 2**28 - 1)<<4) + noise)
        self.set_digital_seed(1, (randint(0, 2**28 - 1)<<4) + noise)
        self.reset_digital_noise()

        # ...but actually use the ADCs
        self.set_source(2, 2)

        # Setup our scopes to capture raw data
        self.set_scope(3, 6, 0)

        # Setup the F-engine
        self._setup_fengine()

        # Setup flat complex gains
        self.set_flat_cgains(0, 2**12)
        self.set_flat_cgains(1, 2**12)

        # Setup the X-engine
        self._setup_xeng_tvg()
        self.set_itime(itime_sec)
        self.reset_xeng()

        # Initial setup of the switched corner-turn
        self._setup_corner_turn(fid, fids_expected)

        # Setup the 10 GbE visibility
        self._setup_visibs(listener)

    def _connect(self, roach2_host):

        # Connect and wait until ready
        self.roach2 = FpgaClient(roach2_host)
        if roach2_host:
            self.roach2.wait_connected()

    def _program(self, bitcode):

        # Program with the bitcode
        self._bitcode = bitcode
        self.roach2.progdev(self._bitcode)

    def set_digital_seed(self, source_n, seed):

        # Set the seed for internal noise
        seed_bin = pack(SWARM_REG_FMT, seed)
        self.roach2.write(SWARM_SOURCE_SEED % source_n, seed_bin)

    def reset_digital_noise(self, source_0=True, source_1=True):

        # Reset the given sources by twiddling the right bits
        mask = (source_1 << 31) + (source_0 << 30)
        val = self.roach2.read_uint(SWARM_SOURCE_CTRL)
        self.roach2.write(SWARM_SOURCE_CTRL, pack(SWARM_REG_FMT, val & ~mask))
        self.roach2.write(SWARM_SOURCE_CTRL, pack(SWARM_REG_FMT, val |  mask))
        self.roach2.write(SWARM_SOURCE_CTRL, pack(SWARM_REG_FMT, val & ~mask))

    def set_source(self, source_0, source_1):

        # Set our sources to the given values
        ctrl_bin = pack(SWARM_REG_FMT, (source_1<<3) + source_0)
        self.roach2.write(SWARM_SOURCE_CTRL, ctrl_bin)

    def set_scope(self, sync_out, scope_1, scope_0):

        # Set our scopes to the given values
        ctrl_bin = pack(SWARM_REG_FMT, (sync_out<<16) + (scope_1<<8) + scope_0)
        self.roach2.write(SWARM_SCOPE_CTRL, ctrl_bin)

    def _setup_fengine(self):

        # Set the shift schedule of the F-engine
        sched_bin = pack(SWARM_REG_FMT, SWARM_SHIFT_SCHEDULE)
        self.roach2.write(SWARM_FENGINE_CTRL, sched_bin)

    def set_flat_cgains(self, input_n, flat_value):

        # Set gains for input to a flat value
        gains = [flat_value,] * SWARM_CHANNELS
        gains_bin = pack('>%dH' % SWARM_CHANNELS, *gains)
        self.roach2.write(SWARM_CGAIN_GAIN % input_n, gains_bin)

    def reset_xeng(self):

        # Twiddle bit 29
        mask = 1 << 29 # reset bit location
        val = self.roach2.read_uint(SWARM_XENG_CTRL)
        self.roach2.write(SWARM_XENG_CTRL, pack(SWARM_REG_FMT, val & ~mask))
        self.roach2.write(SWARM_XENG_CTRL, pack(SWARM_REG_FMT, val |  mask))
        self.roach2.write(SWARM_XENG_CTRL, pack(SWARM_REG_FMT, val & ~mask))

    def set_itime(self, itime_sec):

        # Set the integration (11 spectra per step * 64 steps per cycle)
        self._xeng_itime = 11 * 64 * int(itime_sec/SWARM_WALSH_TIME)
        self.roach2.write(SWARM_XENG_CTRL, pack(SWARM_REG_FMT, self._xeng_itime))

    def _reset_corner_turn(self):

        # Twiddle bits 31 and 30
        mask = (1 << 31) + (1 << 30)
        val = self.roach2.read_uint(SWARM_NETWORK_CTRL)
        self.roach2.write(SWARM_NETWORK_CTRL, pack(SWARM_REG_FMT, val & ~mask))
        self.roach2.write(SWARM_NETWORK_CTRL, pack(SWARM_REG_FMT, val |  mask))
        self.roach2.write(SWARM_NETWORK_CTRL, pack(SWARM_REG_FMT, val & ~mask))

    def _setup_corner_turn(self, this_fid, fids_expected, ipbase=0xc0a88000, macbase=0x000f530cd500, bh_mac=0x000f530cd899):

        # Reset the cores
        self._reset_corner_turn()

        # Store our FID 
        self.fid = this_fid
        self.fids_expected = fids_expected

        # Set static parameters
        self.roach2.write_int(SWARM_NETWORK_FIDS_EXPECTED, self.fids_expected)
        self.roach2.write_int(SWARM_NETWORK_IPBASE, ipbase)
        self.roach2.write_int(SWARM_NETWORK_FID, self.fid)

        # Initialize the ARP table 
        arp = [bh_mac] * 256

        # Fill the ARP table
        for fid in SWARM_ALL_FID:
            for core in SWARM_ALL_CORE:
                last_byte = (fid << 4) + 0b1100 + core
                arp[last_byte] = macbase + last_byte

        # Configure 10 GbE devices
        for core in SWARM_ALL_CORE:
            name = SWARM_NETWORK_CORE % core
            last_byte = (self.fid << 4) + 0b1100 + core
            self.roach2.config_10gbe_core(name, macbase + last_byte, ipbase + last_byte, 18008, arp)

        # Lastly enable the TX only (for now)
        self.roach2.write(SWARM_NETWORK_CTRL, pack(SWARM_REG_FMT, 0x20))

    def reset_ddr3(self):

        # Twiddle bit 30
        mask = 1 << 30 # reset bit location
        val = self.roach2.read_uint(SWARM_VISIBS_DELAY_CTRL)
        self.roach2.write(SWARM_VISIBS_DELAY_CTRL, pack(SWARM_REG_FMT, val & ~mask))
        self.roach2.write(SWARM_VISIBS_DELAY_CTRL, pack(SWARM_REG_FMT, val |  mask))
        self.roach2.write(SWARM_VISIBS_DELAY_CTRL, pack(SWARM_REG_FMT, val & ~mask))

    def xengine_tvg(self, enable=False):

        # Disable/enable using bit 31
        mask = 1 << 31 # enable bit location
        val = self.roach2.read_uint(SWARM_XENG_CTRL)
        if enable:
            self.roach2.write(SWARM_XENG_CTRL, pack(SWARM_REG_FMT, val | mask))
        else:
            self.roach2.write(SWARM_XENG_CTRL, pack(SWARM_REG_FMT, val & ~mask))

    def _setup_xeng_tvg(self):

        # Give each input a different constant value
        const_inputs = [0x0102, 0x0304, 0x0506, 0x0708, 0x090a, 0x0b0c, 0x0d0e, 0x0f10] * (SWARM_VISIBS_CHANNELS/8)
        for i in SWARM_ALL_FID:
            self.roach2.write(SWARM_XENG_TVG % i, pack('>%dH' % SWARM_VISIBS_CHANNELS, *const_inputs))

    def visibs_delay(self, enable=True, delay_test=False, chunk_delay=2**21):

        # Disable/enable Laura's DDR3 delay and test
        self.roach2.write_int(SWARM_VISIBS_DELAY_CTRL, (enable<<31) + (delay_test<<29) + chunk_delay)

    def _setup_visibs(self, listener, delay_test=False):

        # Store (or override) our listener
        self._listener = listener

        # Reset the DDR3
        self.reset_ddr3()

        # Enable DDR3 interleaver
        self.visibs_delay(enable=True)

        # Fill the visibs ARP table
        arp = [0xffffffffffff] * 256
        arp[self._listener.ip & 0xff] = self._listener.mac

        # Configure the transmit interface
        final_hex = (self.fid + 4) * 2
        src_ip = (192<<24) + (168<<16) + (10<<8) + final_hex + 50 
        src_mac = (2<<40) + (2<<32) + final_hex + src_ip
        self.roach2.config_10gbe_core(SWARM_VISIBS_CORE, src_mac, src_ip, 4000, arp)

        # Configure the visibility packet buffer
        self.roach2.write(SWARM_VISIBS_SENDTO_IP, pack(SWARM_REG_FMT, self._listener.ip))
        self.roach2.write(SWARM_VISIBS_SENDTO_PORT, pack(SWARM_REG_FMT, self._listener.port))

        # Reset (and disable) visibility transmission
        self.roach2.write(SWARM_VISIBS_TENGBE_CTRL, pack(SWARM_REG_FMT, 1<<30))
        self.roach2.write(SWARM_VISIBS_TENGBE_CTRL, pack(SWARM_REG_FMT, 0))

        # Finally enable transmission
        self.roach2.write(SWARM_VISIBS_TENGBE_CTRL, pack(SWARM_REG_FMT, 1<<31))

    def get_visibs_ip(self):

        # Update/store the visibs core net info
        self.visibs_netinfo = self.roach2.get_10gbe_core_details(SWARM_VISIBS_CORE)

        # Return the visibs core IP 
        return inet_ntoa(pack(SWARM_REG_FMT, self.visibs_netinfo['my_ip']))

    def sync_sowf(self, offset=0):

        # Send the SWARM sync command over KATCP
        message = Message.request(SWARM_SYNC_ARM_CMD, str(offset))
        reply, informs = self.roach2.blocking_request(message, timeout=SWARM_SYNC_TIMEOUT)
        if not reply.reply_ok():
            self.logger.error("SOWF arming failed for FID #%d" % self.fid)

    def enable_network(self):

        # Enable the RX and TX
        self.roach2.write(SWARM_NETWORK_CTRL, pack(SWARM_REG_FMT, 0x30))


EMPTY_MEMBER = SwarmMember(None)

class Swarm:

    def __init__(self, map_filename=SWARM_MAPPING):

        # Set initial member variables
        self.logger = logging.getLogger('Swarm')

        # Parse mapping for first time
        self.load_mapping(map_filename)

    def __len__(self):
        return len(self.members)

    def __getitem__(self, fid):
        return self.members.values()[fid]

    def load_mapping(self, map_filename):

        # Clear the members instance
        self.members = OrderedDict()

        # Store (or restore) current mapping file
        self.map_filename = map_filename

        # Open the mapping file
        with open(self.map_filename, 'r') as map_file:

            # Parse line by line
            for map_line in map_file:

                # Splits by (and removes) whitespace
                entry = map_line.split()

                # Checks if this line is a comment
                is_comment = entry[0].startswith(SWARM_MAPPING_COMMENT)

                if is_comment:

                    # Display map comment
                    self.logger.debug('Mapping comment found: %s' % map_line.rstrip())

                else:

                    # Make sure entry has the right columns
                    n_cols = len(entry)
                    if n_cols < SWARM_MAPPING_COLUMNS:
                        self.logger.error('Not enough columns in map, ignoring line: %s' % map_line)
                        continue
                    elif n_cols > SWARM_MAPPING_COLUMNS:
                        self.logger.error('Too many columns in map, ignoring line: %s' % map_line)
                        continue
                    
                    # First column is ROACH2 number
                    roach2_num = int(entry[0])
                    roach2_host = SWARM_ROACH2_IP % roach2_num

                    # Create and attach our member instance
                    member_inst = SwarmMember(roach2_host)
                    if not self.members.has_key(roach2_host):
                        self.members[roach2_host] = member_inst

                    # Second column is input number
                    roach2_inp = int(entry[1])
                    if roach2_inp not in SWARM_MAPPING_INPUTS:
                        self.logger.error('%d not a valid SWARM input!' % roach2_inp)
                        continue

                    # Third column is antenna number
                    antenna = int(entry[2])

                    # Fourth column is chunk number
                    chunk = int(entry[3])
                    if chunk not in SWARM_MAPPING_CHUNKS:
                        self.logger.error('%d not a valid SWARM chunk!' % chunk)
                        continue

                    # Fifth column is polarization
                    pol = int(entry[4])
                    if pol not in SWARM_MAPPING_POLS:
                        self.logger.error('%d not a valid SWARM polarization!' % pol)
                        continue

                    # Finally, attach our input instance
                    input_inst = SwarmInput(antenna=antenna, chunk=chunk, polarization=pol)
                    self.members[roach2_host].set_input(roach2_inp, input_inst)

                    # We're done, spit some info out
                    self.logger.info('Mapping antenna=%r to %s:input=%d', 
                                     input_inst, roach2_host, roach2_inp)

        # Set number FIDs expected
        self.fids_expected = len(self.members)

        # Fill missing FIDs with empty members
        missing_fids = set(SWARM_ALL_FID) - set(range(self.fids_expected))
        for fid in missing_fids:
            self.members[fid] = SwarmMember(None)

    def get_member(self, visibs_ip):

        # Create list of valid members
        valid_members = list(self[fid] for fid in range(self.fids_expected))

        # Find the member with the right visibs_ip
        for fid, member in enumerate(valid_members):

            # Return if we found it
            if member.get_visibs_ip() == visibs_ip:
                return fid, member

    def members_do(self, func):

        # Create list of valid members
        valid_members = list(self[fid] for fid in range(self.fids_expected))

        # Run func on each valid member
        for fid, member in enumerate(valid_members):
            func(fid, member)

    def setup(self, bitcode, itime, listener):

        sync_threads = []

        # Create list of valid members
        valid_members = list(self[fid] for fid in range(self.fids_expected))

        # Go through hosts in our mapping
        for fid, member in enumerate(valid_members):

            # Log some transmit side (i.e. ROACH2) network information
            self.logger.info('Configuring ROACH2=%s for transmission as FID #%d' %(member.roach2_host, fid))

            # Setup (i.e. program and configure) the ROACH2
            member.setup(fid, self.fids_expected, bitcode, itime, listener)

        # Send sync to all boards simultaneously 
        for fid, member in enumerate(valid_members):
            sync_threads.append(Thread(target=member.sync_sowf, args=[member,]))
            sync_threads[-1].start()
        
        # Join and wait for syncs to finish
        for thread in sync_threads:
            thread.join()

        # Do the post-sync setup
        for fid, member in enumerate(valid_members):
            member.enable_network()
            member.reset_xeng()
            member.reset_digital_noise()
            member.set_source(3, 3)

