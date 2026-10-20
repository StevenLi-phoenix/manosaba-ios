// Where a left-button event on the Mac window goes (front_mac.m's mouse monitor; plan item 127): a press on the game picture is finger 0 on the
// touchscreen until the release; a press on the black bars is nothing (swallowed); a press outside the content view (the title bar: dragging the
// window, double-clicking it, its buttons) goes to AppKit with its drags and release. The monitor swallowed every press outside the picture,
// the title bar's included. Checked by host/tests/t_mouse_route.c
#pragma once
enum { MR_DOWN, MR_DRAG, MR_UP };
enum { MR_TOUCH, MR_SWALLOW, MR_APPKIT };
typedef struct { int appkit; } mouse_route_st;   // the current press began outside the content view
static inline int mouse_route(mouse_route_st *s, int phase, int in_content, int in_game) {
    if (phase == MR_DOWN) {
#ifndef DM_TEST_MOUSE_SWALLOW_TITLE   // negative control of t_mouse_route only: every press outside the picture swallowed, as before
        s->appkit = !in_content;
#else
        s->appkit = 0;
#endif
        return s->appkit ? MR_APPKIT : in_game ? MR_TOUCH : MR_SWALLOW;
    }
    int a = s->appkit; if (phase == MR_UP) s->appkit = 0;
    return a ? MR_APPKIT : MR_TOUCH;   // drags and the release follow the press (a touch lifts on any release)
}
