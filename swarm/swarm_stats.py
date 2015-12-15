#!/usr/bin/env python2.7

import struct
import argparse
import datetime
import logging
from numpy import array, log10, linspace, histogram

from swarm import *

parser = argparse.ArgumentParser(description='Plot a histogram of the 8-bit sampled correlator data for the given antennas')
parser.add_argument('antennas', metavar='ANTS', type=int, nargs='*', 
                    help='show histogram for ANTS')
parser.add_argument('-c', '--chunks', metavar='CHUNKS', type=int, nargs='*', 
                    help='show histogram for the given CHUNKS only')
parser.add_argument('-p', '--polarizations', metavar='POLARIZATIONS', type=int, nargs='*', 
                    help='show histogram for the given POLARIZATIONS only')
parser.add_argument('-s', '--source', metavar='SOURCE', type=str, choices=('analog', 'digital'), default='analog',  
                    help='set source to SOURCE (default analog)')
parser.add_argument('-b', '--bins', metavar='BINS', type=int, default=32,
                    help='split the data up into BINS')
parser.add_argument('--norm', action='store_true',
                    help='normalize the bins')
parser.add_argument('--save', action='store_true',
                    help='save histograms to file, <datetime>.<norm|cnts><antenna>-<chunk>-<polarization>..<source>.hist')
parser.add_argument('--plot', action='store_true',
                    help="show plots, usually used with --save")
args = parser.parse_args()

if args.save:
    from numpy import savetxt

if args.plot:
    from pylab import gca, bar, xlim, show, figure, title, text

swarm = SwarmQuadrant(0)

swarm.members_do(lambda fid, member: member.set_scope(3, 0, 6))
if args.source == 'analog':
    swarm.members_do(lambda fid, member: member.set_source(2, 2))
elif args.source == 'digital':
    swarm.members_do(lambda fid, member: member.set_source(3, 3))

# Setup some basic logging
logging.basicConfig()
formatter = logging.Formatter('%(name)-24s: %(asctime)s : %(levelname)-8s %(message)s')
logger = logging.getLogger()
logger.handlers[0].setFormatter(formatter)
logger.setLevel(logging.INFO)

for member in swarm.get_valid_members():

    for input_n in SWARM_MAPPING_INPUTS:

        roach = member.roach2
        scope = SWARM_SCOPE_SNAP % input_n
        input_inst = member.get_input(input_n)
        polarization = input_inst._pol
        antenna = input_inst._ant
        chunk = input_inst._chk

        if args.polarizations and polarization not in args.polarizations:
            continue

        if args.antennas and antenna not in args.antennas:
            continue

        if args.chunks and chunk not in args.chunks:
            continue

        snap = roach.snapshot_get(scope, man_trig=True, wait_period=16)
        data = array(struct.unpack('>%db' % snap['length'], snap['data']))
        est_power = ((data.std() * (0.500 / 256))**2) / 50.0
        est_power_dBm = 10 * log10(est_power) + 30.0

        logger.info('Antenna {}, Chunk {}, Pol. {} est. ({}) power: {:.2f} dBm'.format(antenna, chunk, polarization, args.source, est_power_dBm))

        bins = linspace(-128, 128, args.bins, endpoint=False)
        hist, bins = histogram(data, bins=bins, density=args.norm)
        width = bins[1] - bins[0]
        center = (bins[:-1] + bins[1:])/2

        if args.plot:
            figure()
            title('Histogram for Antenna {}, Chunk {}, Pol. {} ({})'.format(antenna, chunk, polarization, args.source))
            bar(center, hist, align='center', width=width)
            text(0.01, 0.95, 'Est. power %.2f dBm' % est_power_dBm, transform=gca().transAxes)
            xlim(-128, 128)

        if args.save:
            now = datetime.datetime.now()
            nowstr = now.strftime('%Y-%d-%m-%H-%M-%S')
            if args.norm:
                fmt = '%10.5f %12.8f'
                filename = '{}.norm{}-{}-{}.{}.hist'.format(nowstr, antenna, chunk, polarization, args.source)
                hist_dtype = ('norm', float)
            else:
                fmt = '%10.5f %12d'
                filename = '{}.cnts{}-{}-{}.{}.hist'.format(nowstr, antenna, chunk, polarization, args.source)
                hist_dtype = ('cnts', int)
            zipped = array(zip(list(center), list(hist)),
                           dtype=[('bins', float), hist_dtype])
            savetxt(filename, zipped, fmt)

if args.plot:
    show()
