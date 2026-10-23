// gfx/nvn/core/tbdr.c: the switches of the TBDR rewrites and the attachment validity ledger, see tbdr.h
#include "tbdr.h"
#include "store_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_tbdr = -1, g_tbdr_off = -1;
int g_ruler_truth = -1, g_ruler_pending;
void tbdr_read(void) {
    const char *e = getenv("DM_NVN_TBDR"), *r = getenv("DM_NVN_RULER"); g_tbdr = !(e && !strcmp(e, "0")); g_ruler_truth = r && !strcmp(r, "1");
    if (!g_tbdr) MLOG("DM_NVN_TBDR=0: all TBDR optimizations off (the reference for equivalence checks; Tears of the Kingdom's default on the Mac, games/totk/defaults.c)");
}
void tbdr_read_off(void) {
    int off = 0; const char *e = getenv("DM_NVN_TBDR_OFF");
    static const struct { const char *n; int bit; } nm[] = {{"zskip", TBP_ZSKIP}, {"xv", TBP_XV}, {"cover", TBP_COVER}, {"store", TBP_STORE}, {"sdx", TBP_SDX}, {"defer", TBP_DEFER},
                                                          {"fuse", TBP_FUSE}, {"hoist", TBP_HOIST}, {"ds", TBP_DS}, {"cont", TBP_CONT}, {"rtheap", TBP_RTHEAP}};
    for (const char *q = e; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) { size_t n = strcspn(q, ",");
        if (n == 3 && !strncmp(q, "all", 3)) off = TBP_RTHEAP * 2 - 1;   // every part (~0, as before, is -1: taken for "not read yet", it was read again at every call)
        for (size_t i = 0; i < sizeof nm / sizeof nm[0]; i++) if (strlen(nm[i].n) == n && !strncmp(q, nm[i].n, n)) off |= nm[i].bit; }
    if (e && *e) MLOG("DM_NVN_TBDR_OFF=%s: these TBDR rewrites stay off (mask %#x)", e, off);
    g_tbdr_off = off;
}
int g_udc_ab = -1, g_udc_ab_on;
int udc_on(void) {
    if (g_udc_ab < 0) { const char *e = getenv("DM_NVN_UNWRITTEN_DC"); g_udc_ab = !(e && !strcmp(e, "0"));
        const char *a = getenv("DM_NVN_UDC_AB"); if (a && !strcmp(a, "1")) { g_udc_ab = 0; g_udc_ab_on = 0; } }
    return g_udc_ab || g_udc_ab_on;
}

