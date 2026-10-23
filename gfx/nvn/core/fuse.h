// gfx/nvn/core/fuse.h: the readable-tile pass merge's plan in plain C (plan item 264). When a draw of the later pass samples, at the same pixel, a
// color attachment written by the earlier pass, the two passes are merged into one render pass: what the earlier pass wrote stays in the tile,
// and the later pass reads it directly with the readable tile version (<key>_bf, function-constant specialization), instead of first landing in
// main memory and being read back just to be sampled. Planning (fuse_try, at the start of the earlier pass) does only structural checks; the
// backend proves same-pixel addressing at every draw of the later pass (fz_p2_draw), and if it cannot the merged render pass is ended and a
// normal render pass is reopened with the later pass's original targets (the earlier pass's result is stored as usual, equivalent to not
// merging). Checked without a GPU by gfx/tests/t_core_fuse.c
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "core.h"
#include "state.h"

// State of the readable-tile pass merge (merge B step 2): the earlier pass (phase 1) and the later pass (phase 2) share one render pass, and the attachments are the union
typedef struct {
    int active, phase, n1, nskip;
    int nreuse;
    const uint8_t *rt2_op, *reuse[8], *skip[12];
    p_set_rt rt1, rt2;                 // Logical targets of each of the two passes (R.rt is the union during the merge)
    int8_t p2map[8];                   // Logical color number of the later pass → physical attachment
    uint8_t lmap1[8], lmap2[8];        // Physical attachment → logical color number of this phase's draws (0xff = not written)
    uint8_t clr2[8], cd2f, cs2f; nvn_rgba cc2[8]; double cd2; uint32_t cs2;   // Clears at the start of the later pass (used as-is when the in-run dual render redraws)
    uint8_t wrote[8];                   // Physical attachments written by the later pass
    uint8_t cov2[8];                     // Physical attachments covered by the first draw of the later pass
} fuse_t;
extern fuse_t g_fz; extern uint64_t g_fz_n, g_fz_draws, g_fz_fallback;   // The merge; merges planned, later-pass draws proven, merged passes ended for a draw not proven
// At the draw opening a pass (no merge active, g_exec_next set): plan merging the next pass into it. enc_clear_only: the last pass opened was a
// clear-only one (setting its targets again is then not taken as this pass's own). On success g_fz is active in phase 1, R.rt is the union of
// attachments (the earlier pass's at their indices, the later pass's own appended) with the later pass's clears pending on them, and its
// SetRenderTargets and clears are in the skip set
void fuse_try(int enc_clear_only);
void fz_unskip(void);   // A merge ended before its later pass: the later pass's SetRenderTargets and clears taken out of the skip set

// The backend provides:
// g_fz.cov2: of the later pass's own attachments (n1 to n), those its first draw (first) covers in full, proven as the load elision proves them
void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n);
