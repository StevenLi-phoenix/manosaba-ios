// The TBDR switches and the attachment validity ledger without a GPU (gfx/nvn/core/tbdr.c, plan item 264). Switches: all on by default;
// DM_NVN_TBDR_OFF turns the parts it names off, "all" every part (read once); DM_NVN_TBDR=0 turns everything off; DM_NVN_RULER=1 starts with the reference run
// (off) and measures once switched; DM_NVN_UNWRITTEN_DC=0 and the A/B mode of DM_NVN_UDC_AB; DM_NVN_XV=0. Ledger: a read of an invalid entry is a
// violation (counted, its key disabled and written to DM_XV_FILE, read back by the next process, every entry of that texture dropped, a disabled
// key adds no entry); a reader during the trial period only disables the key for this process; a compliant touch counts toward the trial; nothing
// is added while blocked, at most 64 entries; the first touch of a submission is kept; window textures; the stable key tells attachments apart by
// index and by the pass's other formats. Against the next submission's store lookahead index (xv_forward, the backend faked as in
// t_core_store_index): a full clear makes the entry valid, a read or copy violates, a draw to it before the clear leaves it, a clear of another
// level leaves it, an index that is not available empties the ledger and blocks it.
// A full clear of another level counting (-DDM_TEST_XV_ANY_LEVEL, the broken variant): 1 fails.
#include "../nvn/core/tbdr.h"
#include "../nvn/core/store_index.h"
#include "core_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static void env(const char *k, const char *v) { if (v) setenv(k, v, 1); else unsetenv(k); }
static void reread(void) { g_tbdr = g_tbdr_off = -1; g_ruler_truth = -1; }
static const int parts[] = {TBP_ZSKIP, TBP_XV, TBP_COVER, TBP_STORE, TBP_SDX, TBP_DEFER, TBP_FUSE, TBP_HOIST, TBP_DS, TBP_CONT, TBP_RTHEAP};
static int all_parts(void) { int on = 0; for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) on += tbdr_part(parts[i]); return on; }

