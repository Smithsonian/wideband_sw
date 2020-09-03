import logging
from queue import Queue
from threading import Thread
from IPython.core.magic import (
    Magics, 
    magics_class, 
    line_magic,
    cell_magic, 
    line_cell_magic
    )

from swarm.defines import SWARM_N_FIDS

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
    def checkroach2(self, line):

        self.logger.info("Check ROACH2s")
        # Gather all SWARM members
        members = [quads[fid] for quads in self.swarm for fid in range(SWARM_N_FIDS)]

        # Set up dictionary of status info
        swarm_roach2_status = {}
        for m in members:
            swarm_roach2_status[m] = {"programmed":None,
              "firmware":None,
              "clock":None}

        # Set up a queue to collect return values in threads
        queue = Queue()

        # Check FPGA programmed status
        threads = [Thread(target=lambda q,m: q.put((m,m.send_katcp_cmd("fpgastatus"))),
          args=(queue,memb)) for memb in members]
        [t.start() for t in threads]
        [t.join() for t in threads]
        while not queue.empty():
            memb, reply = queue.get()
            self.logger.debug("{0} programmed status is {1}".format(memb.roach2_host,
              "OK" if reply[0].reply_ok() else "not OK"))
            swarm_roach2_status[memb]["programmed"] = "OK" if reply[0].reply_ok() else "not OK"

        # Check firmware version information
        threads = [Thread(target=lambda q,m: q.put((m,m.roach2.get_rcs())),
          args=(queue,memb)) for memb in members]
        [t.start() for t in threads]
        [t.join() for t in threads]
        while not queue.empty():
            memb, rcs = queue.get()
            self.logger.debug("{0} is running bitcode version {1:x} ({2})".format(memb.roach2_host,
              rcs["app_rev"],"dirty" if rcs["app_dirty"] else "clean"))
            swarm_roach2_status[memb]["firmware"] = "commit hash {0:x} ({1})".format(rcs["app_rev"],
              "dirty" if rcs["app_dirty"] else "clean")

        # Get clock information
        threads = [Thread(target=lambda q,m: q.put((m,m.roach2.est_brd_clk())),
          args=(queue,memb)) for memb in members]
        [t.start() for t in threads]
        [t.join() for t in threads]
        while not queue.empty():
            memb, clk = queue.get()
            self.logger.debug("{0} clock is ~{1:.1f} MHz".format(memb.roach2_host,clk))
            swarm_roach2_status[memb]["clock"] = "OK" if clk > 200 and clk < 400 else "possibly not OK ({0.1f}MHz)".format(clk)

        # Log results from all checks
        for memb in members:
            this_roach2_status = swarm_roach2_status[memb]
            self.logger.info("{0} FPGA programmed status is {1}, running firmware version {2}, and clock is {3}".format(memb.roach2_host,
              this_roach2_status["programmed"],this_roach2_status["firmware"],this_roach2_status["clock"]))

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
            print("Called as line magic")
            return line
        else:
            print("Called as cell magic")
            return line, cell
