// The audio renderer's pacing (hle_audren.c render_main; plan item 98), header-only for its unit test (tests/t_audren_pace.c).
#pragma once
#include <stdint.h>

// Pacing (plan item 98): a frame at each deadline one frame period apart, on the render thread's own clock, not whenever the output hands a buffer back.
// A sound card that hands back two at once (a 10 ms device period, 5 ms frames) made frames in pairs: 55 % of the gaps under 1 ms, and a catch-up frame
// before 27 % of the game's updates, which found none rendered since the last (SkyRecomp 0620de9, MIT: 53 of 79 event gaps under 1 ms before, none
// after). PREQ silent buffers go first as the device's own buffering (a 10 ms callback takes two frames at once and must find them); at a deadline a
// frame is rendered when a buffer is free, and at once when none is queued (a sound card clock a little faster than the host's drains the queue over
// minutes); a stall of the host restarts the deadline instead of a burst. Returns how long to wait before the next frame (0 = now). Unit test
// tests/t_audren_pace.c
typedef struct { uint64_t next; } audren_pace;
static inline uint64_t pace_wait(audren_pace *p, uint64_t now, uint64_t period, int queued) {
#ifndef DM_TEST_AUDREN_PACE_OUTPUT   // negative control of t_audren_pace only: as soon as a buffer is free (the output's pacing)
    if (!p->next || now > p->next + 2 * period) p->next = now;   // first frame, or after a stall: from now, no burst
    if (!queued || now >= p->next) { p->next += period; if (p->next < now) p->next = now; return 0; }
    return p->next - now;
#else
    (void)p; (void)now; (void)period; (void)queued; return 0;
#endif
}

// The silent sink's pacing (hle_audio.c null_sink; plan item 122): one pull every period on a deadline that advances by the period. After a stall
// longer than resync (an iOS suspension freezes the thread; acnh-mobile and nxnative 3bd1e9e restart past 100 ms, MIT) the deadline starts again
// from now: it kept adding 5 ms to a deadline seconds old, and the sink pulled back to back until it caught up, a burst that emptied the game's
// queue. Returns how long to wait before this pull (0 = now); *resyncs counts the restarts. Unit test tests/t_audren_pace.c
static inline uint64_t sink_step(uint64_t *t, uint64_t now, uint64_t period, uint64_t resync, unsigned *resyncs) {
#ifndef DM_TEST_SINK_NO_RESYNC   // negative control of t_audren_pace only: never restarted, as before
    if (now > *t + resync) { *t = now; if (resyncs) ++*resyncs; }
#else
    (void)resync; (void)resyncs;
#endif
    *t += period;
    return *t > now ? *t - now : 0;
}
