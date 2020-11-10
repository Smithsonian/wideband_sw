from redis import StrictRedis

redis_host = "localhost"
redis_port = 6379
subscribe_key = "cgains-update"


def my_handler(message):
    print('MY HANDLER: ', message['data'])


redis_server = StrictRedis(host='localhost', port=6379, db=0)
redis_pubsub = redis_server.pubsub(ignore_subscribe_messages=True)
redis_pubsub.subscribe(**{"cgains-update": my_handler})
thread = redis_pubsub.run_in_thread(sleep_time=1)


# manually get value from key.
# value_from_redis = redis.get(key)

raw_input("Subscribed to channel, press any key to exit...")
thread.stop()
