// gfx/nvn/core/pass.h: pass planning in plain C (plan item 264): where the replay is (the op being replayed, the op after the current draw, the
// callers to return to, the submission's handles still to come) and a cursor walking on from there in replay order; the ops run before their
// place and skipped where they stand; what a pass opening at a draw needs of depth and stencil; at a pass end, whether each attachment is stored;
// the bandwidth ledger. Checked without a GPU by gfx/tests/t_core_pass.c
#pragma once
#include <stdint.h>
#include <string.h>
#include "../nvn_obj.h"
#include "core.h"

// ---- Where the replay is ----
extern const uint8_t *g_exec_cur;                      // The op being replayed (inclusive): the store decision of a pass ending there starts there
extern const uint8_t *g_exec_next;                     // The op following the current draw in the replay (NULL outside a draw): the lookaheads start there
extern const uint8_t *g_ret[16]; extern int g_ret_n;   // Call return stack of the replay (at OP_CALL the caller's next op is pushed; g_ret_n counts past 16)
extern const uint64_t *g_rest; extern int g_nrest;     // Handles in this submission still to be replayed after the current handle (mtl_exec_rest)

// ---- Ops run before their place ----
// Clear-only pass hoist (hoist_try) and the readable-tile pass merge: ops that were already executed early and must be skipped when replay reaches
// their original position (each group's SetRenderTargets and the clears in it). The exec loop, the store lookahead (sdx index) and the
// depth-stencil lookahead all skip by it - otherwise the lookahead would take the clear at the original position as 'cleared again' and wrongly
// judge the early-cleared content redundant
#define HOIST_MAX 40   // At most 4 groups × (1 set-target op + 8 clears)
extern const uint8_t *g_hoist_skip[HOIST_MAX]; extern int g_hoist_n; extern uint64_t g_hoist_groups;
static inline int hoist_skipped(const uint8_t *p) { for (int i = 0; i < g_hoist_n; i++) if (g_hoist_skip[i] == p) return 1; return 0; }
int hoist_take(const uint8_t *p);   // The replay reached p: 1 when it ran early (it leaves the set)

// A cursor in real replay order from an op: jumps and calls followed (8 deep, deeper sets bad), a list's end back to its caller, then the
// remaining handles of this submit; ops run early are skipped. Returns 0 at the submission's end
typedef struct { const uint8_t *p, *ret[8]; int ret_n, rest_i, bad; } op_cursor;
static inline int op_next(op_cursor *c, const uint8_t **out, op_hdr *oh, const void **oa) {
    for (int guard = 0; guard < 1000000; guard++) {
        if (!c->p) return 0;
        op_hdr h; memcpy(&h, c->p, sizeof h); const void *a = c->p + 8;
        switch (h.op) {
            case OP_END:
                if (c->ret_n) c->p = c->ret[--c->ret_n];
                else if (c->rest_i < g_nrest) c->p = (const uint8_t *)g_rest[c->rest_i++];
                else { c->p = NULL; return 0; }
                continue;
            case OP_JUMP: memcpy(&c->p, a, 8); continue;
            case OP_CALL: { uint64_t t; memcpy(&t, a, 8); if (c->ret_n >= 8) { c->bad = 1; return 0; }
                c->ret[c->ret_n++] = c->p + h.size; c->p = (const uint8_t *)t; continue; }
            default: break;
        }
        const uint8_t *p = c->p; c->p += h.size;
        if (g_hoist_n && hoist_skipped(p)) continue;
        if (out) *out = p; if (oh) *oh = h; if (oa) *oa = a; return 1;
    }
    c->bad = 1; return 0;
}

// ---- Does a pass use depth / stencil ----
// When opening a pass, scan forward from the op after the current draw, simulating the changes of depth-stencil state, up to where this pass must
// end (target reset, copy, compute dispatch, an inline uniform update unless renamed - matching one-to-one the ops that end a pass in the replay).
// If no draw in between enables depth test / depth write / stencil test, this pass may leave the depth attachment unattached: not one byte of the
// depth content is touched, and on TBDR the load and store of the whole depth texture are saved (measured on a real device: a single-draw
// fullscreen pass takes half the GPU, with an extra D32S8 load and store attached). The scan follows real replay order across sub-list returns
// and across the remaining handles of this submission; end of submission = the pass must end. Undecidable cases such as a mid-pass clear or
// too-deep nesting always count as 'used'.
// The need of the pass opening at the current draw (bits: 1 depth 2 stencil; 3 = both needed or cannot be proven)
int pass_ds_need(void);
extern uint64_t g_stat_ds_omit;                  // Number of passes that attach less depth-stencil because the lookahead proved it unused (including stencil-only / depth-only)
extern uint64_t g_stat_s_only, g_stat_d_only;    // Of those, the number of passes that attach only stencil / only depth

// ---- Store lookahead: at pass end, decide from the later commands of the same submission whether each attachment's write-back is redundant ----
// On TBDR every attachment is written back to memory in full when the pass ends; Switch (Maxwell immediate-mode rendering) has no such cost. If an
// attachment is cleared in full before being read, the write-back is redundant (DontCare). Decided by the store index (store_index.c, sdx_first):
//   Redundant: after being set as an attachment again and before any draw, it is cleared in full (color: all four channels of that index written;
//   depth: depth clear; stencil: all 8 stencil bits cleared. Depth and stencil are decided separately and their write-back is set separately)
//   Needed: at a draw / dispatch it is bound to a texture or image slot of any stage (including ones already bound when the scan starts); used as
//   copy source / copy destination / texture-clear target; drawn before cleared after becoming an attachment again (load)
// Different levels / layers of the same texture are decided separately (a downsample chain writes level k and reads level k−1); a sampler binding
// counts as a read of the whole texture (conservative). Only what can be proven within the same submission is accepted, and anything still
// undecided at the end of the submission is always written back; so is a pass ending outside a list (no op being replayed).
// The open pass's attachments (the backend fills them when it opens the pass):
// depth: 0 color 1 depth 2 stencil (the two parts of a combined depth-stencil texture are decided separately); state 0 undecided 1 needed 2
// redundant; idx: color attachment index, -1 for depth; why: 1 a draw reads it as a texture 2 as an attachment, drawn before cleared 3 copy 4
// texture clear 5 scanned to the end undecided 6 no start point / index unavailable; pw: cleared, or written by a draw that samples none of the
// pass's attachments (fbx_prepare's split, plan item 165)
typedef struct { uint64_t ser, bytes; uint32_t lv, sl; int idx; uint8_t depth, state, ridx, drawn, why, load, wrote, wwhy, pw; const char *wkey; int wslot; } sd_att;
extern sd_att g_sd[9]; extern int g_nsd;
extern uint64_t g_sd_dead_bytes, g_sd_n_dead, g_sd_n_all;
// At the pass end: store[i] 1 when g_sd[i] is written back, 0 when not (DontCare); counted, logged (pass dump Z and U lines), the cross-submission
// validity ledger kept (tbdr.h). g_nsd is left to the caller
void sd_decide(uint8_t store[9]);

// ---- The bandwidth ledger ----
extern uint64_t g_bw_load, g_bw_store, g_pass_full_rw, g_udc_saved_bytes;   // Attachment load / store bytes, number of full load-store passes, store bytes saved on attachments loaded and not written (udc_on)
