// A Switch Pro Controller's IMU and HD rumble over IOHID on the Mac (plan item 67b; after atone/nxnative f6dba3d, MIT, written anew).
// GameController reports motion for a Pro Controller but every value stays 0 (MacNativeGames 37d55cb, atone), and its CoreHaptics engines turn the
// game's low-band rumble (about 160 Hz, amplitudes near 0.1) into nothing one can feel. The controller streams its IMU in every standard input report
// and takes HD rumble as the Switch sends it, so its HID device (vendor 0x057e, product 0x2009) is opened alongside the system driver: the IMU of
// each report 0x30 becomes the six-axis sensor's samples (procon.h → dm_motion_set), and the game's vibration goes out as output report 0x10 every
// 15 ms while it is on or just changed, a few silent ones after (gamepad.m's haptics tick hands the values over through dm_pad_hd_sink and leaves
// that controller's CoreHaptics engines alone). The IMU is switched on first (output report 0x01, subcommand 0x40 with 1; on already under macOS).
// DM_PROCON=0 leaves it all to GameController.
#import <Foundation/Foundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <os/lock.h>
#include "dm_input.h"
#include "haptics_map.h"
#include "procon.h"
#include "hle.h"

extern void (*dm_pad_hd_sink)(const float v[2][4]);   // gamepad.m
void dm_gamepad_haptics_on(void);
static IOHIDManagerRef g_mgr;
static IOHIDDeviceRef g_dev; static os_unfair_lock g_lk = OS_UNFAIR_LOCK_INIT;
static uint8_t g_buf[512]; static uint8_t g_cnt; static int g_keep; static uint8_t g_last[8];
static uint64_t g_reports;

static IOHIDDeviceRef dev_take(void) { os_unfair_lock_lock(&g_lk); IOHIDDeviceRef d = g_dev; if (d) CFRetain(d); os_unfair_lock_unlock(&g_lk); return d; }
// The haptics queue (gamepad.m), every 15 ms: both motors' values as HD rumble
static void hd_sink(const float v[2][4]) {
    uint8_t r[10] = {0x10, 0};
    for (int s = 0; s < 2; s++) { float m[4] = {dm_haptic_gain(v[s][0]), v[s][1], dm_haptic_gain(v[s][2]), v[s][3]}; procon_hd_motor(m, r + 2 + 4 * s); }
    int on = memcmp(r + 2, "\x00\x01\x40\x40\x00\x01\x40\x40", 8) != 0, changed = memcmp(r + 2, g_last, 8) != 0;
    if (!on && !changed && g_keep <= 0) return;
    g_keep = on ? 4 : g_keep - 1;   // after the last change to none, a few more silent packets
    memcpy(g_last, r + 2, 8); r[1] = (uint8_t)(g_cnt++ & 0xf);
    IOHIDDeviceRef d = dev_take(); if (!d) return;
    IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 0x10, r, sizeof r);
    CFRelease(d);
}
static void on_report(void *ctx, IOReturn res, void *sender, IOHIDReportType type, uint32_t id, uint8_t *rep, CFIndex len) {
    (void)ctx; (void)res; (void)sender; (void)type; (void)id;
    float a[3], g[3]; if (!procon_imu(rep, len, a, g)) return;
    dm_motion_set(a, g, now_ns());
    if (!g_reports++) LOG("Pro Controller: its IMU from the input reports 0x30 (acceleration %.2f %.2f %.2f G)", a[0], a[1], a[2]);
}
static void on_match(void *ctx, IOReturn res, void *sender, IOHIDDeviceRef d) {
    (void)ctx; (void)res; (void)sender;
    if (g_dev) return;   // one controller
    if (IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { LOG("[red] Pro Controller: its HID device did not open: motion and HD rumble stay GameController's"); return; }
    os_unfair_lock_lock(&g_lk); g_dev = (IOHIDDeviceRef)CFRetain(d); os_unfair_lock_unlock(&g_lk);
    IOHIDDeviceRegisterInputReportCallback(d, g_buf, sizeof g_buf, on_report, NULL);
    uint8_t r[49] = {0x01, (uint8_t)(g_cnt++ & 0xf), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x40, 0x01};   // subcommand 0x40: IMU on
    IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 0x01, r, sizeof r);
    memcpy(g_last, "\x00\x01\x40\x40\x00\x01\x40\x40", 8); g_keep = 0;
    dm_pad_hd_sink = hd_sink; dm_gamepad_haptics_on();
    LOG("Pro Controller: its HID device open alongside the system driver: IMU and HD rumble from here");
}
static void on_remove(void *ctx, IOReturn res, void *sender, IOHIDDeviceRef d) {
    (void)ctx; (void)res; (void)sender;
    if (d != g_dev) return;
    dm_pad_hd_sink = NULL;
    os_unfair_lock_lock(&g_lk); IOHIDDeviceRef old = g_dev; g_dev = NULL; os_unfair_lock_unlock(&g_lk);
    if (old) { IOHIDDeviceRegisterInputReportCallback(old, g_buf, sizeof g_buf, NULL, NULL); CFRelease(old); }
    dm_motion_lost(); g_reports = 0;
    LOG("Pro Controller: gone (the six-axis sensor not connected)");
}
void dm_procon_start(void) {   // main thread
    const char *e = getenv("DM_PROCON"); if ((e && !strcmp(e, "0")) || g_mgr) return;
    g_mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    NSDictionary *match = @{@kIOHIDVendorIDKey: @0x057E, @kIOHIDProductIDKey: @0x2009};
    IOHIDManagerSetDeviceMatching(g_mgr, (__bridge CFDictionaryRef)match);
    IOHIDManagerRegisterDeviceMatchingCallback(g_mgr, on_match, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(g_mgr, on_remove, NULL);
    IOHIDManagerScheduleWithRunLoop(g_mgr, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    IOHIDManagerOpen(g_mgr, kIOHIDOptionsTypeNone);
}
