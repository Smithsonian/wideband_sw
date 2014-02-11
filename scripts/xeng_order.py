#!/usr/bin/env python

import decode


inputs = range(16)
switched_mapping = list((inputs[i], inputs[i+1]) for i in range(0, len(inputs), 2))
xengine_out = list(decode.get_xengine_output_order(mapping=switched_mapping))

visib_order = {}
for i, (left, right) in enumerate(xengine_out):
    visib_order[(left, right)] = i - (i % 2) * (left != right)

if __name__ == "__main__":
    n_autos = sum(left==right for left, right in visib_order.keys())
    n_cross = len(visib_order.keys()) - n_autos
    print "Total  autos: %4d" % n_autos
    print "Total  cross: %4d" % n_cross
    print "Total values: %4d" % (n_autos + n_cross*2)
    print
    print "    "*2 + " ",
    # Print header
    for ant in inputs:
        print "%4d" % ant,
    print
    # Then print the order
    print "     " + "#####" * (len(inputs) + 1) + "#"
    for left in inputs:
        print "%4d" % left,
        print "#   ",
        for right in inputs:
            try:
                print "%4d" % visib_order[(left, right)],
            except KeyError:
                print "----",
        print "#"
    print "     " + "#####" * (len(inputs) + 1) + "#"
