// Diagnostics: probes (DM_PROBE, DM_THROWLOG; Mac only), boot timeline (DM_BOOTPROF), guest thread sampling (DM_SAMPLE), the perf record
// (perf.txt / DM_PERF_FILE) and long-frame notes. Signal handling itself is in crash.c, which does not depend on this file.
#include "dm.h"
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <signal.h>
#include <dlfcn.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
void crash_off(const char *tag, uint64_t a);
int crash_set_trap_hook(int (*fn)(ucontext_t *uc));

// ---------- Probes DM_PROBE=module:offset[@x0 offset][,...] (Mac development only) ----------
// Writes a brk into the guest code page at runtime and runs the original instruction from a trampoline afterwards. iOS cannot do this (signed code
// pages are not writable, runtime code is not executable). On a hit: x0..x3 (with readable strings they point to), x19..x23, x8 and a backtrace.
// @off prints 16 bytes at x0+off, @*off at *(x0+off). The trampoline borrows x16: probe only at function entries or where x16 is dead, and not on
// PC-relative instructions (adr / adrp / b / bl / ldr literal / cbz ...).
// DM_PROBE_DUMP=module:offset:length[:file] writes that guest memory to a file on the first probe hit. DM_THROWLOG=1 probes the SDK's __cxa_throw.
#if !TARGET_OS_IPHONE
#define MAX_PROBE 16
typedef void (*probe_fn)(ucontext_t *uc);
static struct { uint64_t at; uint32_t *slot; unsigned hits; char name[40]; long dump; probe_fn fn; } g_probe[MAX_PROBE];
static int g_nprobe;
static uint32_t *g_slots; static int g_slots_sealed;
static void probe_str(uint64_t p) {
    char buf[80]; vm_size_t got = 0;
    if (p < 0x1000 || vm_read_overwrite(mach_task_self(), p, sizeof buf - 1, (vm_address_t)buf, &got) || !got) return;
    buf[got] = 0; int n = 0; while (buf[n] >= 0x20 && buf[n] < 0x7f) n++;
    if (n >= 3 && (buf[n] == 0 || n == (int)got)) { fprintf(stderr, " \"%.*s\"", n, buf); return; }
    if (got >= 24) {   // a {char *data, u64 length, ...} string object
        uint64_t ptr, len; memcpy(&ptr, buf, 8); memcpy(&len, buf + 8, 8);
        char t[96]; got = 0;
        if (len >= 3 && len < sizeof t && !vm_read_overwrite(mach_task_self(), ptr, len, (vm_address_t)t, &got) && got == len) {
            int k = 0; while (k < (int)len && t[k] >= 0x20 && t[k] < 0x7f) k++;
            if (k == (int)len) fprintf(stderr, " S\"%.*s\"", (int)len, t);
        }
    }
}
static void probe_dump_once(void) {
    static int done; const char *e = getenv("DM_PROBE_DUMP"); if (done || !e) return; done = 1;
    char spec[512]; strlcpy(spec, e, sizeof spec);
    char *mod = strtok(spec, ":"), *o = strtok(NULL, ":"), *n = strtok(NULL, ":"), *f = strtok(NULL, ":");
    module_t *m = NULL; for (int i = 0; i < g_nmod; i++) if (mod && !strcmp(g_mod[i].name, mod)) m = &g_mod[i];
    if (!m || !o || !n) { LOG("DM_PROBE_DUMP: expected module:offset:length[:file]"); return; }
    const char *path = f ? f : "probe_dump.bin";
    FILE *fp = fopen(path, "wb"); if (!fp) return;
    fwrite(m->base + strtoull(o, 0, 16), 1, strtoull(n, 0, 16), fp); fclose(fp);
    LOG("probe dump %s+%s, %s bytes -> %s", mod, o, n, path);
}
static int probe_hit(ucontext_t *uc) {   // SIGTRAP hook of crash.c
    uint64_t pc = uc->uc_mcontext->__ss.__pc;
    for (int i = 0; i < g_nprobe; i++) if (g_probe[i].at == pc) {
        probe_dump_once();
        if (g_probe[i].fn) { g_probe[i].hits++; g_probe[i].fn(uc); uc->uc_mcontext->__ss.__pc = (uint64_t)g_probe[i].slot; return 1; }
        fprintf(stderr, "[dm] PROBE %s #%u [%.4fs]", g_probe[i].name, ++g_probe[i].hits, (now_ns() - g_t0_ns) / 1e9);
        for (int r = 0; r < 4; r++) { uint64_t v = uc->uc_mcontext->__ss.__x[r]; fprintf(stderr, " x%d=%#llx", r, v); probe_str(v); }
        for (int r = 19; r < 24; r++) fprintf(stderr, " x%d=%#llx", r, uc->uc_mcontext->__ss.__x[r]);
        { uint64_t x8 = uc->uc_mcontext->__ss.__x[8]; Dl_info di; fprintf(stderr, " x8=%#llx", x8);   // indirect call targets often sit in x8
          if (dladdr((void *)x8, &di) && di.dli_sname) fprintf(stderr, " (%s+%#lx)", di.dli_sname, (unsigned long)(x8 - (uint64_t)di.dli_saddr)); }
        if (g_probe[i].dump >= 0) {
            uint8_t b[16]; vm_size_t got = 0;
            uint64_t at = uc->uc_mcontext->__ss.__x[0] + (g_probe[i].dump & 0xffffff);
            if (g_probe[i].dump & 0x1000000) { uint64_t p = 0; vm_size_t g = 0; if (!vm_read_overwrite(mach_task_self(), at, 8, (vm_address_t)&p, &g)) at = p; else at = 0; }
            if (at && !vm_read_overwrite(mach_task_self(), at, 16, (vm_address_t)b, &got) && got == 16) {
                fprintf(stderr, " [%s%#lx]=", (g_probe[i].dump & 0x1000000) ? "*x0+" : "x0+", g_probe[i].dump & 0xffffff); for (int k = 0; k < 16; k++) fprintf(stderr, "%02x", b[k]); }
        }
        fputc('\n', stderr);
        dm_backtrace(uc->uc_mcontext->__ss.__fp);
        uc->uc_mcontext->__ss.__pc = (uint64_t)g_probe[i].slot;
        return 1;
    }
    return 0;
}
// DM_THROWLOG: the type of every C++ throw (x1 = std::type_info, +8 = mangled name). IL2CPP managed exceptions are thrown as Il2CppExceptionWrapper
// (x0 → it, first field = Il2CppException*): also the exception class (klass +0x10 name, +0x18 namespace) and its message (object +0x18, an
// Il2CppString {klass, monitor, int32 length +0x10, UTF-16 +0x14}, read only after checking the class name is String)
static int rd(uint64_t a, void *out, size_t n) { vm_size_t got = 0; return a >= 0x1000 && !vm_read_overwrite(mach_task_self(), a, n, (vm_address_t)out, &got) && got == n; }
static int rdstr(uint64_t a, char *out, size_t n) { if (!rd(a, out, 1)) return 0; size_t k = 0; while (k + 1 < n && rd(a + k, out + k, 1) && out[k]) k++; out[k] = 0; return 1; }
static void throw_probe(ucontext_t *uc) {
    uint64_t x0 = uc->uc_mcontext->__ss.__x[0], x1 = uc->uc_mcontext->__ss.__x[1], tn = 0; char tname[128] = "?";
    if (rd(x1 + 8, &tn, 8)) rdstr(tn, tname, sizeof tname);
    fprintf(stderr, "[dm] THROW [%.4fs] %s", (now_ns() - g_t0_ns) / 1e9, tname);
    uint64_t ex = 0, k = 0, kn = 0, kns = 0;
    if (strstr(tname, "Il2CppExceptionWrapper") && rd(x0, &ex, 8) && rd(ex, &k, 8) && rd(k + 0x10, &kn, 8) && rd(k + 0x18, &kns, 8)) {
        char n1[96] = "", n2[96] = ""; rdstr(kn, n1, sizeof n1); rdstr(kns, n2, sizeof n2);
        fprintf(stderr, " managed exception %s.%s", n2, n1);
        uint64_t msg = 0, mk = 0, mkn = 0; char mn[16] = "";
        if (rd(ex + 0x18, &msg, 8) && msg && rd(msg, &mk, 8) && rd(mk + 0x10, &mkn, 8) && rdstr(mkn, mn, sizeof mn) && !strcmp(mn, "String")) {
            int32_t len = 0; rd(msg + 0x10, &len, 4); if (len < 0) len = 0; if (len > 300) len = 300;
            fputs(" message=\"", stderr);
            for (int i = 0; i < len; i++) { uint16_t c = 0; rd(msg + 0x14 + 2 * i, &c, 2);
                if (c < 0x80) fputc(c, stderr); else if (c < 0x800) { fputc(0xc0 | c >> 6, stderr); fputc(0x80 | (c & 0x3f), stderr); }
                else { fputc(0xe0 | c >> 12, stderr); fputc(0x80 | ((c >> 6) & 0x3f), stderr); fputc(0x80 | (c & 0x3f), stderr); } }
            fputc('"', stderr);
        }
    }
    fputc('\n', stderr);
    dm_backtrace(uc->uc_mcontext->__ss.__fp);
}
static void probe_add(module_t *m, uint64_t off, long dump, probe_fn fn) {
    if (g_nprobe >= MAX_PROBE) die("too many probes (max %d)", MAX_PROBE);
    if (!g_slots) g_slots = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint64_t at = (uint64_t)m->base + off;
    uint32_t *w = (uint32_t *)at, *sl = g_slots + g_nprobe * 8;
    sl[0] = *w;                                  // original instruction
    sl[1] = 0x58000050;                          // ldr x16, #8
    sl[2] = 0xD61F0200;                          // br x16
    *(uint64_t *)(sl + 3) = at + 4;
    uint64_t pg = at & ~0x3FFFull;
    if (mprotect((void *)pg, 0x4000, PROT_READ | PROT_WRITE)) die("probe mprotect");
    *w = 0xD43E0000;                             // brk #0xf000
    mprotect((void *)pg, 0x4000, PROT_READ | PROT_EXEC);
    sys_icache_invalidate(w, 4);
    g_probe[g_nprobe].at = at; g_probe[g_nprobe].slot = sl; g_probe[g_nprobe].dump = dump; g_probe[g_nprobe].fn = fn;
    snprintf(g_probe[g_nprobe].name, sizeof g_probe[0].name, "%s+%#llx", m->name, off);
    LOG("probe %s (original instruction %08x)", g_probe[g_nprobe].name, sl[0]);
    g_nprobe++;
}
#endif   // !TARGET_OS_IPHONE

