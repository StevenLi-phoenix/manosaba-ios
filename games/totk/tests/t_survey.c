// The world survey (games/totk/survey.c, plan item 167) against a scene built in host memory: a "main" whose scene slot leads to a scene with 14
// components, the resident actors' component holding an NPC and then the player, and a stand-in for forceSetMatrix that records each call.
// Checked: a main without 1.4.3's forceSetMatrix words is refused; bad lines are taken (so the host does not call them unknown) but not queued:
// NaN, Y out of -1500..3500, 0 or 601 frames, a tag with '/', trailing words; another line is not the survey's; pos reports the player's place at
// the frame point; move 3 calls forceSetMatrix once a frame for three frames with the rotation's rows and the place as its fourth column, then
// move_done and no more calls; a line while a move runs answers busy; without an actor named Player: no_player; a rotation with NaN: no call.
// Built with clang -DSURVEY_TEST -I host (games/totk/tests). The rotation transposed (-DDM_TEST_SURVEY_COLUMNS): 1 fails.
#define SURVEY_TEST 1
#include "../survey.c"
#include <unistd.h>

static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }

static int g_calls; static float g_m[12]; static void *g_actor_got;
static void fake_force(void *actor, const float *m, uint32_t flags) {
    g_calls++; g_actor_got = actor; memcpy(g_m, m, sizeof g_m); (void)flags;
    memcpy((uint8_t *)actor + ACTOR_POS, (float[3]){m[3], m[7], m[11]}, 12);   // the game places the actor there
}
static char g_log[1 << 16];
static const char *log_since(long *at) {   // stderr's text written since *at
    fflush(stderr); FILE *f = fopen(getenv("T_SURVEY_LOG"), "r"); if (!f) return ""; fseek(f, *at, SEEK_SET);
    size_t k = fread(g_log, 1, sizeof g_log - 1, f); g_log[k] = 0; *at += (long)k; fclose(f); return g_log;
}
static void put_ptr(uint8_t *at, const void *p) { uintptr_t v = (uintptr_t)p; memcpy(at, &v, 8); }

int main(void) {
    char lp[256]; snprintf(lp, sizeof lp, "%s/t_survey_%d.log", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); setenv("T_SURVEY_LOG", lp, 1);
    if (!freopen(lp, "w", stderr)) return 2;
    long at = 0;
    size_t size = SCENE_SLOT + 8; uint8_t *main_ = calloc(1, size);
    uint8_t *mod = calloc(1, 0x200), *scene = calloc(1, 0x100), *comps = calloc(14, 8), *res = calloc(1, 0x40), *links = calloc(2, 0x70);
    uint8_t *data[2] = {calloc(1, 0x80), calloc(1, 0x80)}, *actor[2] = {calloc(1, 0x400), calloc(1, 0x400)};
    char npc[8] = "Npc_A", pl[8] = "Player";
    put_ptr(main_ + SCENE_SLOT, mod); put_ptr(mod + 0x1e8, scene); int c14 = 14, c2 = 2; memcpy(scene + 0x50, &c14, 4); put_ptr(scene + 0x58, comps);
    put_ptr(comps + 13 * 8, res); memcpy(res + 0x20, &c2, 4); put_ptr(res + 0x28, links);
    for (int i = 0; i < 2; i++) { put_ptr(links + i * 0x70 + 8, data[i]); put_ptr(data[i] + 0x40, actor[i]); put_ptr(actor[i] + ACTOR_NAME, i ? (void *)pl : (void *)npc);
        memcpy(actor[i] + ACTOR_POS, (float[3]){100.f + i, 200.f, -300.f}, 12); }
    const float rot[9] = {0.6f, 0.0f, -0.8f, 0.0f, 1.0f, 0.0f, 0.8f, 0.0f, 0.6f};   // a turn about Y: not symmetric, so a transposed one differs
    memcpy(actor[1] + ACTOR_ROT, rot, sizeof rot);

    memcpy(main_ + FORCE_MATRIX, FORCE_WORDS, sizeof FORCE_WORDS); main_[FORCE_MATRIX] ^= 1;
    check("a main whose forceSetMatrix words differ: refused", !survey_setup((uintptr_t)main_, 0x2bb9c00, fake_force) && !atomic_load(&g_on));
    main_[FORCE_MATRIX] ^= 1;
    check("1.4.3's words: on", survey_setup((uintptr_t)main_, 0x2bb9c00, fake_force) && atomic_load(&g_on));
    const char *bad[] = {"survey move a nan 1 2 3", "survey move a 0 -5000 0 3", "survey move a 1 2 3 0", "survey move a 1 2 3 601", "survey pos a/b", "survey pos a extra"};
    int taken = 1; for (int i = 0; i < 6; i++) taken &= survey_command(bad[i], 0) == 1 && survey_command(bad[i], 1) == 1;
    check("bad lines (NaN, Y out of range, 0 and 601 frames, a '/' tag, trailing words): taken, none queued", taken && !atomic_load(&g_pending));
    check("another line is not the survey's", survey_command("surveyx pos a", 0) == 0 && survey_command("key A", 0) == 0);
    log_since(&at);
    survey_command("survey pos here", 1); int queued = atomic_load(&g_pending); survey_tick(10);
    const char *t = log_since(&at);
    check("pos: queued, then at the frame point the player's place (not the NPC's)", queued && strstr(t, "tag=here status=position frame=10 xyz=101.000,200.000,-300.000") && !atomic_load(&g_pending));
    survey_command("survey move hop 12.5 30 -40 3", 1);
    survey_tick(11); int one = g_calls;
    survey_command("survey pos again", 1); t = log_since(&at);
    check("move: one call at the first frame point, and a line meanwhile answers busy", one == 1 && g_actor_got == actor[1] && strstr(t, "tag=hop status=move_begin frame=11") && strstr(t, "tag=again status=busy"));
    const float want[12] = {0.6f, 0.0f, -0.8f, 12.5f, 0.0f, 1.0f, 0.0f, 30.0f, 0.8f, 0.0f, 0.6f, -40.0f};
    int same = 1; for (int i = 0; i < 12; i++) same &= fabsf(g_m[i] - want[i]) < 1e-6f;
    check("... the matrix: the rotation's rows, the place as the fourth column", same);
    survey_tick(12); survey_tick(13); survey_tick(14); t = log_since(&at);
    check("... three frames, then move_done at the new place and no more calls", g_calls == 3 && strstr(t, "tag=hop status=move_done frame=13 xyz=12.500,30.000,-40.000") && !atomic_load(&g_pending));
    memcpy(actor[1] + ACTOR_NAME, &(void *){npc}, 8);
    survey_command("survey pos gone", 1); survey_tick(20); t = log_since(&at);
    check("no actor named Player: no_player", strstr(t, "tag=gone status=no_player frame=20") != NULL && !atomic_load(&g_pending));
    put_ptr(actor[1] + ACTOR_NAME, pl); float nanrot[9]; memcpy(nanrot, rot, sizeof rot); nanrot[4] = NAN; memcpy(actor[1] + ACTOR_ROT, nanrot, sizeof nanrot);
    int c0 = g_calls; survey_command("survey move bad 1 2 3 2", 1); survey_tick(21); t = log_since(&at);
    check("a rotation with NaN: invalid_rotation, no call", g_calls == c0 && strstr(t, "tag=bad status=invalid_rotation frame=21") && !atomic_load(&g_pending));
    unlink(lp);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
