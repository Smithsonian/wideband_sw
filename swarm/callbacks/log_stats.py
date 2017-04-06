import logging
from numpy import (
    array,
    angle,
    isnan,
    sqrt,
    pi,
    )
from swarm import (
    SwarmDataCallback,
    SwarmBaseline,
    SwarmInput,
    )

class LogStats(SwarmDataCallback):

    def __init__(self, swarm, reference=None):
        super(LogStats, self).__init__(swarm)
        self.reference = reference if reference else SwarmInput(1, 0, 0)

    def __call__(self, data):
        """ Callback for showing statistics """
        sideband = 'USB'
        auto_amps = {}
        for baseline in data.baselines:
            if baseline.is_valid():
                chunk = baseline.left.chk
                interleaved = array(list(p for p in data[baseline, sideband] if not isnan(p)))
                complex_data = interleaved[0::2] + 1j * interleaved[1::2]
                if baseline.is_auto():
                    auto_amps[baseline] = abs(complex_data).mean()
                    norm = auto_amps[baseline]
                else:
                    norm_left = auto_amps[SwarmBaseline(baseline.left, baseline.left)]
                    norm_right = auto_amps[SwarmBaseline(baseline.right, baseline.right)]
                    norm = sqrt(norm_left * norm_right)
                norm = max(1.0, norm) # make sure it's not zero
                if ((self.reference == baseline.left) or (self.reference == baseline.right)):
                    self.logger.info(
                        '{baseline!s}[chunk={chunk}].{sideband} : Amp(avg)={amp:>12.2e}, Phase(avg)={pha:>8.2f} deg, Corr.={corr:>8.2f}%'.format(
                            baseline=baseline, chunk=chunk, sideband=sideband,
                            corr=100.0*abs(complex_data).mean()/norm,
                            amp=abs(complex_data).mean(),
                            pha=(180.0/pi)*angle(complex_data.mean()),
                            )
                        )
