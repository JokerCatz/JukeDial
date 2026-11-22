#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""lint-app guard: the chiptune test files (copyrighted rips among them) must never be tracked by git.
Fails if (1) any tracked path has a `chiptest` component under .local, or (2) a tracked file's sha256 is in
.local/chiptest/MANIFEST.tsv of the main checkout (the copies can come back under another name).
(2) is skipped with a note when the manifest does not exist (a checkout that never ran `make chiptest-files`).
--selftest: runs the check against a throw-away repo."""
import hashlib, os, subprocess, sys, tempfile

def git(args, cwd):
    return subprocess.check_output(['git'] + args, cwd=cwd, text=True)

def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()

def check(repo, manifest):
    bad = []
    tracked = [t for t in git(['ls-files'], repo).split('\n') if t]
    for t in tracked:
        if 'chiptest' in t.split('/') and t.startswith('.local/'):
            bad.append('tracked chiptest path: ' + t)
    if manifest and os.path.isfile(manifest):
        want = {}
        for ln in open(manifest, encoding='utf-8').read().split('\n')[1:]:
            c = ln.split('\t')
            if len(c) >= 5:
                want.setdefault(int(c[1]), {})[c[0]] = c[4]
        for t in tracked:
            p = os.path.join(repo, t)
            try:
                sz = os.path.getsize(p)
            except OSError:
                continue
            if sz in want and os.path.isfile(p) and not os.path.islink(p):
                h = sha256(p)
                # licence texts shipped inside a test corpus (COPYING, LICENSE, ...) are legitimately also in docs/licenses/: not audio
                if h in want[sz] and not (t.startswith('docs/licenses/') or os.path.basename(want[sz][h]).upper().split('.')[0] in ('COPYING', 'LICENSE', 'LICENCE', 'README')):
                    bad.append('tracked file equals chiptest copy %s: %s' % (want[sz][h], t))
    return bad

def selftest():
    with tempfile.TemporaryDirectory() as d:
        git(['init', '-q'], d)
        os.makedirs(os.path.join(d, '.local/chiptest/sid'))
        open(os.path.join(d, 'ok.txt'), 'w').write('fine')
        open(os.path.join(d, 'copy.bin'), 'wb').write(b'secret rip')
        man = os.path.join(d, 'MANIFEST.tsv')
        open(man, 'w').write('sha256\tsize\tclass\tfmt\tpath\tsource\n%s\t10\trip\tsid\tsid/x.sid\tsrc\n' % hashlib.sha256(b'secret rip').hexdigest())
        git(['add', 'ok.txt'], d)
        assert check(d, man) == [], 'clean repo flagged'
        git(['add', 'copy.bin'], d)
        assert len(check(d, man)) == 1, 'renamed copy not caught'
        git(['rm', '-q', '--cached', 'copy.bin'], d)
        open(os.path.join(d, '.local/chiptest/sid/x.sid'), 'w').write('x')
        git(['add', '-f', '.local/chiptest/sid/x.sid'], d)
        assert len(check(d, man)) == 1, 'tracked chiptest path not caught'
    print('check_chiptest_untracked selftest ok')

if __name__ == '__main__':
    if '--selftest' in sys.argv:
        selftest(); sys.exit(0)
    here = os.path.dirname(os.path.abspath(__file__))
    repo = git(['rev-parse', '--show-toplevel'], here).strip()
    common = git(['rev-parse', '--path-format=absolute', '--git-common-dir'], here).strip()
    man = os.path.join(os.path.dirname(common), '.local', 'chiptest', 'MANIFEST.tsv')
    bad = check(repo, man)
    if not os.path.isfile(man):
        print('check_chiptest_untracked: no manifest (%s): only the path check ran' % man)
    for b in bad:
        print('FAIL', b)
    if bad:
        sys.exit(1)
    print('check_chiptest_untracked ok')
