import logging, fcntl
from struct import pack, unpack
from socket import (
    socket, inet_ntoa,
    AF_INET, SOCK_DGRAM,
    SO_RCVBUF, SO_SNDBUF,
    )

from defines import *


SIOCGIFADDR = 0x8915
SIOCSIFHWADDR  = 0x8927

class DBEImposter:

    def __init__(self, interface, port=0xbea3):
        self.logger = logging.getLogger('DBEImposter')
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
