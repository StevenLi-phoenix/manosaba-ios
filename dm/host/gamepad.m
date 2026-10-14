// Game controllers (GameController framework, shared by the Mac and iOS front ends) → the Switch controller input (dm_pad_set, dm_input.h).
// Face buttons follow the letter printed on the button, so a game's button prompts match the controller in hand:
//   Nintendo controllers (Switch Pro, Joy-Con): right A, bottom B, top X, left Y — the Switch layout as is.
//   Everything else (Xbox, PlayStation, unknown): bottom A, right B, left X, top Y; controllers without letters get the Xbox positions.
//   GameController reports face buttons by position, not by the printed letter (GCExtendedGamepad.h: buttonA bottom, buttonB right, buttonX left,
//   buttonY top); a remapping the player made in system settings is already applied. That Nintendo controllers are reported by position too comes
//   from the header's convention only, not from a test with a real controller.
// L/R shoulders, ZL/ZR triggers, stick clicks, D-pad; menu button → +, options button → − (controllers without one have no −; the Home button is not
// used: the system may take it first). Sticks are scaled to ±32767, up positive on both sides; the game applies its own dead zone.
// Several controllers: each keeps its own state; buttons are ORed, each stick comes from the controller that pushes it furthest. Disconnecting one
// drops only its share. Focus loss (dm_gamepad_reset): all state is cleared, since a button released in the background never reports its release.
#import <GameController/GameController.h>
#include "dm_input.h"
#include "hle.h"

enum { NSLOT = 6 };   // slots 0 and 1 are the virtual controllers of dm_gamepad_inject, the rest are real controllers
static struct { const void *who; uint64_t b; int32_t ax[4]; } g_s[NSLOT];
static int slot_of(const void *who, int make) {
    intptr_t v = (intptr_t)who; int i0 = 2, i1 = NSLOT;
    if (v == 1 || v == 2) i0 = (int)v - 1, i1 = i0 + 1;
    int fr = -1;
    for (int i = i0; i < i1; i++) { if (g_s[i].who == who) return i; if (fr < 0 && !g_s[i].who) fr = i; }
    if (make && fr >= 0) { memset(&g_s[fr], 0, sizeof g_s[fr]); g_s[fr].who = who; return fr; }
    return -1;
}

static int has_word(const char *s, const char *w) { size_t n = strlen(w); for (; s && *s; s++) if (!strncasecmp(s, w, n)) return 1; return 0; }
static int is_nintendo(GCController *c) {
    static const char *const k[] = { "switch", "nintendo", "joy-con" };
    for (unsigned i = 0; i < sizeof k / sizeof *k; i++) if (has_word(c.productCategory.UTF8String, k[i]) || has_word(c.vendorName.UTF8String, k[i])) return 1;
    return 0;
}
static int32_t axis(float v) { if (v > 1) v = 1; if (v < -1) v = -1; return (int32_t)lroundf(v * DM_STICK_MAX); }

// Merge every controller and hand the result to the host; recomputed whenever one changes
static void apply(void) {
    uint64_t b = 0; int32_t ax[4] = {0}; double best[2] = {0};
    for (int i = 0; i < NSLOT; i++) {
        if (!g_s[i].who) continue;
        b |= g_s[i].b;
        for (int k = 0; k < 2; k++) {
            double m = hypot(g_s[i].ax[2 * k], g_s[i].ax[2 * k + 1]);
            if (m > best[k]) { best[k] = m; ax[2 * k] = g_s[i].ax[2 * k]; ax[2 * k + 1] = g_s[i].ax[2 * k + 1]; }
        }
    }
    dm_pad_set(b, ax[0], ax[1], ax[2], ax[3]);
}
static void put(const void *who, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    int i = slot_of(who, 1); if (i < 0) return;
    g_s[i].b = b; g_s[i].ax[0] = lx; g_s[i].ax[1] = ly; g_s[i].ax[2] = rx; g_s[i].ax[3] = ry; apply();
}
static void drop(const void *who) { int i = slot_of(who, 0); if (i < 0) return; memset(&g_s[i], 0, sizeof g_s[i]); apply(); }

