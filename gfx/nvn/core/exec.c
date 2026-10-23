// gfx/nvn/core/exec.c: the replay loop (see exec.h; plan item 264 moved it here from the Metal backend's mtl/exec.inc, where it was mtl_exec)
#include "exec.h"
#include "../nvn_backend.h"
#include "capture.h"
#include "defer.h"
#include "fuse.h"
#include "pass.h"
#include "state.h"
#include "store_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t g_sub_work;

void nvn_exec(const uint8_t *p) {
    static int inited; if (!inited) { inited = 1; st_reset(); nvn_be_reset(); }
    static int dump_all = -1; if (dump_all < 0) dump_all = getenv("DM_NVN_PASSDUMP_ALL") != NULL;
    int trace = dump_all && passdump();   // Normal mode does not check the frame range per operation; the full command position is only used for the designated diagnostic frames
    for (;;) {
        op_hdr h; memcpy(&h, p, sizeof h);
        g_exec_cur = p;   // Scan start point of the store lookahead: the operation that ends the render pass (inclusive)
        if (p == g_defer_pend) { for (int gi = 0; gi < 4; gi++) if (g_defer_slots[gi].active && g_defer_slots[gi].p_end_op == p) g_defer_slots[gi].in_p = 0; g_defer_pend = NULL; }
        sdx_seq(p);
        if (trace) PD("o cmd=%p pass=%llu op=%u size=%u", p, g_stat_pass, (unsigned)h.op, (unsigned)h.size);
        if (g_hoist_n && hoist_take(p)) { if (g_fz.active && p == g_fz.rt2_op) { g_fz.phase = 2; g_rt_merged = 0; PD("F %llu merged pass enters the later pass's draws", g_stat_pass); } p += h.size; continue; }   // A pure-clear render pass already executed early / the start of a later pass already merged in   // Entering the later pass: the same-target continuation of the earlier pass ends here; the later pass reading the earlier pass's attachment is proven to be same-pixel by fz_p2_draw, and is no longer split by the continuation rule
        const void *a = p + 8;
        switch (h.op) {   // work for the counter timestamps' position (nvn_ts.h)
            case OP_DRAW: case OP_DRAW_IDX: case OP_DISPATCH: case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T:
            case OP_CLEAR_COLOR: case OP_CLEAR_DS: case OP_CLEAR_TEX: g_sub_work++; break;
            default: break;
        }
        switch (h.op) {
            case OP_END: g_exec_cur = NULL;
                if (g_hoist_n && !g_ret_n && !g_nrest) {
                    if (g_fz.active) fz_unskip();
                    if (g_hoist_n) { MLOG("[red] pure-clear pass hoist: %d hoisted ops not reached at the end of the submit, clearing (should not happen)", g_hoist_n); g_hoist_n = 0; }
                }
                return;   // An end-of-pass triggered outside a list (present / CPU readback etc.) must not scan expired command memory: a null start point means store everything
            case OP_BEGIN: break;
            case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); p = t; continue; }
            case OP_CALL: { uint64_t t; memcpy(&t, a, 8);
                if (g_ret_n >= 64) { RED_ONCE(106, "a CALL 64 lists deep (a call cycle?): not followed (first case)"); break; }   // (plan item 64b; it recursed until the stack ran out)
#ifndef DM_TEST_EXEC_RET_AT_CALL   // negative control of t_core_exec only: the call itself pushed for the caller instead of the op after it
                if (g_ret_n < 16) g_ret[g_ret_n] = p + h.size; g_ret_n++;   // Return stack for lookahead: after a sub-list ends, continue looking at the caller
#else
                if (g_ret_n < 16) g_ret[g_ret_n] = p; g_ret_n++;
#endif
                nvn_exec((const uint8_t *)t); g_ret_n--; break; }
            case OP_SIGNAL_EVENT: { volatile uint32_t *val; uint32_t m, v; memcpy(&val, a, 8); memcpy(&m, (const uint8_t *)a + 8, 4); memcpy(&v, (const uint8_t *)a + 12, 4);
                PD("Y signal_event cmd=%p dst=%p mode=%u value=%u", p, val, m, v); nvn_be_signal_event(val, m, v); break; }
            case OP_FENCE_SYNC: { sync_t *sy; memcpy(&sy, a, 8);   // this submission commits with fence nvn_be_sub_fence()
                if (g_cap_cur) {   // the translation thread: the capture fenced it to this submission's token; a later submission's token stays (a newer FenceSync)
                    uint64_t want = g_cap_cur->token | SYNC_TOKEN;
                    __atomic_compare_exchange_n(&sy->fence, &want, nvn_be_sub_fence(), 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
                    break; }
#ifndef DM_TEST_FENCE_SYNC_LAST   // negative control of t_rqueue only: the last submission's fence, as a fence read before its own is published
                sync_fence_store(sy, nvn_be_sub_fence());
#else
                sync_fence_store(sy, nvn_be_sub_fence() - 1);
#endif
                break; }
            case OP_REPORT_COUNTER: nvn_be_report_counter(p, a); break;
            case OP_RESET_COUNTER: nvn_be_reset_counter(p, a); break;
            case OP_COPY_B2B: { uint64_t s_, d, n; memcpy(&s_, a, 8); memcpy(&d, (const uint8_t *)a + 8, 8); memcpy(&n, (const uint8_t *)a + 16, 8);
                PD("Y copy_b2b cmd=%p src=%#llx dst=%#llx bytes=%llu", p, (unsigned long long)s_, (unsigned long long)d, (unsigned long long)n); nvn_be_copy_b2b(s_, d, n); break; }
            case OP_UPDATE_UB: { uint64_t d, n; memcpy(&d, a, 8); memcpy(&n, (const uint8_t *)a + 8, 8);
                const uint8_t *data = (const uint8_t *)a + 16;
                if (passdump()) { char fv[400] = ""; size_t fo = 0; for (uint64_t q = 0; q + 4 <= n && q < 64; q += 4) { float f; memcpy(&f, data + q, 4); fo += snprintf(fv + fo, sizeof fv - fo, " %g", f); }
                    PD("Y update_ub cmd=%p dst=%#llx bytes=%llu data%s", p, (unsigned long long)d, (unsigned long long)n, fv); }
                nvn_be_update_ub(data, d, n);
                break; }
            case OP_SET_RT: nvn_be_set_rt(p, a); break;
            case OP_RENDER_ENABLE: nvn_be_render_enable(p, a); break;   // SetRenderEnable: off discards clears and draws until it is on again (queue state, kept across command buffers)
            case OP_CLEAR_COLOR: nvn_be_clear_color(p, a); break;
            case OP_CLEAR_DS: nvn_be_clear_ds(p, a); break;
            case OP_DRAW: g_exec_next = p + h.size; nvn_be_draw(a, NULL); g_exec_next = NULL; break;   // Lookahead start point
            case OP_DRAW_IDX: g_exec_next = p + h.size; nvn_be_draw(NULL, a); g_exec_next = NULL; break;
            // Do not mark dirty if the state did not change (the game often re-sends the same program / state block / vertex format before each draw; the dirty flag only decides whether to re-fetch the pipeline / depth-stencil object, and equal values mean the same object)
            case OP_SCISSOR: case OP_BIND_PROGRAM: case OP_BIND_STATE: case OP_BIND_VATTR: case OP_BIND_VSTREAM: case OP_BIND_VBUF:
            case OP_BIND_UBO: case OP_BIND_SSBO: case OP_BIND_TEX: case OP_BIND_SAMP: case OP_BIND_IMG: case OP_STENCIL:
            case OP_POLY_OFFSET: case OP_VIEWPORTS: case OP_DEPTH_RANGE: case OP_BLEND_COLOR: case OP_COPY_STRIDE: case OP_COPY_IMAGE_STRIDE: case OP_DEPTH_CLAMP: case OP_PRIM_RESTART: case OP_DEPTH_BOUNDS: case OP_RASTER_DISCARD:
                { int st = st_op(h.op, a); if (st) nvn_be_state(h.op, a, st); } break;
            case OP_COPY_B2T: nvn_be_copy_b2t(a); break;
            case OP_COPY_T2B: nvn_be_copy_t2b(a); break;
            case OP_COPY_T2T: nvn_be_copy_t2t(a); break;
            case OP_CLEAR_TEX: nvn_be_clear_tex(a); break;
            case OP_DISPATCH: nvn_be_dispatch(a); break;
            case OP_BARRIER: nvn_be_barrier(p, a); break;
            case OP_SET_TEX_POOL: case OP_SET_SAMP_POOL: nvn_be_set_pool(p, a); break;
            default: break;
        }
        p += h.size;
    }
}

