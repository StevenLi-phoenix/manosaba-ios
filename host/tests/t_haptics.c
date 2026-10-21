// The vibration values as the host's haptics play them (host/haptics_map.h, plan item 67c): the gain 0 → 0, 1 → 1, rising, the landing amplitude
// 0.1 → 0.208; a side's intensity from its larger band, its sharpness between the HD rumble bands' ends (the lowest 0, the highest 1, the defaults
// 160 / 320 Hz in between, weighted by amplitude); the phone's pulses: none under 0.03, at least 120 ms apart, at most 8 in a burst, none from a
// value older than 200 ms. Pulses as often as asked (-DDM_TEST_PULSE_NO_GAP): 1 fails.
// The phone's engine: made only for a pulse due in focus, not out of focus or in the background; after failed starts not before 2, 4, 8, 16, then
// 30 s; a start clears that. Made whatever the focus (-DDM_TEST_PHONE_ENG_ANY_FOCUS): 1 fails.
#include "../haptics_map.h"
#include <stdio.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int near(float a, float b, float t) { return fabsf(a - b) <= t; }
int main(void) {
    int up = 1; float prev = 0; for (int i = 0; i <= 1000; i++) { float g = dm_haptic_gain(i / 1000.f); up &= g >= prev && g <= 1; prev = g; }
    check("gain: 0 → 0, 1 → 1, rising in between", dm_haptic_gain(0) == 0 && near(dm_haptic_gain(1), 1, 1e-6f) && up);
    check("gain: the landing amplitude 0.1 → 0.208; not finite or below 0 → 0", near(dm_haptic_gain(0.1f), 0.2081f, 1e-3f) && dm_haptic_gain(NAN) == 0 && dm_haptic_gain(-1) == 0);
    float i, s; float v1[4] = {0.1f, 198.7f, 0, 299.9f}; dm_haptic_level(v1, &i, &s);
    float lo = log2f(40.875f), span = log2f(1252.572f) - lo;
    check("the landing value: intensity from the low band (0.208), sharpness at 198.7 Hz's place", near(i, 0.2081f, 1e-3f) && near(s, (log2f(198.7f) - lo) / span, 1e-4f));
    float v2[4] = {0.2f, 40.875f, 0.6f, 1252.572f}; dm_haptic_level(v2, &i, &s);
    check("two bands: intensity from the larger (0.6), sharpness weighted 1 : 3 toward the highest end (0.75)", near(i, dm_haptic_gain(0.6f), 1e-6f) && near(s, 0.75f, 1e-4f));
    float v3[4] = {0, 160, 0, 320}; dm_haptic_level(v3, &i, &s);
    check("silence: intensity 0, sharpness 0.5", i == 0 && s == 0.5f);
    dm_pulse_lim L = {0}; uint64_t t = 1000000000ull, ms = 1000000ull;
    dm_pulse_note(&L, 0.02f, t); int none_low = dm_pulse_take(&L, t) == 0;
    dm_pulse_note(&L, 0.3f, t); float p1 = dm_pulse_take(&L, t); dm_pulse_note(&L, 0.5f, t + 50 * ms); float p2 = dm_pulse_take(&L, t + 50 * ms); float p3 = dm_pulse_take(&L, t + 130 * ms);
    check("pulses: none under 0.03; 0.3 at once; 50 ms later none, at 130 ms the stronger 0.5 kept", none_low && near(p1, 0.3f, 0) && p2 == 0 && near(p3, 0.5f, 0));
    dm_pulse_lim B = {0}; int got = 0; for (int k = 0; k < 14; k++) { uint64_t at = t + (uint64_t)k * 100 * ms; dm_pulse_note(&B, 0.4f, at); got += dm_pulse_take(&B, at + 30 * ms) > 0; }
    check("a 1.4 s burst of values every 100 ms: at most 8 pulses", got <= 8 && got >= 6);
    dm_pulse_lim O = {0}; dm_pulse_note(&O, 0.4f, t); int stale = dm_pulse_take(&O, t + 250 * ms) == 0;
    check("a value 250 ms old: no pulse", stale);
    dm_phone_eng E = {0};
    check("the phone's engine: not made out of focus (2) or in the background (3), even for a pulse", !dm_phone_engine_due(&E, 0.5f, 2, t) && !dm_phone_engine_due(&E, 0.5f, 3, t));
    check("... not made in focus without a pulse; made in focus for one", !dm_phone_engine_due(&E, 0, 1, t) && dm_phone_engine_due(&E, 0.5f, 1, t));
    int waits_ok = 1; uint64_t at = t; const uint64_t want[] = {2, 4, 8, 16, 30, 30};
    for (int k = 0; k < 6; k++) { dm_phone_engine_failed(&E, at); uint64_t w = want[k] * 1000 * ms;
        waits_ok &= !dm_phone_engine_due(&E, 0.5f, 1, at + w - 1) && dm_phone_engine_due(&E, 0.5f, 1, at + w); at += w; }
    dm_phone_engine_started(&E);
    check("... after failed starts not before 2, 4, 8, 16, 30, 30 s; a start clears the wait", waits_ok && dm_phone_engine_due(&E, 0.5f, 1, 0));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
