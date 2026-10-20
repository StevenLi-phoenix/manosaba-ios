// The audio renderer's pacing (audren_pace.h), against a simulated sound card whose 10 ms callback takes two 5 ms frames at once and hands their
// buffers back (six buffers, three queued as silence first, as render_main does). On the renderer's own clock: no two frames closer than 1 ms, no
// callback without its two frames, one frame per 5 ms; after a 50 ms stall at most two frames come together (the empty queue's catch-up, then the
// deadline again); with a sound card 1 % faster the queue still never runs dry for long. Paced by the output instead (-DDM_TEST_AUDREN_PACE_OUTPUT,
// as before) the frames come in pairs: 2 fail.
// The silent sink (sink_step, plan item 122): 200 pulls a second; after a 2 s suspension of its thread the next 50 ms hold 9-11 pulls, not the
// 400 a deadline seconds old would pull back to back; a 60 ms hiccup is caught up (no restart under 100 ms). Never restarted
// (-DDM_TEST_SINK_NO_RESYNC, as before): 1 fails.
#include "../audren_pace.h"
#include <stdio.h>
static int checks, fails;
#define CHECK(c, s) do { checks++; int ok_ = !!(c); fails += !ok_; printf("%-74s %s\n", s, ok_ ? "✓" : "✗"); } while (0)
enum { NBUF = 6, PREQ = 3 };
typedef struct { int renders, close, starved, maxburst; } result;
// dev_period: the callback's period in µs; stall_at / stall_us: the render thread stops that long once
static result sim(uint64_t dev_period, uint64_t stall_at, uint64_t stall_us) {
    const uint64_t frame = 5000, end = 2000000; result r = {0};
    audren_pace p = {0}; int queued = PREQ; uint64_t t = 0, next_cb = dev_period, last = 0; int stalled = 0;
    uint64_t times[512]; int nt = 0;
    while (t < end) {
        if (!stalled && stall_us && t >= stall_at) { stalled = 1; t += stall_us; }
        while (next_cb <= t) { if (queued >= 2) queued -= 2; else { r.starved++; queued = 0; } next_cb += dev_period; }   // the callback takes 2 frames, returns their buffers
        uint64_t w = queued < NBUF ? pace_wait(&p, t, frame, queued) : next_cb - t;
        if (w) { uint64_t nt2 = t + w; t = nt2 < next_cb ? nt2 : next_cb; continue; }
        queued++; r.renders++; if (last && t - last < 1000) r.close++; last = t;
        if (nt < 512) times[nt++] = t;
        t += 50;   // the frame takes 50 µs
    }
    for (int i = 0; i < nt; i++) { int k = 0; for (int j = i; j < nt && times[j] - times[i] < 5000; j++) k++; if (k > r.maxburst) r.maxburst = k; }
    return r;
}
// pulls of a silent sink over [0, end) µs whose thread is frozen at freeze_at for freeze_us; *after = pulls in the 50 ms after the freeze ends
static int sink_sim(uint64_t end, uint64_t freeze_at, uint64_t freeze_us, int *after, unsigned *resyncs) {
    uint64_t t0 = 0, now = 0, dl = 0; int pulls = 0, frozen = 0; *after = 0; *resyncs = 0; (void)t0;
    while (now < end) {
        if (!frozen && freeze_us && now >= freeze_at) { frozen = 1; now += freeze_us; }
        uint64_t w = sink_step(&dl, now * 1000, 5000000, 100000000, resyncs) / 1000; now += w;
        pulls++; if (frozen && now >= freeze_at + freeze_us && now < freeze_at + freeze_us + 50000) ++*after;
        now += 20;   // a pull takes 20 µs
    }
    return pulls;
}
int main(void) {
    result a = sim(10000, 0, 0);
    printf("steady: %d frames, %d under 1 ms apart, %d starved callbacks\n", a.renders, a.close, a.starved);
    CHECK(a.close == 0, "a 10 ms callback taking two frames: no two frames under 1 ms apart");
    CHECK(a.starved == 0, "... every callback finds its two frames");
    CHECK(a.renders >= 398 && a.renders <= 402, "... one frame per 5 ms (400 in 2 s)");
    result b = sim(10000, 1000000, 50000);
    printf("stall: %d frames, %d starved, at most %d in 5 ms\n", b.renders, b.starved, b.maxburst);
    CHECK(b.maxburst <= 2, "after a 50 ms stall: at most two frames within 5 ms (no burst)");
    result c = sim(9900, 0, 0);
    printf("1 %% faster card: %d frames, %d starved, %d under 1 ms apart\n", c.renders, c.starved, c.close);
    CHECK(c.starved <= 2, "a sound card 1 % faster: the queue runs dry at most twice in 2 s");
    int after; unsigned rs;
    int sp = sink_sim(1000000, 0, 0, &after, &rs);
    CHECK(sp >= 199 && sp <= 201 && rs == 0, "silent sink: 200 pulls a second, no restart");
    sink_sim(4000000, 1000000, 2000000, &after, &rs);
    printf("silent sink after a 2 s suspension: %d pulls in the next 50 ms, %u restarts\n", after, rs);
    CHECK(after >= 9 && after <= 11 && rs == 1, "... after a 2 s suspension: 9-11 pulls in the next 50 ms (no burst), one restart");
    sink_sim(2000000, 500000, 60000, &after, &rs);
    CHECK(rs == 0, "... a 60 ms hiccup is caught up, no restart");
    printf("t_audren_pace: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
