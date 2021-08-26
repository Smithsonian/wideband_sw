import pydsm

SWARM_EXT_HB_PER_WCYCLE = 64
SWARM_EXT_HB_PERIOD = (2**19) / 52e6
SWARM_WALSH_PERIOD = SWARM_EXT_HB_PERIOD * SWARM_EXT_HB_PER_WCYCLE
dsm_num_walsh_cycles = pydsm.read('hal9000', 'SWARM_SCAN_LENGTH_L')[0]
dsm_integration_time = dsm_num_walsh_cycles * SWARM_WALSH_PERIOD
print(dsm_integration_time)

