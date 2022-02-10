import logging
from configparser import ConfigParser
import struct

from casperfpga import CasperFpga
from katcp import Message

from .defines import (
    SWARM_BENGINE_SIDEBANDS,
    SWARM_DBE_IPBASE,
    SWARM_DBE_MACBASE,
    SWARM_BENGINE_SIDEBAND_MACIP_OFFSET,
    SWARM_BENGINE_PORT,
    SWARM_IDLE_BITCODE,
    SWARM_PLUGINS_CONFIG,
)


module_logger = logging.getLogger(__name__)


class Interface(object):

    def __init__(self, mac, ip, port):
        self.logger = logging.getLogger(self.__class__.__name__)
        self.port = port
        self.mac = mac
        self.arp = []
        self.ip = ip


class SwarmDBEInterface(Interface):

    def __init__(self, qid, fid, sideband="USB"):

        # Set base values
        core = fid >> 1
        my_mac = SWARM_DBE_MACBASE + (qid << 8) + 0x20 + core
        my_ip = SWARM_DBE_IPBASE + (qid << 8) + 0x20 + core

        # Check if sideband specification is valid
        sideband_upper = sideband.upper()
        if sideband_upper not in SWARM_BENGINE_SIDEBANDS:
            raise RuntimeError(
                "{0} sideband should be one of {1}".format(
                    self.__class__, SWARM_BENGINE_SIDEBANDS
                    )
                )

        # Determine offset per sideband
        mac_ip_offset = SWARM_BENGINE_SIDEBANDS.index(sideband_upper) * (
            SWARM_BENGINE_SIDEBAND_MACIP_OFFSET
        )
        my_mac += mac_ip_offset
        my_ip += mac_ip_offset

        # Create interface
        super(SwarmDBEInterface, self).__init__(
            my_mac, my_ip, SWARM_BENGINE_PORT
        )


