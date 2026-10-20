// Anisotropic filtering raised by a switch (DM_NVN_ANISO=<2..16>, off by default so the shots stay comparable with the console footage; tvc97's
// TOTK_ANISO): about 6,900 of Tears of the Kingdom's samplers ask for 1. Raised only on mipmapped samplers that minify linearly, without a depth
// compare or a min / max reduction (those read exact texels); a sampler asking for more keeps its own. NVN min filters as nvn_mtl.m samp_by_id
// reads them: 3 linear within the nearest level, 5 linear between levels (the mipmapped linear ones). Unit test tests/t_aniso.c
#pragma once

static inline unsigned nvn_aniso(int want, int minf, int cmp, int reduction, int force) {
    unsigned a = want < 1 ? 1 : want > 16 ? 16 : (unsigned)want;
#ifndef DM_TEST_ANISO_ALL   // negative control of t_aniso only: every sampler raised
    if (force > 1 && a < (unsigned)force && (minf == 3 || minf == 5) && !cmp && !reduction) a = force > 16 ? 16 : (unsigned)force;
#else
    (void)minf; (void)cmp; (void)reduction; if (force > 1 && a < (unsigned)force) a = force > 16 ? 16 : (unsigned)force;
#endif
    return a;
}
