// DM_SDK_TEST=time[,font] (plan item 147; after totk-mobile's tests/test_time_sdk.c and test_font_sdk.c, Apache-2.0, written anew): checks that run
// the game's own SDK code against the host, so they need the game (not CI). At the 60th present the host starts a thread through the SDK's own
// nn::os::CreateThread (hle_guestthread.c), calls by name what the game's own calls reach (the host's take-over where there is one, else the SDK's code) from it, writes "[sdk-test]" lines and exits: 0 when every check
// passed, 1 otherwise (the run ends there: nothing is saved). A check of something the game does not have is skipped and says so.
//   time: nn::time::Initialize; StandardUserSystemClock and StandardNetworkSystemClock::GetCurrentTime within a second of the host's clock;
//         ToCalendarTime of now equal to the host's localtime_r, its UTC offset too; the SDK libc's time(), its own path to the clock.
//   font: nn::pl shared font 0 loaded, a TrueType header at its address; where the game has the SDK's font engine (nn::fontll: Tears of the
//         Kingdom), the engine loads that font and draws 'A' at 30 px without an error.
// DM_TEST_SDK_TIME_SKEW=1 (hle_time.c, the negative control): the host's user clock an hour ahead; the user and network clock checks must fail.
#include "hle.h"
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack_size);
void *dm_sym_bound(const char *name);   // loader.c: what the game's calls to name reach (a host take-over, or the SDK's code)
void dm_guest_thread_join(void *tt);

static int g_n, g_fail;
static void check(const char *group, const char *what, int ok, const char *fmt, ...) {
    char d[200] = ""; va_list ap; va_start(ap, fmt); vsnprintf(d, sizeof d, fmt, ap); va_end(ap);
    g_n++; g_fail += !ok;
    LOG("[sdk-test] %s: %s %s%s%s", group, what, ok ? "✓" : "✗ (", ok ? "" : d, ok ? "" : ")");
}
static void skip(const char *group, const char *what) { LOG("[sdk-test] %s: %s: skipped (not in this game)", group, what); }

typedef struct { int16_t year; int8_t month, day, hour, minute, second; int8_t pad; } cal_t;
typedef struct { uint32_t wday, yday; char tzname[8]; uint32_t dst; int32_t offset; } cal_info_t;
static void t_time(void) {
    Result (*init)(void) = dm_sym_bound("_ZN2nn4time10InitializeEv");
    if (!init) { skip("time", "nn::time"); return; }
    check("time", "nn::time::Initialize", init() == 0, "failed");
    static const char *const clocks[2][2] = {{"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", "the user clock"},
                                             {"_ZN2nn4time26StandardNetworkSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", "the network clock"}};
    for (int i = 0; i < 2; i++) {
        Result (*get)(int64_t *) = dm_sym_bound(clocks[i][0]);
        if (!get) { skip("time", clocks[i][1]); continue; }
        int64_t t = -1, before = time(NULL); Result r = get(&t); int64_t after = time(NULL);
        check("time", clocks[i][1], r == 0 && t >= before - 1 && t <= after + 1, "result %#x, %lld against the host's %lld", r, (long long)t, (long long)after);
    }
    Result (*cal)(cal_t *, cal_info_t *, const int64_t *) = dm_sym_bound("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE");
    if (cal) {
        int64_t now = time(NULL); time_t s = (time_t)now; struct tm lt; localtime_r(&s, &lt);
        cal_t c = {0}; cal_info_t ci = {0}; Result r = cal(&c, &ci, &now);
        check("time", "ToCalendarTime of now", r == 0 && c.year == lt.tm_year + 1900 && c.month == lt.tm_mon + 1 && c.day == lt.tm_mday && c.hour == lt.tm_hour
              && c.minute == lt.tm_min && c.second == lt.tm_sec && ci.offset == lt.tm_gmtoff, "%d-%02d-%02d %02d:%02d:%02d UTC%+d against %d-%02d-%02d %02d:%02d:%02d UTC%+ld",
              c.year, c.month, c.day, c.hour, c.minute, c.second, ci.offset, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, lt.tm_gmtoff);
    } else skip("time", "ToCalendarTime");
    int64_t (*libc_time)(int64_t *) = dm_sym_bound("time");
    if (libc_time) { int64_t before = time(NULL), t = libc_time(NULL), after = time(NULL);
        check("time", "the SDK libc's time()", t >= before - 1 && t <= after + 1, "%lld against the host's %lld", (long long)t, (long long)after); }
    else skip("time", "the SDK libc's time()");
}

