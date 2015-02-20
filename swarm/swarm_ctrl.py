#!/usr/bin/env python2.7

import logging, argparse
from struct import unpack
from numpy.linalg import lstsq
from numpy import (
    array, 
    around,
    zeros,
    empty, 
    isnan,
    angle, 
    log10,
    sqrt,
    abs,
    pi,
)

from swarm import (
    SWARM_VISIBS_ACC_SIZE,
    SWARM_XENG_SIDEBANDS,
    SWARM_MAPPING_CHUNKS,
    SWARM_MAPPING,
    SwarmDataHandler,
    SwarmListener,
    SwarmBaseline,
    Swarm,
    )


DEFAULT_BITCODE = 'sma_corr_2015_Feb_10_1113.bof.gz'


def save_bin(filename, datas):
    with open(filename, 'wb') as file_:
        file_.write(datas)

def main():

    # Setup some basic logging
    logging.basicConfig()
    formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
    logger = logging.getLogger()
    logger.handlers[0].setFormatter(formatter)

    # Callback for showing statistics
    def log_stats(data):
        sideband = 'USB'
        auto_amps = {}
        for baseline in data.baselines:
            if baseline.is_valid():
                chunk = baseline.left._chk
                interleaved = array(list(p for p in data[baseline][chunk][sideband] if not isnan(p)))
                complex_data = interleaved[0::2] + 1j * interleaved[1::2]
                if baseline.is_auto():
                    auto_amps[baseline] = abs(complex_data).mean()
                    norm = auto_amps[baseline]
                else:
                    norm_left = auto_amps[SwarmBaseline(baseline.left, baseline.left)]
                    norm_right = auto_amps[SwarmBaseline(baseline.right, baseline.right)]
                    norm = sqrt(norm_left * norm_right)
                norm = max(1.0, norm) # make sure it's not zero
                logger.info(
                    '{baseline!s}[chunk={chunk}].{sideband} : Amp(avg)={amp:>12.2e}, Phase(avg)={pha:>8.2f} deg, Corr.={corr:>8.2f}%'.format(
                        baseline=baseline, chunk=chunk, sideband=sideband, 
                        corr=100.0*abs(complex_data).mean()/norm,
                        amp=abs(complex_data).mean(),
                        pha=(180.0/pi)*angle(complex_data.mean()),
                        )
                    )

    # Callback for VLBI calibration
    def calibrate_vlbi(data):
        sideband = 'USB'
        n_inputs = len(data.inputs)
        baselines = [data._autos[0],] + data._cross 
        n_baselines = len(baselines)
        ordinate_vector = zeros([n_baselines,])
        coefficient_matrix = zeros([n_baselines, n_inputs])
        for baseline in baselines:
            if baseline.is_valid():
                chunk = baseline.left._chk
                interleaved = array(list(p for p in data[baseline][chunk][sideband] if not isnan(p)))
                complex_data = interleaved[0::2] + 1j * interleaved[1::2]
                left_i = data.inputs.index(baseline.left)
                right_i = data.inputs.index(baseline.right)
                baseline_i = baselines.index(baseline)
                ordinate_vector[baseline_i] = angle(complex_data.mean())
                coefficient_matrix[baseline_i][left_i] = 1.0
                if not baseline.is_auto():
                    coefficient_matrix[baseline_i][right_i] = -1.0
        solution_vector, resids, rank, sing = lstsq(coefficient_matrix, ordinate_vector)
        phases = around(solution_vector, 4) + 0
        for i in range(n_inputs):
            logger.info('{} phase= {:<06.2f} deg'.format(data.inputs[i], (180.0/pi)*phases[i]))

    # Callback for saving raw data
    def save_rawdata(rawdata):
        for fid, datas in enumerate(rawdata):
            filename = 'fid%d.dat' % fid
            save_bin(filename, datas)
            logger.info('Data for FID #%d saved to %r' % (fid, filename))

    # Callback for checking ramp
    def check_ramp(rawdata):
        ramp = empty(SWARM_VISIBS_ACC_SIZE)
        for fid, datas in enumerate(rawdata):
            raw = array(unpack('>%dI'%SWARM_VISIBS_ACC_SIZE, datas))
            ramp[0::2] = raw[1::2]
            ramp[1::2] = raw[0::2]
            errors_ind = list(i for i, p in enumerate(ramp) if p !=i)
            logger.info('Ramp errors for FID #%d: %d' % (fid, len(errors_ind)))

    # Parse the user's command line arguments
    parser = argparse.ArgumentParser(description='Catch and process visibility data from a set of SWARM ROACH2s')
    parser.add_argument('-v', dest='verbose', action='store_true', help='display debugging logs')
    parser.add_argument('-m', '--swarm-mapping', dest='swarm_mapping', metavar='SWARM_MAPPING', type=str, default=SWARM_MAPPING,
                        help='Use file SWARM_MAPPING to determine the SWARM input to IF mapping (default="%s")' % SWARM_MAPPING)
    parser.add_argument('-i', '--interface', dest='interface', metavar='INTERFACE', type=str, default='eth2',
                        help='listen for UDP data on INTERFACE (default="eth2")')
    parser.add_argument('-b', '--bitcode', dest='bitcode', metavar='BITCODE', type=str, default=DEFAULT_BITCODE,
                        help='program ROACH2s with BITCODE (default="%s")' % DEFAULT_BITCODE)
    parser.add_argument('-t', '--integrate-for', dest='itime', metavar='INTEGRATION-TIME', type=float, default=28.45,
                        help='integrate for approximately INTEGRATION-TIME seconds (default=30)')
    parser.add_argument('--setup-only', dest='setup_only', action='store_true',
                        help='only program and setup the board; do not wait for data')
    parser.add_argument('--listen-only', dest='listen_only', action='store_true',
                        help='do NOT setup the board; only wait for data')
    parser.add_argument('--visibs-test', dest='visibs_test', action='store_true',
                        help='enable the DDR3 visibility ramp test')
    parser.add_argument('--save-raw-data', dest='save_rawdata', action='store_true',
                        help='Save raw data from each FID to file')
    parser.add_argument('--log-stats', dest='log_stats', action='store_true',
                        help='Print out some baselines statistics (NOTE: very slow!)')
    args = parser.parse_args()

    # Set logging level given verbosity
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    # Silence katcp INFO messages
    katcp_logger = logging.getLogger('katcp')
    katcp_logger.setLevel(logging.WARNING)

    # Setup the listener class
    listener = SwarmListener(args.interface)

    # Create our SWARM instance
    swarm = Swarm()

    if not args.listen_only:

        # Setup using the Swarm class and our parameters
        swarm.setup(args.bitcode, args.itime, listener)

    if args.visibs_test:

        # Enable the visibs test
        swarm.members_do(lambda fid, member: member.visibs_delay(delay_test=True))

    else:

        # Disable the visibs test
        swarm.members_do(lambda fid, member: member.visibs_delay(delay_test=False))

    if not args.setup_only:

        # Create the data handler 
        swarm_handler = SwarmDataHandler(swarm, listener)

        if args.log_stats:

            # Use a callback to show visibility stats
            swarm_handler.add_callback(log_stats)
            swarm_handler.add_callback(calibrate_vlbi)

        if args.save_rawdata:

            # Give a rawback that saves the raw data
            swarm_handler.add_rawback(save_rawdata)

        if args.visibs_test:

            # Give a rawback that checks for ramp errors
            swarm_handler.add_rawback(check_ramp)

        # Start the main loop
        swarm_handler.loop()



# Do main if not imported
if __name__=="__main__": main()
