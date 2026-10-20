// Anisotropic filtering raised by DM_NVN_ANISO (nvn_aniso.h): off by default, the sampler's own value; raised only on mipmapped linear samplers
// (NVN min filter 3 or 5) without a depth compare or a reduction, never lowered, at most 16. Raising every sampler (-DDM_TEST_ANISO_ALL) fails 3.
#include "../nvn/nvn_aniso.h"
#include <stdio.h>
static int fail, total;
static void t(const char *name, unsigned want, unsigned got) { total++; fail += want != got; printf("%-72s expected %-2u got %-2u %s\n", name, want, got, want == got ? "✓" : "✗"); }
int main(void) {
    t("off: the sampler's own 1", 1, nvn_aniso(1, 5, 0, 0, 0));
    t("off: the sampler's own 8", 8, nvn_aniso(8, 5, 0, 0, 0));
    t("16: a mipmapped linear sampler (min filter 5)", 16, nvn_aniso(1, 5, 0, 0, 16));
    t("16: linear within the nearest level (min filter 3)", 16, nvn_aniso(1, 3, 0, 0, 16));
    t("16: not mipmapped (min filter 1): its own", 1, nvn_aniso(1, 1, 0, 0, 16));
    t("16: a depth-compare sampler: its own", 1, nvn_aniso(1, 5, 1, 0, 16));
    t("16: a min / max reduction sampler: its own", 1, nvn_aniso(1, 5, 0, 2, 16));
    t("4: a sampler asking for 8 keeps 8", 8, nvn_aniso(8, 5, 0, 0, 4));
    t("32: at most 16", 16, nvn_aniso(1, 5, 0, 0, 32));
    t("0 asked: 1", 1, nvn_aniso(0, 5, 0, 0, 0));
    printf("ANISO %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
}
