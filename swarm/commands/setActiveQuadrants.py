import argparse
import sys

# Temp variables, to be deleted when operational.
SWARM_NUM_QUADRANTS = 4
ACTIVE_QUADRANTS_PATH = '/Users/cmoriarty/repos/wideband_sw/quadsa'

# Parse the input arguments.
parser = argparse.ArgumentParser(description='Sets which SWARM quadrants are active. eg setActiveQuadrants 1 2 4')
parser.add_argument('quadrants', type=int, nargs="+", choices=range(1, SWARM_NUM_QUADRANTS + 1),
                    help='Space delimited list of active quadrants. eg 1 2 4')
args = parser.parse_args()

# Additional error checking.
if len(args.quadrants) > SWARM_NUM_QUADRANTS:
    print "Too many quadrants listed, " + str(SWARM_NUM_QUADRANTS) + " is the max. Exiting..."
    sys.exit()

# Update the SwarmQuadrantsInArray file.
try:
    with open(ACTIVE_QUADRANTS_PATH, "w") as quad_file:
        quad_file.write(" ".join(str(x) for x in args.quadrants))

except BaseException as e:
    print e
    print "There was a problem updating the SwarmQuadrantsInArray file, exiting..."
    sys.exit()

# Touch appropriate killdaemon files to restart swarm processes.



