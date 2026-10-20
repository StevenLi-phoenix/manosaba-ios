// Real-time frame step. Tears of the Kingdom advances its world every frame by a step it derives from the present interval its frame-rate
// controller chose (ratio = 30 / fps: 1 at 30 fps, 0.5 at 60), which is right only while every frame takes exactly that interval. rules.py
// (HOSTREFS) makes the two producers of the step read it from main's host page instead, and this writes the measured frame time there at every
// present, so the game keeps its speed at 60, 90 or 120 fps (DM_PRESENT_HZ) and when a frame misses its interval.
// Method and limits from MacNativeGames' game_patches_v143.c (runtime_frame_step_update; ly05010419/MacNativeGames, MIT): the mean of the last five
// present intervals, each clamped to 1/120 .. 1/15 s; an interval above 0.25 s or below 1 ms (loading, a pause) restarts the mean and keeps the
// step. Intervals are on the guest clock, the one behind the game's own system tick.
// Host page: +0 ratio = seconds × 30, +4 ticks = seconds × 60, written with one 64-bit store so the game never reads the two from different frames;
// +8 seconds, for the cloth and hair clock (rules.py CAVES, plan item 163), written just before (that clock reads +0 and +8 within a few instructions).
// DM_FRAME_STEP_MS=<ms> pins the step (diagnostics: a pinned 16.667 at 90 fps makes the game run 1.5 times too fast).
// +16 / +20: the scene render size as floats (rules.py, plan item 52: main 0x585e48-0x585e60 store it to the renderer instead of the graphics
// parameter's): DM_RENDER_RES=<w>x<h> (64 up to 1920x1080, the output size; 2560x1440 and 3200x1800 ran the game's texture heap out at main+0x1c2f20
// in its first seconds), else the parameter's own size for the operation mode, which a probe at main 0x585e4c read once at boot (docked 1600x900,
// handheld 1280x720), so the game looks as before unless asked. Above 1920x1080 with the RomFS overlay games/totk/heapdef.py writes (plan item 95):
// its HeapDef grows the heap for a scene size it notes beside it (.dm_scene), up to which DM_RENDER_RES is then taken.
// +24 / +32: the swapchain and the canvas size, each width | height << 32 (rules.py option output, plan item 63: main 0xbdb578 / 0xbdb584 read the
// swapchain's, the code cave at 0x3dcb28 the canvas's, which the game composes its UI and the scene's final upscale in). DM_OUTPUT_RES=<w>x<h>
// (640x360 up to 1920x1080, or up to the size the heap overlay notes, .dm_output: games/totk/heapdef.py --output) makes both that size; else the
// swapchain 1920x1080 (the game's constant) and the canvas 0, which the cave leaves as the game laid it out, so the game looks as before.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dm.h"
#include "game.h"
int dm_opmode(void);   // hle_oe.c
int dm_overlay_file(const char *rel, char *out, size_t n);   // hle_fs.c: the file in the RomFS overlay layers

typedef struct { uint64_t last_ns; double dt[5]; unsigned n, next; } frame_mean;
// Feeds one present time; returns the step in seconds, or 0 when the step stays as it is
static double frame_mean_add(frame_mean *m, uint64_t now_ns) {
    if (!m->last_ns || now_ns <= m->last_ns) { m->last_ns = now_ns; return 0; }
    double dt = (now_ns - m->last_ns) * 1e-9; m->last_ns = now_ns;
    if (dt > 0.25 || dt < 0.001) { m->n = m->next = 0; return 0; }
    if (dt < 1.0 / 120) dt = 1.0 / 120;
    if (dt > 1.0 / 15) dt = 1.0 / 15;
    m->dt[m->next++ % 5] = dt; if (m->n < 5) m->n++;
    double sum = 0; for (unsigned i = 0; i < m->n; i++) sum += m->dt[i];
    return sum / m->n;
}
// The scene size for the page; returns 1 when it comes from env (DM_RENDER_RES), 0 for the game's own, -1 for a value it refused (the game's own).
// max: the largest the heap allows, "<w>x<h>" (the overlay's note), NULL for the output size 1920x1080
static int scene_size(const char *env, int opmode, const char *max, float out[2]) {
    out[0] = opmode ? 1600 : 1280; out[1] = opmode ? 900 : 720;
    if (!env || !*env || !strcmp(env, "0")) return 0;
    unsigned w, h, mw = 1920, mh = 1080; char extra;
#ifndef DM_TEST_SCENE_NO_HEAP   // negative control of t_frame_step only: the overlay's note ignored
    if (max && (sscanf(max, "%ux%u%c", &mw, &mh, &extra) != 2 || mw < 1920 || mh < 1080 || mw > 3840 || mh > 2160)) { mw = 1920; mh = 1080; }
#endif
    if (sscanf(env, "%ux%u%c", &w, &h, &extra) != 2 || w < 64 || h < 64 || w > mw || h > mh) return -1;
    out[0] = (float)w; out[1] = (float)h; return 1;
}
// The output size for the page; returns 1 when it comes from env (DM_OUTPUT_RES), 0 for the game's own, -1 for a value it refused (the game's own).
// max: the largest the heap allows, "<w>x<h>" (the overlay's .dm_output note), NULL for the game's own 1920x1080. out: {swapchain, canvas} packed
static int output_size(const char *env, const char *max, uint64_t out[2]) {
    out[0] = (uint64_t)1080 << 32 | 1920; out[1] = 0;
    if (!env || !*env || !strcmp(env, "0")) return 0;
    unsigned w, h, mw = 1920, mh = 1080; char extra;
    if (max && (sscanf(max, "%ux%u%c", &mw, &mh, &extra) != 2 || mw < 1920 || mh < 1080 || mw > 3840 || mh > 2160)) { mw = 1920; mh = 1080; }
    if (sscanf(env, "%ux%u%c", &w, &h, &extra) != 2 || w < 640 || h < 360 || w > mw || h > mh) return -1;
    out[0] = (uint64_t)h << 32 | w;
#ifndef DM_TEST_OUTPUT_NO_CANVAS   // negative control of t_frame_step only: the canvas left to the game (only the swapchain grows)
    out[1] = out[0];
#endif
    return 1;
}
static void frame_step_store(_Atomic uint64_t *page, double seconds) {   // page: at least 16 bytes
    float v[2] = {(float)(seconds * 30), (float)(seconds * 60)}, sec = (float)seconds; uint64_t bits; memcpy(&bits, v, sizeof bits); uint32_t sb; memcpy(&sb, &sec, sizeof sb);
#ifndef DM_TEST_CLOTH_NO_SECONDS   // negative control of t_frame_step only: +8 left as it was
    atomic_store_explicit((_Atomic uint32_t *)(void *)((uint8_t *)page + 8), sb, memory_order_relaxed);
#endif
    atomic_store_explicit(page, bits, memory_order_release);
}

