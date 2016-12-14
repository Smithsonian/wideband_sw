from redis import StrictRedis
from swarm.pysendint import send_integration
from swarm import SwarmDataCallback


REDIS_UNIX_SOCKET = '/tmp/redis.sock'


class SMAData(SwarmDataCallback):

    def __init__(self, swarm, redis_host='localhost', redis_port=6379, pub_channel='swarm.data'):
        self.redis = StrictRedis(redis_host, redis_port, unix_socket_path=REDIS_UNIX_SOCKET)
        super(SMAData, self).__init__(swarm)
        self.pub_channel = pub_channel

    def __call__(self, data):
        """ Callback for sending data to SMA's dataCatcher/corrSaver """

        # Send data to dataCatcher/corrSaver
        for baseline in data.baselines:

            # Send the appropriate chunk
            if baseline.is_valid():

                # Get baseline antennas
                ant_left = baseline.left._ant
                ant_right = baseline.right._ant

                # Get the chunk
                chunk = baseline.left._chk

                # Get baseline polarizations
                pol_left = baseline.left._pol
                pol_right = baseline.right._pol

                # Get each sidebands data
                usb_data = data[baseline, 'USB']
                if baseline.is_auto():
                    lsb_data = usb_data.copy()
                else:
                    lsb_data = data[baseline, 'LSB']

                # Send our integration
                send_integration(data.int_time - (data.int_length/2.0), 
                                 data.int_length, chunk,
                                 ant_left, pol_left, 
                                 ant_right, pol_right, 
                                 lsb_data.tolist(), usb_data.tolist(), 0)

                # Debug log this baseline
                self.logger.debug("Processed baseline: {!s}".format(baseline))

        # Info log the set
        self.logger.info("Processed all baselines")