void probe_install(void) {
#if !TARGET_OS_IPHONE
    crash_set_trap_hook(probe_hit);
    const char *e = getenv("DM_PROBE");
    if (e) {
        char spec[512]; strlcpy(spec, e, sizeof spec);
        for (char *t = strtok(spec, ","); t; t = strtok(NULL, ",")) {
            char *c = strchr(t, ':'); if (!c) die("DM_PROBE: expected module:offset");
            *c = 0; module_t *m = NULL;
            for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, t)) m = &g_mod[i];
            if (!m) die("DM_PROBE: no module %s", t);
            char *at_ = strchr(c + 1, '@');
            long dump = -1;
            if (at_) dump = at_[1] == '*' ? (0x1000000 | strtol(at_ + 2, 0, 16)) : strtol(at_ + 1, 0, 16);
            probe_add(m, strtoull(c + 1, 0, 16), dump, NULL);
        }
    }
    if (getenv("DM_THROWLOG")) {
        void *t = guest_sym("__cxa_throw"); module_t *m = t ? module_of(t) : NULL;
        if (!m) LOG("[red] DM_THROWLOG: __cxa_throw not found");
        else probe_add(m, (uint64_t)t - (uint64_t)m->base, -1, throw_probe);
    }
    if (g_slots && !g_slots_sealed) { mprotect(g_slots, 0x4000, PROT_READ | PROT_EXEC); sys_icache_invalidate(g_slots, 0x4000); g_slots_sealed = 1; }
