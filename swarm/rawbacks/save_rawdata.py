from swarm import SwarmDataCallback

class SaveRawData(SwarmDataCallback):

    def save_bin(self, filename, datas):
        """ Simple binary data saver """
        with open(filename, 'wb') as file_:
            file_.write(datas)

    def __call__(self, rawdata):
        """ Callback for saving raw data """
        for fid, datas in enumerate(rawdata):
            filename = 'fid%d.dat' % fid
            self.save_bin(filename, datas)
            self.logger.info('Data for FID #%d saved to %r' % (fid, filename))
