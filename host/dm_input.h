// Front end (keyboard / touchscreen / game controller) → nn::hid input state (hle_hid.c).
#pragma once
#include <stdint.h>
// Button bits = nn::hid::NpadButton bit order (the DM_KEYS key names in hle_hid.c come from these). Front ends compose buttons from these names.
enum {
    DM_BTN_A = 1u << 0, DM_BTN_B = 1u << 1, DM_BTN_X = 1u << 2, DM_BTN_Y = 1u << 3,
    DM_BTN_LS = 1u << 4, DM_BTN_RS = 1u << 5, DM_BTN_L = 1u << 6, DM_BTN_R = 1u << 7,
    DM_BTN_ZL = 1u << 8, DM_BTN_ZR = 1u << 9, DM_BTN_PLUS = 1u << 10, DM_BTN_MINUS = 1u << 11,
    DM_BTN_LEFT = 1u << 12, DM_BTN_UP = 1u << 13, DM_BTN_RIGHT = 1u << 14, DM_BTN_DOWN = 1u << 15,
};
#define DM_STICK_MAX 32767   // sticks are ±32767, up is positive
// The sticks as direction buttons (plan item 124): NpadButton StickLLeft / Up / Right / Down = bits 16-19, StickRLeft / Up / Right / Down = 20-23
// (the SDK's NpadButton). The console sets them from the stick's position; its dead zone is the circle of radius AnalogStickMax / 2 (the SDK's
// changelog). Outside it a direction is set when that axis is at half its range or more, blade2-mobile 34755e9's rule (MIT; UE4's "stick as
// button" mappings only see these bits: Blade II's first tutorial card never closed without them). Whether the console also sets one on a
// diagonal outside the circle with both axes under half (radius 16,384 to 23,170) is not documented: not checked on a console. Unit test
// host/tests/t_stickdir.c
enum { DM_BTN_STICK_L_LEFT = 1u << 16, DM_BTN_STICK_L_UP = 1u << 17, DM_BTN_STICK_L_RIGHT = 1u << 18, DM_BTN_STICK_L_DOWN = 1u << 19,
       DM_BTN_STICK_R_LEFT = 1u << 20, DM_BTN_STICK_R_UP = 1u << 21, DM_BTN_STICK_R_RIGHT = 1u << 22, DM_BTN_STICK_R_DOWN = 1u << 23 };
static inline uint64_t dm_stick_dirs(int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
#ifndef DM_TEST_STICK_NO_DIRS   // negative control of t_stickdir only: none, as before
    const int32_t h = (DM_STICK_MAX + 1) / 2;   // 16,384
    return (lx <= -h ? DM_BTN_STICK_L_LEFT : 0) | (ly >= h ? DM_BTN_STICK_L_UP : 0) | (lx >= h ? DM_BTN_STICK_L_RIGHT : 0) | (ly <= -h ? DM_BTN_STICK_L_DOWN : 0) |
           (rx <= -h ? DM_BTN_STICK_R_LEFT : 0) | (ry >= h ? DM_BTN_STICK_R_UP : 0) | (rx >= h ? DM_BTN_STICK_R_RIGHT : 0) | (ry <= -h ? DM_BTN_STICK_R_DOWN : 0);
#else
    (void)lx; (void)ly; (void)rx; (void)ry; return 0;
#endif
}
// Keyboard / on-screen controls of the front end
void dm_input_set(uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
// Game controller (dm_gamepad.m): kept apart from dm_input_set; buttons are ORed and a stick is used when the front end's is at rest
void dm_pad_set(uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
// ios/Sources/dm_gamepad.m (GameController, Mac and iOS), main thread: start watching controllers; clear all held state on focus loss;
// virtual controllers 1 / 2 for scripted tests (inject sets the state, unplug removes it)
void dm_gamepad_start(void);
void dm_gamepad_reset(void);
void dm_gamepad_inject(int n, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
void dm_gamepad_unplug(int n);
uint64_t dm_input_samples(void);   // how many times the game has read a controller (a front end can tell whether a press was seen)
// DM_HID_RECEIPT_LOG=<JSONL path>: log of what GetNpadState really returned (see hle_hid.c)
