from redis import StrictRedis, ConnectionError

redis_host = "localhost"
redis_port = 6379
key = "cgains-update"
redis = StrictRedis(redis_host, redis_port)
value_from_redis = redis.get(key)

print(value_from_redis)
