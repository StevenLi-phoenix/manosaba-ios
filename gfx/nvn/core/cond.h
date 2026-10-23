// gfx/nvn/core/cond.h: conditional rendering in plain C (plan item 264): SetRenderEnable(Conditional) and the samples-passed results it reads.
// Checked without a GPU by gfx/tests/t_core_cond.c
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "core.h"

extern int g_render_off;      // SetRenderEnable(false) is in effect (NVN reference: clears and draws are dropped while it is)
// Conditional rendering counted (plan item 112's measure): conditionals, those on a result still on the GPU (rendering kept on), and the draws and
// clears replayed while one of those was in effect; a line every 300 frames when there were any
extern uint64_t g_rc_n, g_rc_pending, g_rc_pend_draws;
extern int g_rc_on_pending;   // the conditional in effect was on a pending result
void rc_report(uint64_t f);
// SetRenderEnableConditional compares the 64-bit values at counters and counters + 16 (the NVN reference: two counter reports, one after the other) and
// renders if they are equal (mode 2) or not equal (mode 3). The host decides when it replays the command, from guest memory: counter reports other
// than samples passed are written in command order at replay (OP_REPORT_COUNTER), so a report earlier in the same submission is seen. A samples-passed
// result exists only after the GPU completes its submission: while one is pending on either value, rendering stays enabled (drawing too much is safe).
// Tears of the Kingdom clears each of its lighting buffers once per frame this way: conditional (not equal) clear, then a TIMESTAMP_TOP report into
// the same counters, whose counter half is 0 (the NVN reference: timestamp reports write 0 there), so the next conditional clear of the frame is
// skipped and the second lighting pass adds to the first. Clearing every time had wiped the first pass's light: the title mural stayed black.
// Samples-passed report destinations: address → fence of the latest submission that writes it (open addressing, one entry per distinct address,
// grown at half load; replay thread only). This was a ring of the last 256 reports: with more reports in flight a destination still pending had
// left the ring and its previous result was read (t_nvn 6c2b). Tears of the Kingdom makes about one report a frame (3 destinations), so it never
// hit that.
void occ_note(uint64_t addr, uint64_t fence);   // A samples-passed report to addr, written when the submission with that fence completes
int occ_pending(uint64_t addr);                  // A samples-passed result at addr is still on the GPU
void cond_render_enable(const p_render_enable *r);   // SetRenderEnable(Conditional): g_render_off from its mode, or from the two counters it compares
