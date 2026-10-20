// The sticks as direction buttons (dm_input.h dm_stick_dirs, plan item 124): at rest none; inside the dead zone (the circle of radius 16,384) none;
// each stick pushed fully left / up / right / down sets its own bit (16-19 left stick, 20-23 right stick) and no other; a full diagonal sets two;
// half the range (16,384) sets the direction, just under it not. None at all (-DDM_TEST_STICK_NO_DIRS, as before): 5 fail.
#include "../dm_input.h"
#include <stdio.h>
static int n, fail;
static void check(const char *name, uint64_t want, uint64_t got) { n++; fail += want != got; printf("%-58s %06llx (want %06llx) %s\n", name, (unsigned long long)got, (unsigned long long)want, want == got ? "✓" : "✗"); }
int main(void) {
    check("at rest", 0, dm_stick_dirs(0, 0, 0, 0));
    check("inside the dead zone (12000, 11000)", 0, dm_stick_dirs(12000, 11000, -12000, -11000));
    check("left stick fully left / up / right / down", 0x0f0000, dm_stick_dirs(-32767, 0, 0, 0) | dm_stick_dirs(0, 32767, 0, 0) | dm_stick_dirs(32767, 0, 0, 0) | dm_stick_dirs(0, -32767, 0, 0));
    check("left stick fully left alone: bit 16", DM_BTN_STICK_L_LEFT, dm_stick_dirs(-32767, 0, 0, 0));
    check("right stick fully down alone: bit 23", DM_BTN_STICK_R_DOWN, dm_stick_dirs(0, 0, 0, -32767));
    check("right stick full diagonal up-right: bits 21 and 22", DM_BTN_STICK_R_UP | DM_BTN_STICK_R_RIGHT, dm_stick_dirs(0, 0, 23170, 23170));
    check("half the range (16384) right: set", DM_BTN_STICK_L_RIGHT, dm_stick_dirs(16384, 0, 0, 0));
    check("just under half (16383) right: not", 0, dm_stick_dirs(16383, 0, 0, 0));
    printf("t_stickdir: %d checks, %d failed\n", n, fail);
    return fail != 0;
}
