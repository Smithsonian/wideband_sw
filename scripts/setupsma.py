#!/usr/bin/env python

import corr, time, numpy, struct, sys

#bitstream = 'xeng_core_test_2012_Nov_28_1322.bof' # works but no headers
#bitstream = 'xeng_core_test_2012_Nov_28_1726.bof'# headers but overflows
#bitstream = 'xeng_core_test_2012_Nov_28_2023.bof' # compiled for simulation
#bitstream = 'xeng_core_test_2012_Nov_28_2125.bof' # works!
#bitstream = 'sma_corr_2012_Nov_29_1028.bof' # compiled for rev 2!
#bitstream = 'sma_corr_2012_Nov_30_1505.bof' # funky FFT output
#bitstream = 'sma_corr_2012_Dec_03_2051.bof' # still funky, WTF?
#bitstream = 'sma_corr_2012_Dec_04_1118.bof' # rev 2 again, but works
#bitstream = 'sma_corr_floorplanned.bof' # works, floorplanned
#bitstream = 'sma_corr_2012_Dec_13_1131.bof' # rounding changes, works, no floorplanning
#bitstream = 'sma_corr_2012_Dec_27_1530.bof' # compiled with 14.4, also added sync-to-LEDs, QDR cal_fail
#bitstream = 'sma_corr_2013_Jan_03_2317.bof' # works, no walshing
#bitstream =  'sma_corr_2013_Jan_16_1737.bof' # works but adc5g SPI buggy
#bitstream = 'sma_corr_2013_Jan_18_1750.bof' # works, no corner-turn
#bitstream = 'sma_corr_2013_Jan_31_1506.bof' # direct connect corner-turn, sync issue
#bitstream = 'sma_corr_2013_Mar_02_1919.bof' # missing some debug features
#bitstream = 'sma_corr_2013_Mar_13_0213.bof' # data ordering problem
#bitstream = 'sma_corr_2013_Apr_15_0059.bof' # direct connect interleaving problem
#bitstream = 'sma_corr_2013_Apr_19_0144.bof' # direct connect sync-off-by-1(?)
#bitstream = 'sma_corr_2013_Apr_28_1952.bof' # off-by-one fix
bitstream = 'sma_corr_2013_Apr_30_1449.bof' # spurious sync fix


roach     = sys.argv[1] #'roach2-02'
network   = 'bypass'
if len(sys.argv) > 2:
	network = sys.argv[2] # pass 'network' to enable direct connect

if roach=='roach2-01':
	final_hex = 10
elif roach=='roach2-02':
	final_hex = 11
elif roach=='roach2-03':
	final_hex = 12
elif roach=='roach2-04':
	final_hex = 13
elif roach=='roach2-05':
	final_hex = 14
else:
	raise Exception("ROACH2 not supported!")


#dest_ip   = 192*(2**24) + 168*(2**16) + 11*(2**8) + 11
dest_ip   = 192*(2**24) + 168*(2**16) + 11*(2**8) + 30
dest_port = 4100

src_ip0    = 192*(2**24) + 168*(2**16) + 11*(2**8) + final_hex+51 
src_ip1    = 192*(2**24) + 168*(2**16) + 10*(2**8) + final_hex+52
src_port0  = 4000
src_port1  = 4001

mac_base0 = (2<<40) + (2<<32) + final_hex
mac_base1 = (2<<40) + (2<<32) + final_hex + 1
gbe0     = 'visibs_gbe0'
gbe1     = 'visibs_gbe1'

print('Connecting to server %s on port... '%(roach)),
fpga = corr.katcp_wrapper.FpgaClient(roach, 7147)

time.sleep(1)

if fpga.is_connected():
	print 'ok\n'	
else:
	print 'ERROR\n'

print '------------------------'
print 'Programming FPGA with %s...' %bitstream,
fpga.progdev(bitstream)
print 'ok\n'

time.sleep(1)

print '------------------------'
print 'Starting tgtap server...',   
fpga.tap_start('gbe0', gbe0, mac_base0 + src_ip0, src_ip0, src_port0)
fpga.tap_start('gbe1', gbe1, mac_base1 + src_ip1, src_ip1, src_port1)
print 'done'

time.sleep(2)

print '------------------------'
print 'Setting-up the correlator...',
sys.stdout.flush()
fpga.write('xeng_ctrl', struct.pack('>I', 11*64*5))
fpga.write('visibs_sendto_ip', struct.pack('>I', dest_ip))
fpga.write('visibs_sendto_port', struct.pack('>I', dest_port))
fpga.write('visibs_tengbe_ctrl', struct.pack('>I', 1<<30))
fpga.write('visibs_tengbe_ctrl', struct.pack('>I', 0))
fpga.write('xengine_xeng_tvg_data0', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data1', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data2', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data3', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data4', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data5', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data6', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('xengine_xeng_tvg_data7', struct.pack('>%dH' % (8*256), *[6734, 6734, 6734, 6734, 6734, 6734, 6734, 6734]*256))
fpga.write('source_seed_0', struct.pack('>I', 0xffffffff))
#fpga.write('source_seed_1', struct.pack('>I', 0xf0000000))
fpga.write('source_seed_1', struct.pack('>I', 0xffffffff))
fpga.write('source_ctrl', struct.pack('>I', (1<<31) + (1<<30) + (3<<3) + 3))
fpga.write('source_ctrl', struct.pack('>I', (3<<3) + 3))
fpga.write('scope_ctrl', struct.pack('>I', (3<<16) + (12<<8) + 12))
fpga.write('fengine_ctrl', struct.pack('>I', 0x55505550))
gains = [2**12,]*2**14
fpga.write('cgain_gain_0', struct.pack('>%dH'%(2**14), *gains))
fpga.write('cgain_gain_1', struct.pack('>%dH'%(2**14), *gains))
fpga.write('visibs_tengbe_ctrl', struct.pack('>I', 1<<31))
print 'done'

if network == 'network':
	print 'Setting up network direct connect mode...',
	fpga.write('network_ctrl', struct.pack('>I', 0xc0000000)) # Assert resets
	fpga.write('network_ctrl', struct.pack('>I', 0x00000000)) # Clear resets
	#fpga.write('network_ctrl', struct.pack('>I', 0x00000030)) # Enable TX/RX
	fpga.write('network_ctrl', struct.pack('>I', 0x00000020)) # Enable TX only
else:
	print 'Setting up network bypass mode...',
	fpga.write('network_ctrl', struct.pack('>I', 1))

print 'done'
