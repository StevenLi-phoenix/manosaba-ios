// gfx/nvn/core/rt.h: render target helpers in plain C (plan item 264): a mip level's extent, an attachment's subresource, whether a scissor covers
// a target
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"

// A render attachment selects one mip level, while the texture's size is the base level's
static inline uint64_t nvn_mip_extent(uint64_t size, uint64_t level) { return level < 64 && size >> level > 1 ? size >> level : 1; }
static inline void rt_sub(const p_set_rt *r, int k, uint64_t *ser, int *lv, int *sl) {   // Subresource of attachment k (0–7 color, 8 depth)
    const tex_t *t = k == 8 ? r->depth : r->color[k]; const view_t *v = k == 8 ? &r->dview : &r->cview[k]; int hv = k == 8 ? r->has_dview : r->has_cview[k];
    *ser = t ? t->serial : 0; *lv = hv && v->set_levels ? v->base_level : 0; *sl = hv && v->set_layers ? v->min_layer : 0;
}
static inline int scissor_full_of(const p_scissor *sc, int has_sc, const tex_t *t, const view_t *v, int has_v) {
    uint64_t level = has_v && v->set_levels ? v->base_level : 0;
    uint64_t w = nvn_mip_extent(t->d.width, level), h = nvn_mip_extent(t->d.target == 0 || t->d.target == 3 ? 1 : t->d.height, level);
    return !has_sc || (sc->x <= 0 && sc->y <= 0 && (int64_t)sc->x + sc->w >= (int64_t)w && (int64_t)sc->y + sc->h >= (int64_t)h);
}
