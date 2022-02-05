import time
import random
import pysendint

amp = 1e9
antennas = list(range(2, 9))
#lsbCross = [0.0,] * 2**15
#usbCross = [0.0,] * 2**15

now = time.time(); print(now)
for chunk in [0, 1]:   
    for i in antennas:
        for j in antennas:
            lsbCross = list(amp*random.random() for p in range(2**15))
            usbCross = list(amp*random.random() for p in range(2**15))
            print(i, j, pysendint.send_integration(time.time(), 32.0, chunk, i, 1, j, 1, lsbCross, usbCross, 0))
