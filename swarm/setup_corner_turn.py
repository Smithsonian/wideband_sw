from swarm import Swarm

swarm = Swarm()

for quad in swarm.quads:
  quad.members_do(lambda fid, member: member.setup(quad.qid, fid, 8, 5.0))
