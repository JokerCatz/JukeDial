#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Every generated player sdkconfig must still carry what app/sdkconfig.defaults* pin (FIX3 I5).

IDF merges the defaults only into a NEW sdkconfig: an existing one keeps its old values (a Kconfig choice such as MBEDTLS_EXTERNAL_MEM_ALLOC
silently stays INTERNAL = ~25 KB of internal RAM per TLS connection). Usage: check_sdkconfig.py [build ...]  (app/sdkconfig is "build"; default: all
that exist). Exit 1 and "delete <file> to regenerate" when a pinned key differs. A pinned key the generated file does not mention at all is skipped
(its dependency is off in that build)."""
import os, re, sys

APP = os.environ.get('SDKCONFIG_APP_DIR') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'app')
BASE = ['sdkconfig.defaults', 'sdkconfig.defaults.bt']
BUILDS = {                                   # name -> (sdkconfig path under app/, the defaults it is generated from)
    # 2026-11-22: the vendor-codec (LDAC) image is the default: a pre-switch stock app/sdkconfig has CONFIG_BT_A2DP_SEP_NUM_MAX=2 and fails here
    'build': ('sdkconfig', BASE + ['sdkconfig.ldac']),
    'build-stock': ('build-stock/sdkconfig', BASE),
    'build-fakepod': ('build-fakepod/sdkconfig', BASE + ['sdkconfig.fakepod', 'sdkconfig.ldac']),
    'build-fakepod-stock': ('build-fakepod-stock/sdkconfig', BASE + ['sdkconfig.fakepod']),
    'build-peer': ('build-peer/sdkconfig', BASE + ['sdkconfig.peer_uart']),
    'build-trace': ('build-trace/sdkconfig', BASE + ['sdkconfig.trace']),
    'build-lint': ('build-lint/sdkconfig', BASE + ['sdkconfig.ldac']),
}
SET = re.compile(r'^(CONFIG_[A-Za-z0-9_]+)=(.*)$')
UNSET = re.compile(r'^# (CONFIG_[A-Za-z0-9_]+) is not set$')


def parse(path):
    d = {}
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        m = SET.match(line)
        if m:
            v = m.group(2).strip()
            if not v.startswith('"'): v = re.sub(r'\s+#.*$', '', v)      # a trailing comment of a defaults file
            d[m.group(1)] = None if v == 'n' else v                    # =n is "not set"
            continue
        m = UNSET.match(line)
        if m: d[m.group(1)] = None
    return d


def main(argv):
    names = argv or list(BUILDS)
    bad = 0
    for n in names:
        rel, files = BUILDS[n]
        path = os.path.normpath(os.path.join(APP, rel))
        if not os.path.exists(path):
            continue
        pinned = {}
        for f in files:
            p = os.path.join(APP, f)
            if os.path.exists(p): pinned.update(parse(p))
        have = parse(path)
        for k, v in pinned.items():
            if k in have and have[k] != v:
                print("sdkconfig: %s has %s=%s, sdkconfig.defaults* pin %s: delete %s to regenerate" % (
                    os.path.relpath(path, os.path.join(APP, '..')), k, have[k] if have[k] is not None else 'n', v if v is not None else 'n',
                    os.path.relpath(path, os.path.join(APP, '..'))))
                bad += 1
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
