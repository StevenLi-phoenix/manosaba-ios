// host/thermal.m (plan item 193): dm_thermal_now is NSProcessInfo's thermal state (0-3), not the weak default -1 the iOS app had. Without
// thermal.m (-DDM_TEST_THERMAL_WEAK_ONLY: the weak default alone, as hle_nvn.c's): 1 fails.
#import <Foundation/Foundation.h>
#include <stdio.h>
#ifdef DM_TEST_THERMAL_WEAK_ONLY
__attribute__((weak)) int dm_thermal_now(void) { return -1; }
#else
int dm_thermal_now(void);
#endif
int main(void) { @autoreleasepool {
    int got = dm_thermal_now(), want = (int)NSProcessInfo.processInfo.thermalState;
    int ok = got == want && got >= 0 && got <= 3;
    printf("dm_thermal_now %d, NSProcessInfo %d %s\n== 1 checks, %d failed ==\n", got, want, ok ? "✓" : "✗", !ok);
    return !ok;
} }
