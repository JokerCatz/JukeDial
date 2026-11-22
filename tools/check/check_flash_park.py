#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Cache-suspend / parked-core guard: check_flash_park.py round|fakepod [elf]   (make size / make size-fakepod run it; --selftest tests it).
Rule (app/main/core_park.h): a parked core has its branch predictor off; every park goes through core_park.
Reads the built ELF (objdump / nm of the IDF toolchain) and the build's sdkconfig, and fails when
 1. the flash-operation hook is missing (app/main/core_park.c, psram-hunt 2026-11-19): IDF's flash-operation entry
    spi_flash_disable_interrupts_caches_and_other_cpu must call __wrap_esp_ipc_call_nonblocking (never the real one directly),
    the wrapper must substitute core_park_flash_block for spi_flash_op_block_func, and core_park_flash_block must clear MHCR (CSR
    0x7c1: the core's branch predictor; directly or through core_park_predictor_off) before it enters spi_flash_op_block_func.
    Without it the parked core may fetch flash code speculatively while the cache is suspended (boot crash "Core 1 panic'ed
    (Cache error)" of commit 3d1227c5);
 2. code in IRAM (.iram0.text: code that may run while the cache is suspended) references a symbol in .ext_ram.bss (PSRAM: a miss
    there faults while the cache is suspended), unless the pair "function symbol" is listed in tools/check/iram_extram.allow with
    a reason. Limits: direct references only (objdump resolves lui+addi/load pairs to a symbol); accesses through pointers,
    flash functions wrongly called from IRAM and handlers whose code is in flash are NOT seen;
 3. trip-wire for the OTHER park path, esp_ipc_isr_stall_other_cpu (the stalled core spins in an IPC ISR with its predictor ON,
    the same hazard; IDF uses it for light / deep sleep, MSPI retune at CPU-frequency changes, gdbstub): a function present in
    the ELF (kept by --gc-sections) that references esp_ipc_isr_stall_other_cpu, other than core_park_begin and the pairs listed
    in tools/check/ipc_stall.allow with a reason, fails; so does CONFIG_PM_ENABLE together with light sleep
    (CONFIG_FREERTOS_USE_TICKLESS_IDLE or CONFIG_PM_LIGHT_SLEEP_*), or a call of esp_light_sleep_start / esp_deep_sleep_start /
    esp_deep_sleep from our code (app/main, app/components, components), while the stall entry is not hooked (no
    __wrap_esp_ipc_isr_stall_other_cpu in the ELF). Fix order: hook the stall entry the way core_park.c hooks the flash path,
    test it on the board, then enable sleep (the internal notes, "What remains unverified").
    "Present" is conservative: a function that survived --gc-sections counts as reachable even when nothing calls it at run time."""
import glob, os, re, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ELFS = {"round": "app/build/jc_player.elf", "fakepod": "app/build-fakepod/jc_player.elf",
        "round-stock": "app/build-stock/jc_player.elf", "fakepod-stock": "app/build-fakepod-stock/jc_player.elf"}
ALLOW = os.path.join(ROOT, "tools/check/iram_extram.allow")
STALL_ALLOW = os.path.join(ROOT, "tools/check/ipc_stall.allow")
STALL = "esp_ipc_isr_stall_other_cpu"
STALL_OWN = {"core_park_begin"}
REPORT = "the psram-hunt report"
SLEEP_CALLS = ("esp_light_sleep_start", "esp_deep_sleep_start", "esp_deep_sleep")
MHCR = "0x7c1"


def tool(name):
    hits = sorted(glob.glob(os.path.expanduser(f"~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-{name}")))
    return hits[-1] if hits else f"riscv32-esp-elf-{name}"


def functions(dis):
    """objdump -d text -> {function: [instruction lines]}"""
    out, cur = {}, None
    for ln in dis.splitlines():
        m = re.match(r"^[0-9a-f]{8} <([^>]+)>:\s*$", ln)
        if m: cur = m[1]; out.setdefault(cur, []); continue
        if cur and re.match(r"^\s*[0-9a-f]+:\t", ln): out[cur].append(ln)
    return out


def refs(lines):
    """-> [(address, symbol)] of the '# addr <sym>' comments and the call / jump targets"""
    r = []
    for ln in lines:
        for m in re.finditer(r"\b([0-9a-f]{8}) <([^>+]+)(?:\+0x[0-9a-f]+)?>", ln): r.append((int(m[1], 16), m[2]))
    return r


def check_hook(iram, flash_fn):
    """iram: {fn: lines} of .iram0.text; flash_fn(name) -> lines of a function anywhere. -> [problems]"""
    p = []
    ent = iram.get("spi_flash_disable_interrupts_caches_and_other_cpu")
    if ent is None: return ["spi_flash_disable_interrupts_caches_and_other_cpu not found in IRAM: IDF changed, re-check core_park.c"]
    names = {s for _, s in refs(ent)}
    if "__wrap_esp_ipc_call_nonblocking" not in names: p.append("the flash-operation entry does not call __wrap_esp_ipc_call_nonblocking (the -Wl,--wrap in app/main/CMakeLists.txt is gone?)")
    if "esp_ipc_call_nonblocking" in names: p.append("the flash-operation entry calls the real esp_ipc_call_nonblocking directly")
    w = flash_fn("__wrap_esp_ipc_call_nonblocking") or []
    wn = {s for _, s in refs(w)}
    if not {"spi_flash_op_block_func", "core_park_flash_block"} <= wn: p.append("__wrap_esp_ipc_call_nonblocking does not swap spi_flash_op_block_func for core_park_flash_block")
    s = flash_fn("core_park_flash_block") or []
    clears = lambda lines: [i for i, ln in enumerate(lines) if re.search(r"\bcsrr?c\w*\s.*" + MHCR + r"\b", ln)]
    prim = bool(clears(flash_fn("core_park_predictor_off") or []))
    csr = next((i for i, ln in enumerate(s) if re.search(r"\bcsrr?c\w*\s.*" + MHCR + r"\b", ln) or (prim and "<core_park_predictor_off>" in ln)), None)
    blk = next((i for i, ln in enumerate(s) if "<spi_flash_op_block_func>" in ln), None)
    if csr is None or blk is None or csr > blk: p.append("core_park_flash_block does not clear MHCR (0x7c1) before it enters spi_flash_op_block_func")
    return p


def stall_callers(funcs):
    """funcs: {fn: lines} of every function in the ELF -> sorted functions that reference esp_ipc_isr_stall_other_cpu"""
    return sorted(fn for fn, lines in funcs.items() if fn != STALL and any(s == STALL for _, s in refs(lines)))


def sdk_flags(path):
    f = {}
    if os.path.exists(path):
        for ln in open(path):
            m = re.match(r"(CONFIG_\w+)=(.*)", ln.strip())
            if m: f[m[1]] = m[2]
    return f


def our_sleep_calls(src_roots):
    """-> ['file:line call'] of sleep entries called from our sources (third-party trees and docs skipped)"""
    hits = []
    for root in src_roots:
        for d, dirs, files in os.walk(root):
            dirs[:] = [x for x in dirs if x not in ("third_party", "build", "managed_components")]
            for n in files:
                if not n.endswith((".c", ".cpp", ".h")): continue
                fp = os.path.join(d, n)
                for i, ln in enumerate(open(fp, errors="replace"), 1):
                    for c in SLEEP_CALLS:
                        if re.search(r"\b" + c + r"\s*\(", ln) and not ln.lstrip().startswith(("//", "*", "/*")):
                            hits.append(f"{os.path.relpath(fp, ROOT)}:{i} {c}")
    return hits


def check_stall(callers, allow, hooked, flags, sleeps):
    """-> [problems] of the stall trip-wire (3)"""
    p = []
    for fn in callers:
        if fn in STALL_OWN or fn in allow: continue
        p.append(f"{fn} calls {STALL} (it stalls the other core with its branch predictor ON): route it through core_park, or list it in tools/check/ipc_stall.allow with the reason it is unreachable / harmless (see {REPORT})")
    if not hooked:
        light = flags.get("CONFIG_FREERTOS_USE_TICKLESS_IDLE") == "y" or any(k.startswith("CONFIG_PM_LIGHT_SLEEP") and v == "y" for k, v in flags.items())
        if flags.get("CONFIG_PM_ENABLE") == "y" and light:
            p.append(f"CONFIG_PM_ENABLE with light sleep (tickless idle) while {STALL} is not hooked: hook the stall entry (core_park) and verify it on the board first ({REPORT})")
        for h in sleeps:
            p.append(f"{h}: sleep called from our code while {STALL} is not hooked: hook the stall entry (core_park) and verify it on the board first ({REPORT})")
    return p


def check_extram(iram, lo, hi, allow):
    """-> [(function, symbol)] of IRAM references into [lo, hi) not in allow"""
    bad = []
    for fn, lines in iram.items():
        for a, s in refs(lines):
            if lo <= a < hi and (fn, s) not in allow and (fn, s) not in bad: bad.append((fn, s))
    return bad


def read_allow1(path):
    """'function  # reason' lines -> {function}"""
    a = set()
    if not os.path.exists(path): return a
    for ln in open(path):
        ln = ln.strip()
        if not ln or ln.startswith("#"): continue
        m = re.match(r"(\S+)\s*#\s*\S", ln)
        if not m: sys.exit(f"check_flash_park: {path}: '{ln}' needs 'function  # reason'")
        a.add(m[1])
    return a


def read_allow(path):
    a = set()
    if not os.path.exists(path): return a
    for ln in open(path):
        ln = ln.strip()
        if not ln or ln.startswith("#"): continue
        m = re.match(r"(\S+)\s+(\S+)\s*#\s*\S", ln)
        if not m: sys.exit(f"check_flash_park: {path}: '{ln}' needs 'function symbol  # reason'")
        a.add((m[1], m[2]))
    return a


def sdkconfig_of(elf):
    d = os.path.dirname(os.path.abspath(elf))
    own = os.path.join(d, "sdkconfig")
    return own if os.path.exists(own) else os.path.join(os.path.dirname(d), "sdkconfig")   # app/build uses app/sdkconfig


def check(elf, allow_path=ALLOW):
    if not os.path.exists(elf): print(f"check_flash_park: FAIL no ELF {elf} (build first)"); return 1
    od = lambda *a: subprocess.run([tool("objdump"), *a, elf], capture_output=True, text=True, check=True).stdout
    nm = {}
    for ln in subprocess.run([tool("nm"), "-S", elf], capture_output=True, text=True, check=True).stdout.splitlines():
        f = ln.split()
        if len(f) == 4: nm[f[3]] = (int(f[0], 16), int(f[1], 16))
        elif len(f) == 3: nm.setdefault(f[2], (int(f[0], 16), 0))
    iram = functions(od("-d", "-j", ".iram0.text"))
    def fn(name):
        if name in iram: return iram[name]
        if name not in nm or not nm[name][1]: return None
        a, n = nm[name]
        return functions(od("-d", f"--start-address={a:#x}", f"--stop-address={a + n:#x}")).get(name)
    probs = check_hook(iram, fn)
    lo, hi = nm.get("_ext_ram_bss_start", (0, 0))[0], nm.get("_ext_ram_bss_end", (0, 0))[0]
    if not hi: probs.append("_ext_ram_bss_start/_end not in the ELF")
    bad = check_extram(iram, lo, hi, read_allow(allow_path))
    for b in bad: probs.append(f"IRAM function {b[0]} references PSRAM .ext_ram.bss symbol {b[1]}: give it DRAM_ATTR, or list it in tools/check/iram_extram.allow with the reason it is cache-safe")
    callers = stall_callers(functions(od("-d")))
    hooked = "__wrap_" + STALL in nm
    probs += check_stall(callers, read_allow1(STALL_ALLOW), hooked, sdk_flags(sdkconfig_of(elf)),
                         our_sleep_calls([os.path.join(ROOT, x) for x in ("app/main", "app/components", "components")]))
    if probs:
        print(f"check_flash_park: FAIL {os.path.relpath(elf, ROOT)}"); [print("  " + x) for x in probs]; return 1
    print(f"check_flash_park: {os.path.relpath(elf, ROOT)} ok (parked-core predictor hook present; {len(iram)} IRAM functions, no new direct .ext_ram.bss reference; {STALL} callers: {', '.join(callers) or 'none'}; no sleep while unhooked)")
    return 0


def selftest():
    good_ent = ("2f000478 <spi_flash_disable_interrupts_caches_and_other_cpu>:\n"
                "2f0004d4:\t394030ef          \tjal\t2f003868 <__wrap_esp_ipc_call_nonblocking>\n")
    wrap = ("40000100 <__wrap_esp_ipc_call_nonblocking>:\n"
            "40000100:\t2f0007b7          \taddi\ta5,a5,1086 # 2f00043e <spi_flash_op_block_func>\n"
            "40000104:\t2f0045b7          \taddi\ta1,a1,-1968 # 40000200 <core_park_flash_block>\n")
    shim = ("40000200 <core_park_flash_block>:\n40000200:\t7c17b7f3          \tcsrrc\ta5,0x7c1,a5\n"
            "40000204:\tbdbfc06f          \tj\t2f00043e <spi_flash_op_block_func>\n")
    shim_bad = ("40000200 <core_park_flash_block>:\n40000200:\tbdbfc06f          \tj\t2f00043e <spi_flash_op_block_func>\n"
                "40000204:\t7c17b7f3          \tcsrrc\ta5,0x7c1,a5\n")
    shim_call = ("40000200 <core_park_flash_block>:\n40000200:\t2f0045b7          \tjal\t40000300 <core_park_predictor_off>\n"
                 "40000204:\tbdbfc06f          \tj\t2f00043e <spi_flash_op_block_func>\n"
                 "40000300 <core_park_predictor_off>:\n40000300:\t7c17b7f3          \tcsrrc\ta5,0x7c1,a5\n")
    isr = ("2f000600 <my_isr>:\n2f000600:\tca048513          \taddi\ta0,s1,-864 # 5005bca0 <s_pool>\n"
           "2f000604:\taa168693          \taddi\ta3,a3,-1375 # 50d83aa1 <_ext_ram_bss_end+0xcc1e61>\n")
    def run(ent, w, s):
        iram = functions(ent + isr); lib = functions(w + s)
        return check_hook(iram, lambda n: iram.get(n) or lib.get(n))
    assert run(good_ent, wrap, shim) == [], run(good_ent, wrap, shim)
    assert len(run(good_ent.replace("__wrap_esp_ipc_call_nonblocking", "esp_ipc_call_nonblocking"), wrap, shim)) == 2
    assert run(good_ent, wrap.replace("core_park_flash_block", "other"), shim)
    assert run(good_ent, wrap, shim_call) == []
    assert run(good_ent, wrap, shim_call.replace("0x7c1", "0x7c0"))
    assert run(good_ent, wrap, shim_bad) and run(good_ent, wrap, shim.replace("0x7c1", "0x7c0"))
    assert check_hook({}, lambda n: None)
    iram = functions(isr)
    assert check_extram(iram, 0x50000000, 0x50100000, set()) == [("my_isr", "s_pool")]          # the constant past the end is ignored
    assert check_extram(iram, 0x50000000, 0x50100000, {("my_isr", "s_pool")}) == []
    import tempfile
    p = os.path.join(tempfile.mkdtemp(), "a")
    open(p, "w").write("# c\nmy_isr s_pool  # read with the cache on only\n"); assert read_allow(p) == {("my_isr", "s_pool")}
    open(p, "w").write("my_isr s_pool\n")
    try: read_allow(p); assert False
    except SystemExit: pass
    # 3. stall trip-wire
    fns = functions("2f001000 <esp_ipc_isr_stall_other_cpu>:\n2f001000:\t0001          \tnop\n"
                    "42000000 <core_park_begin>:\n42000000:\t0001          \tjal\t2f001000 <esp_ipc_isr_stall_other_cpu>\n"
                    "42000100 <mspi_timing_change_speed_mode_cache_safe>:\n42000100:\t0001          \tjal\t2f001000 <esp_ipc_isr_stall_other_cpu>\n"
                    "42000200 <esp_ipc_isr_release_other_cpu>:\n42000200:\t0001          \tnop\n")
    c = stall_callers(fns)
    assert c == ["core_park_begin", "mspi_timing_change_speed_mode_cache_safe"], c
    assert len(check_stall(c, set(), False, {}, [])) == 1
    assert check_stall(c, {"mspi_timing_change_speed_mode_cache_safe"}, False, {}, []) == []
    assert check_stall(["core_park_begin"], set(), False, {"CONFIG_PM_ENABLE": "y"}, []) == []          # frequency scaling alone
    assert len(check_stall(["core_park_begin"], set(), False, {"CONFIG_PM_ENABLE": "y", "CONFIG_FREERTOS_USE_TICKLESS_IDLE": "y"}, [])) == 1
    assert check_stall(["core_park_begin"], set(), True, {"CONFIG_PM_ENABLE": "y", "CONFIG_FREERTOS_USE_TICKLESS_IDLE": "y"}, []) == []
    assert len(check_stall([], set(), False, {}, ["x.c:3 esp_deep_sleep_start"])) == 1
    import tempfile
    t = tempfile.mkdtemp(); os.makedirs(os.path.join(t, "third_party"))
    open(os.path.join(t, "a.c"), "w").write("void f(void){ esp_light_sleep_start(); }\n// esp_deep_sleep_start(); in a comment\n")
    open(os.path.join(t, "third_party", "b.c"), "w").write("esp_deep_sleep_start();\n")
    h = our_sleep_calls([t]); assert len(h) == 1 and h[0].endswith(":1 esp_light_sleep_start"), h
    open(p, "w").write("# c\nmspi_x   # dead: no frequency change\n"); assert read_allow1(p) == {"mspi_x"}
    open(p, "w").write("mspi_x\n")
    try: read_allow1(p); assert False
    except SystemExit: pass
    print("check_flash_park: selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]: selftest(); sys.exit(0)
    t = sys.argv[1]
    sys.exit(check(sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, ELFS[t])))
