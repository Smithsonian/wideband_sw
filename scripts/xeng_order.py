#!/usr/bin/env python

import decode


ants  = (0, 1, 2, 3)
chans = (0, 8, 16, 24)
direct_connect_mapping = (
    ((0,  0), (1,  0)), # roach2-00: {input 0: [ant, chanl], input 1: [...]}
    ((2,  0), (3,  0)), # roach2-01: {input 0: [...], input 1: [...]}
    ((0,  8), (1,  8)), # roach2-00: {...}
    ((2,  8), (3,  8)), # ...
    ((0, 16), (1, 16)), # ...
    ((2, 16), (3, 16)), # ...
    ((0, 24), (1, 24)), # ...
    ((2, 24), (3, 24)), # roach2-01: {...}
)

visib_order = dict(((a, b), [0]*4) for a in range(4) for b in range(4) if b >= a)
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
