import logging

# Our callback's logger
logger = logging.getLogger('Rawback:save_rawdata')

# Simple binary data saver
def save_bin(filename, datas):
    with open(filename, 'wb') as file_:
        file_.write(datas)

# Callback for saving raw data
def save_rawdata(rawdata):
    for fid, datas in enumerate(rawdata):
        filename = 'fid%d.dat' % fid
        save_bin(filename, datas)
        logger.info('Data for FID #%d saved to %r' % (fid, filename))
