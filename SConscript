# RT-Thread SCons integration for the Lely CANopen target.

import os
from building import *

cwd = GetCurrentDir()
allowlist_path = os.path.join(cwd, 'metadata', 'RTTHREAD_SOURCE_ALLOWLIST.txt')


def _load_source_allowlist(path):
    sources = []
    seen = set()

    if not os.path.isfile(path):
        raise RuntimeError('Missing Lely RT-Thread source allowlist: %s' % path)

    with open(path, 'r') as stream:
        for lineno, raw_line in enumerate(stream, 1):
            entry = raw_line.strip()
            if not entry or entry.startswith('#'):
                continue
            if os.path.isabs(entry) or '..' in entry.split(os.sep):
                raise RuntimeError('Invalid source allowlist path at line %d: %s'
                                   % (lineno, entry))
            if not entry.endswith('.c'):
                raise RuntimeError('Only C sources are allowed at line %d: %s'
                                   % (lineno, entry))
            if entry in seen:
                raise RuntimeError('Duplicate source allowlist entry at line %d: %s'
                                   % (lineno, entry))
            abs_path = os.path.join(cwd, entry)
            if not os.path.isfile(abs_path):
                raise RuntimeError('Allowlisted Lely source is missing: %s' % entry)
            seen.add(entry)
            sources.append(entry)

    return sources


def _remove_source(sources, path):
    if path in sources:
        sources.remove(path)


def _remove_when_disabled(sources, symbol, paths):
    if GetDepend(symbol):
        return
    for path in paths:
        _remove_source(sources, path)


def _validate_target_boundary(sources):
    forbidden_exact = {
        'upstream/src/libc/stdatomic.c',
        'upstream/src/libc/threads-pthread.c',
        'upstream/src/libc/threads-win32.c',
        'upstream/src/libc/time.c',
        'upstream/src/ev/fiber_exec.c',
        'upstream/src/ev/strand.c',
        'upstream/src/ev/thrd_loop.c',
        'upstream/src/io2/can_rt.c',
        'upstream/src/io2/sys/clock.c',
        'upstream/src/io2/vcan.c',
    }
    forbidden_prefixes = (
        'upstream/src/io2/linux/',
        'upstream/src/io2/posix/',
        'upstream/src/io2/win32/',
    )

    for path in sources:
        if path in forbidden_exact or path.startswith(forbidden_prefixes):
            raise RuntimeError('Excluded Lely runtime source entered target allowlist: %s' % path)


src = []
if GetDepend('PKG_USING_LELY'):
    src = _load_source_allowlist(allowlist_path)
    src += [
        'port/rtthread/src/runtime.c',
        'port/rtthread/src/time.c',
        'port/rtthread/src/timer.c',
        'port/rtthread/src/can.c',
        'port/rtthread/src/log.c',
    ]
    # Keep the logging translation unit in the package build unconditionally.
    # log.c/log.h use rtconfig.h as the C-preprocessor source of truth, which
    # prevents SCons dependency resolution and C compilation from disagreeing
    # about whether the lely_rtt_log_* definitions must exist.
    if GetDepend('PKG_LELY_APP_AUTO_INIT'):
        src.append('port/rtthread/src/auto_init.c')
    if GetDepend('PKG_LELY_USING_MASTER_COMMAND'):
        src.append('port/rtthread/src/master_command.c')
    if GetDepend('PKG_LELY_USING_MASTER_SDO'):
        src.append('port/rtthread/src/master_sdo.c')
    if GetDepend('PKG_LELY_USING_MASTER_NMT_CFG'):
        src.append('port/rtthread/src/master_cfg.c')
    if GetDepend('PKG_LELY_USING_LOCAL_OD'):
        src.append('port/rtthread/src/master_od.c')
    if GetDepend('PKG_LELY_USING_MASTER_PDO_TX'):
        src.append('port/rtthread/src/master_pdo.c')
    if GetDepend('PKG_LELY_USING_MASTER_EMCY'):
        src.append('port/rtthread/src/master_emcy.c')
    if GetDepend('PKG_LELY_USING_MASTER_TIME'):
        src.append('port/rtthread/src/master_time.c')
    if GetDepend('PKG_LELY_USING_MSH'):
        src.append('port/rtthread/src/msh.c')
    if GetDepend('PKG_LELY_EXAMPLE_MASTER_NODE1'):
        src.append('examples/master_node1/master_sdev.c')
        if GetDepend('PKG_LELY_USING_MASTER_NMT_CFG'):
            src.append('examples/master_node1/master_cfg_dcf.c')
    _validate_target_boundary(src)

    # The normal RT-Thread libc builds cstring.c, which already owns the
    # POSIX string helpers supplied by these Lely compatibility shims. Keep
    # the shims only for RT_USING_NANO, where RT-Thread skips components/libc.
    if not GetDepend('RT_USING_NANO'):
        _remove_source(src, 'upstream/src/libc/string.c')
        _remove_source(src, 'upstream/src/libc/strings.c')

    _remove_when_disabled(src, 'PKG_LELY_USING_CO_CSDO',
                          ['upstream/src/co/csdo.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_EMCY',
                          ['upstream/src/co/emcy.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_LSS',
                          ['upstream/src/co/lss.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_NMT_BOOT',
                          ['upstream/src/co/nmt_boot.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_NMT_CFG',
                          ['upstream/src/co/nmt_cfg.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_RPDO',
                          ['upstream/src/co/rpdo.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_SYNC',
                          ['upstream/src/co/sync.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_TIME',
                          ['upstream/src/co/time.c'])
    _remove_when_disabled(src, 'PKG_LELY_USING_CO_TPDO',
                          ['upstream/src/co/tpdo.c'])

CPPPATH = [
    os.path.join(cwd, 'port', 'rtthread', 'include'),
    os.path.join(cwd, 'port', 'rtthread'),
    os.path.join(cwd, 'upstream', 'include'),
]
if GetDepend('PKG_LELY_EXAMPLE_MASTER_NODE1'):
    CPPPATH.append(os.path.join(cwd, 'examples', 'master_node1'))
# CPPPATH is intentionally non-local: RT-Thread's DefineGroup() propagates it to
# later application sources, so generated device_sdev.c and application code see
# the same lely/features.h wrapper and therefore the same ABI-affecting macros.
# LELY_NO_THREADS=1 means no package-private <threads.h> or compiler TLS shim is
# required by the selected source graph.
group = DefineGroup('LelyCANopen', src, depend=['PKG_USING_LELY'],
                    CPPPATH=CPPPATH)

Return('group')
