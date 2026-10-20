// Where a render target of a virtual pool goes in that pool's heap (plan item 138; the placement rule is totk-mobile's nvn_texture_alias.inc,
// Apache-2.0, written anew here). The heap is the pool's size × scale; a target goes at its guest offset × scale, aligned up, only if its Metal size
// fits inside its guest range × scale. So targets disjoint in guest memory are disjoint in the heap (the first ends by floor((o1 + s1) × scale), the
// second starts at ceil(o2 × scale) or later), and those that overlap share memory only where the game itself reuses it. A target that does not
// fit keeps a texture of its own. Unit test gfx/tests/t_rtheap.m. Pure: no Metal
#pragma once
#include <math.h>
#include <stdint.h>

#define RT_HEAP_SCALE 1.25   // totk-mobile: Great Sky Island at 1.125 left 56 MiB of targets outside, at 1.25 all but 3 MiB

static inline uint64_t rt_heap_size(uint64_t pool, double scale) { return ((uint64_t)ceil((double)pool * scale) + 0xffff) & ~(uint64_t)0xffff; }

// 1 and *place = the heap offset for a target at guest offset off of size bytes whose Metal texture needs need bytes aligned to align; 0 when it
// does not fit
static inline int rt_heap_place(uint64_t off, uint64_t size, double scale, uint64_t need, uint64_t align, uint64_t heap, uint64_t *place) {
    if (!align) align = 1;
    uint64_t p = ((uint64_t)ceil((double)off * scale) + align - 1) / align * align;
    uint64_t limit = (uint64_t)floor((double)(off + size) * scale);
#ifdef DM_TEST_RTHEAP_NO_LIMIT   // negative control of t_rtheap only: a target may run past its guest range × scale
    limit = heap;
#endif
    if (p + need > limit || p + need > heap) return 0;
    *place = p;
    return 1;
}
