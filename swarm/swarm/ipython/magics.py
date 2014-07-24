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
        super(SwarmShellMagics, self).__init__(shell)
        self.swarm = swarm

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
