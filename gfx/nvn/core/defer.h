// gfx/nvn/core/defer.h: the deferred merged pass's decisions in plain C (plan item 264). At the draw opening a pass P that clears its targets, when
// a later pass Q of other targets attaches every colour target of P at the same index (the same texture, level and layer) and the passes in
// between neither sample P's colour targets, nor write what P samples, nor write P's depth / stencil, P is not opened there: its draws are kept
// with their draw state (defer_take) and drawn when Q's pass opens, P's clears made Q's load actions, so P's attachments are not stored and loaded
// again in between. The store lookahead index screens first (defer_pre), then one scan in replay order decides (defer_try). Up to 4 groups at
// once; a pass that is the Q of waiting groups and is itself deferred takes them into its group. At Q's SetRenderTargets the replay ends the pass
// before it and sets Q's targets (core/exec.c); the backend draws the kept draws when Q's pass opens (defer_replay) and, for groups still waiting
// at a submission's end, draws them on their own targets (defer_fallback).
// DM_NVN_DEFER=0 turns it off; DM_NVN_DEFER_AB=1 keeps it off until the second screenshot. Checked without a GPU by gfx/tests/t_core_defer.c
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "core.h"
#include "state.h"

typedef struct { int idx; p_draw dw; p_draw_idx di; p_scissor sc; int has_sc; void *S_; uint8_t pm[8]; } defer_snap;   // pm: group P target index → logical color number of this draw (identity for its own draws; a composed mapping for absorbed upstream groups)
typedef struct {
    int active, at_q, in_p, n, reason, reason_op, logged, reset_between; uint64_t born;   // born: creation serial (replay is ordered by it, not by slot)
    p_set_rt p_rt, q_rt;
    const uint8_t *p_end_op, *q_op;
    uint8_t clear_c[8], lmap[8];
    nvn_rgba cc[8];
    uint8_t clear_d, clear_s;
    double cd; uint32_t cs;
    uint64_t p_reads[512]; int nreads, p_dw, p_st;
    defer_snap snap[64];
} defer_t;
extern defer_t g_defer_slots[4];
extern defer_t *g_defer_cur;   // The group being built or drawn
#define g_defer (*g_defer_cur)
extern uint64_t g_defer_n, g_defer_draws, g_defer_fallbacks;   // Groups made, draws kept, groups drawn on their own targets at a submission's end
extern const uint8_t *g_defer_pend;   // The P-range end op of the group that is in its P range (NULL if none): the exec loop compares each op against just this one pointer
extern int g_defer_nact, g_defer_natq;   // Number of active groups, number of groups that reached Q and await replay (the hot path only looks at the counts, no per-slot scan)
extern int g_defer_ab, g_defer_ab_on;    // DM_NVN_DEFER_AB=1: off until the second screenshot turns g_defer_ab_on (shot.inc)
extern int g_defer_replaying;            // The backend is drawing kept draws: no new group, no lookahead
static inline int defer_has_in_p(void) { return g_defer_pend != NULL; }
static inline int defer_has_active(void) { return g_defer_nact > 0; }
int defer_next(int at_q_only, uint64_t after);   // Slot number of the next active group (smallest creation serial that is > after); returns −1 if none
void defer_release_one(defer_t *d);
void defer_reset(void);                          // At the start of a submission: no group; the switches read again
// At the draw opening a pass (no group in its P range, no merged pass being built or checked, g_exec_next set): try to make a group of this
// pass. On success the current group is in its P range (g_defer.in_p), the draw kept and R's pending clears taken into the group
void defer_try(const p_draw *dw, const p_draw_idx *di);
int defer_take(const p_draw *dw, const p_draw_idx *di);   // A draw of the current group's P kept with the draw state (0: 64 kept already, reason 20)
int defer_q_first(const uint8_t *op);                     // op opens the Q of a waiting group: the first such slot, else -1
void defer_q_enter(const uint8_t *op, int first);         // After the backend ended the pass before op: Q's targets (with any group's depth) and the groups' clears in R, the groups at Q

// The backend provides:
int nvn_be_handle_view(uint64_t h, uint64_t *ser, view_t *v, int *has_v);   // The texture serial and view of a texture / image handle; 0 when none is registered