void dm_gamepad_reset(void) {
    int any = 0;
    for (int i = 0; i < NSLOT; i++) if (g_s[i].who) {
        any |= g_s[i].b != 0 || g_s[i].ax[0] || g_s[i].ax[1] || g_s[i].ax[2] || g_s[i].ax[3];
        g_s[i].b = 0; memset(g_s[i].ax, 0, sizeof g_s[i].ax);
    }
    if (any) { LOG("gamepad: focus lost, buttons and sticks cleared"); apply(); }
}
void dm_gamepad_inject(int n, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { put((const void *)(intptr_t)(n == 2 ? 2 : 1), b, lx, ly, rx, ry); }
void dm_gamepad_unplug(int n) { LOG("gamepad: virtual controller %d unplugged", n == 2 ? 2 : 1); drop((const void *)(intptr_t)(n == 2 ? 2 : 1)); }

static void publish(GCController *c, GCExtendedGamepad *g) {
    int s = g.buttonA.pressed, e = g.buttonB.pressed, w = g.buttonX.pressed, n = g.buttonY.pressed;   // bottom / right / left / top
    uint64_t b = is_nintendo(c) ? (e ? DM_BTN_A : 0) | (s ? DM_BTN_B : 0) | (n ? DM_BTN_X : 0) | (w ? DM_BTN_Y : 0)
                                : (s ? DM_BTN_A : 0) | (e ? DM_BTN_B : 0) | (w ? DM_BTN_X : 0) | (n ? DM_BTN_Y : 0);
    if (g.leftShoulder.pressed) b |= DM_BTN_L; if (g.rightShoulder.pressed) b |= DM_BTN_R;
    if (g.leftTrigger.pressed) b |= DM_BTN_ZL; if (g.rightTrigger.pressed) b |= DM_BTN_ZR;
    if (g.leftThumbstickButton.pressed) b |= DM_BTN_LS; if (g.rightThumbstickButton.pressed) b |= DM_BTN_RS;
    if (g.buttonMenu.pressed) b |= DM_BTN_PLUS; if (g.buttonOptions.pressed) b |= DM_BTN_MINUS;
    if (g.dpad.left.pressed) b |= DM_BTN_LEFT; if (g.dpad.up.pressed) b |= DM_BTN_UP;
    if (g.dpad.right.pressed) b |= DM_BTN_RIGHT; if (g.dpad.down.pressed) b |= DM_BTN_DOWN;
    put((__bridge const void *)c, b, axis(g.leftThumbstick.xAxis.value), axis(g.leftThumbstick.yAxis.value),
        axis(g.rightThumbstick.xAxis.value), axis(g.rightThumbstick.yAxis.value));
}
static void attach(GCController *c) {
    GCExtendedGamepad *g = c.extendedGamepad;
    if (!g) { LOG("gamepad: %s has no extended gamepad profile, ignored", c.vendorName.UTF8String ?: "?"); return; }
    LOG("gamepad: connected %s (category %s), face buttons in %s layout", c.vendorName.UTF8String ?: "?", c.productCategory.UTF8String ?: "?",
        is_nintendo(c) ? "Nintendo (right A, bottom B, top X, left Y)" : "Xbox (bottom A, right B, left X, top Y)");
    __weak GCController *wc = c;
    g.valueChangedHandler = ^(GCExtendedGamepad *gp, GCControllerElement *el) { (void)el; GCController *sc = wc; if (sc) publish(sc, gp); };
}
void dm_gamepad_start(void) {
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserverForName:GCControllerDidConnectNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *n) { attach(n.object); }];
    [nc addObserverForName:GCControllerDidDisconnectNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *n) {
        LOG("gamepad: disconnected %s", ((GCController *)n.object).vendorName.UTF8String ?: "?"); drop((__bridge const void *)n.object); }];
    for (GCController *c in GCController.controllers) attach(c);
}
