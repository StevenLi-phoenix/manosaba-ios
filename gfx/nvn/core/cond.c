// gfx/nvn/core/cond.c: conditional rendering (see cond.h; plan item 264 moved it here from the Metal backend's mtl/args.inc and mtl/exec.inc)
#include "cond.h"
#include "usnap.h"
#include <stdlib.h>
#include <string.h>

int g_render_off;
uint64_t g_rc_n, g_rc_pending, g_rc_pend_draws;
void rc_report(uint64_t f) {
    static uint64_t p[3]; uint64_t c[3] = {g_rc_n, g_rc_pending, g_rc_pend_draws};
    if (f % 300 || c[0] == p[0]) return;
    MLOG("conditional rendering up to frame %llu: %llu conditionals, %llu of them on a samples-passed result still on the GPU (rendering kept on), %llu draws and clears under those",
         (unsigned long long)f, (unsigned long long)(c[0] - p[0]), (unsigned long long)(c[1] - p[1]), (unsigned long long)(c[2] - p[2]));
    memcpy(p, c, sizeof p);
}
int g_rc_on_pending;
typedef struct { uint64_t addr, fence; } occ_ent;
static occ_ent *g_occ; static unsigned g_occ_cap, g_occ_n;
static occ_ent *occ_slot(uint64_t addr) {   // the entry of addr, or the empty slot where it goes
    unsigned m = g_occ_cap - 1, h = (unsigned)((addr >> 3) * 0x9E3779B97F4A7C15ull >> 40) & m;
    while (g_occ[h].addr && g_occ[h].addr != addr) h = (h + 1) & m;
    return &g_occ[h];
}
void occ_note(uint64_t addr, uint64_t fence) {
    if (!addr) return;
    if (2 * (g_occ_n + 1) > g_occ_cap) {
        occ_ent *old = g_occ; unsigned oc = g_occ_cap; g_occ_cap = oc ? oc * 2 : 1024; g_occ = calloc(g_occ_cap, sizeof *g_occ); if (!g_occ) MDIE("occlusion table");
        for (unsigned i = 0; i < oc; i++) if (old[i].addr) *occ_slot(old[i].addr) = old[i];
        free(old);
    }
    occ_ent *e = occ_slot(addr); if (!e->addr) { e->addr = addr; g_occ_n++; }
    e->fence = fence;
}
int occ_pending(uint64_t addr) {
    if (!g_occ_cap) return 0;
    uint64_t done = nvn_be_done_fence();
    const occ_ent *e = occ_slot(addr);
#ifndef DM_TEST_COND_PENDING_DONE   // negative control of t_core_cond only: a result still on the GPU taken as complete
    return e->addr && e->fence > done;
#else
    (void)done; (void)e; return 0;
#endif
}
void cond_render_enable(const p_render_enable *r) {   // SetRenderEnable: off discards clears and draws until it is on again (queue state, kept across command buffers)
    g_render_off = r->mode == 0; g_rc_on_pending = 0;
    if (r->mode >= 2) {   // see occ_pending
        g_rc_n++;
        if (!r->counters || occ_pending(r->counters) || occ_pending(r->counters + 16)) { g_render_off = 0; g_rc_pending++; g_rc_on_pending = 1; RED_ONCE(74, "SetRenderEnableConditional on a samples-passed result still on the GPU: rendering stays enabled (first case)"); }
        else { uint64_t v0, v1; memcpy(&v0, (const void *)(uintptr_t)r->counters, 8); memcpy(&v1, (const void *)(uintptr_t)(r->counters + 16), 8);
            g_render_off = r->mode == 2 ? v0 != v1 : v0 == v1; } }
    if (r->mode >= 2 && r->counters && passdump()) { const uint64_t *cv = (const uint64_t *)(uintptr_t)r->counters;
        PD("e render_enable mode=%d counters=%#llx values %#llx %#llx | %#llx %#llx", r->mode, (unsigned long long)r->counters, (unsigned long long)cv[0], (unsigned long long)cv[1], (unsigned long long)cv[2], (unsigned long long)cv[3]); }
    else PD("e render_enable mode=%d counters=%#llx", r->mode, (unsigned long long)r->counters);
}
