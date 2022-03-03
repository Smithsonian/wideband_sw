import logging, os
from json import JSONEncoder, dumps

class JSONListFile(object):

    def __init__(self, filename, indent=4):
        self.logger = logging.getLogger(self.__class__.__name__)
        self.separator = (JSONEncoder.item_separator + os.linesep).encode()
        self.indent_line = lambda line: ' ' * indent + line
        list_open, list_close = dumps([])
        self.header = (list_open + os.linesep).encode()
        self.footer = (os.linesep + list_close).encode()
        self.indent = indent
        try:
            self.file = open(filename, 'r+b')
        except IOError:
            self.logger.debug("{0} does not existing; creating it".format(filename))
            self.file =  open(filename, 'w')
            self.write_header()
            self.write_footer()

    def write_header(self):
        self.file.write(self.header)

    def write_footer(self):
        self.file.write(self.footer)

    def __enter__(self):
        self.file.seek(-len(self.footer), os.SEEK_END)
        return self

    def append(self, obj):
        serial_obj = dumps(obj, indent=self.indent, sort_keys=True)
        indent_obj = ''.join(map(self.indent_line, serial_obj.splitlines(True)))
        if self.file.tell() != len(self.header):
            self.file.write(self.separator)
        self.file.write(indent_obj.encode())

    def __exit__(self, type_, value, traceback):
        self.write_footer()
        self.file.close()
