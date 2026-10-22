// The fps / GPU line (nvn_perfline.h, plan item 106) fed with made-up times: 60 presents a second read 60 fps, then after a switch to 30 a second
// read 30 once a second has passed (the window is the last second, not the run); frames on screen at 60 a second with every other one discarded
// read "on screen 30" beside the game's 60, and a time that is not newer is not counted again; the GPU mean is that of the last 32 frames (32 at
// 8 ms then 32 at 4 ms read 4.0); the ring wraps at 240 Hz and still reads 240; before any frame "no frame yet", before any GPU time "GPU –",
// without screen times no screen part, 3 s after the last present "no frame for 3.0 s". Every time kept since the start
// (-DDM_TEST_PERFLINE_SINCE_START) fails 3. No screen time for 1.5 s of presents (plan item 215: a mirrored iPhone reports none) reads
// "on screen –", and the rate again once frames are shown; the old rate kept (-DDM_TEST_PERFLINE_STALE_SCREEN) fails 1.
#include "../nvn/nvn_perfline.h"
#include <string.h>
static int n, fail;
#define EXPECT(name, want, got) do { n++; int ok_ = !strcmp(want, got); printf("%-72s expected %-34s → got %-34s %s\n", name, want, got, ok_ ? "✓" : "✗"); fail += !ok_; } while (0)
static perfline g_p, g_q, g_s;
int main(void) {
    char s[128]; uint64_t t = 1000000000ull;
    perfline_fmt(&g_p, t, s, sizeof s); EXPECT("before any frame", "no frame yet", s);
    for (int i = 0; i < 120; i++) { t += 16666667; perfline_frame(&g_p, t); }
    perfline_fmt(&g_p, t, s, sizeof s); EXPECT("60 presents a second, no GPU time, no window", "60 fps · GPU –", s);
    for (int i = 0; i < 32; i++) perfline_gpu(&g_p, 8);
    perfline_fmt(&g_p, t, s, sizeof s); EXPECT("with GPU times of 8 ms", "60 fps · GPU 8.0 ms", s);
    for (int i = 0; i < 32; i++) { t += 33333333; perfline_frame(&g_p, t); perfline_gpu(&g_p, 4); }
    perfline_fmt(&g_p, t, s, sizeof s); EXPECT("after a switch to 30 a second, one second on: the last second's rate", "30 fps · GPU 4.0 ms", s);
    perfline_fmt(&g_p, t + 1000000000ull, s, sizeof s); EXPECT("1 s after the last present: still the rate", "30 fps · GPU 4.0 ms", s);
    perfline_fmt(&g_p, t + 3000000000ull, s, sizeof s); EXPECT("3 s after the last present", "no frame for 3.0 s", s);
    uint64_t u = 2000000000ull;
    for (int i = 0; i < 120; i++) { u += 16666667; perfline_frame(&g_s, u); perfline_screen(&g_s, i % 2 ? 0 : u + 5000000); perfline_screen(&g_s, u - 20000000); }
    perfline_fmt(&g_s, u, s, sizeof s); EXPECT("60 presents, every other one discarded by the display, old times again", "60 fps · on screen 30 · GPU –", s);
    for (int i = 0; i < 60; i++) { u += 16666667; perfline_frame(&g_s, u); perfline_screen(&g_s, 0); }
    perfline_fmt(&g_s, u, s, sizeof s); EXPECT("215: then 1 s of presents with every frame discarded: the rate still", "60 fps · on screen 30 · GPU –", s);
    for (int i = 0; i < 60; i++) { u += 16666667; perfline_frame(&g_s, u); perfline_screen(&g_s, 0); }
    perfline_fmt(&g_s, u, s, sizeof s); EXPECT("215: 2 s of them (a mirrored iPhone): not the old rate", "60 fps · on screen – · GPU –", s);
    for (int i = 0; i < 60; i++) { u += 16666667; perfline_frame(&g_s, u); perfline_screen(&g_s, u + 5000000); }
    perfline_fmt(&g_s, u, s, sizeof s); EXPECT("215: frames shown again: the rate", "60 fps · on screen 60 · GPU –", s);
    uint64_t v = 5000000000ull;
    for (int i = 0; i < 1000; i++) { v += 4166667; perfline_frame(&g_q, v); }
    perfline_fmt(&g_q, v, s, sizeof s); EXPECT("240 Hz, the ring wrapped several times", "240 fps · GPU –", s);
    printf("PERFLINE %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
}
