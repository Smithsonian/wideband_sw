from setuptools import setup, Extension
setup(
    name='pysendint', 
    version='1.0', 
    ext_modules = [Extension('pysendint', 
                             ['pysendint.c', 'sendIntegration.c',
                              'chunkPlot_clnt.c', 'chunkPlot_xdr.c',
                              'dataCatcher_clnt.c', 'dataCatcher_xdr.c',
                              '/global/functions/getAntennaList.c',
                              '/global/functions/defaultingEnabled.c',
                              ])])