#endif
}

// ---------- Boot timeline DM_BOOTPROF=1 ----------
// Segments marked with dm_prof_mark (0–3 in main.c), the last one at the first presented frame, when the table is printed. Wait times are summed
// over all threads (thread-seconds).
static _Atomic uint64_t g_pns[PROF_N], g_pby[PROF_N];
static const char *const g_pname[PROF_N] = {"RomFS read", "lock wait", "condvar wait", "address wait", "sync wait", "sleep", "main wait", "present (gfx)", "shader/pipeline"};
#define MAX_SEG 12
static struct { const char *name; uint64_t t, ns[PROF_N], by[PROF_N]; } g_seg[MAX_SEG];
static int g_nseg, g_prof_done;
static pthread_mutex_t g_proflock = PTHREAD_MUTEX_INITIALIZER;
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) {
    if (!g_flag.bootprof || (unsigned)id >= PROF_N) return;
    atomic_fetch_add(&g_pns[id], ns); if (bytes) atomic_fetch_add(&g_pby[id], bytes);
}
static void prof_table(void) {
    fprintf(stderr, "[dm] ===== boot timeline (DM_BOOTPROF; waits are thread-seconds over all threads) =====\n");
    fprintf(stderr, "[dm] %-50s %8s %8s", "segment", "wall s", "total s");
    for (int k = 0; k < PROF_N; k++) fprintf(stderr, " %15s", g_pname[k]);
    fprintf(stderr, " %9s\n", "read MiB");
    uint64_t prev_t = g_t0_ns, prev_ns[PROF_N] = {0}, prev_by[PROF_N] = {0};
    for (int j = 0; j < g_nseg; j++) {
        fprintf(stderr, "[dm] %-50s %8.3f %8.3f", g_seg[j].name, (g_seg[j].t - prev_t) / 1e9, (g_seg[j].t - g_t0_ns) / 1e9);
        for (int k = 0; k < PROF_N; k++) fprintf(stderr, " %15.3f", (g_seg[j].ns[k] - prev_ns[k]) / 1e9);
        fprintf(stderr, " %9.1f\n", (g_seg[j].by[PROF_FS_READ] - prev_by[PROF_FS_READ]) / 1048576.0);
        prev_t = g_seg[j].t; memcpy(prev_ns, g_seg[j].ns, sizeof prev_ns); memcpy(prev_by, g_seg[j].by, sizeof prev_by);
    }
}
static void prof_mark(const char *seg, int last) {
    if (!g_flag.bootprof) return;
    pthread_mutex_lock(&g_proflock);
    if (!g_prof_done && g_nseg < MAX_SEG) {
        g_seg[g_nseg].name = seg; g_seg[g_nseg].t = now_ns();
        for (int k = 0; k < PROF_N; k++) { g_seg[g_nseg].ns[k] = g_pns[k]; g_seg[g_nseg].by[k] = g_pby[k]; }
        g_nseg++;
        if (last) { g_prof_done = 1; prof_table(); }
    }
    pthread_mutex_unlock(&g_proflock);
}
void dm_prof_mark(const char *seg) { prof_mark(seg, 0); }

