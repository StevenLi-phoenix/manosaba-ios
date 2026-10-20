// The world survey (plan item 167, after MacNativeGames' games/totk/runtime/world_survey.c, ly05010419/MacNativeGames d164868, MIT: its 1.4.3
// addresses, the actor layout and the bounds as data, written anew for this host). Command channel lines (DM_CMD, host/cmdchan.c) read Link's
// place and move him through the game's own forceSetMatrix, so a soak can stand at a fixed place and a run can visit the world for shaders:
//   survey pos <tag>                    "[survey] tag=<tag> status=position frame=<n> xyz=<x>,<y>,<z>"
//   survey move <tag> X Y Z <frames>    Link set at X Y Z, his rotation kept, at each of the next <frames> frames (1-600), then status=move_done
// Tags are letters, digits, '_' and '-'; X within ±5500, Y within -1500..3500, Z within ±5000 (the map's extent). A request while one is running
// answers status=busy, a frame without the player status=no_player.
// The work runs at the frame point (dm_game_frame_sync, gfx/nvn/hle_nvn.c: the game's thread has just waited for the window's image, main 0x2db958,
// between two frames), never on the command thread. The player is found from main's scene slot (bss 0x3add238): *slot +0x1e8 the scene, its +0x50
// component count and +0x58 components, component 13 the resident actors (+0x20 count, +0x28 links of 0x70 bytes, +8 data, data +0x40 the actor),
// the one named "Player" (actor +0x218), position actor +0x2b4, rotation (3x3) +0x2c0. Guest memory is read with mach_vm_read_overwrite, so a
// stale pointer reads as no player instead of a fault. forceSetMatrix (main 0x45ce70: actor, a 3x4 row matrix, 0) routes the move through the
// physics component as the game's own warps do; its first four words are checked before it is ever called.
// Off unless DM_SURVEY=1: a save written after a move keeps the new place. Checked by games/totk/tests/t_survey.c.
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include "dm.h"
#include "game.h"

enum { SCENE_SLOT = 0x3add238, FORCE_MATRIX = 0x45ce70, ACTOR_NAME = 0x218, ACTOR_POS = 0x2b4, ACTOR_ROT = 0x2c0 };
static const uint32_t FORCE_WORDS[4] = {0xa9bc7bfd, 0xa9015ff8, 0xa90257f6, 0xa9034ff4};   // stp x29, x30, [sp, #-0x40]!; stp x24, x23 / x22, x21 / x20, x19
typedef void (*force_fn)(void *actor, const float *matrix, uint32_t flags);

static uintptr_t g_main;
static force_fn g_force;
static _Atomic int g_on, g_pending;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { int kind; char tag[64]; float xyz[3]; unsigned hold; } g_req;
static unsigned g_hold_left; static float g_hold_xyz[3]; static char g_hold_tag[64];