static void t_font(void) {
    int (*state)(int) = dm_sym_bound("_ZN2nn2pl22GetSharedFontLoadStateENS0_14SharedFontTypeE");
    const uint8_t *(*addr)(int) = dm_sym_bound("_ZN2nn2pl20GetSharedFontAddressENS0_14SharedFontTypeE");
    size_t (*size)(int) = dm_sym_bound("_ZN2nn2pl17GetSharedFontSizeENS0_14SharedFontTypeE");
    if (!state || !addr || !size) { skip("font", "nn::pl shared fonts"); return; }
    const uint8_t *f = addr(0); size_t n = size(0);
    check("font", "shared font 0 loaded with a TrueType header", state(0) == 1 && f && n > 12 && ((f[0] == 0 && f[1] == 1 && f[2] == 0 && f[3] == 0) || !memcmp(f, "OTTO", 4)),
          "state %d, %zu bytes, header %02x%02x%02x%02x", state(0), n, f ? f[0] : 0, f ? f[1] : 0, f ? f[2] : 0, f ? f[3] : 0);
    void (*ctor)(void *) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngineC1Ev");
    uint32_t (*init)(void *, void *, uint32_t) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine10InitializeEPvj");
    uint32_t (*load)(void *, char *, const void *, uint32_t, uint32_t) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine8LoadFontEPcPKvjj");
    uint32_t (*set)(void *, char *) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine7SetFontEPc");
    uint32_t (*scale)(void *, int, int, int, int) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine8SetScaleEiiii");
    uint32_t (*exists)(void *, uint32_t) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine15CheckGlyphExistEj");
    void *(*acquire)(void *, uint32_t, uint16_t) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine15AcquireGlyphmapEjt");
    uint32_t (*error)(void *) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine8GetErrorEv");
    void (*release)(void *, void *) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine13ReleasesGlyphEPv");
    uint32_t (*fin)(void *) = dm_sym_bound("_ZN2nn6fontll18ScalableFontEngine8FinalizeEv");
    if (!ctor || !init || !load || !set || !scale || !exists || !acquire || !error || !release || !fin) { skip("font", "the SDK font engine (nn::fontll)"); return; }
    _Alignas(16) uint8_t engine[0x100] = {0}; void *work = calloc(1, 512 << 10); char name[256] = "";
    ctor(engine);
    uint32_t ri = init(engine, work, 512 << 10), rl = ri ? 1 : load(engine, name, f, 0, 50), rs = rl ? 1 : set(engine, name);
    check("font", "the SDK font engine loads shared font 0", !ri && !rl && !rs, "Initialize %#x, LoadFont %#x, SetFont %#x", ri, rl, rs);
    if (!ri && !rl && !rs) {
        uint32_t rc = scale(engine, 30 << 16, 0, 0, 30 << 16), has = exists(engine, 'A'); void *g = has ? acquire(engine, 'A', 8) : NULL; uint32_t e = error(engine);
        check("font", "... and draws 'A' at 30 px", !rc && has && g && !e, "SetScale %#x, exists %u, glyph %p, error %#x", rc, has, g, e);
        if (g) release(engine, g);
    }
    fin(engine); free(work);
}

static void run(void *arg) {
    const char *which = arg;
    if (strstr(which, "time")) t_time();
    if (strstr(which, "font")) t_font();
}
void dm_sdk_test_present(uint64_t frame) {   // hle_nvn.c, every present on the game's thread
    static const char *which; static int init;
    if (!init) { init = 1; which = getenv("DM_SDK_TEST"); if (which) LOG("[sdk-test] DM_SDK_TEST=%s: checks at the 60th present, then the run ends", which); }
    if (!which || frame != 60) return;
    void *tt = dm_guest_thread_spawn(run, (void *)which, 44, 256 << 10);
    dm_guest_thread_join(tt);
    LOG("[sdk-test] %d checks, %d failed", g_n, g_fail);
    fflush(stderr); _exit(g_fail || !g_n ? 1 : 0);
}
