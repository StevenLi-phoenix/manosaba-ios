// The replay's state without a GPU (gfx/nvn/core/state.c, plan item 264). The state at the start; a command that only sets state is applied and
// answers what changed: the program (bound per stage: the pipeline and every stage's arguments), a blend / depth-stencil state (the pipeline / the
// depth-stencil state), vertex formats (the pipeline), uniform / storage buffers, textures, samplers and images (that stage's arguments; textures
// and samplers also their handles), the stencil masks (the depth-stencil state; the reference value not); the same value again changes nothing;
// a binding out of range is not taken; the scissor, viewports, depth range, blend colour, depth clamp and bounds, raster discard, primitive restart
// and copy strides are set; any other command is not a state command; a deferred binding record is read at its first use in a submission.
// A depth-stencil state change taken for a pipeline change (-DDM_TEST_ST_DSS_AS_PSO, the broken variant): 1 fails.
#include "../nvn/core/state.h"
#include "../nvn/core/usnap.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <string.h>


int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    st_reset();
    check_x("the start: blends ONE / ZERO add at each target, logic op copy, every channel written", 1, S.blend[3].target == 3 && S.blend[0].src_rgb == NVN_BLEND_FUNC_ONE && S.color.logic_op == NVN_LOGIC_OP_COPY && S.chmask.mask == 0xffffffffu);
    check_x("... depth LESS with writes, stencil masks 0xff, depth range [0, 1]", 1, S.ds.dw == 1 && S.ds.df == NVN_DEPTH_FUNC_LESS && S.st_vmask[1] == 0xff && S.dr[0] == 0 && S.dr[1] == 1);
    p_draw dw = {0}; check_x("a draw is not a state command", 0, st_op(OP_DRAW, &dw));
    prog_t v = {0}, f = {0}; v.nstage = 1; v.kstage[0] = 0; v.key[0][0] = 1; f.nstage = 1; f.kstage[0] = 1; f.key[0][0] = 2;
    p_bind_program bp = {&v, 1}; check_x("a program: the pipeline and every stage's arguments", ST_STATE | ST_PSO | ST_ARGS_ALL, st_op(OP_BIND_PROGRAM, &bp));
    bp = (p_bind_program){&f, 2}; st_op(OP_BIND_PROGRAM, &bp);
    check_x("... bound per stage: a composite of both", 2, S.prog ? (long long)S.prog->nstage : -1);
    check_x("... the same again: nothing", ST_STATE, st_op(OP_BIND_PROGRAM, &bp));
    p_bind_state bs = {.kind = 5}; ds_st ds = S.ds; ds.df = NVN_DEPTH_FUNC_GREATER; memcpy(bs.bytes, &ds, sizeof ds);
    check_x("a depth-stencil state: the depth-stencil state [-DDM_TEST_ST_DSS_AS_PSO]", ST_STATE | ST_DSS, st_op(OP_BIND_STATE, &bs));
    check_x("... the same again: nothing", ST_STATE, st_op(OP_BIND_STATE, &bs));
    bs = (p_bind_state){.kind = 0}; blend_st bl = S.blend[2]; bl.dst_rgb = NVN_BLEND_FUNC_ONE; memcpy(bs.bytes, &bl, sizeof bl);
    check_x("a blend state: the pipeline, at its target", 1, st_op(OP_BIND_STATE, &bs) == (ST_STATE | ST_PSO) && S.blend[2].dst_rgb == NVN_BLEND_FUNC_ONE && S.blend[1].dst_rgb == NVN_BLEND_FUNC_ZERO);
    p_bind_vattr va = {0, 2, {0x11, 0x22}}; check_x("vertex attributes: the pipeline", ST_STATE | ST_PSO, st_op(OP_BIND_VATTR, &va));
    check_x("... the same again: nothing", ST_STATE, st_op(OP_BIND_VATTR, &va));
    p_bind_buf ub = {1, 2, 0x1000, 256}; check_x("a uniform buffer at stage 1: that stage's arguments", ST_STATE | ST_ARGS(1), st_op(OP_BIND_UBO, &ub));
    check_x("... the same again: nothing", ST_STATE, st_op(OP_BIND_UBO, &ub));
    p_bind_buf sb = {6, 0, 0x2000, 64}; check_x("a storage buffer at stage 6 (out of range): not taken", ST_STATE, st_op(OP_BIND_SSBO, &sb));
    p_bind_handles th = {0}; th.stage = 0; th.first = 3; th.n = 1; th.h[0] = 0x55;
    check_x("a texture at stage 0: its arguments and the handles", ST_STATE | ST_ARGS(0) | ST_HANDLES, st_op(OP_BIND_TEX, &th));
    check_x("... in its slot", 0x55, (long long)S.tex[0][3]);
    th.stage = 4; check_x("a sampler at stage 4: its arguments and the handles", ST_STATE | ST_ARGS(4) | ST_HANDLES, st_op(OP_BIND_SAMP, &th));
    th.stage = 1; th.first = 31; th.n = 2; check_x("an image record past slot 31: not taken", ST_STATE, st_op(OP_BIND_IMG, &th));
    th.first = 0; th.n = 1; check_x("an image at stage 1: its arguments", ST_STATE | ST_ARGS(1), st_op(OP_BIND_IMG, &th));
    static uint64_t src[1] = {0x99}; p_bind_handles dh = {0}; dh.stage = 2; dh.n = 1; dh.defer = 1; dh.src = (uint64_t)(uintptr_t)src; g_bh_gen = 5;
    st_op(OP_BIND_TEX, &dh); check_x("a deferred record read at its first use", 0x99, (long long)S.tex[2][0]);
    p_stencil sm = {3, 0x0f, 0, 0, 0}; check_x("a stencil value mask: the depth-stencil state, both faces", 1, st_op(OP_STENCIL, &sm) == (ST_STATE | ST_DSS) && S.st_vmask[0] == 0x0f && S.st_vmask[1] == 0x0f);
    sm = (p_stencil){1, 0, 0, 0x2d, 2}; check_x("a stencil reference: set, not a depth-stencil state change", 1, st_op(OP_STENCIL, &sm) == ST_STATE && S.st_ref[0] == 0x2d && S.st_ref[1] == 0);
    p_scissor sc = {1, 2, 3, 4}; st_op(OP_SCISSOR, &sc); check_x("the scissor (the render targets' state)", 1, R.has_sc && R.sc.w == 3);
    p_viewports vp = {0}; vp.first = 1; vp.n = 1; vp.v[0][2] = 640; st_op(OP_VIEWPORTS, &vp); check_x("viewport 1", 1, S.has_vp && S.vp[1][2] == 640);
    p_depth_range dr = {0.25f, 0.75f}; p_blend_color bc = {{1, 0, 0, 1}}; p_depth_clamp dc = {1}; p_depth_bounds db = {1, 0.1f, 0.9f}; int32_t rd = 1; p_prim_restart pr = {1, 0xffff}; p_copy_stride cs = {256};
    int all = st_op(OP_DEPTH_RANGE, &dr) & st_op(OP_BLEND_COLOR, &bc) & st_op(OP_DEPTH_CLAMP, &dc) & st_op(OP_DEPTH_BOUNDS, &db) & st_op(OP_RASTER_DISCARD, &rd) & st_op(OP_PRIM_RESTART, &pr) & st_op(OP_COPY_STRIDE, &cs);
    check_x("depth range, blend colour, depth clamp and bounds, raster discard, restart, copy stride: set, nothing else changes", 1,
          all == ST_STATE && S.dr[0] == 0.25f && S.blend_color[0] == 1 && S.depth_clamp && S.db_en && S.raster_discard && S.restart_en && S.restart_idx == 0xffff && S.copy_stride == 256);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
