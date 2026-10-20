// A per-event trace of a window of frames, for the critical path of each (plan item 61c; the idea from totk-mobile host/timeline.c, Apache-2.0,
// written anew): DM_NVN_TRACE=<first frame>+<frames>:<path> records from the first frame's start for that many frames, then writes a TSV
// (t0_ns, t1_ns, kind, thread, a, b; nanoseconds on CLOCK_UPTIME_RAW, the base of Metal's GPU and presented times). tools/trace_report.py reads it.
//   submit   nvnQueueSubmitCommands            a lists, b the submission's fence
//   commit   a part committed to Metal         a the submission's fence, b the part (1.. in order; the last is the submission's commit)
//   gpu      that part on the GPU              a fence, b part
//   wait     nvnSyncWait                       a the fence waited for, b the result
//   fence    nvnQueueFenceSync (an instant)    a fence
//   acquire  nvnWindowAcquireTexture           a the texture index, b the nanoseconds it waited for the texture to be free
//   present  nvnQueuePresentTexture            a the texture index, b the frame
//   display  the drawable on screen (instant)  a the present's ticket
// Off, a hook costs one relaxed load (trace_now() returns 0 and the span is not taken).
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum { TR_SUBMIT = 1, TR_COMMIT, TR_GPU, TR_WAIT, TR_FENCE, TR_ACQUIRE, TR_PRESENT, TR_DISPLAY, TR_NKIND };
typedef struct { uint64_t t0, t1, a, b, thread; uint32_t kind; } trace_ev;
extern _Atomic int g_trace_on;
static inline uint64_t trace_now(void) { return atomic_load_explicit(&g_trace_on, memory_order_relaxed) ? clock_gettime_nsec_np(CLOCK_UPTIME_RAW) : 0; }
void trace_span(unsigned kind, uint64_t t0, uint64_t t1, uint64_t a, uint64_t b);   // t0 = 0 (taken while off): skipped
static inline void trace_end(unsigned kind, uint64_t t0, uint64_t a, uint64_t b) { if (t0) trace_span(kind, t0, clock_gettime_nsec_np(CLOCK_UPTIME_RAW), a, b); }
// Metal's GPU and presented times (seconds on the same base) as a span, while recording
static inline void trace_at(unsigned kind, double s0, double s1, uint64_t a, uint64_t b) {
    if (atomic_load_explicit(&g_trace_on, memory_order_relaxed) && s0 > 0 && s1 >= s0) trace_span(kind, (uint64_t)(s0 * 1e9), (uint64_t)(s1 * 1e9), a, b);
}
// "<first>+<frames>:<path>": 1 and the parts, else 0 (frames 1..1000000, a path)
int trace_parse(const char *spec, uint64_t *first, uint64_t *frames, char *path, size_t n);
// At each frame's start (hle_nvn.c, frame = the new frame number): starts at the first frame, stops and writes after the last; 1 when it wrote the file
int trace_frame(uint64_t frame);
// Direct control (tests): start with room for cap events; stop and write (1 written, 0 not recording or the file failed)
int trace_start(uint64_t cap);
int trace_stop(const char *path, uint64_t first, uint64_t last);
