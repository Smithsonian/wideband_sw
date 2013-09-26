#!/usr/bin/env python

import threading, sys, katcp, corr, time, struct

WALSH_PERIOD =  (2**26)/(52e6)

threads = []
roach_names = sys.argv[1:] # list of roach's to sync

def send_sync(name, sec, nsec, off):
    message = katcp.Message.request("sma-walsh-arm", str(sec), str(nsec), str(off))
    roach = corr.katcp_wrapper.FpgaClient(name, 7147)
    roach.wait_connected()
    reply, informs = roach.blocking_request(message)
    if not reply.reply_ok():
        print "SOWF arming failed for %s" % name
    else:
        # Wait for sync to happen
        time.sleep(2)
        # Arm the mcount generator
        roach.write('sync_ctrl', struct.pack('>I', 0))
        roach.write('sync_ctrl', struct.pack('>I', 0x20000000))
        roach.write('sync_ctrl', struct.pack('>I', 0))

# Cycle through ROACH's and sync
arm_sec = int(time.mktime(time.localtime())) + 2
for name in roach_names:
    threads.append(threading.Thread(target=send_sync, args=(name, arm_sec, 0, 0)))
    threads[-1].start()


# Wait until all threads are done
for thread in threads:
    thread.join()
