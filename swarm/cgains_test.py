import redis

redis_host = "localhost"
redis_port = 6379
key = "cgains-update"


def my_handler(message):
    print('MY HANDLER: ', message['data'])


redis_connection = redis.Redis(host=redis_host, port=redis_port)
redis_pubsub = redis_connection.pubsub()
redis_pubsub.psubscribe(**{key: my_handler})
thread = redis_pubsub.run_in_thread(sleep_time=1)


# manually get value from key.
# value_from_redis = redis.get(key)

raw_input("Subscribed to channel, press any key to exit...")
