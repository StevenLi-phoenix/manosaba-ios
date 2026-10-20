// The game's vibration values (hle_motion.c, nn::hid::VibrationValue: {amplitude low, frequency low, amplitude high, frequency high}) as the
// host's haptics play them (plan item 67c; host/gamepad.m). Pure functions, tested by host/tests/t_haptics.c.
//   A controller's CoreHaptics continuous event: CoreHaptics has no two-band control (dr3-mobile's notes, MIT): intensity from the larger band's
//   amplitude through (a + sqrt(a)) / 2, atone/nxnative's lift for weak values (MIT; the Switch's actuators make 0.1 a firm landing, plain
//   CoreHaptics players hardly at all), sharpness from the amplitude-weighted frequencies' place between the HD rumble bands' ends (40.875 and
//   1252.572 Hz, dekuNukem's rumble_data_table, on a log scale). Both to calibrate on a controller (device check).
//   The phone's own haptics (iOS, no controller): single pulses, rate-limited as acnh-mobile does (MIT; written anew): a value under 0.03 is
//   none; pulses at least 120 ms apart and at most 8 in a burst (a burst ends 1.5 s after it began or 200 ms after the last value), the
//   strongest value since the last pulse.
#pragma once
#include <math.h>
#include <stdint.h>

static inline float dm_haptic_gain(float a) { if (!(a > 0)) return 0; if (a > 1) a = 1; return (a + sqrtf(a)) * 0.5f; }
// v: one side's value; intensity 0-1 (0: stop), sharpness 0-1
static inline void dm_haptic_level(const float v[4], float *intensity, float *sharpness) {
    float al = v[0] > 0 ? v[0] : 0, ah = v[2] > 0 ? v[2] : 0, m = al > ah ? al : ah;
    *intensity = dm_haptic_gain(m);
    const float lo = log2f(40.875f), span = log2f(1252.572f) - lo;
    float pl = v[1] > 0 ? (log2f(v[1]) - lo) / span : 0.5f, ph = v[3] > 0 ? (log2f(v[3]) - lo) / span : 0.5f;
    float s = al + ah > 0 ? (al * pl + ah * ph) / (al + ah) : 0.5f;
    *sharpness = s < 0 ? 0 : s > 1 ? 1 : s;
}

typedef struct { float peak; uint64_t last_signal, burst_start, last_pulse; unsigned pulses; } dm_pulse_lim;
enum { DM_PULSE_GAP_NS = 120000000, DM_PULSE_STALE_NS = 200000000, DM_PULSE_BURST_NS = 1500000000, DM_PULSE_BURST_MAX = 8 };
// A value seen (peak: the larger amplitude of both sides) at now
static inline void dm_pulse_note(dm_pulse_lim *s, float peak, uint64_t now) {
    if (!(peak >= 0.03f)) return;
    if (!s->last_signal || now < s->last_signal || now - s->last_signal > DM_PULSE_STALE_NS) { s->burst_start = now; s->pulses = 0; }
    s->last_signal = now; if (peak > s->peak) s->peak = peak > 1 ? 1 : peak;
}
// A pulse's strength now, or 0
static inline float dm_pulse_take(dm_pulse_lim *s, uint64_t now) {
    if (!(s->peak > 0)) return 0;
    if (now < s->last_signal || now - s->last_signal > DM_PULSE_STALE_NS || now - s->burst_start > DM_PULSE_BURST_NS || s->pulses >= DM_PULSE_BURST_MAX) { s->peak = 0; return 0; }
#ifndef DM_TEST_PULSE_NO_GAP   // negative control of t_haptics only: pulses as often as asked
    if (s->last_pulse && now >= s->last_pulse && now - s->last_pulse < DM_PULSE_GAP_NS) return 0;
#endif
    float p = s->peak; s->peak = 0; s->last_pulse = now; s->pulses++; return p;
}
