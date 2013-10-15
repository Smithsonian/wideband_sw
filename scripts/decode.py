
ideal_switched_mapping = (
    ('ant0:pol0', 'ant0:pol1'), # roach2-00: {input 0: [ant, chunk/pol], input 1: [...]}
    ('ant1:pol0', 'ant1:pol1'), # roach2-01: {input 0: [...], input 1: [...]}
    ('ant2:pol0', 'ant2:pol1'), # roach2-02: {...}
    ('ant3:pol0', 'ant3:pol1'), # ...
    ('ant4:pol0', 'ant4:pol1'), # ...
    ('ant5:pol0', 'ant5:pol1'), # ...
    ('ant6:pol0', 'ant6:pol1'), # ...
    ('ant7:pol0', 'ant7:pol1'), # roach2-07: {...}
)

direct_connect_mapping = (
    ('ant1:chan00', 'ant2:chan00'), # roach2-00: {input 0: [ant, chanl], input 1: [...]}
    ('ant3:chan00', 'ant4:chan00'), # roach2-01: {input 0: [...], input 1: [...]}
    ('ant1:chan08', 'ant2:chan08'), # roach2-00: {...}
    ('ant3:chan08', 'ant4:chan08'), # ...
    ('ant1:chan16', 'ant2:chan16'), # ...
    ('ant3:chan16', 'ant4:chan16'), # ...
    ('ant1:chan24', 'ant2:chan24'), # ...
    ('ant3:chan24', 'ant4:chan24'), # roach2-01: {...}
)

def get_xengine_output_order(mapping=ideal_switched_mapping):

    inputs = len(mapping)
    top_stage = inputs / 2
    clocks = inputs + inputs/2 - 1

    for clock in range(clocks):

        input = clock % inputs

        if clock < inputs:
            valid_stages = range(min(clock, top_stage), -1, -1)
        else:
            valid_stages = range(top_stage-1, input, -1)

        for stage in valid_stages:

            left = mapping[input]
            right = mapping[input-stage]

            if left == right: # auto-correlation

                yield left[0], right[0] # pol0xpol0 real

                yield left[1], right[1] # pol1xpol1 real

                yield left[0], right[1] # pol0xpol1 real
                yield left[0], right[1] # pol1xpol0 imag

            else: # cross-correlation

                yield left[0], right[0] # pol0xpol0 real
                yield left[0], right[0] # pol0xpol0 imag

                yield left[1], right[1] # pol1xpol1 real
                yield left[1], right[1] # pol1xpol1 imag

                yield left[1], right[0] # pol1xpol0 real
                yield left[1], right[0] # pol1xpol0 imag

                yield left[0], right[1] # pol0xpol1 real
                yield left[0], right[1] # pol0xpol1 imag

