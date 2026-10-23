// gfx/nvn/nvn_backend.h: the execution interface (plan item 264). The replay (gfx/nvn/core/exec.c, nvn_exec) walks a submission's commands in
// replay order and keeps what needs no GPU: where the replay is and the ops run before their place (core/pass.h), the draw state (core/state.h),
// the deferred groups' marks (core/defer.h), FenceSync's fence. Each command that executes something it hands to the backend here, in command
// order: gfx/nvn/nvn_mtl.m implements them on Metal, and a second backend (Vulkan) implements the same set. What the core's decisions ask of the
// backend besides is declared with each module ("The backend provides": core/store_index.h, usnap.h, pass.h, defer.h, fuse.h).
#pragma once
#include <stdint.h>
#include "nvn_obj.h"

void nvn_be_reset(void);                                     // The first command ever replayed: the backend's own state as at the start (S and R are reset already)
void nvn_be_state(uint32_t op, const void *a, int c);        // A command that only sets state, applied to S / R; c: what changed (st_op's answer, ST_*)
void nvn_be_draw(const p_draw *dw, const p_draw_idx *di);    // A draw (one of the two); g_exec_next is the op after it
void nvn_be_dispatch(const p_dispatch *d);
void nvn_be_copy_b2b(uint64_t src, uint64_t dst, uint64_t n);            // A buffer copy in GPU order
void nvn_be_update_ub(const void *data, uint64_t dst, uint64_t n);       // An inline uniform update: dst's n bytes become data, in GPU order
void nvn_be_copy_b2t(const p_copy_b2t *c);
void nvn_be_copy_t2b(const p_copy_b2t *c);
void nvn_be_copy_t2t(const p_copy_t2t *c);
void nvn_be_clear_tex(const p_clear_tex *c);
void nvn_be_signal_event(volatile uint32_t *val, uint32_t mode, uint32_t v);   // The event's value written once the GPU has completed this submission (mode 0 write, 1 add)
uint64_t nvn_be_sub_fence(void);                             // The fence this submission commits with (FenceSync records take it)
// The render pass (SetRenderTargets and the clears decide with these: core/exec.c)
int nvn_be_pass_mode(void);                                  // The open pass's mode: -1 none, 0 all attachments, 1 opened only for clears, 2-4 depth / stencil left out
void nvn_be_pass_flush(void);                                // End the open pass; with clears pending and none open, a pass that lands them
void nvn_be_pass_end(void);                                  // End the open pass (its pending clears stay for the next one)
int nvn_be_layered(void);                                    // The open pass draws several layers (layered pass)
int nvn_be_layer_continue(const p_set_rt *r);                // In a layered pass: r is another layer of its range, which goes on in it
void nvn_be_clear_quad(const p_clear_color *c);              // A colour clear drawn (inside a pass, a partial channel mask or a partial scissor)
void nvn_be_clear_ds_quad(const p_clear_ds *c);              // A depth / stencil clear drawn
// The records whose handling stays with the backend (p: the record, a: its payload)
void nvn_be_report_counter(const uint8_t *p, const void *a);
void nvn_be_reset_counter(const uint8_t *p, const void *a);
void nvn_be_barrier(const uint8_t *p, const void *a);
void nvn_be_set_pool(const uint8_t *p, const void *a);       // SetTexturePool / SetSamplerPool
