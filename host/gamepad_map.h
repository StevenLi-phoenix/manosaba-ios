// Controller face buttons → the Switch buttons (pure, used by gamepad.m, checked by tests/t_padmap.c): the GameController buttonA / B / X / Y go to
// the game's A / B / X / Y as they are, whatever the controller. The system reports controllers with printed letters by the letter on the keycap:
// on a Switch Pro controller or a Joy-Con pair the right button (printed A) is buttonA and the bottom one (printed B) is buttonB, as on the Switch;
// Xbox letters and positions agree anyway (bottom A, right B, left X, top Y); controllers without letters (PlayStation) come by position, bottom =
// buttonA. This used to swap Nintendo controllers by position once more (from GCExtendedGamepad.h's diamond convention, never tried on a real
// controller), which swapped A / B and X / Y on them: upstream d2r-mobile 46cb318 (issue #57, a Switch Pro on iPadOS 27 recognised as Nintendo and
// still swapped; SDL's Apple controller code takes a Nintendo controller's buttonA as the right button) and Kamisorara/d2r-mobile 55c6fba.
// DM_FACE_BUTTONS=position (plan item 149; MacNativeGames 31052d6, MIT, written anew): a controller that is not Nintendo's maps by position, so each
// button sits where the game's Switch prompt shows it: right (the system's buttonB) A, bottom (buttonA) B, top (buttonY) X, left (buttonX) Y. A
// Nintendo controller (its category or vendor naming Nintendo, Switch or Joy-Con) is reported by its printed letters and keeps them. Without the
// switch every controller maps by the system's letters, as before.
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "dm_input.h"
static inline uint64_t dm_gamepad_face(int a, int b, int x, int y) {   // whether the system's buttonA / B / X / Y are pressed
    return (a ? DM_BTN_A : 0) | (b ? DM_BTN_B : 0) | (x ? DM_BTN_X : 0) | (y ? DM_BTN_Y : 0);
}
static inline int dm_names_nintendo(const char *s) {   // case-insensitive: Nintendo, Switch, Joy-Con
    static const char *const w[] = {"nintendo", "switch", "joy-con", "joycon"};
    if (!s) return 0;
    char l[128]; size_t n = 0; for (; s[n] && n < sizeof l - 1; n++) l[n] = (char)tolower((unsigned char)s[n]); l[n] = 0;
    for (size_t i = 0; i < sizeof w / sizeof *w; i++) if (strstr(l, w[i])) return 1;
    return 0;
}
// mode: DM_FACE_BUTTONS; category / vendor: GCController's productCategory / vendorName
static inline int dm_face_by_position(const char *mode, const char *category, const char *vendor) {
#ifndef DM_TEST_FACE_POS_ALL   // negative control of t_padmap only: Nintendo controllers by position too
    if (dm_names_nintendo(category) || dm_names_nintendo(vendor)) return 0;
#endif
    return mode && !strcmp(mode, "position");
}
static inline uint64_t dm_gamepad_face_by(int a, int b, int x, int y, int by_position) {
    if (!by_position) return dm_gamepad_face(a, b, x, y);
    return (b ? DM_BTN_A : 0) | (a ? DM_BTN_B : 0) | (y ? DM_BTN_X : 0) | (x ? DM_BTN_Y : 0);
}
