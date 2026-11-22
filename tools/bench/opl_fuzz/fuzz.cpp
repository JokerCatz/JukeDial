// SPDX-License-Identifier: LGPL-2.1-or-later
/* AdPlug backend fuzzer: usage  opl_fuzz EXT [-c corpus_dir]... [-o out_dir] [-t max_secs] [-p plateau_secs] [-s seed] [-r input_file]
 * Fuzzes one extension (the factory falls back to every player, so each input also reaches all of them). Seeds: synthetic headers built in
 * here + the files of the -c directories that carry the extension (the real corpus stays outside git). Coverage: edges from GCC's trace-pc;
 * magic values from trace-cmp feed a dictionary. A crash, sanitizer report, abort() or hang (SIGALRM) saves the input to out_dir first.
 * Each exec: open, scan, render a few blocks, start the other sub-songs, mute some voices. Open / block times are checked against a budget
 * (OPLF_OPEN_MS, OPLF_BLOCK_MS): over it the input is saved as slow-* and counted.
 * Env: OPLF_TRUNC32=1 truncates every allocation size to 32 bits (32-bit size_t simulation); OPLF_MAXALLOC (bytes, default 8 MiB) makes bigger
 * allocations fail (the board's heap), which adp_alloc turns into abort(): a finding. */
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <sanitizer/common_interface_defs.h>
extern "C" size_t __sanitizer_get_current_allocated_bytes(void);
extern "C" {
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_vmute.h"
}

/* ---- allocation wrapper: 32-bit simulation and a board-sized cap ---- */
extern "C" {
void *__real_malloc(size_t); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t); void __real_free(void *);
static long g_live;                  /* malloc minus free: an exec must end where it started (a leak check per input) */
static size_t g_cap = 8u << 20; static int g_trunc;
static size_t fixsz(size_t n) { if (g_trunc) n &= 0xFFFFFFFFu; return n; }
void *__wrap_malloc(size_t n) { n = fixsz(n); void *p = n > g_cap ? NULL : __real_malloc(n); if (p) g_live++; return p; }
void __wrap_free(void *p) { if (p) g_live--; __real_free(p); }
void *__wrap_calloc(size_t a, size_t b) { size_t n = fixsz(a * b); void *p = n > g_cap ? NULL : __real_calloc(1, n); if (p) g_live++; return p; }   /* a * b in 64 bit, then 32 */
void *__wrap_realloc(void *p, size_t n) { n = fixsz(n); if (n > g_cap) return NULL; void *q = __real_realloc(p, n); if (q && !p) g_live++; return q; }
}

/* ---- coverage ---- */
enum { MAPSZ = 1 << 17 };
static uint8_t g_map[MAPSZ];
static uint8_t g_seen[MAPSZ];
static uint32_t g_prev;
static size_t g_edges;
extern "C" void __sanitizer_cov_trace_pc(void)
{
    uint32_t cur = (uint32_t)(((uintptr_t)__builtin_return_address(0) * 0x9E3779B1u) >> 9) & (MAPSZ - 1);
    uint32_t e = cur ^ g_prev;
    g_prev = cur >> 1;
    if (g_map[e] < 255) g_map[e]++;
}

