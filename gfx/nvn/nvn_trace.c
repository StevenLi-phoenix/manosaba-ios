// The event trace of nvn_trace.h (plan item 61c). Writers count themselves in before they look at the switch, so a stop that turns it off and
// then sees no writer knows every event taken is complete. Events beyond the room are counted, not kept.
#include "nvn_trace.h"
#include "../../host/dm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

_Atomic int g_trace_on;
static trace_ev *g_tr;
static uint64_t g_tr_cap;
static _Atomic uint64_t g_tr_n;
static _Atomic int g_tr_writers;
static const char *const g_tr_kind[TR_NKIND] = {"?", "submit", "commit", "gpu", "wait", "fence", "acquire", "present", "display"};

void trace_span(unsigned kind, uint64_t t0, uint64_t t1, uint64_t a, uint64_t b) {
#ifndef DM_TEST_TRACE_KEEP_UNSTARTED   // negative control of t_trace only: a span begun while off is kept (t0 0)
    if (!t0) return;
#endif
    atomic_fetch_add(&g_tr_writers, 1);
    if (atomic_load(&g_trace_on)) {
        uint64_t i = atomic_fetch_add(&g_tr_n, 1);
        if (i < g_tr_cap) { uint64_t th = 0; pthread_threadid_np(NULL, &th); g_tr[i] = (trace_ev){t0, t1, a, b, th, kind < TR_NKIND ? kind : 0}; }
    }
    atomic_fetch_sub(&g_tr_writers, 1);
}
int trace_parse(const char *spec, uint64_t *first, uint64_t *frames, char *path, size_t n) {
    if (!spec) return 0;
    char *e; unsigned long long f = strtoull(spec, &e, 10);
    if (e == spec || *e != '+') return 0;
    const char *s = e + 1; unsigned long long k = strtoull(s, &e, 10);
    if (e == s || *e != ':' || k < 1 || k > 1000000 || !e[1] || strlen(e + 1) >= n) return 0;
    *first = f; *frames = k; snprintf(path, n, "%s", e + 1); return 1;
}
int trace_start(uint64_t cap) {
    if (atomic_load(&g_trace_on) || !cap) return 0;
    if (cap != g_tr_cap) { free(g_tr); g_tr = calloc((size_t)cap, sizeof *g_tr); g_tr_cap = g_tr ? cap : 0; if (!g_tr) return 0; }
    atomic_store(&g_tr_n, 0); atomic_store(&g_trace_on, 1); return 1;
}
static int ev_cmp(const void *x, const void *y) { const trace_ev *a = x, *b = y; return (a->t0 > b->t0) - (a->t0 < b->t0); }
int trace_stop(const char *path, uint64_t first, uint64_t last) {
    if (!atomic_exchange(&g_trace_on, 0)) return 0;
    while (atomic_load(&g_tr_writers)) usleep(50);
    uint64_t n = atomic_load(&g_tr_n), kept = n < g_tr_cap ? n : g_tr_cap;
    qsort(g_tr, (size_t)kept, sizeof *g_tr, ev_cmp);
    FILE *f = fopen(path, "w"); if (!f) return 0;
    fprintf(f, "# frames %llu-%llu, %llu events, %llu not kept (room for %llu)\nt0_ns\tt1_ns\tkind\tthread\ta\tb\n", (unsigned long long)first,
            (unsigned long long)last, (unsigned long long)kept, (unsigned long long)(n - kept), (unsigned long long)g_tr_cap);
    for (uint64_t i = 0; i < kept; i++) { const trace_ev *v = &g_tr[i];
        fprintf(f, "%llu\t%llu\t%s\t%llu\t%llu\t%llu\n", (unsigned long long)v->t0, (unsigned long long)v->t1, g_tr_kind[v->kind], (unsigned long long)v->thread,
                (unsigned long long)v->a, (unsigned long long)v->b); }
    return !fclose(f);
}
int trace_frame(uint64_t frame) {
    static int state = -1; static uint64_t first, frames; static char path[1024];   // state: -1 not read, 0 off or done, 1 waiting, 2 recording
    if (state < 0) { state = trace_parse(getenv("DM_NVN_TRACE"), &first, &frames, path, sizeof path);
        if (!state && getenv("DM_NVN_TRACE")) LOG("[red] DM_NVN_TRACE=%s: not <first frame>+<frames>:<path>, no trace", getenv("DM_NVN_TRACE")); }
    if (state == 1 && frame >= first) {
        if (trace_start(frames * 64 + 4096)) { state = 2; LOG("[nvn] trace: frames %llu-%llu to %s", (unsigned long long)frame, (unsigned long long)(frame + frames - 1), path); first = frame; }
        else { state = 0; LOG("[red] trace: no memory for %llu frames", (unsigned long long)frames); }
    } else if (state == 2 && frame >= first + frames) {
        state = 0; int ok = trace_stop(path, first, first + frames - 1);
        if (ok) LOG("[nvn] trace written: %s", path); else LOG("[red] trace: cannot write %s", path); return ok;
    }
    return 0;
}