static uint8_t g_mem[4096] __attribute__((aligned(8))); static size_t g_len;
static void rec(uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem + g_len; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz); g_len += size;
}
static void set_rt(const tex_t *c0, int lv) { p_set_rt r = {0}; r.n = 1; r.color[0] = c0; if (lv) { r.has_cview[0] = 1; r.cview[0].set_levels = 1; r.cview[0].base_level = (uint8_t)lv; } rec(OP_SET_RT, &r, sizeof r); }
static void clear(void) { p_clear_color c = {0, 0xf, {0, 0, 0, 0}}; rec(OP_CLEAR_COLOR, &c, sizeof c); }
static void draw(void) { p_draw d = {.count = 3}; rec(OP_DRAW, &d, sizeof d); }
static void bind_tex(const tex_t *t) { p_bind_handles b = {0}; b.stage = 1; b.n = 1; b.h[0] = t->serial; rec(OP_BIND_TEX, &b, sizeof b); }
static void copy_from(const tex_t *t) { p_copy_t2t c = {0}; c.src = t; rec(OP_COPY_T2T, &c, sizeof c); }
static void submission(int on) {   // as the backend's mtl_sub_index: the ledger's textures are candidates, then the index, then the ledger's check
    rec(OP_END, NULL, 0); g_xv.ntouch = 0;
    if (!sdx_begin(on)) { g_xv.n = 0; g_xv.block = 1; g_len = 0; return; }
    g_xv.block = 0;
    for (int i = 0; i < g_xv.n; i++) sdx_bucket(g_xv.ent[i].ser, 1);
    static sdx_start st; uint64_t h = (uint64_t)(uintptr_t)g_mem; sdx_build(&st, &h, 1);
    if (g_sdx.ok) xv_forward();
    g_len = 0;
}
static tex_t tex(uint64_t ser) { tex_t t = {0}; t.d.target = 1; t.d.width = t.d.height = 64; t.d.levels = 4; t.d.format = 37; t.serial = ser; return t; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *keys[] = {"DM_NVN_TBDR", "DM_NVN_TBDR_OFF", "DM_NVN_RULER", "DM_NVN_UNWRITTEN_DC", "DM_NVN_UDC_AB"};
    for (int i = 0; i < 5; i++) env(keys[i], NULL);
    reread(); check("no switch set: the rewrites on, all 11 parts", 1, tbdr_on() && all_parts() == 11);
    env("DM_NVN_TBDR_OFF", "sdx,hoist"); reread();
    check("DM_NVN_TBDR_OFF=sdx,hoist: those two off, store on", 1, !tbdr_part(TBP_SDX) && !tbdr_part(TBP_HOIST) && tbdr_part(TBP_STORE) && all_parts() == 9);
    env("DM_NVN_TBDR_OFF", "cont,rtheap,range"); reread();
    check("DM_NVN_TBDR_OFF=cont,rtheap,range: the last two parts by their bits; range, removed, names nothing", 1, !tbdr_part(TBP_CONT) && !tbdr_part(TBP_RTHEAP) && g_tbdr_off == (TBP_CONT | TBP_RTHEAP));
    env("DM_NVN_TBDR_OFF", "all"); reread();
    check("DM_NVN_TBDR_OFF=all: every part off, the switch itself on", 1, tbdr_on() && all_parts() == 0);
    check("... read once (the mask is not taken for \"not read yet\")", 0xfff, g_tbdr_off);
    env("DM_NVN_TBDR_OFF", NULL); env("DM_NVN_TBDR", "0"); reread();
    check("DM_NVN_TBDR=0: everything off", 1, !tbdr_on() && all_parts() == 0);
    env("DM_NVN_TBDR", NULL); env("DM_NVN_RULER", "1"); reread();
    check("DM_NVN_RULER=1: the reference run first, with the rewrites off", 0, tbdr_on());
    g_ruler_truth = 0; check("... then the measured mode, on", 1, tbdr_on());
    env("DM_NVN_RULER", NULL); reread();
    env("DM_NVN_UNWRITTEN_DC", "0"); g_udc_ab = -1; check("DM_NVN_UNWRITTEN_DC=0: unwritten attachments stored", 0, udc_on());
    env("DM_NVN_UNWRITTEN_DC", NULL); env("DM_NVN_UDC_AB", "1"); g_udc_ab = -1; g_udc_ab_on = 0;
    check("DM_NVN_UDC_AB=1: off until its phase turns it on", 1, !udc_on() && (g_udc_ab_on = 1, udc_on()));
    env("DM_NVN_UDC_AB", NULL); g_udc_ab = -1; g_udc_ab_on = 0; check("... by default on", 1, udc_on());
    env("DM_NVN_XV", "0"); check("DM_NVN_XV=0: the ledger off", 0, xv_on()); env("DM_NVN_XV", NULL);

    char path[512]; snprintf(path, sizeof path, "%s/t_core_tbdr_%d.txt", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid()); unlink(path);
    env("DM_XV_FILE", path);
    xv_set(100, 0, 0, XV_INVALID, 0xa1); check("an invalid entry", 1, xv_find(100, 0, 0) != NULL);
    int dc, v0, v1; xv_test_counts(&dc, &v0); xv_check(100, 0, 0, "read"); xv_test_counts(&dc, &v1);
    check("... read: a violation, counted", 1, v1 - v0);
    check("... its key disabled, the entry gone", 1, xv_disabled(0xa1) && !xv_find(100, 0, 0));
    xv_set(101, 0, 0, XV_INVALID, 0xa1); check("a disabled key adds no entry", 0, xv_find(101, 0, 0) != NULL);
    xv_set(102, 0, 0, XV_INVALID, 0xb1); xv_set(102, 1, 0, XV_INVALID, 0xb2); xv_check(102, 0, 0, "read");
    check("a violation drops every entry of that texture (level 1 too)", 0, xv_find(102, 1, 0) != NULL);
    xv_set(103, 0, 0, XV_PROBE, 0xc1); xv_test_counts(&dc, &v0); xv_check(103, 0, 0, "read"); xv_test_counts(&dc, &v1);
    check("a reader during the trial period: not a violation, the key disabled, the entry gone", 1, v1 == v0 && xv_disabled(0xc1) && !xv_find(103, 0, 0));
    g_xv.init = 0; g_xv.ndis = 0;   // the next process
    check("the next process reads the violation's key back from DM_XV_FILE, not the trial reader's", 1, xv_disabled(0xa1) && xv_disabled(0xb1) && !xv_disabled(0xc1));
    xv_set(104, 0, 0, XV_PROBE, 0xd1); int t0 = xv_trial_get(0xd1, 0); xv_good(xv_find(104, 0, 0));
    check("a compliant touch in the trial period counts once, the entry valid again", 1, xv_trial_get(0xd1, 0) == t0 + 1 && !xv_find(104, 0, 0));
    g_xv.block = 1; xv_set(105, 0, 0, XV_INVALID, 0xe1); g_xv.block = 0; check("nothing added while blocked", 0, xv_find(105, 0, 0) != NULL);
    for (int i = 0; i < 70; i++) xv_set(200 + (uint64_t)i, 0, 0, XV_INVALID, 0x1000 + (uint64_t)i);
    check("at most 64 entries", 64, g_xv.n);
    g_xv.n = 0;
    xv_touch_add(300, 0, 0, 1); xv_touch_add(300, 0, 0, 0); check("the first touch of a submission is kept", 1, xv_first_good(300, 0, 0) && !xv_first_good(301, 0, 0));
    mtl_xv_window_tex(400); mtl_xv_window_tex(400); check("a window texture, registered once", 1, xv_present(400) && g_xv.npresent == 1 && !xv_present(401));
    { tex_t a = tex(1), b = tex(2); p_set_rt r = {0}; r.n = 2; r.color[0] = &a; r.color[1] = &b; p_set_rt r2 = r; tex_t c = tex(3); c.d.format = 38; r2.color[1] = &c;
      check("the stable key: the same in the same pass, apart by index and by the pass's other formats", 1,
            xv_tag_pass(&a, 0, 0, &r) == xv_tag_pass(&a, 0, 0, &r) && xv_tag_pass(&a, 0, 0, &r) != xv_tag_pass(&a, 0, 1, &r) && xv_tag_pass(&a, 0, 0, &r) != xv_tag_pass(&a, 0, 0, &r2)); }
    unlink(path); env("DM_XV_FILE", NULL);

    tex_t A = tex(500);
    xv_set(500, 0, 0, XV_INVALID, 0xf1); set_rt(&A, 0); clear(); draw(); submission(1);
    check("the next submission clears it in full: valid", 0, xv_find(500, 0, 0) != NULL);
    xv_set(500, 0, 0, XV_INVALID, 0xf2); xv_test_counts(&dc, &v0); bind_tex(&A); draw(); submission(1); xv_test_counts(&dc, &v1);
    check("... reads it: a violation", 1, v1 - v0);
    xv_set(501, 0, 0, XV_INVALID, 0xf3); { tex_t B = tex(501); xv_test_counts(&dc, &v0); copy_from(&B); submission(1); xv_test_counts(&dc, &v1); }
    check("... copies from it: a violation", 1, v1 - v0);
    xv_set(502, 0, 0, XV_INVALID, 0xf4); { tex_t C = tex(502); set_rt(&C, 0); draw(); clear(); submission(1); }
    check("... draws to it before clearing: the entry stays (the pass's load decides)", 1, xv_find(502, 0, 0) != NULL);
    g_xv.n = 0; xv_set(503, 0, 0, XV_INVALID, 0xf5); { tex_t D = tex(503); set_rt(&D, 1); clear(); submission(1); }
    check("... clears another level of it: the entry stays [-DDM_TEST_XV_ANY_LEVEL]", 1, xv_find(503, 0, 0) != NULL);
    set_rt(&A, 0); draw(); g_xv.n = 0; xv_set(504, 0, 0, XV_INVALID, 0xf6); rec(OP_END, NULL, 0); g_len = 0;
    { static sdx_start st; uint64_t h = (uint64_t)(uintptr_t)g_mem; sdx_begin(1); sdx_build(&st, &h, 1); g_sdx.ok = 0; xv_forward(); }
    check("an index that is not available: the ledger emptied and blocked", 1, g_xv.n == 0 && g_xv.block == 1);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
