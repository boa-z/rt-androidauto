# SPDX-License-Identifier: GPL-3.0-or-later
# The package scanner is the sole owner. Applications add include paths and
# adapters but must never invoke this SConscript a second time.
import os
from building import *

cwd = GetCurrentDir()
objs = []
if GetDepend('LPKG_USING_RT_ANDROIDAUTO'):
    include = GetConfigValue('RTAA_MBEDTLS_INCLUDE').strip('"')
    if not os.path.isabs(include):
        include = os.path.join(os.environ.get('AIC_ROOT', ''), include)
    if not os.path.isfile(os.path.join(include, 'mbedtls', 'ssl.h')):
        raise RuntimeError('rt-androidauto requires mbedTLS 2.28 headers; set RTAA_MBEDTLS_INCLUDE and link the matching libraries')
    src = Glob('src/*.c') + Glob('generated/*.c') + Glob('third_party/nanopb/*.c')
    if GetDepend('RTAA_PORT_RTTHREAD'):
        src += Glob('port/rtthread/*.c')
    objs = DefineGroup('rt-androidauto', src, depend=['LPKG_USING_RT_ANDROIDAUTO'],
                       CPPPATH=[cwd + '/include', cwd + '/generated',
                                cwd + '/third_party/nanopb', include],
                       LOCAL_CCFLAGS=' -std=c11')
Return('objs')