/* ---- trace-cmp dictionary ---- */
struct Tok { uint64_t v; int sz; };
static Tok g_dict_a[6000];            /* no allocation here: the callbacks run inside operator new */
static size_t g_nd;
struct DictView { size_t size() const { return g_nd; } bool empty() const { return !g_nd; } Tok &operator[](size_t i) const { return g_dict_a[i]; } };
static DictView g_dict;
static uint64_t g_cache[4096];
static void note(uint64_t v, int sz)
{
    if (v == 0 || (sz == 1 && v == 0xFF)) return;
    uint64_t k = (v * 0x9E3779B97F4A7C15ull) ^ (uint64_t)sz;
    uint32_t i = (uint32_t)(k >> 52);
    if (g_cache[i] == k) return;
    g_cache[i] = k;
    if (g_nd < 6000) g_dict_a[g_nd++] = { v, sz };
}
extern "C" {
void __sanitizer_cov_trace_cmp1(uint8_t a, uint8_t b) { note(a, 1); note(b, 1); }
void __sanitizer_cov_trace_cmp2(uint16_t a, uint16_t b) { note(a, 2); note(b, 2); }
void __sanitizer_cov_trace_cmp4(uint32_t a, uint32_t b) { note(a, 4); note(b, 4); }
void __sanitizer_cov_trace_cmp8(uint64_t a, uint64_t b) { note(a, 8); note(b, 8); }
void __sanitizer_cov_trace_const_cmp1(uint8_t a, uint8_t b) { note(a, 1); note(b, 1); }
void __sanitizer_cov_trace_const_cmp2(uint16_t a, uint16_t b) { note(a, 2); note(b, 2); }
void __sanitizer_cov_trace_const_cmp4(uint32_t a, uint32_t b) { note(a, 4); note(b, 4); }
void __sanitizer_cov_trace_const_cmp8(uint64_t a, uint64_t b) { note(a, 8); note(b, 8); }
void __sanitizer_cov_trace_switch(uint64_t val, void *cv)
{
    uint64_t *cases = (uint64_t *)cv, n = cases[0], bits = cases[1];
    for (uint64_t i = 0; i < n && i < 64; i++) note(cases[2 + i], (int)(bits / 8));
    (void)val;
}
void __sanitizer_cov_trace_cmpf(float, float) {}
void __sanitizer_cov_trace_cmpd(double, double) {}
void __sanitizer_cov_trace_div4(uint32_t) {}
void __sanitizer_cov_trace_div8(uint64_t) {}
void __sanitizer_cov_trace_gep(uintptr_t) {}
}

/* ---- rng ---- */
static uint64_t g_rs = 88172645463325252ull;
static uint32_t rnd() { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 7; g_rs ^= g_rs << 17; return (uint32_t)(g_rs >> 11); }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }

typedef std::vector<uint8_t> Buf;
static std::string g_ext = ".rad", g_out = ".", g_keep;
static const uint8_t *g_cur; static uint32_t g_curn;
static double g_open_ms = 3000, g_block_ms = 600;
static int g_slow, g_leaks;

static double now_ms() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static void save(const char *tag)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < g_curn; i++) h = (h ^ g_cur[i]) * 16777619u;
    char p[600];
    snprintf(p, sizeof p, "%s/%s-%s-%08x%s", g_out.c_str(), tag, g_ext.c_str() + 1, h, g_ext.c_str());
    int f = open(p, 0101 /* O_WRONLY|O_CREAT */, 0644);
    if (f >= 0) { if (write(f, g_cur, g_curn)) {} close(f); }
    char m[700]; int n = snprintf(m, sizeof m, "opl_fuzz: %s saved to %s (%u bytes)\n", tag, p, g_curn);
    if (write(2, m, (size_t)n)) {}
}
static void on_death() { save("crash"); }
static void on_sig(int s) { save(s == SIGALRM ? "hang" : "crash"); signal(s, SIG_DFL); raise(s); }

static void exec_one(const uint8_t *d, uint32_t n)
{
    g_cur = d; g_curn = n;
    long live0 = (long)__sanitizer_get_current_allocated_bytes();   /* ASan sees every allocation, libstdc++'s too */
    pc_reader_t r;
    pc_reader_mem(&r, d, n);
    std::string nm = "song" + g_ext;
    pc_reader_set_ext(&r, nm.c_str());
    alarm(getenv("OPLF_ALARM") ? atoi(getenv("OPLF_ALARM")) : 8);   // a2m-class scans are ~50x slower under trace-cmp
    void *h = NULL;
    double t0 = now_ms();
    int rc = pc_backend_opl.open(&h, &r, 0);
    double to = now_ms() - t0;
    bool slow = to > g_open_ms;
    if (!rc) {
        static float out[2 * 441];
        int tracks = pc_backend_opl.track_count(h);
        for (int t = 0; t < tracks && t < 3; t++) {
            if (t && pc_backend_opl.start_track(h, t)) break;
            if (t) { double t1 = now_ms(); (void)t1; }
            for (int b = 0; b < 40; b++) {
                if (b == 10) pc_vmute_set(rn(18), PC_VM_MUTED, 18);
                if (b == 20) pc_vmute_clear(18);
                double t1 = now_ms();
                int g = pc_backend_opl.render(h, out, 441);
                if (now_ms() - t1 > g_block_ms) slow = true;
                if (g < 441) break;
            }
        }
        pc_backend_opl.close(h);
        pc_vmute_clear(18);
    }
    alarm(0);
    static int warm; if (warm < 3) warm++;     /* one-time statics of the first execs */
    if (warm >= 3 && (long)__sanitizer_get_current_allocated_bytes() != live0) { g_curn = n; save("leak"); g_leaks++; }
    if (slow) { g_slow++; save("slow"); }
}

