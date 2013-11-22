#!/usr/bin/env python

import decode


ants  = (2, 5, 6, 7)
chans = (0, 8, 16, 24)
direct_connect_mapping = (
    ((2,  0), (5,  0)), # roach2-00: {input 0: [ant, chanl], input 1: [...]}
    ((6,  0), (7,  0)), # roach2-01: {input 0: [...], input 1: [...]}
    ((2,  8), (5,  8)), # roach2-00: {...}
    ((6,  8), (7,  8)), # ...
    ((2, 16), (5, 16)), # ...
    ((6, 16), (7, 16)), # ...
    ((2, 24), (5, 24)), # ...
    ((6, 24), (7, 24)), # roach2-01: {...}
)

visib_order = dict(((a, b), [0]*4) for a in ants for b in ants if b >= a)
out = list(decode.get_xengine_output_order(mapping=direct_connect_mapping))

for i, ((left, left_chan), (right, right_chan)) in enumerate(out):
    if left_chan == right_chan:
        baseline = tuple(sorted((left, right)))
        visib_order[baseline][left_chan>>3] = i - (i % 2) * (left != right)

for chan in chans:
    print "##### Chan %2d #########" % chan
    for left in ants:
        print "#",
        for right in ants:
            baseline = tuple(sorted((left, right)))
            print "%4d" % visib_order[baseline][chan>>3],
        print "#"
    print "#######################"
    print ""
