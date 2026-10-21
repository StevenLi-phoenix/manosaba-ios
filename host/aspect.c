// The wide picture's state (plan item 154; host/aspect.h): a game that can widen its cameras calls dm_aspect_init at boot; dm_aspect_now is the
// ratio in force, 0 for the game's own. DM_ASPECT=<w>:<h>, <w>x<h> or a ratio (1 to 4) starts with it on at that ratio, "window" at the
// window's shape, following it while on; unset, the Mac's View → Wide Picture (⌘4, dm_aspect_toggle) switches 21:9 on and off. The front end
// is told of each change (dm_aspect_on_change). Mac only: on iOS DM_ASPECT is refused in red and the game keeps its own ratio.
#include "dm.h"
#include "aspect.h"
#include <TargetConditionals.h>
#include <stdatomic.h>

static _Atomic uint32_t g_now;           // float bits of the ratio in force, 0 = the game's own
static _Atomic int g_avail;              // a game that can widen called dm_aspect_init (and not on iOS)
static double g_wide = 21.0 / 9;         // the ratio switched to
static int g_follow;                     // DM_ASPECT=window: the ratio follows the window's shape
static _Atomic double g_view = 16.0 / 9; // the window's shape (dm_aspect_view)
static void (*g_cb)(void);

static float now_f(void) { uint32_t b = atomic_load(&g_now); float f; memcpy(&f, &b, sizeof f); return f; }
static void set_now(float f) {
    uint32_t b; memcpy(&b, &f, sizeof b);
    if (atomic_exchange(&g_now, b) != b && g_cb) g_cb();
}
float dm_aspect_now(void) { return now_f(); }
int dm_aspect_available(void) { return atomic_load(&g_avail); }
void dm_aspect_on_change(void (*cb)(void)) { g_cb = cb; }
void dm_aspect_init(void) {
    const char *e = getenv("DM_ASPECT"); double r; int k = dm_aspect_parse(e, &r);
#if TARGET_OS_IPHONE
    if (k) LOG("[red] DM_ASPECT=%s: the wide picture is Mac only for now; the game's own ratio", e);
#else
    if (k < 0) { LOG("[red] DM_ASPECT=%s: expected <w>:<h>, <w>x<h>, a ratio from 1 to 4, or window; the game's own ratio until View → Wide Picture", e); k = 0; }
    g_follow = r < 0; if (r > 0) g_wide = r;
    atomic_store(&g_avail, 1);
    if (g_follow) LOG("wide picture: the window's shape (DM_ASPECT=window), %s; View → Wide Picture (⌘4) switches", k > 0 ? "on" : "off");
    else LOG("wide picture: %.4f (%s), %s; View → Wide Picture (⌘4) switches", g_wide, r > 0 ? "DM_ASPECT" : "21:9", k > 0 ? "on" : "off");
    if (k > 0) set_now((float)(g_follow ? atomic_load(&g_view) : g_wide));
#endif
}
int dm_aspect_toggle(void) {   // returns 0 where no game offers it
    if (!atomic_load(&g_avail)) return 0;
    float next = now_f() ? 0 : (float)(g_follow ? atomic_load(&g_view) : g_wide);
    set_now(next);
    if (next) LOG("wide picture on: %.4f", next); else LOG("wide picture off: the game's own ratio");
    return 1;
}
void dm_aspect_view(double ratio) {   // the front end, at each layout: the window's shape
    if (!(ratio > 0)) return;
    atomic_store(&g_view, ratio);
#ifndef DM_TEST_ASPECT_NO_FOLLOW   // negative control of t_aspect only: "window" keeps the shape it started with
    if (g_follow && now_f()) set_now((float)ratio);
#endif
}
