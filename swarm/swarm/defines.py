from signal import SIGQUIT
import sys

SWARM_IDLE_BITCODE = 'idle.bof'
SWARM_CTRL_LOG_CHANNEL = "swarm.logs.ctrl"
SWARM_ROACH2_IP = 'roach2-%02x'
SWARM_COLDSTART_PATH = '/otherInstances/tenzing/smainit_req/swarm_ctrl.URG'
SWARM_LAST_COLDSTART_PATH = '/global/logs/swarm/lastColdStart'
ACTIVE_QUADRANTS_FILE_PATH = '/global/projects/SWARMQuadrantsInArray'

# SWARM_MAPPINGS dictates the number of possible quadrants for the rest of the code.
# If you change the number of quadrants in SWARM, it won't work correctly until you
# create/remove the mappings in this list.
SWARM_MAPPINGS = [
    '/global/configFiles/swarmMapping.quad1',
    '/global/configFiles/swarmMapping.quad2',
    '/global/configFiles/swarmMapping.quad3',
    '/global/configFiles/swarmMapping.quad4',
    '/global/configFiles/swarmMapping.quad5',
    '/global/configFiles/swarmMapping.quad6',
    ]

SWARM_MAX_NUM_QUADRANTS = len(SWARM_MAPPINGS)
SWARM_MAPPING_CHUNKS = tuple([q for q in range(SWARM_MAX_NUM_QUADRANTS)])

SWARM_MAPPING_COMMENT = '#'
SWARM_MAPPING_MEM_PARAM = '!'
SWARM_MAPPING_QUAD_PARAM = '$'
SWARM_MAPPING_COLUMNS = 5
SWARM_MAPPING_INPUTS = (0, 1)
SWARM_MAPPING_POLS = (0, 1)

SWARM_PLUGINS_CONFIG = '/global/configFiles/swarmPlugins'

SWARM_LISTENER_INTERFACES = ['eth2', ]

SWARM_WALSH_PATTERNS = '/global/configFiles/swarmWalshPatterns'
SWARM_WALSH_TABLE_BRAM = 'walsh_table'
SWARM_WALSH_CTRL = 'walsh_ctrl'
SWARM_WALSH_TABLE_LEN = 1024

SWARM_SB_STATE_BRAM = 'xengine_final_acc_sb_state'

# Clocking parameters
# (changes with external clock)
SWARM_CLOCK_RATE = 286e6
SWARM_TARGET_RATE = 286e6
SWARM_ELEVENTHS = int(11 * SWARM_CLOCK_RATE / SWARM_TARGET_RATE)

# Overall design parameters
# (should not change unless design changes)
SWARM_CHANNELS = 2**14

# Internal de-Walshing parameters
# (should not change unless design changes)
SWARM_INT_HB_PER_SOWF = 64
SWARM_TRANSPOSE_SIZE = 128
SWARM_CLOCKS_PER_INT_HB = SWARM_ELEVENTHS * SWARM_TRANSPOSE_SIZE * 2048
SWARM_INT_HB_PERIOD = SWARM_CLOCKS_PER_INT_HB / SWARM_CLOCK_RATE

# External Walshing parameters
# (changes with SMA Walshing changes)
SWARM_EXT_HB_PER_WCYCLE = 64
SWARM_EXT_HB_PERIOD = (2**19) / 52e6
SWARM_WALSH_PERIOD = SWARM_EXT_HB_PERIOD * SWARM_EXT_HB_PER_WCYCLE

# Internal/external relationship
# (changes with either; see above)
SWARM_WALSH_SKIP = int(SWARM_INT_HB_PERIOD / SWARM_EXT_HB_PERIOD)

SWARM_ALL_FID = range(8)
SWARM_ALL_CORE = range(4)
SWARM_ALL_QDR = range(4)    # QDR memory, not "quadrants".
SWARM_N_FIDS = len(SWARM_ALL_FID)
SWARM_N_INPUTS = len(SWARM_MAPPING_INPUTS)
SWARM_SHIFT_SCHEDULE = 0x7fff7fff
SWARM_FENGINE_CTRL = 'fengine_ctrl'
SWARM_CGAIN_GAIN = 'cgain_gain_%d'
SWARM_XENG_CTRL = 'xeng_ctrl'
SWARM_XENG_XN_NUM = 'xeng_xn_num'
SWARM_XENG_TVG = 'xengine_xeng_tvg_data%d'
SWARM_VISIBS_CORE = 'visibs_gbe0'
SWARM_VISIBS_DELAY_CTRL = 'visibs_delay_ctrl'
SWARM_VISIBS_TENGBE_CTRL = 'visibs_tengbe_ctrl'
SWARM_VISIBS_SENDTO_IP = 'visibs_sendto_ip'
SWARM_VISIBS_SENDTO_PORT = 'visibs_sendto_port'
SWARM_SOURCE_SEED = 'source_seed_%d'
SWARM_SOURCE_CTRL = 'source_ctrl'
SWARM_QDR_CTRL = 'qdr%d_ctrl'
SWARM_SYNC_CTRL = 'sync_ctrl'
SWARM_SCOPE_CTRL = 'scope_ctrl'
SWARM_SCOPE_SNAP = 'scope_snap%d'
SWARM_NETWORK_IPBASE = 'network_ipbase'
SWARM_NETWORK_CORE = 'network_eth_%d_core'
SWARM_NETWORK_CTRL = 'network_ctrl'
SWARM_NETWORK_FID = 'network_fid'
SWARM_NETWORK_FIDS_EXPECTED = 'network_fids_expected'
SWARM_REG_FMT = '>I'

