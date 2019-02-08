from defines import *
from numba import jit

simple_mapping = (
    ('in0',  'in1'),
    ('in2',  'in3'),
    ('in4',  'in5'),
    ('in6',  'in7'),
    ('in8',  'in9'),
    ('in10', 'in11'),
    ('in12', 'in13'),
    ('in14', 'in15'),
)


class SwarmBaseline:

    def __init__(self, left, right):
        self.right = right
        self.left = left

    def __repr__(self):
        repr_str = 'SwarmBaseline({left!r}, {right!r})'
        return repr_str.format(left=self.left, right=self.right)

    def __str__(self):
        repr_str = '{left!s} X {right!s}'
        return repr_str.format(left=self.left, right=self.right)

    def __hash__(self):
        return hash((self.left, self.right))

    def __eq__(self, other):
        if other is not None:
            return (self.left, self.right) == (other.left, other.right)
        else:
            return not self.is_valid()

    def __ne__(self, other):
        return not self.__eq__(other)

    def is_auto(self):
        return self.left == self.right

    def is_valid(self):
        valid_inputs = (self.left is not None) and (self.right is not None)
        return compiled_is_valid(self.left.chk,
                                 self.right.chk,
                                 self.left.pol,
                                 self.right.pol,
                                 self.left.ant,
                                 self.right.ant,
                                 valid_inputs)


@jit
def compiled_is_valid(left_chk, right_chk, left_pol, right_pol, left_ant, right_ant, valid_inputs):
    cross_chunk = left_chk != right_chk
    cross_pol = left_pol != right_pol
    cross_ant = left_ant != right_ant
    if cross_ant:
        return valid_inputs and not cross_chunk
    else:
        return valid_inputs and not cross_chunk and not cross_pol


class SwarmXengineWord(SwarmBaseline):

    def __init__(self, left, right, imag=False, sideband='LSB'):
        SwarmBaseline.__init__(self, left, right)
        self.baseline = SwarmBaseline(self.left, self.right)
        if sideband not in SWARM_XENG_SIDEBANDS:
            raise ValueError, "Sideband must be one of {}!".format(SWARM_XENG_SIDEBANDS)
        else:
            self.sideband = sideband
        self.imag = imag

    def __repr__(self):
        repr_str = 'SwarmXengineWord({left!r}, {right!r}, imag={imag!r}, sideband={sideband!r})'
        return repr_str.format(left=self.left, right=self.right, imag=self.imag, sideband=self.sideband)

    def __str__(self):
        if not self.imag:
            repr_str = 'real({left!s} X {right!s})'
        else:
            repr_str = 'imag({left!s} X {right!s})'
        if self.sideband:
            repr_str += '.' + self.sideband
        return repr_str.format(left=self.left, right=self.right)


class SwarmXengine:

    def __init__(self, mapping=simple_mapping):
        self.mapping = mapping

    def packet_order(self):
        return self._ddr3_out()

    def xengine_order(self):
        return self._xeng_out()

    def _ddr3_out(self):
        xeng_gen = self._xeng_out()
        while True:
            try:
                # DDR3 interleaves two 64-bit words,
                # i.e. 4 32-bit words
                word_0 = xeng_gen.next()
                word_1 = xeng_gen.next()
            except StopIteration:
                break

            # LSB comes out first
            yield SwarmXengineWord(word_0.left, word_0.right, imag=word_0.imag, sideband='LSB')
            yield SwarmXengineWord(word_1.left, word_1.right, imag=word_1.imag, sideband='LSB')

            # and then USB (I think)
            yield SwarmXengineWord(word_0.left, word_0.right, imag=word_0.imag, sideband='USB')
            yield SwarmXengineWord(word_1.left, word_1.right, imag=word_1.imag, sideband='USB')


    def _xeng_out(self):

        inputs = len(self.mapping)
        top_stage = inputs / 2
        clocks = inputs + inputs/2 - 1

        for clock in range(clocks):

            input = clock % inputs

            if clock < inputs:
                valid_stages = range(min(clock, top_stage), -1, -1)
                conjugate = True
            else:
                valid_stages = range(top_stage-1, input, -1)
                conjugate = False

            for stage in valid_stages:

                if not conjugate:
                    left = self.mapping[input]
                    right = self.mapping[input-stage]
                else:
                    right = self.mapping[input]
                    left = self.mapping[input-stage]

                if left is right: # auto-correlation

                    yield SwarmXengineWord(left[0], right[0], imag=False) # pol0xpol0 real
                    yield SwarmXengineWord(left[1], right[1], imag=False) # pol1xpol1 real

                    yield SwarmXengineWord(left[0], right[1], imag=False) # pol0xpol1 real
                    yield SwarmXengineWord(left[0], right[1],  imag=True) # pol1xpol0 imag

                else: # cross-correlation

                    yield SwarmXengineWord(left[0], right[0], imag=False) # pol0xpol0 real
                    yield SwarmXengineWord(left[0], right[0],  imag=True) # pol0xpol0 imag

                    yield SwarmXengineWord(left[1], right[1], imag=False) # pol1xpol1 real
                    yield SwarmXengineWord(left[1], right[1],  imag=True) # pol1xpol1 imag

                    yield SwarmXengineWord(left[0], right[1], imag=False) # pol0xpol1 real
                    yield SwarmXengineWord(left[0], right[1],  imag=True) # pol0xpol1 imag

                    yield SwarmXengineWord(left[1], right[0], imag=False) # pol1xpol0 real
                    yield SwarmXengineWord(left[1], right[0],  imag=True) # pol1xpol0 imag