/* ---- synthetic seeds (code only: no tune, nothing derived from the real corpus) ---- */
static Buf mk(const char *magic, size_t mlen, size_t total, uint8_t fill = 0)
{
    Buf b(total, fill);
    memcpy(b.data(), magic, mlen < total ? mlen : total);
    return b;
}
static void synth(std::vector<Buf> &v)
{
    static const struct { const char *ext; const char *magic; size_t n; } M[] = {
        { ".rad", "RAD by REALiTY!!\x21\x00", 18 }, { ".a2m", "_A2module_", 10 }, { ".a2t", "_A2tiny_module_", 15 },
        { ".rac", "RAWADATA", 8 }, { ".dro", "DBRAWOPL\x00\x00\x01\x00", 12 }, { ".cmf", "CTMF\x01\x01", 6 }, { ".mdi", "MThd\0\0\0\6\0\0\0\1", 12 },
        { ".mkj", "MKJamz", 6 }, { ".cff", "<CUD-FM-File>N.SS.", 18 }, { ".xad", "XAD!", 4 }, { ".bmf", "BMF1.2", 6 }, { ".dtm", "DeFy DTM ", 9 },
        { ".mad", "MAD+", 4 }, { ".sop", "sopepos\x00", 8 }, { ".d00", "JCH\x26\x02\x66", 6 }, { ".msc", "Ceres \x13 MSCplay ", 16 },
        { ".dmo", "\xB1\x3F\xF9\x6C", 4 }, { ".amd", "", 0 }, { ".mtk", "mpu401tr\x92kk", 11 }, { ".sat", "RAD", 3 }, { ".vgm", "Vgm ", 4 },
        { ".sng", "ObsM", 4 }, { ".lds", "", 0 }, { ".hsq", "", 0 }, { ".ims", "", 0 }, { ".mus", "", 0 }, { ".ksm", "", 0 }, { ".rol", "", 0 },
    };
    for (auto &m : M) if (g_ext == m.ext) {
        v.push_back(mk(m.magic, m.n, 96));
        v.push_back(mk(m.magic, m.n, 1500, 0));
        v.push_back(mk(m.magic, m.n, 600, 0x7F));
    }
    v.push_back(Buf(64, 0)); v.push_back(Buf(400, 0xFF)); v.push_back(Buf(2000, 1));
    Buf r(1500); for (auto &c : r) c = (uint8_t)rnd(); v.push_back(r);
}

static bool load_file(const std::string &p, Buf &b)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    b.clear();
    uint8_t t[65536]; size_t k;
    while ((k = fread(t, 1, sizeof t, f)) > 0) { b.insert(b.end(), t, t + k); if (b.size() > (160u << 10)) break; }
    fclose(f);
    return b.size() <= (160u << 10);
}
static bool ext_eq(const char *name, const std::string &ext)
{
    const char *dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ext.c_str());
}

