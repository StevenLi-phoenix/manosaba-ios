// Named time-zone rules (hle_time.c LoadTimeZoneRule and the rule overloads, plan item 64k), with the host's own zone UTC so the rule alone decides,
// on t_time's cases (acnh-mobile's dm/host/tests/t_timezone.c, MIT, as data): the calendar, weekday, day of the year, offset and daylight time of
// instants in Shanghai, Tokyo, Kathmandu (+5:45), New York and Sydney in winter and summer, and back to the same instant; New York's 01:30 on
// 2026-11-01 twice gives both instants in order, with room for one the earlier; 02:30 on 2026-03-08, skipped, none; February 30 none with success;
// an unknown name converts as UTC; GetDeviceLocationName "UTC" under DM_FIXED_TIME. The rule ignored for the device zone (-DDM_TEST_TZRULE_DEVICE):
// 7 fail (every zone but UTC).
#include "../hle_time.c"
#include <stdio.h>
struct dm_flags g_flag; uint64_t g_t0_ns;
module_t g_mod[MAX_MOD]; int g_nmod;
uint64_t guest_ns(void) { return 0; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static _Alignas(4096) uint8_t g_rule[0x4000];
static void load(const char *name) { char loc[0x24] = {0}; strlcpy(loc, name, sizeof loc); h_time_LoadRule(g_rule, loc); }
int main(void) {
    setenv("TZ", "UTC", 1); tzset(); setenv("DM_FIXED_TIME", "1792368600", 1);
    static const struct { const char *z; int64_t t; int y, mo, d, h, mi, off, wday, yday, dst; } c[] = {
        {"Asia/Shanghai", 1792367400, 2026, 10, 19, 7, 50, 28800, 1, 291, 0}, {"Asia/Tokyo", 1767297600, 2026, 1, 2, 5, 0, 32400, 5, 1, 0},
        {"Asia/Kathmandu", 1780281000, 2026, 6, 1, 8, 15, 20700, 1, 151, 0},
        {"America/New_York", 1767268800, 2026, 1, 1, 7, 0, -18000, 4, 0, 0}, {"America/New_York", 1782907200, 2026, 7, 1, 8, 0, -14400, 3, 181, 1},
        {"Australia/Sydney", 1767268800, 2026, 1, 1, 23, 0, 39600, 4, 0, 1}, {"Australia/Sydney", 1782907200, 2026, 7, 1, 22, 0, 36000, 3, 181, 0}};
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        load(c[i].z); CalendarTime ct; CalendarAdditionalInfo ai; int64_t out[2] = {0}; int32_t cnt = -1;
        Result r1 = h_time_ToCalRule(&ct, &ai, &c[i].t, g_rule), r2 = h_time_ToPosixRule(&cnt, out, 2, &ct, g_rule);
        CHECK(!r1 && ct.year == c[i].y && ct.month == c[i].mo && ct.day == c[i].d && ct.hour == c[i].h && ct.minute == c[i].mi && ai.offset == c[i].off && ai.wday == c[i].wday
              && ai.yday == c[i].yday && ai.dst == (uint32_t)c[i].dst && !r2 && cnt == 1 && out[0] == c[i].t,
              "%-16s %lld: %04d-%02d-%02d %02d:%02d, offset %d, daylight %u, weekday %d, day %d, and back", c[i].z, (long long)c[i].t, ct.year, ct.month, ct.day, ct.hour, ct.minute, ai.offset, ai.dst, ai.wday, ai.yday);
    }
    load("America/New_York"); int64_t out[2] = {0}; int32_t cnt = -1;
    CalendarTime fall = {2026, 11, 1, 1, 30, 0, 0}, spring = {2026, 3, 8, 2, 30, 0, 0}, feb30 = {2026, 2, 30, 12, 0, 0, 0};
    CHECK(!h_time_ToPosixRule(&cnt, out, 2, &fall, g_rule) && cnt == 2 && out[0] == 1793511000LL && out[1] == 1793514600LL, "New York 2026-11-01 01:30, twice: both instants in order (%d)", cnt);
    CHECK(!h_time_ToPosixRule(&cnt, out, 1, &fall, g_rule) && cnt == 1 && out[0] == 1793511000LL, "... room for one: the earlier");
    CHECK(!h_time_ToPosixRule(&cnt, out, 2, &spring, g_rule) && cnt == 0, "New York 2026-03-08 02:30, skipped: none");
    CHECK(!h_time_ToPosixRule(&cnt, out, 2, &feb30, g_rule) && cnt == 0, "February 30: none, with success (the SDK)");
    load("Nowhere/Atlantis"); CalendarTime ct; int64_t t = 1792368600;
    CHECK(!h_time_ToCalRule(&ct, NULL, &t, g_rule) && ct.hour == 0 && ct.minute == 10, "an unknown location: converted as UTC (00:10)");
    char loc[0x24]; memset(loc, 0x55, sizeof loc); h_time_DevLoc(loc);
    CHECK(!strcmp(loc, "UTC"), "GetDeviceLocationName under DM_FIXED_TIME: UTC");
    printf("t_tzrule: %d checks, %d failed\n", n, fail); return fail != 0;
}
