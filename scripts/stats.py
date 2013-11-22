#!/usr/bin/env python

import struct
import argparse
import datetime
from corr.katcp_wrapper import FpgaClient

parser = argparse.ArgumentParser(description='Plot a histogram of the 8-bit sampled correlator data for the given antennas')
parser.add_argument('antennas', metavar='ANTS', type=int, nargs='+', 
                    help='show histogram for ANTS')
parser.add_argument('-s', '--source', metavar='SOURCE', type=str, choices=('analog', 'digital'), default='analog',  
                    help='set source to SOURCE (default analog)')
parser.add_argument('-b', '--bins', metavar='BINS', type=int, default=32,
                    help='split the data up into BINS')
parser.add_argument('--norm', action='store_true',
                    help='normalize the bins')
parser.add_argument('--save', action='store_true',
                    help='save histograms to file, <datetime>.<norm|cnts><antenna>.<source>.hist')
parser.add_argument('--no-plot', action='store_true',
                    help="don't show plots, usually used with --save")
args = parser.parse_args()

if not args.no_plot:
    from numpy import array, linspace, histogram, savetxt, log10
    from pylab import gca, bar, xlim, show, figure, title, text

if args.source=='digital':
    source_ctrl = struct.pack('>I', (3<<3) + 3)
else:
    source_ctrl = struct.pack('>I', (2<<3) + 2)

roach03 = FpgaClient('roach2-03', 7147)
roach03.wait_connected()
roach03.write('source_ctrl', source_ctrl) # set both inputs to requested source
roach03.write('scope_ctrl',  struct.pack('>I', (3<<16) + (6<<8) + 0) ) # set scope to record raw samples

roach04 = FpgaClient('roach2-04', 7147)
roach04.wait_connected()
roach04.write('source_ctrl', source_ctrl) # set both inputs to requested source
roach04.write('scope_ctrl',  struct.pack('>I', (3<<16) + (6<<8) + 0) ) # set scope to record raw samples

mapping = {1: (roach03, 'scope_snap0'),
           2: (roach03, 'scope_snap1'),
           5: (roach04, 'scope_snap0'),
           8: (roach04, 'scope_snap1')}

for a in args.antennas[::-1]:

    if a not in mapping:
        raise ValueError('Antenna %d is not mapped to a correlator input!' % a)

    roach, scope = mapping[a]
    snap = roach.snapshot_get(scope, man_trig=True)
    data = array(struct.unpack('>%db' % snap['length'], snap['data']))
    est_power = ((data.std() * (0.500 / 256))**2) / 50.0
    est_power_dBm = 10 * log10(est_power) + 30.0

    bins = linspace(-128, 128, args.bins, endpoint=False)
    hist, bins = histogram(data, bins=bins, density=args.norm)
    width = bins[1] - bins[0]
    center = (bins[:-1] + bins[1:])/2

    if not args.no_plot:
        figure()
        title('Histogram for Antenna {} ({})'.format(a, args.source))
        bar(center, hist, align='center', width=width)
        text(0.01, 0.95, 'Est. power %.2f dBm' % est_power_dBm, transform=gca().transAxes)
        xlim(-128, 128)

    if args.save:
        now = datetime.datetime.now()
        nowstr = now.strftime('%Y-%d-%m-%H-%M-%S')
        if args.norm:
            fmt = '%10.5f %12.8f'
            filename = '{}.norm{}.{}.hist'.format(nowstr, a, args.source)
            hist_dtype = ('norm', float)
        else:
            fmt = '%10.5f %12d'
            filename = '{}.cnts{}.{}.hist'.format(nowstr, a, args.source)
            hist_dtype = ('cnts', int)
        zipped = array(zip(list(center), list(hist)),
                        dtype=[('bins', float), hist_dtype])
        savetxt(filename, zipped, fmt)

if not args.no_plot:
    show()
