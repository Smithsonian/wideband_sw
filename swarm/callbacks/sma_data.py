from redis import Redis
from numpy import array, conjugate, exp, pi, uint8, vstack, zeros
from swarm import SwarmDataCallback
from swarm.data import SwarmDataPackage

REDIS_UNIX_SOCKET = '/tmp/redis.sock'


class SMAData(SwarmDataCallback):

    def __init__(self, swarm, redis_host='localhost', redis_port=6379, pub_channel='swarm.data', rephase_2nd_sideband_data=False):
        self.redis = Redis(redis_host, redis_port, unix_socket_path=REDIS_UNIX_SOCKET)
        self.rephase_2nd_sideband_data = rephase_2nd_sideband_data
        super(SMAData, self).__init__(swarm)
        self.pub_channel = pub_channel

    def __call__(self, data):
        """ Callback for sending data to SMA's dataCatcher/corrSaver """

        # Apply beamformer second sideband phases if needed
        if self.rephase_2nd_sideband_data:
            if data.is_phase_applied():
                self.logger.debug("Beamformer phases already applied to data, skipping.")
            else:
                phases = self.swarm.calc_baseline_second_sideband_phase(data.baselines)
                count = data.apply_phase(data.baselines, phases, sb="LSB")

                # Debug log that second sideband phases applied
                self.logger.debug(
                    "Applied 2nd sideband beamformer phases on %d of %d baselines." % (
                        count, len(phases)
                    )
                )

        # Publish the raw data to redis
        subs = self.redis.publish(self.pub_channel, data._byte_view)
        # Info log the set
        self.logger.info("Data sent to %d subscribers", subs)
