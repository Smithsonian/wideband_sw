import logging
from IPython.core.magic import (
    Magics, 
    magics_class, 
    line_magic,
    cell_magic, 
    line_cell_magic
    )

@magics_class
class SwarmShellMagics(Magics):

    def __init__(self, shell, swarm):
        self.logger = logging.getLogger(self.__class__.__name__)
        super(SwarmShellMagics, self).__init__(shell)
        self.swarm = swarm

    @line_magic
    def config(self, line):

        if line.lower() == 'dual-rx':

            # Set quadrant one to be dual-Rx, 8-10 GHz (no flip)
            self.swarm[0].members_do(lambda fid, mem: setattr(mem, 'dc_if_freqs', [7.85, 7.85]))

            # Enable fringe stopping first
            self.swarm[0].fringe_stopping(True)

            # Reset all Walshing and sideband separation tables
            self.swarm[0].set_walsh_patterns()
            self.swarm[0].set_sideband_states()

            # Alert user of any other tasks
            self.logger.info("Done. If you have not done so already (after a setIFLevels), "
                             "please remember to run *all* BDC scripts with the 'b' option.")

        elif line.lower() == 'single-rx':

            # Set quadrant one to be single-Rx, 8-12 GHz (no flip in chunk 0, flip in chunk 1)
            self.swarm[0].members_do(lambda fid, mem: setattr(mem, 'dc_if_freqs', [7.85, -12.15]))

            # Enable fringe stopping first
            self.swarm[0].fringe_stopping(True)

            # Reset all Walshing and sideband separation tables
            self.swarm[0].set_walsh_patterns()
            self.swarm[0].set_sideband_states()

            # Alert user of any other tasks
            self.logger.info("Done. If you have not done so already (after a setIFLevels), "
                             "please remember to run *both* BDC scripts with the 'l' or 'h' option.")

        else:
            self.logger.error("Config \"{0}\" not supported!".format(line))

    @line_magic
    def calibrate(self, line):

        if line.lower() == 'adc-warm':

            # Disable the ADC monitor
            self.swarm.send_katcp_cmd('stop-adc-monitor')

            # Do the warm ADC cal
            self.swarm.warm_calibrate_adc()

            # Enable the ADC monitor
            self.swarm.send_katcp_cmd('start-adc-monitor')

        else:
            self.logger.error("Calibration \"{0}\" not supported!".format(line))

    @line_magic
    def setprompt(self, line):
        self.shell.in_template = line

    #@cell_magic
    def cmagic(self, line, cell):
        "my cell magic"
        return line, cell

    #@line_cell_magic
    def lcmagic(self, line, cell=None):
        "Magic that works both as %lcmagic and as %%lcmagic"
        if cell is None:
            print "Called as line magic"
            return line
        else:
            print "Called as cell magic"
            return line, cell
