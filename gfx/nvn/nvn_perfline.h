// The fps / GPU line (plan item 106; lordran-native 9567c94's idea, MIT, the code is ours): the game's presents per second and the frames that
// reached the screen per second, each over the last second, and the GPU time per frame (the mean of the last 32). hle_nvn.c feeds it from the
// present thread (perfline_frame at each present; perfline_screen with a frame's on-screen time and perfline_gpu with its GPU time, both read 8
// frames late); any thread formats it (perfline_fmt: the iOS overlay twice a second, the Mac window title, the diagnostics export, the log every
// 300 frames). The writer keeps the results in atomics, so the reader takes no lock. A screen rate below the game's shows frames the display
// dropped; without a window there is none. A stall shows as "no frame for N s" once the last present is 1.5 s old. Checked by
// gfx/tests/t_perfline.c
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define PERFLINE_RING 256   // times kept for the one-second window (enough for 240 Hz)
#define PERFLINE_AVG 32     // frames in the GPU mean
typedef struct { uint64_t t[PERFLINE_RING]; unsigned n; } perfline_ring;
typedef struct {
    perfline_ring game, screen;                                   // present times, on-screen times (writer only)
    double gpu[PERFLINE_AVG], gpu_sum; unsigned ngpu;
    _Atomic double fps, screen_fps, gpu_ms; _Atomic uint64_t last_ns;   // what readers see
} perfline;

// Adds t (not before the last one) and returns the rate over the second up to it
static inline double perfline_rate(perfline_ring *r, uint64_t t) {
    r->t[r->n++ % PERFLINE_RING] = t;
    unsigned have = r->n < PERFLINE_RING ? r->n : PERFLINE_RING, k = 1;
#ifndef DM_TEST_PERFLINE_SINCE_START
    while (k < have && t - r->t[(r->n - 1 - k) % PERFLINE_RING] <= 1000000000ull) k++;   // times in the last second, this one included
#else
    k = have;   // broken variant: every time kept, not the last second's
#endif
    uint64_t first = r->t[(r->n - k) % PERFLINE_RING];
    return k > 1 && t > first ? (k - 1) * 1e9 / (double)(t - first) : 0;
}
static inline void perfline_frame(perfline *p, uint64_t now) {
    atomic_store_explicit(&p->fps, perfline_rate(&p->game, now), memory_order_relaxed);
    atomic_store_explicit(&p->last_ns, now, memory_order_release);
}
// A frame's on-screen time (same clock as perfline_frame's), or 0 when the display discarded it (counted as not shown)
static inline void perfline_screen(perfline *p, uint64_t shown) {
    if (!shown) return;
    if (p->screen.n && shown <= p->screen.t[(p->screen.n - 1) % PERFLINE_RING]) return;   // not a new frame on screen
    atomic_store_explicit(&p->screen_fps, perfline_rate(&p->screen, shown), memory_order_relaxed);
}
static inline void perfline_gpu(perfline *p, double ms) {
#ifndef DM_TEST_PERFLINE_SINCE_START
    if (p->ngpu >= PERFLINE_AVG) p->gpu_sum -= p->gpu[p->ngpu % PERFLINE_AVG];
    p->gpu[p->ngpu++ % PERFLINE_AVG] = ms; p->gpu_sum += ms;
    atomic_store_explicit(&p->gpu_ms, p->gpu_sum / (p->ngpu < PERFLINE_AVG ? p->ngpu : PERFLINE_AVG), memory_order_relaxed);
#else
    p->gpu_sum += ms; p->ngpu++; atomic_store_explicit(&p->gpu_ms, p->gpu_sum / p->ngpu, memory_order_relaxed);
#endif
}
// "30 fps · on screen 30 · GPU 6.1 ms" (no screen part without a window, "GPU –" before the first GPU time), "no frame yet", or "no frame for
// 3.2 s"; returns snprintf's count
static inline int perfline_fmt(perfline *p, uint64_t now, char *out, size_t n) {
    uint64_t last = atomic_load_explicit(&p->last_ns, memory_order_acquire);
    if (!last) return snprintf(out, n, "no frame yet");
    if (now > last && now - last > 1500000000ull) return snprintf(out, n, "no frame for %.1f s", (now - last) / 1e9);
    double fps = atomic_load_explicit(&p->fps, memory_order_relaxed), sc = atomic_load_explicit(&p->screen_fps, memory_order_relaxed), g = atomic_load_explicit(&p->gpu_ms, memory_order_relaxed);
    char gs[24]; if (g > 0) snprintf(gs, sizeof gs, "%.1f ms", g); else snprintf(gs, sizeof gs, "–");
    return sc > 0 ? snprintf(out, n, "%.0f fps · on screen %.0f · GPU %s", fps, sc, gs) : snprintf(out, n, "%.0f fps · GPU %s", fps, gs);
}