static int rd(uintptr_t a, void *out, size_t n) {
    if (!a || a > UINTPTR_MAX - n) return 0;
    mach_vm_size_t got = 0;
    return mach_vm_read_overwrite(mach_task_self(), a, n, (mach_vm_address_t)(uintptr_t)out, &got) == KERN_SUCCESS && got == n;
}
static uintptr_t rdp(uintptr_t a) { uintptr_t p = 0; return rd(a, &p, sizeof p) ? p : 0; }
static int xyz_ok(const float v[3]) {
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]) && fabsf(v[0]) <= 5500 && v[1] >= -1500 && v[1] <= 3500 && fabsf(v[2]) <= 5000;
}
static uintptr_t player(void) {
    uintptr_t mod = rdp(g_main + SCENE_SLOT), scene = mod ? rdp(mod + 0x1e8) : 0;
    int n = 0;
    if (!scene || !rd(scene + 0x50, &n, 4) || n < 14 || n > 256) return 0;
    uintptr_t comps = rdp(scene + 0x58), res = comps ? rdp(comps + 13 * 8) : 0;
    if (!res || !rd(res + 0x20, &n, 4) || n < 1 || n > 64) return 0;
    uintptr_t links = rdp(res + 0x28);
    for (int i = 0; links && i < n; i++) {
        uintptr_t data = rdp(links + (uintptr_t)i * 0x70 + 8), actor = data ? rdp(data + 0x40) : 0, name = actor ? rdp(actor + ACTOR_NAME) : 0;
        char t[7]; float v[3];
        if (name && rd(name, t, sizeof t) && !memcmp(t, "Player", 7) && rd(actor + ACTOR_POS, v, sizeof v) && xyz_ok(v)) return actor;
    }
    return 0;
}
static void report(const char *tag, const char *status, uint64_t frame, uintptr_t actor) {
    float v[3];
    if (actor && rd(actor + ACTOR_POS, v, sizeof v)) LOG("[survey] tag=%s status=%s frame=%llu xyz=%.3f,%.3f,%.3f", tag, status, (unsigned long long)frame, v[0], v[1], v[2]);
    else LOG("[survey] tag=%s status=%s frame=%llu", tag, status, (unsigned long long)frame);
}
// main: the module's base, text_end its text size; f: forceSetMatrix (NULL: at its address in main). Returns 1 when on
static int survey_setup(uintptr_t main, uint64_t text_end, force_fn f) {
    uint32_t w[4];
    if (!main || FORCE_MATRIX + sizeof w > text_end || !rd(main + FORCE_MATRIX, w, sizeof w) || memcmp(w, FORCE_WORDS, sizeof w)) {
        LOG("[red] survey: main+%#x is not 1.4.3's forceSetMatrix: off", FORCE_MATRIX); return 0; }
    g_main = main; g_force = f ? f : (force_fn)(main + FORCE_MATRIX); atomic_store(&g_on, 1);
    LOG("[survey] on: survey pos <tag> / survey move <tag> X Y Z <frames> on the command channel (DM_CMD), run at the frame point");
    return 1;
}
// A command channel line: 0 not a survey line, 1 taken (run=0: only checked; run=1: queued, or answered)
static int survey_command(const char *line, int run) {
    if (strncmp(line, "survey ", 7)) return 0;
    char op[16], tag[64]; float v[3] = {0}; unsigned hold = 0; int used = 0, kind = 0;
    if (sscanf(line, "survey %15s %63s %n", op, tag, &used) == 2 && used && !line[used] && !strcmp(op, "pos")) kind = 1;
    used = 0;
    if (sscanf(line, "survey move %63s %f %f %f %u %n", tag, &v[0], &v[1], &v[2], &hold, &used) == 5 && used && !line[used] && xyz_ok(v) && hold >= 1 && hold <= 600) kind = 2;
    for (const char *p = tag; kind && *p; p++) if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') kind = 0;
    if (!kind) { if (run) LOG("[red] survey: expected pos <tag> or move <tag> X Y Z <1-600 frames> (X ±5500, Y -1500..3500, Z ±5000; tags of letters, digits, _ and -): %s", line); return 1; }
    if (!run) return 1;
    pthread_mutex_lock(&g_mu);
    if (!atomic_load(&g_on)) report(tag, "off (DM_SURVEY=1)", 0, 0);
    else if (g_req.kind || g_hold_left) report(tag, "busy", 0, 0);
    else { g_req.kind = kind; strlcpy(g_req.tag, tag, sizeof g_req.tag); memcpy(g_req.xyz, v, sizeof v); g_req.hold = hold; atomic_store(&g_pending, 1); }
    pthread_mutex_unlock(&g_mu);
    return 1;
}
// At the frame point: the request taken, a move held for its frames
static void survey_tick(uint64_t frame) {
    if (!atomic_load_explicit(&g_pending, memory_order_acquire)) return;
    pthread_mutex_lock(&g_mu);
    uintptr_t actor = player();
    if (!actor) { report(g_req.kind ? g_req.tag : g_hold_tag, "no_player", frame, 0); g_req.kind = 0; g_hold_left = 0; }
    else if (g_req.kind == 1) { report(g_req.tag, "position", frame, actor); g_req.kind = 0; }
    else {
        if (g_req.kind == 2) { memcpy(g_hold_xyz, g_req.xyz, sizeof g_hold_xyz); strlcpy(g_hold_tag, g_req.tag, sizeof g_hold_tag); g_hold_left = g_req.hold; g_req.kind = 0;
                               report(g_hold_tag, "move_begin", frame, actor); }
        float rot[9], m[12]; int ok = rd(actor + ACTOR_ROT, rot, sizeof rot);
        for (int i = 0; ok && i < 9; i++) if (!isfinite(rot[i]) || fabsf(rot[i]) > 2) ok = 0;
        if (!ok) { report(g_hold_tag, "invalid_rotation", frame, actor); g_hold_left = 0; }
        else {
            for (int r = 0; r < 3; r++) {   // row r: the rotation's row, then the position's coordinate r
#ifndef DM_TEST_SURVEY_COLUMNS   // negative control of t_survey only: the rotation transposed
                for (int c = 0; c < 3; c++) m[r * 4 + c] = rot[r * 3 + c];
#else
                for (int c = 0; c < 3; c++) m[r * 4 + c] = rot[c * 3 + r];
#endif
                m[r * 4 + 3] = g_hold_xyz[r];
            }
            g_force((void *)actor, m, 0);
            if (!--g_hold_left) report(g_hold_tag, "move_done", frame, actor);
        }
    }
    atomic_store_explicit(&g_pending, g_req.kind || g_hold_left, memory_order_release);
    pthread_mutex_unlock(&g_mu);
}

#ifndef SURVEY_TEST
uint64_t dm_nvn_frame_count(void);
int dm_game_command(const char *line, int run) {
    static int init; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    if (!strncmp(line, "survey ", 7) && run) {
        pthread_mutex_lock(&mu);
        if (!init) { init = 1; const char *e = getenv("DM_SURVEY");
            if (e && !strcmp(e, "1")) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) survey_setup((uintptr_t)g_mod[i].base, g_mod[i].text_end, NULL); }
        pthread_mutex_unlock(&mu);
    }
    return survey_command(line, run);
}
void dm_game_frame_sync(void) { survey_tick(dm_nvn_frame_count()); }
#endif