SWARM_SYNC_ARM_CMD = 'sma-walsh-arm'
SWARM_FSTOP_STOP_CMD = 'sma-astro-fstop-stop'
SWARM_FSTOP_START_CMD = 'sma-astro-fstop-start'
SWARM_FSTOP_SET_CMD = 'sma-astro-fstop-set {0} {1} -2.71359 1 1'
SWARM_DELAY_GET_CMD = 'sma-astro-delay-get'
SWARM_DELAY_SET_CMD = 'sma-astro-delay-set'

SWARM_XENG_PARALLEL_CHAN = 8
SWARM_XENG_SIDEBANDS = ('LSB', 'USB')
SWARM_XENG_TOTAL = SWARM_XENG_PARALLEL_CHAN * len(SWARM_ALL_FID)

SWARM_VISIBS_N_PKTS = 512
SWARM_VISIBS_CHANNELS = 2048
SWARM_VISIBS_BASELINES = 128
SWARM_VISIBS_HEADER_SIZE = 8
SWARM_VISIBS_HEADER_FMT = '>HBHBH'
SWARM_VISIBS_PKT_SIZE = 1025 * 8
SWARM_VISIBS_CHUNK_DELAY = (2**21)
SWARM_VISIBS_SIDEBANDS = len(SWARM_XENG_SIDEBANDS)
SWARM_VISIBS_ACC_SIZE = SWARM_VISIBS_CHANNELS * SWARM_VISIBS_BASELINES * SWARM_VISIBS_SIDEBANDS * 2
SWARM_VISIBS_TOTAL = 2**SWARM_VISIBS_N_PKTS-1

SWARM_BENGINE_PORT = 0xbea3
SWARM_BENGINE_CORE = 'beng_eth_core'
SWARM_BENGINE_CTRL = 'beng_eth_ctrl'
SWARM_BENGINE_SENDTO_IP = 'beng_eth_dest_ip'
SWARM_BENGINE_DISABLE = 'beng_disable'
SWARM_BENGINE_GAIN = 'beng_gain'
SWARM_BENGINE_PKT_SIZE = 2056
SWARM_BENGINE_HEADER_SIZE = 8
SWARM_BENGINE_HEADER_FMT = '>BIBH'
SWARM_BENGINE_PAYLOAD_SIZE = SWARM_BENGINE_PKT_SIZE - SWARM_BENGINE_HEADER_SIZE

SWARM_DBE_PORT = 4000
SWARM_DBE_N_RX_CORE = 4
SWARM_DBE_RX_CORE = 'tengbe_rx%d_core'

SWARM_FIXED_OFFSETS_DSM_NAME = 'SWARM_FIXED_OFFSETS_X'
SWARM_FIXED_OFFSETS_DELAY = 'DELAY_V2_D'
SWARM_FIXED_OFFSETS_PHASE = 'PHASE_V2_D'

SWARM_SCAN_DSM_NAME = 'SWARM_SCAN_X'
SWARM_SCAN_LENGTH = 'LENGTH_L'
SWARM_SCAN_PROGRESS = 'PROGRESS_L'

SWARM_BLACK_HOLE_MAC = 0x000f530cd899

# These are copied right from smainit's smadaemon.h
SMAINIT_NORMAL_RTN = 0
SMAINIT_SIGNAL_RTN = 0x20
SMAINIT_SYSERR_RTN = 0x40
SMAINIT_QUIT_RTN = (SMAINIT_SIGNAL_RTN + SIGQUIT)


# Reusable Utility Functions
def query_yes_no(question, default="yes"):
    """Ask a yes/no question via raw_input() and return their answer.

    "question" is a string that is presented to the user.
    "default" is the presumed answer if the user just hits <Enter>.
        It must be "yes" (the default), "no" or None (meaning
        an answer is required of the user).

    The "answer" return value is True for "yes" or False for "no".
    """
    valid = {"yes": True, "y": True, "ye": True,
             "no": False, "n": False}
    if default is None:
        prompt = " [y/n] "
    elif default == "yes":
        prompt = " [(y)/n] "
    elif default == "no":
        prompt = " [y/(n)] "
    else:
        raise ValueError("invalid default answer: '%s'" % default)

    while True:
        sys.stdout.write(question + prompt)
        choice = raw_input().lower()
        if default is not None and choice == '':
            return valid[default]
        elif choice in valid:
            return valid[choice]
        else:
            sys.stdout.write("Please respond with 'yes' or 'no' "
                             "(or 'y' or 'n').\n")
