import argparse
import logging
import os
from collections import namedtuple
from struct import pack
from threading import Thread

from numpy import uint16
from redis import StrictRedis

from swarm import Swarm
from swarm.defines import SWARM_CHANNELS, SWARM_CGAIN_GAIN

CgainUpdate = namedtuple("CgainUpdate", "quadrant antenna rx gains")

# Tenzing Redis
redis_host = "localhost"
redis_port = 6379
subscribe_key = "cgains-update"

# SMAX Redis
smax_host = "128.171.116.189"
smax_port = 6379
smax_table_name = "cgains"
host_name = os.uname()[1]   # Hostname is used when sending to SMAX.

parser = argparse.ArgumentParser(description='Starts a listener to the cgains-update channel')
parser.add_argument('-v', dest='verbose', action='store_true', help='Set logging level to DEBUG')
parser.add_argument('-t', dest='test', action='store_true', help='Test mode, does not update roach2 or smax')
args = parser.parse_args()

logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)


def update_roach2s(cgain_updates):
    """
    Uses the quadrant and antenna number to create a list of roach2s to update, and then
    does a threaded "write" to the SWARM_CGAIN_GAIN register of each of the roach2s in the list.
    :param cgain_updates: NamedTuple "CgainUpdate" to hold the attributes received from the redis message.
    :return: List of tuples, where the tuples are of format (SwarmMember, Rx, Gains_Bin)
    """

    # Instantiate a swarm object
    swarm = Swarm()
    roach2_update_list = []

    # Prepare the gains, and collect the roach2 objects.
    for cgain_update in cgain_updates:
        gains_bin = pack('>%dH' % SWARM_CHANNELS, *cgain_update.gains)

        # Look up the quadrant, and then access the correct swarm member object using the antenna index.
        swarm_member = swarm.quads[cgain_update.quadrant][cgain_update.antenna - 1]

        roach2_update_list.append((swarm_member, cgain_update.rx, gains_bin))
        logging.debug("Mapped quadrant:%d,antenna:%d to %s",
                      cgain_update.quadrant,
                      cgain_update.antenna,
                      swarm_member.roach2_host)

    # Send cgain updates in parallel.
    cgain_threads = list(Thread(target=write_cgain_register, args=[swarm_member.roach2, rx, gains])
                         for swarm_member, rx, gains in roach2_update_list)
    for thread in cgain_threads:
        thread.start()

    # Finally join all threads
    for thread in cgain_threads:
        thread.join()
    logging.info("All cgain update threads completed.")

    return roach2_update_list


def write_cgain_register(roach2_object, rx, gains_bin):
    """
    Simple function to be used in the threaded roach2 cgain update. Uses katcp to update fpga cgain registers.
    :param roach2_object: Kactp "FpgaClient" object
    :param rx: Integer value of 0 or 1 representing which receiver to update.
    :param gains_bin: Packed set of values to write to cgains register
    """
    if not args.test:
        roach2_object.write(SWARM_CGAIN_GAIN % rx, gains_bin)


def update_cgain_smax(cgain_updates):
    """
    After the roach2 updates are successful, this function is used to post the values to SMAX.
    The smax code was yanked from our smax-python-redis-client which is python3. Here is the comment from the
    send() function in that library:
        Send data to redis using the smax macro HSetWithMeta to include
        metadata.  The metadata is typeName, dataDimension(s), dataDate,
        source of the data, and a sequence number.
        Date and sequence number are added by the redis macro.

    :param cgain_updates: NamedTuple "CgainUpdate" to hold the attributes received from the redis message.
    """
    redis_client = StrictRedis(host=smax_host, port=smax_port, db=0)
    setSHA = redis_client.hget('scripts', 'HSetWithMeta')

    for cgain in cgain_updates:
        segment = cgain.quadrant
        antenna = cgain.antenna
        rx = cgain.rx
        smax_key = "correlator:swarm:segment:{}:antenna:{}:input:{}".format(segment + 1, antenna, rx + 1)

        # Convert list of integers into a space separated string of values.
        string_data = str(cgain.gains).translate(None, '[],\'')

        redis_client.evalsha(setSHA, '1', smax_key, host_name, smax_table_name, string_data, "int16",
                                 len(string_data))
    logging.info("SMAX updated")


def cgains_handler(message):
    """
    Callback function for the redis client to use when subscribing to the cgains-update channel.
    :param message: Redis message received from cgains-update channel.
    """
    logging.info("Message received from cgains-update channel")

    # Parse the Redis message.
    data = message['data']
    roach2_raw_lines = data.split(" | ")
    cgain_updates = [parse_cgains_line(line) for line in roach2_raw_lines]

    # Write new cgain values to roach2s.
    update_roach2s(cgain_updates)

    # Post updated table to SMAX.
    if not args.test:
        update_cgain_smax(cgain_updates)


def parse_cgains_line(line):
    """
    Parses an individual "line" in the redis message, and creates an object used the NamedTuple CgainUpdate
    :param line: A single entry in the redis message received from the cgains-update channel.
    :return: namedTuple CgainUpdate object holding the parsed information from the redis message entry.
    """
    # From C code: sprintf("%d%d %d", (segment-1), antenna, rx, space delimited integers)
    line_split = line.split(" ")
    quadrant = int(line_split[0][0])
    antenna = int(line_split[0][1])
    rx = int(line_split[1])

    # Cast all the cgain values to unsigned integers
    gains = [uint16(x) for x in line_split[2:]]
    logging.debug("Parsed line of message: quadrant:%d, antenna:%d, rx:%d, num_gains:%d",
                  quadrant, antenna, rx, len(gains))
    logging.debug("Gain values: " + str(gains))
    return CgainUpdate(quadrant, antenna, rx - 1, gains)


# Create the redis client and subscribe to the cgains-update channel on a separate thread.
redis_server = StrictRedis(host='localhost', port=6379, db=0)
redis_pubsub = redis_server.pubsub(ignore_subscribe_messages=True)
redis_pubsub.subscribe(**{"cgains-update": cgains_handler})
redis_pubsub_thread = redis_pubsub.run_in_thread(sleep_time=1)

raw_input("Subscribed to " + subscribe_key + " " + "channel, press enter/return key to exit...")
redis_pubsub_thread.stop()
