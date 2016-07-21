from swarm import SwarmDataCallback

class SaveRawData(SwarmDataCallback):

    def save_bin(self, filename, datas):
        """ Simple binary data saver """
        with open(filename, 'wb') as file_:
            file_.write(datas)

    def __call__(self, rawdata):
        """ Callback for saving raw data """
        for quad in self.swarm.quads:
            for fid, datas in enumerate(rawdata[quad.qid]):
                filename = 'qid%d.fid%d.dat' % (quad.qid, fid)
                self.save_bin(filename, datas)
                self.logger.info('Data for QID #%d, FID #%d saved to %r' % (quad.qid, fid, filename))