// ---------- Sampling DM_SAMPLE=<ms>[:<thread name>|:*] ----------
// Periodically interrupts the thread that called crash_sample_self() (the guest main thread), a named guest thread, or all of them (*), and prints
// pc / lr and a backtrace. The signal handler only copies registers and the frame chain into a ring buffer; the sampling thread prints.
static pthread_t g_sample_pt;
#define SRING 1024
static struct { uint64_t t, pc, lr, bt[16]; int n; uint32_t h; } g_sring[SRING];
static _Atomic unsigned g_shead;
static void on_sample(int sig, siginfo_t *si, void *uc_) {
    (void)sig; (void)si; ucontext_t *uc = uc_;
    unsigned i = atomic_load(&g_shead) % SRING;
    uint32_t thr_cur_handle(void);
    g_sring[i].h = thr_cur_handle(); g_sring[i].t = now_ns(); g_sring[i].pc = uc->uc_mcontext->__ss.__pc; g_sring[i].lr = uc->uc_mcontext->__ss.__lr;
    uint64_t fp = uc->uc_mcontext->__ss.__fp; int n = 0;
    for (; n < 16 && fp && !(fp & 7); n++) {   // safe reads: x29 may be an ordinary register in the interrupted function
        uint64_t fr[2]; vm_size_t got = 0;
        if (vm_read_overwrite(mach_task_self(), fp, sizeof fr, (vm_address_t)fr, &got) || got != sizeof fr) break;
        g_sring[i].bt[n] = fr[1];
        if (fr[0] <= fp) { n++; break; }
        fp = fr[0];
    }
    g_sring[i].n = n;
    atomic_fetch_add(&g_shead, 1);
}
int thr_find_by_name(const char *name, pthread_t *out);
static const char *g_sample_name;
static void *sampler(void *p) {
    useconds_t us = (useconds_t)(uintptr_t)p; unsigned printed = 0;
    for (;;) {
        usleep(us);
        if (g_sample_name && !strcmp(g_sample_name, "*")) {
            int thr_list(pthread_t *out, int max); pthread_t all[64]; int n = thr_list(all, 64);
            for (int k = 0; k < n; k++) { pthread_kill(all[k], SIGUSR2); usleep(200); }
        } else {
            if (g_sample_name) { pthread_t t; if (!thr_find_by_name(g_sample_name, &t)) continue; g_sample_pt = t; }
            pthread_kill(g_sample_pt, SIGUSR2);
        }
        for (unsigned h = atomic_load(&g_shead); printed < h; printed++) {
            unsigned i = printed % SRING;
            fprintf(stderr, "[dm] SAMPLE #%u [%.3fs] thr=%#x", printed + 1, (g_sring[i].t - g_t0_ns) / 1e9, g_sring[i].h); crash_off("pc", g_sring[i].pc); crash_off("lr", g_sring[i].lr);
            fprintf(stderr, "\n[dm] bt:"); for (int k = 0; k < g_sring[i].n; k++) crash_off("", g_sring[i].bt[k]); fputc('\n', stderr);
        }
    }
}
void crash_sample_self(void) {
    const char *e = getenv("DM_SAMPLE"); if (!e) return;
    g_sample_pt = pthread_self();
    const char *c = strchr(e, ':'); if (c) g_sample_name = strdup(c + 1);
    struct sigaction sa = {.sa_sigaction = on_sample, .sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART};
    sigaction(SIGUSR2, &sa, 0);
    pthread_t t; pthread_create(&t, NULL, sampler, (void *)(uintptr_t)(atoi(e) * 1000)); pthread_detach(t);
}

