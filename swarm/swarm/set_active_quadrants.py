import argparse

parser = argparse.ArgumentParser(description='Script to set the active SWARM quadrants.')
parser.add_argument('integers', metavar='N', type=int, nargs='+',
                    help='Enter a space delimited list of integers representing SWARM quadrants.')

args = parser.parse_args()
print(args.integers)

