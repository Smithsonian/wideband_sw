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

        # Publish the raw data to redis
        subs = self.redis.publish(self.pub_channel, data)

        # Info log the set
        self.logger.info("Data sent to %d subscribers", subs)