/* ---- mutation ---- */
static const int64_t INTV[] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 129, 255, 256, 511, 512, 1000, 1023, 1024, 4095, 4096, 32767, 32768, 65535, 65536, 0x7FFFFFFF, 0x80000000ll, 0xFFFFFFFFll, -1, -2, -128, -32768 };
static void put_le(Buf &b, size_t pos, uint64_t v, int sz) { for (int i = 0; i < sz && pos + i < b.size(); i++) b[pos + i] = (uint8_t)(v >> (8 * i)); }
static void mutate(Buf &b, const std::vector<Buf *> &pool)
{
    int rounds = 1 + (int)rn(rn(4) ? 3 : 10);
    for (int k = 0; k < rounds; k++) {
        if (b.empty()) { b.push_back((uint8_t)rnd()); }
        size_t n = b.size();
        switch (rn(14)) {
        case 0: b[rn((uint32_t)n)] ^= (uint8_t)(1u << rn(8)); break;
        case 1: b[rn((uint32_t)n)] = (uint8_t)rnd(); break;
        case 2: { int64_t v = INTV[rn(sizeof INTV / sizeof *INTV)]; int sz = 1 << rn(3); put_le(b, rn((uint32_t)n), (uint64_t)v, sz); break; }
        case 3: b[rn((uint32_t)n)] += (uint8_t)((int)rn(70) - 35); break;
        case 4: { size_t p = rn((uint32_t)n); uint16_t w = (uint16_t)(b[p] | (p + 1 < n ? b[p + 1] << 8 : 0)); w += (uint16_t)((int)rn(70) - 35); put_le(b, p, w, 2); break; }
        case 5: if (n > 2) { size_t p = rn((uint32_t)n), l = 1 + rn((uint32_t)(n - p < 64 ? n - p : 64)); b.erase(b.begin() + p, b.begin() + p + l); } break;
        case 6: if (n < (150u << 10)) { size_t p = rn((uint32_t)n + 1), l = 1 + rn(32); Buf ins(l); for (auto &c : ins) c = rn(2) ? (uint8_t)rnd() : 0; b.insert(b.begin() + p, ins.begin(), ins.end()); } break;
        case 7: if (n < (150u << 10)) { size_t s = rn((uint32_t)n), l = 1 + rn((uint32_t)(n - s < 128 ? n - s : 128)), p = rn((uint32_t)n); Buf c(b.begin() + s, b.begin() + s + l); b.insert(b.begin() + p, c.begin(), c.end()); } break;
        case 8: if (!g_dict.empty()) { Tok t = g_dict[rn((uint32_t)g_dict.size())]; put_le(b, rn((uint32_t)n), t.v, t.sz); } break;
        case 9: if (!g_dict.empty() && n < (150u << 10)) { Tok t = g_dict[rn((uint32_t)g_dict.size())]; size_t p = rn((uint32_t)n + 1); Buf ins(t.sz); for (int i = 0; i < t.sz; i++) ins[i] = (uint8_t)(t.v >> (8 * i)); b.insert(b.begin() + p, ins.begin(), ins.end()); } break;
        case 10: if (n > 8) b.resize(1 + rn((uint32_t)n)); break;                                                 /* truncate */
        case 11: if (n < (150u << 10)) { size_t add = 1 + rn(300); b.resize(n + add, rn(2) ? 0 : 0xFF); } break;     /* extend */
        case 12: if (!pool.empty()) { const Buf &o = *pool[rn((uint32_t)pool.size())]; if (!o.empty()) { size_t s = rn((uint32_t)o.size()), l = 1 + rn((uint32_t)(o.size() - s < 256 ? o.size() - s : 256)), p = rn((uint32_t)n); for (size_t i = 0; i < l && p + i < n; i++) b[p + i] = o[s + i]; } } break;
        case 13: { size_t p = rn((uint32_t)n), l = 1 + rn((uint32_t)(n - p < 16 ? n - p : 16)); for (size_t i = 0; i < l; i++) b[p + i] = (uint8_t)rn(2 ? 256 : 4); break; }
        }
    }
    if (b.size() > (160u << 10)) b.resize(160u << 10);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: opl_fuzz .ext [-c dir]... [-o outdir] [-t secs] [-p plateau secs] [-s seed] [-r file]\n"); return 2; }
    g_ext = argv[1];
    std::vector<std::string> cdirs;
    const char *rfile = NULL;
    double tmax = 60, plateau = 30;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-c")) cdirs.push_back(argv[i + 1]);
        else if (!strcmp(argv[i], "-o")) g_out = argv[i + 1];
        else if (!strcmp(argv[i], "-t")) tmax = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "-p")) plateau = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "-s")) g_rs ^= (uint64_t)atoll(argv[i + 1]) * 0x9E3779B97F4A7C15ull;
        else if (!strcmp(argv[i], "-r")) rfile = argv[i + 1];
        else if (!strcmp(argv[i], "-k")) g_keep = argv[i + 1];
    }
    if (getenv("OPLF_TRUNC32")) g_trunc = atoi(getenv("OPLF_TRUNC32"));
    if (getenv("OPLF_MAXALLOC")) g_cap = (size_t)atoll(getenv("OPLF_MAXALLOC"));
    if (getenv("OPLF_OPEN_MS")) g_open_ms = atof(getenv("OPLF_OPEN_MS"));
    if (getenv("OPLF_BLOCK_MS")) g_block_ms = atof(getenv("OPLF_BLOCK_MS"));
    __sanitizer_set_death_callback(on_death);
    signal(SIGALRM, on_sig); signal(SIGABRT, on_sig);
    for (int i = 0; i < 8; i++) rnd();
    if (rfile) {                                     /* reproduce one input */
        Buf b;
        if (!load_file(rfile, b)) return 2;
        exec_one(b.data(), (uint32_t)b.size());
        printf("ok %zu bytes\n", b.size());
        return 0;
    }
    std::vector<Buf *> corpus;
    std::vector<Buf> seeds;
    synth(seeds);
    if (!g_keep.empty()) cdirs.push_back(g_keep);        /* -k dir: a corpus kept between runs (written as inputs find new edges); under .local, never git */
    for (auto &d : cdirs) if (DIR *dir = opendir(d.c_str())) {
        while (dirent *e = readdir(dir)) if (ext_eq(e->d_name, g_ext) || d == g_keep) { Buf b; if (load_file(d + "/" + e->d_name, b)) seeds.push_back(b); }
        closedir(dir);
    }
    double t0 = now_ms(), tlast = t0;
    uint64_t execs = 0;
    auto run = [&](Buf &b) -> bool {
        memset(g_map, 0, sizeof g_map); g_prev = 0;
        exec_one(b.data(), (uint32_t)b.size());
        execs++;
        bool nw = false;
        for (int i = 0; i < MAPSZ; i++) if (g_map[i] && !g_seen[i]) { g_seen[i] = 1; g_edges++; nw = true; }
        return nw;
    };
    for (auto &s : seeds) { Buf c = s; run(c); corpus.push_back(new Buf(s)); }
    tlast = now_ms();
    size_t newest = 0;
    while (now_ms() - t0 < tmax * 1000 && now_ms() - tlast < plateau * 1000) {
        std::vector<Buf *> &pool = corpus;
        size_t pick = rn(3) == 0 && !pool.empty() ? pool.size() - 1 - rn((uint32_t)(pool.size() < 8 ? pool.size() : 8)) : rn((uint32_t)pool.size());
        Buf m = *pool[pick];
        if (rn(8) == 0 && pool.size() > 1) { const Buf &o = *pool[rn((uint32_t)pool.size())]; if (!o.empty() && !m.empty()) { size_t cut = rn((uint32_t)m.size()); m.resize(cut); m.insert(m.end(), o.begin() + rn((uint32_t)o.size()), o.end()); } }
        mutate(m, pool);
        if (run(m)) {
            corpus.push_back(new Buf(m)); tlast = now_ms(); newest = corpus.size();
            if (!g_keep.empty()) { uint32_t h = 2166136261u; for (uint8_t c : m) h = (h ^ c) * 16777619u; char pth[700]; snprintf(pth, sizeof pth, "%s/%08x%s", g_keep.c_str(), h, g_ext.c_str()); if (FILE *f = fopen(pth, "wb")) { fwrite(m.data(), 1, m.size(), f); fclose(f); } }
        }
    }
    printf("%s execs=%llu edges=%zu corpus=%zu seeds=%zu dict=%zu slow=%d leaks=%d secs=%.0f last_new_at=%.0f\n", g_ext.c_str(), (unsigned long long)execs, g_edges, corpus.size(), seeds.size(), g_dict.size(), g_slow, g_leaks,
           (now_ms() - t0) / 1000, (tlast - t0) / 1000);
    (void)newest;
    return 0;
}
