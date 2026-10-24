// The wide picture (plan item 154, after MacNativeGames' aspect_patch.h, MIT, written anew): a game whose rules can widen its cameras' aspect
// ratio (Tears of the Kingdom's option aspect, games/totk/rules.py) offers it; host/game/aspect.c keeps the ratio in force, which the game hands its
// code (games/totk/frame_step.c), the Mac front end lays the game picture out at (front_mac.m) and the present pass stretches the picture over
// (gfx/nvn/nvn_mtl.m mtl_present_stretch). Pure parts here, tested by host/tests/t_aspect.c.
#pragma once
#include <stdio.h>
#include <string.h>

// DM_ASPECT: "<w>:<h>", "<w>x<h>" or a ratio, from 1 to 4 (*out = the ratio), or "window" (*out = -1: the window's shape). Returns 1 when
// given, 0 unset or "0", -1 refused
static inline int dm_aspect_parse(const char *e, double *out) {
    *out = 0;
    if (!e || !*e || !strcmp(e, "0")) return 0;
    if (!strcmp(e, "window")) { *out = -1; return 1; }
    double a = 0, w, h; char rest;
    if ((sscanf(e, "%lf:%lf%c", &w, &h, &rest) == 2 || sscanf(e, "%lfx%lf%c", &w, &h, &rest) == 2) && w > 0 && h > 0) a = w / h;
    else if (sscanf(e, "%lf%c", &w, &rest) == 1) a = w;
    if (!(a >= 1 && a <= 4)) return -1;
    *out = a; return 1;
}
// The factor on a camera's ratio for the wide ratio `wide` over the game's own `game`: wide / game; 0 (the cave's "off") when wide is 0
static inline float dm_aspect_factor(double wide, double game) { return wide > 0 && game > 0 ? (float)(wide / game) : 0; }

// host/game/aspect.c
void dm_aspect_init(void);                 // a game that can widen, at boot (DM_ASPECT)
float dm_aspect_now(void);                 // the ratio in force, 0 = the game's own
int dm_aspect_available(void);
int dm_aspect_toggle(void);                // View → Wide Picture; 0 where no game offers it
void dm_aspect_view(double ratio);         // the window's shape, at each layout
void dm_aspect_on_change(void (*cb)(void)); // called on each change of dm_aspect_now (any thread)