xv_state g_xv;
int xv_trial_get(uint64_t tag, int add) { for (int i = 0; i < g_xv.ntrial; i++) if (g_xv.trial[i].tag == tag) return g_xv.trial[i].good += add; if (g_xv.ntrial < 128) { g_xv.trial[g_xv.ntrial].tag = tag; return g_xv.trial[g_xv.ntrial++].good = add; } return 0; }
uint64_t xv_tag(const tex_t *t, uint32_t lv) { return ((uint64_t)t->d.format << 40) ^ ((uint64_t)t->d.width << 24) ^ ((uint64_t)t->d.height << 8) ^ lv; }
// Stable key (unchanged across launches, does not use the texture serial): format / width / height / level, then mixed with the formats of the color attachments of the pass where the write-back is skipped and this attachment's index - textures of the same format and size but different purpose (e.g. transmittance 27 and upscale target 36) do not share a key
uint64_t xv_tag_pass(const tex_t *t, uint32_t lv, int idx, const p_set_rt *rt) { uint64_t h = xv_tag(t, lv) ^ ((uint64_t)(idx + 1) << 60); for (int i = 0; i < rt->n && i < 8; i++) h = (h ^ (rt->color[i] ? (uint64_t)rt->color[i]->d.format + 1 : 0)) * 0x100000001b3ull; return h; }
void xv_init(void) {
    if (g_xv.init) return; g_xv.init = 1;
    const char *p = getenv("DM_XV_FILE"); if (!p || !*p) return;
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[256], why[160]; unsigned long long tag; int ch, n = 0;
    while (fgets(line, sizeof line, f) && g_xv.ndis < 128) if (sscanf(line, "%llx\t%159[^\t]\t%d", &tag, why, &ch) >= 1) g_xv.disabled[g_xv.ndis++] = tag, n++;
    fclose(f); MLOG("read %d disable records", n);
}
int xv_disabled(uint64_t tag) { xv_init(); for (int i = 0; i < g_xv.ndis; i++) if (g_xv.disabled[i] == tag) return 1; return 0; }
xv_ent *xv_find(uint64_t ser, uint32_t lv, uint32_t sl) { for (int i = 0; i < g_xv.n; i++) if (g_xv.ent[i].ser == ser && g_xv.ent[i].lv == lv && g_xv.ent[i].sl == sl) return &g_xv.ent[i]; return NULL; }
void xv_drop(uint64_t ser, uint32_t lv, uint32_t sl) { for (int i = 0; i < g_xv.n; i++) if (g_xv.ent[i].ser == ser && g_xv.ent[i].lv == lv && g_xv.ent[i].sl == sl) { g_xv.ent[i] = g_xv.ent[--g_xv.n]; return; } }
void xv_drop_all_of(uint64_t ser) { for (int i = 0; i < g_xv.n;) if (g_xv.ent[i].ser == ser) g_xv.ent[i] = g_xv.ent[--g_xv.n]; else i++; }
void xv_set(uint64_t ser, uint32_t lv, uint32_t sl, uint8_t state, uint64_t tag) {
    xv_init(); if (g_xv.block || xv_disabled(tag)) return;
    xv_ent *e = xv_find(ser, lv, sl); if (e) { e->state = state; e->tag = tag; return; }
    if (g_xv.n < 64) g_xv.ent[g_xv.n++] = (xv_ent){ser, tag, lv, sl, state};
}
void xv_violation(const xv_ent *e, const char *why) {
    xv_init();
    if (e->state == XV_PROBE) {   // Trial period: the content is still there and the picture is intact - only disable the key (this process)
        if (!xv_disabled(e->tag) && g_xv.ndis < 128) g_xv.disabled[g_xv.ndis++] = e->tag;
        PD("V xv reader seen during trial: key=%016llx tex=%llu lv=%u why=%s → key disabled", (unsigned long long)e->tag, (unsigned long long)e->ser, e->lv, why);
        xv_drop_all_of(e->ser); return; }
    g_xv.viol++;
    int seen = xv_disabled(e->tag); if (!seen && g_xv.ndis < 128) g_xv.disabled[g_xv.ndis++] = e->tag;
    MLOG("[red] xv tag=%016llx tex=%llu lv=%u sl=%u why=%s pass=%llu", (unsigned long long)e->tag, (unsigned long long)e->ser, e->lv, e->sl, why, (unsigned long long)g_stat_pass);
    const char *p = getenv("DM_XV_FILE"); if (p && *p) { FILE *f = fopen(p, "a"); if (f) { fprintf(f, "%016llx\t%s\t%llu\n", (unsigned long long)e->tag, why, (unsigned long long)g_stat_pass); fclose(f); } }
    xv_drop_all_of(e->ser);
}
void xv_check(uint64_t ser, uint32_t lv, uint32_t sl, const char *why) { xv_ent *e = xv_find(ser, lv, sl); if (e && (e->state == XV_INVALID || e->state == XV_PROBE)) { xv_ent copy = *e; xv_violation(&copy, why); } }
void xv_good(xv_ent *e) { if (e->state == XV_PROBE) xv_trial_get(e->tag, 1); xv_drop(e->ser, e->lv, e->sl); }   // Compliant touch: counts once toward the trial period, and the entry returns to valid
void xv_touch_add(uint64_t ser, uint32_t lv, uint32_t sl, int good) { for (int i = 0; i < g_xv.ntouch; i++) if (g_xv.touch[i].ser == ser && g_xv.touch[i].lv == lv && g_xv.touch[i].sl == sl) return; if (g_xv.ntouch < 128) g_xv.touch[g_xv.ntouch++] = (xv_touch){ser, lv, sl, (uint8_t)good}; }
int xv_first_good(uint64_t ser, uint32_t lv, uint32_t sl) { for (int i = 0; i < g_xv.ntouch; i++) if (g_xv.touch[i].ser == ser && g_xv.touch[i].lv == lv && g_xv.touch[i].sl == sl) return g_xv.touch[i].good; return 0; }
int xv_present(uint64_t ser) { for (int i = 0; i < g_xv.npresent; i++) if (g_xv.present[i] == ser) return 1; return 0; }
int xv_on(void) { static int off = -1; if (off < 0) { const char *e = getenv("DM_NVN_XV"); off = e && !strcmp(e, "0"); } return !off && tbdr_part(TBP_XV); }
void mtl_xv_window_tex(uint64_t serial) { if (!xv_present(serial) && g_xv.npresent < 128) g_xv.present[g_xv.npresent++] = serial; }   // Window (swapchain) texture: never enters the ledger
void xv_test_counts(int *dc, int *viol) { if (dc) *dc = (int)g_xv.dc; if (viol) *viol = (int)g_xv.viol; }
void xv_forward(void) {
    if (!g_xv.n) return;
    if (!g_sdx.ok) { g_xv.n = 0; g_xv.block = 1; return; }
    for (int i = 0; i < g_xv.n;) {
        xv_ent *e = &g_xv.ent[i]; int b = sdx_bucket(e->ser, 0), action = 0;
        if (b >= 0) for (uint32_t j = g_sdx.bst[b]; j < g_sdx.bst[b + 1]; j++) { sdx_ev *v = &g_sdx.ev[j];
            if (v->kind == SDX_READ || v->kind == SDX_NEED) { xv_ent copy = *e; xv_violation(&copy, "submit lookahead read/need"); action = 2; break; }
#ifndef DM_TEST_XV_ANY_LEVEL   // negative control of t_core_tbdr only: a full clear of another level or layer makes the entry valid
            if (v->lv != e->lv || v->sl != e->sl) continue;
#endif
            if (v->kind == SDX_CLEAR) { xv_good(e); action = 1; break; }
            if (v->kind == SDX_ATT) break;
        }
        if (!action) i++;
    }
}