class SwarmROACH(object):

    def __init__(self, roach2_host, parent_logger=module_logger):

        # Set all initial members
        self.roach2_host = roach2_host
        self.logger = parent_logger.getChild(
            '{name}[host={host!r}]'.format(
                name=self.__class__.__name__,
                host=self.roach2_host,
                )
            )

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

    def _connect(self, roach2_host):

        # Connect and wait until ready
        self.roach2 = CasperFpga(roach2_host)
        if roach2_host:
            if not self.roach2.transport.wait_connected(timeout=5):
                raise RuntimeError(
                    "Timeout trying to connect to %s. "
                    "Is it up and running the swarm sever?" % self.roach2.host
                )

    def _program(self, bitcode):

        # Program with the bitcode
        self._bitcode = bitcode
        self.roach2.transport.program(self._bitcode)

    def config_10gbe_core(self,device_name,mac,ip,port,arp_table,gateway=1):
        """Hard-codes a 10GbE core with the provided params. It does a blindwrite,
        so there is no verifcation that configuration was successful (this is necessary
        since some of these registers are set by the fabric depending on traffic
        received).
        
        Note that this is pulled from the corr library originally.

           @param self  This object.
           @param device_name  String: name of the device.
           @param mac   integer: MAC address, 48 bits.
           @param ip    integer: IP address, 32 bits.
           @param port  integer: port of fabric interface (16 bits).
           @param arp_table  list of integers: MAC addresses (48 bits ea).
           """
        # assemble struct for header stuff...
        # 0x00 - 0x07: My MAC address
        # 0x08 - 0x0b: Not used
        # 0x0c - 0x0f: Gateway addr
        # 0x10 - 0x13: my IP addr
        # 0x14 - 0x17: Not assigned
        # 0x18 - 0x1b: Buffer sizes
        # 0x1c - 0x1f: Not assigned
        # 0x20       : soft reset (bit 0)
        # 0x21       : fabric enable (bit 0)
        # 0x22 - 0x23: fabric port

        # 0x24 - 0x27: XAUI status (bit 2,3,4,5=lane sync, bit6=chan_bond)
        # 0x28 - 0x2b: PHY config

        # 0x28       : RX_eq_mix
        # 0x29       : RX_eq_pol
        # 0x2a       : TX_preemph
        # 0x2b       : TX_diff_ctrl

        # 0x1000     : CPU TX buffer
        # 0x2000     : CPU RX buffer
        # 0x3000     : ARP tables start

        ctrl_pack = struct.pack(
            '>QLLLLLLBBH', mac, 0, gateway, ip, 0, 0, 0, 0, 1, port
        )
        arp_pack = struct.pack('>256Q',*arp_table)
        self.roach2.blindwrite(device_name, ctrl_pack, offset=0)
        self.roach2.write(device_name, arp_pack, offset=0x3000)

    def get_rcs(self, rcs_block_name='rcs'):
        """Retrieves and decodes a revision control block.

        Stolen from katcp_wrapper inside of the corr library.
        """
        rv = {}
        rv['user'] = self.roach2.read_uint(rcs_block_name+'_user')
        app = self.roach2.read_uint(rcs_block_name+'_app')
        lib = self.roach2.read_uint(rcs_block_name+'_lib')

        if lib & (1 << 31):
            rv['compile_timestamp'] = lib & ((2**31)-1)
        else:
            rv['lib_rcs_type'] = 'svn' if (lib & (1 << 30)) else 'git'
            rv['lib_dirty'] = True if (lib & (1 << 28)) else False
            rv['lib_rev'] = lib & ((2**28)-1)

        if app & (1 << 31):
            rv['app_last_modified'] = app & ((2**31)-1)
        else:
            rv['app_rcs_type'] = 'svn' if (app & (1 << 30)) else 'git'
            rv['app_dirty'] = True if (app & (1 << 28)) else False
            rv['app_rev'] = app & ((2**28) - 1)

        return rv

    def idle(self, bitcode=SWARM_IDLE_BITCODE):
        try:
            # Unload plugins and program with idle code
            self.unload_plugins()
            self._program(bitcode)
            self.logger.info('Idled %s with %s' % (self.roach2_host, bitcode))
        except Exception as err:
            self.logger.warning(
                'Unable to Idle %s with %s (%s)' % (
                    self.roach2_host, bitcode, str(err.value),
                )
            )

    def load_bitcode(self, bitcode="sma_corr_full_rev2_-2.bof.gz"):
        # Unload plugins and program
        self.unload_plugins()
        self._program(bitcode)
        self.logger.info(
            "Programmed %s with %s." % (self.roach2_host, bitcode)
        )

    def send_katcp_cmd(self, cmd, *args):

        # Create the message object
        message = Message.request(cmd, *args)

        # Send the request, and block for 60 seconds
        reply, informs = self.roach2.transport.blocking_request(message, timeout=60)

        # Check for error, and raise one if present
        if not reply.reply_ok():
            raise RuntimeError(reply)

        # Otherwise return what we got
        return reply, informs

    def plugin_list(self):

        # Send plugin-list command
        reply, informs = self.send_katcp_cmd('plugin-list')

        # Return the list of loaded plugin names
        return list(inform.arguments[0] for inform in informs)

    def unload_plugins(self):

        # Unload all currently loaded plugins
        for plugin in reversed(self.plugin_list()):
            self.send_katcp_cmd('plugin-unload', plugin)

    def reload_plugins(self, plugins_config=SWARM_PLUGINS_CONFIG):

        # Unload all currently loaded plugins
        self.unload_plugins()

        # Read the default plugins file
        cfg = ConfigParser({'init': ''})
        cfg.read(plugins_config)

        # Get the names of all default plugins
        default_plugins = cfg.sections()

        # Cycle through defaults
        for plugin in default_plugins:

            # First, load the plugin
            path = cfg.get(plugin, 'file')
            self.send_katcp_cmd('plugin-load', path)

            # Then, run user init commands (if requested)
            for cmdstr in cfg.get(plugin, 'init').splitlines():
                cmd, sep, args = cmdstr.partition(' ')

                # If failure: catch, log, and proceed
                try:
                    self.send_katcp_cmd(cmd, *args.split())
                except RuntimeError as err:
                    self.logger.error("Plugin init failure: {0}".format(err))
