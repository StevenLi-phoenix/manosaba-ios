// The Mac window's left-button routing (mouse_route.h, plan item 127): a press on the picture, its drag and release are the touch; a press on the
// black bars is swallowed (and its release still lifts any touch); a press on the title bar, its drag and release go to AppKit; the next press on
// the picture is a touch again. Every press outside the picture swallowed (-DDM_TEST_MOUSE_SWALLOW_TITLE, as before): 3 fail.
#include "../mouse_route.h"
#include <stdio.h>
static int n, fail;
static void check(const char *name, int want, int got) { static const char *nm[] = {"touch", "swallow", "AppKit"}; n++; fail += want != got; printf("%-52s %-7s %s\n", name, nm[got], want == got ? "✓" : "✗"); }
int main(void) {
    mouse_route_st s = {0};
    check("press on the picture", MR_TOUCH, mouse_route(&s, MR_DOWN, 1, 1));
    check("... drag", MR_TOUCH, mouse_route(&s, MR_DRAG, 1, 0));
    check("... release", MR_TOUCH, mouse_route(&s, MR_UP, 1, 1));
    check("press on a black bar", MR_SWALLOW, mouse_route(&s, MR_DOWN, 1, 0));
    check("... release (lifts any touch)", MR_TOUCH, mouse_route(&s, MR_UP, 1, 0));
    check("press on the title bar", MR_APPKIT, mouse_route(&s, MR_DOWN, 0, 0));
    check("... drag (moving the window)", MR_APPKIT, mouse_route(&s, MR_DRAG, 0, 0));
    check("... release", MR_APPKIT, mouse_route(&s, MR_UP, 1, 1));
    check("next press on the picture: a touch again", MR_TOUCH, mouse_route(&s, MR_DOWN, 1, 1));
    printf("t_mouse_route: %d checks, %d failed\n", n, fail); return fail != 0;
}