// ---------- Perf record ----------
// One line every 10 s: wall time / process seconds / thermal state / present frame rate / share of frame intervals in the 16.7, 25, >=33 ms buckets
// (boundaries 20.8, 29.2 ms) / dropped frames / submit CPU ms per frame. On by default on iOS (perf.txt in the working directory = Documents);
// DM_PERF_FILE=<path> chooses the file (on the Mac only when set), DM_PERF_FILE=0 turns it off. At 256 KB it is renamed perf.old.txt.
int dm_thermal_now(void);
static const char *perf_path(void) {
    static const char *path; static int init;
    if (!init) { init = 1; const char *e = getenv("DM_PERF_FILE"); path = e ? (strcmp(e, "0") ? e : NULL) : (TARGET_OS_IPHONE ? "perf.txt" : NULL); }
    return path;
}
// Long frame (hle_nvn.c calls this for a frame over 20 ms or GPU over 16.7 ms): logged, and written to the perf record (at most 2000 lines per run)
void dm_longframe_note(const char *what) {
    static unsigned n;
    LOG("[long frame] %s", what);
    const char *path = perf_path(); if (!path || n++ >= 2000) return;
    FILE *fp = fopen(path, "a"); if (!fp) return;
    time_t w = time(NULL); struct tm tmv; localtime_r(&w, &tmv); char ts[16]; strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
    fprintf(fp, "long frame\t%s\t%s\n", ts, what); fclose(fp);
}
// Called by the present thread once per frame: the actual present interval in ms (−1 = dropped by the system, 0 = no reading)
void dm_perf_present(double iv) {
    static int init; static unsigned c16, c25, c33, disc; static double sum; static uint64_t t0, f0, cpu0, tstart;
    if (!init) { init = 1; t0 = tstart = now_ns(); f0 = dm_nvn_frame_count(); cpu0 = dm_nvn_sub_cpu_ns(); prof_mark("4 first frame presented", 1); }
    const char *path = perf_path(); if (!path) return;
    if (iv < 0) disc++; else if (iv > 0) { sum += iv; if (iv <= 20.8) c16++; else if (iv <= 29.2) c25++; else c33++; }
    uint64_t now = now_ns(); if (now - t0 < 10000000000ull) return;
    unsigned n = c16 + c25 + c33; uint64_t f = dm_nvn_frame_count(), cpu = dm_nvn_sub_cpu_ns();
    double fps = sum > 0 ? n * 1000.0 / sum : (f - f0) / ((now - t0) / 1e9);
    struct stat st; if (!stat(path, &st) && st.st_size > 256 * 1024) rename(path, "perf.old.txt");
    FILE *fp = fopen(path, "a");
    if (fp) {
        if (ftell(fp) == 0) fprintf(fp, "# time\tseconds\tthermal\tfps\t16.7ms%%\t25ms%%\t>=33ms%%\tdropped\tsubmit CPU ms\n");
        time_t w = time(NULL); struct tm tmv; localtime_r(&w, &tmv); char ts[16]; strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
        fprintf(fp, "%s\t%.0f\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%u\t%.2f\n", ts, (now - tstart) / 1e9, dm_thermal_now(), fps,
                n ? c16 * 100.0 / n : 0, n ? c25 * 100.0 / n : 0, n ? c33 * 100.0 / n : 0, disc, f > f0 ? (cpu - cpu0) / 1e6 / (double)(f - f0) : 0);
        fclose(fp);
    }
    LOG("[perf] thermal %d, %.1f fps, intervals %u/%u/%u, dropped %u", dm_thermal_now(), fps, c16, c25, c33, disc);
    c16 = c25 = c33 = disc = 0; sum = 0; t0 = now; f0 = f; cpu0 = cpu;
}
// The graphics side finished writing a screenshot (DM_NVN_SHOT); nothing to do here at present
void dm_shot_written(const char *path, int ok) { (void)path; (void)ok; }
