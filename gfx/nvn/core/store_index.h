// gfx/nvn/core/store_index.h: the store lookahead index (plan item 264 moved it here from the Metal backend). At the start of a submission one linear
// scan in replay order records the events of each texture that is, or was, an attachment; when a render pass ends, a binary search finds, for each
// attachment, the first valid event after the pass end, which decides whether its write-back is needed (sdx_first).
// Replaces the original "scan the whole remainder forward at every render pass end" (pass count x command count, 49 render passes per frame in camp). Cross-checked per attachment against the original scan: after 0 mismatches over the 17 pages of the screenshot gate + a camp run with about 340,000 attachment decisions (about 46,000 redundant), the original scan was removed.
// Events (seq = replay sequence number): READ a draw / dispatch reads this texture per program reflection; ATT it is a current attachment at a draw; CLEAR cleared as a whole block while an attachment; COPY / CLRTEX either end of a copy / the cleared texture.
// Decision (the first valid event decides): READ / COPY / CLRTEX -> needed; ATT whose SetRenderTargets is after the render pass end -> needed (load); CLEAR with the same -> redundant; no event -> needed.
// On any uncertainty (replay sequence number does not match the index, nesting too deep) the index is invalidated and all remaining render passes of this submit store (storing is always correct, it just saves less bandwidth).
// Checked without a GPU by gfx/tests/t_core_store_index.c.
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "../nvn_refl.h"
#include "core.h"

typedef struct { uint32_t seq, rtseq; uint64_t ser; const uint8_t *op; uint16_t lv, sl; uint8_t kind, plane, anyls, why; } sdx_ev;   // plane 0 color 8 depth-stencil (ATT) 1 depth 2 stencil (CLEAR)
enum { SDX_READ = 1, SDX_NEED, SDX_ATT, SDX_CLEAR };
#define SDX_NONE 0xffffffffu
#define SDX_HB 2048   // Candidate texture slots (open addressing); a submission with more than half of them in use loses its index
// This submission's index. After sdx_build the events of bucket b (sdx_bucket) are ev[bst[b] .. bst[b + 1]): in replay order, and within one
// sequence number a draw's reads before its attachments
typedef struct {
    sdx_ev *ev; uint32_t n;              // Events
    int ok;                              // Index valid
    uint32_t nb, bst[SDX_HB + 1];        // Buckets in use, and where each one starts in ev
    const uint8_t **op; uint32_t nop;    // op[seq]: the record at each replay sequence number
    uint32_t exec_seq;                   // Sequence number of the next operation during replay (sdx_seq)
    uint32_t *rtx, nrtx, *drx, ndrx;     // This submit's SET_RT / draw sequence number tables (used by the pre-screen of the readable tile merge)
} sdx_index;
extern sdx_index g_sdx;
// The replay state at the start of the submission, where the scan starts from
typedef struct {
    const p_set_rt *rt;                  // The targets at the start of the submit (NULL: none): the first draw may reopen their render pass without going through SetRenderTargets
    p_scissor sc; int has_sc;
    const prog_t *prog;                  // The bound program
    int render_off;                      // Render enable not known to be on: queue state carried from the last submission
    uint64_t slot[6][160];               // Texture serials bound per stage: 0-127 textures, 128-159 images
} sdx_start;

int sdx_begin(int on);                   // Start of a submission's index (on 0: unavailable for this submission); returns g_sdx.ok
int sdx_bucket(uint64_t ser, int add);   // The bucket number of a texture; -1 if not in the candidate set (and not adding); invalidates the index when full
void sdx_build(const sdx_start *st, const uint64_t *h, int n);   // Scan the submission's n command handles h and bucket the events
// Every operation in the replay, in order: check the sequence number against the index (on mismatch invalidate, and store everything for the rest)
static inline void sdx_seq(const uint8_t *p) {
    if (!g_sdx.ok) return;
    if (g_sdx.exec_seq >= g_sdx.nop || g_sdx.op[g_sdx.exec_seq] != p) { RED_ONCE(53, "store lookahead index does not match the replay order (seq %u / %u), all remaining passes of this submit store", g_sdx.exec_seq, g_sdx.nop); g_sdx.ok = 0; return; }
    g_sdx.exec_seq++;
}
// The sequence number P of cur, the operation being replayed, when the index can decide there; 0 when it cannot
static inline int sdx_pos(const uint8_t *cur, uint32_t *P) {
    if (!g_sdx.ok || !cur || !g_sdx.exec_seq || g_sdx.op[g_sdx.exec_seq - 1] != cur) return 0;
    *P = g_sdx.exec_seq - 1; return 1;
}
// The decision for one attachment subresource (depth 0 color 1 depth 2 stencil) of the render pass that ends at P: 2 redundant; otherwise why << 2,
// the reason it is needed (1 a draw reads it 2 drawn to as an attachment before cleared 3 copy 4 texture clear 5 no event to the end). The
// operations in skip (nskip of them: the clear-only pass hoist moved them) do not count
uint8_t sdx_first(uint64_t ser, uint32_t lv, uint32_t sl, int depth, uint32_t P, const uint8_t *const *skip, int nskip);

// The backend provides:
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]);   // Each shader's reflection (NULL: none) and its stage; 0 when the program is not known (every bound texture then counts as read)
void nvn_be_bind_sers(const p_bind_handles *b, int n, uint64_t *ser);          // The texture serials of a binding record's first n handles (n may be 0: the record is still read, once a submission when deferred)