#ifndef FRAME_STEP_TEST
static _Atomic uint64_t *g_page;
static frame_mean g_mean;
static double g_pin;
static int g_realtime;   // the option realtime is on: the step is the measured frame time
void dm_game_boot(void) {
    g_page = dm_host_page("main");
    if (!g_page) { LOG("[red] frame step: main has no host page (product built without rules.py HOSTREFS): the game's step stays at its chosen interval"); return; }
    const char *e = getenv("DM_FRAME_STEP_MS"); g_pin = e ? atof(e) / 1000 : 0;
    frame_step_store(g_page, g_pin > 0 ? g_pin : 1.0 / 30);   // the controller starts at interval 2
    if (dm_option_on("scene")) {
        char note[1024], max[32] = ""; FILE *f = dm_overlay_file("System/HeapSize/Product.Nin_NX_NVN.HeapDef.dm_scene", note, sizeof note) ? fopen(note, "r") : NULL;
        if (f) { if (!fgets(max, sizeof max, f)) max[0] = 0; fclose(f); max[strcspn(max, "\r\n")] = 0; }
        float sz[2]; int from = scene_size(getenv("DM_RENDER_RES"), dm_opmode(), max[0] ? max : NULL, sz); memcpy((uint8_t *)g_page + 16, sz, sizeof sz);
        if (from < 0) LOG("[red] DM_RENDER_RES=%s: expected <width>x<height>, from 64x64 up to %s; the game's own size", getenv("DM_RENDER_RES"),
                          max[0] ? max : "1920x1080 (more needs the heap overlay of games/totk/heapdef.py)");
        else if (max[0]) LOG("[scene size] the RomFS overlay's heap allows up to %s", max);
        LOG("[scene size] %.0fx%.0f (%s)", sz[0], sz[1], from > 0 ? "DM_RENDER_RES" : dm_opmode() ? "the game's own, docked" : "the game's own, handheld");
    } else if (getenv("DM_RENDER_RES")) LOG("[red] DM_RENDER_RES needs the option scene (DM_OPTIONS)");
    if (dm_option_on("output")) {
        char note[1024], max[32] = ""; FILE *f = dm_overlay_file("System/HeapSize/Product.Nin_NX_NVN.HeapDef.dm_output", note, sizeof note) ? fopen(note, "r") : NULL;
        if (f) { if (!fgets(max, sizeof max, f)) max[0] = 0; fclose(f); max[strcspn(max, "\r\n")] = 0; }
        uint64_t sz[2]; int from = output_size(getenv("DM_OUTPUT_RES"), max[0] ? max : NULL, sz); memcpy((uint8_t *)g_page + 24, sz, sizeof sz);
        if (from < 0) LOG("[red] DM_OUTPUT_RES=%s: expected <width>x<height>, from 640x360 up to %s; the game's own", getenv("DM_OUTPUT_RES"),
                          max[0] ? max : "1920x1080 (more needs the heap overlay of games/totk/heapdef.py --output)");
        else if (max[0]) LOG("[output size] the RomFS overlay's heap allows up to %s", max);
        if (from > 0) LOG("[output size] swapchain and canvas %ux%u (DM_OUTPUT_RES)", (unsigned)sz[0], (unsigned)(sz[0] >> 32));
        else LOG("[output size] the game's own (swapchain 1920x1080, the canvas as the game lays it out)");
    } else if (getenv("DM_OUTPUT_RES")) LOG("[red] DM_OUTPUT_RES needs the option output (DM_OPTIONS)");
    if (!(g_realtime = dm_option_on("realtime"))) { LOG("[frame step] the game's own (option realtime not on: DM_OPTIONS or the product's options.default)"); return; }
    if (g_pin > 0) LOG("[frame step] pinned at %.3f ms (DM_FRAME_STEP_MS, diagnostics)", g_pin * 1000);
    else LOG("[frame step] real time: the mean of the last five present intervals (1/120 .. 1/15 s each)");
}
int dm_game_realtime_step(void) { return g_realtime && g_pin <= 0; }   // (a pinned step needs the fixed pacing)
int nvn_fstep_step(void);   // gfx/nvn/hle_nvn.c: DM_FRAME_STEP=vsync, the host's step in vsyncs of 60 Hz (plan item 133), 0 otherwise
void dm_game_present(void) {
    if (!g_realtime || g_pin > 0) return;
    int k = nvn_fstep_step(); if (k > 0) { frame_step_store(g_page, k / 60.0); return; }
    double s = frame_mean_add(&g_mean, guest_ns());
    if (s > 0) frame_step_store(g_page, s);
}
#endif
