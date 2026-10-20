// Unit tests for the pipeline warm-up's lists (nvn_warm_list.h): every combination of the seed manifest and the player's list existing or not,
// a list without a path, and the order kept. The player's list alone must be warmed (the old rule, -DDM_TEST_WARM_NEED_SEED, warms nothing then).
#include "../nvn/nvn_warm_list.h"
#include <stdio.h>
#include <string.h>

static const char *present[4]; static int npresent;
static int exists(const char *p) { for (int i = 0; i < npresent; i++) if (!strcmp(present[i], p)) return 1; return 0; }
static int fail, total;
static void t(const char *name, int ok) { total++; fail += !ok; printf("%-72s %s\n", name, ok ? "✓" : "✗"); }

int main(void) {
    const char *cand[2] = {"/s/pso_manifest.tsv", "/u/pso_user.tsv"}; int got[2];
    npresent = 0;
    t("neither list exists: nothing to warm", warm_lists(cand, 2, exists, got) == 0);
    present[0] = cand[1]; npresent = 1;
    t("only the player's list: it is warmed", warm_lists(cand, 2, exists, got) == 1 && got[0] == 1);
    present[0] = cand[0]; npresent = 1;
    t("only the seed: it is warmed", warm_lists(cand, 2, exists, got) == 1 && got[0] == 0);
    present[0] = cand[1]; present[1] = cand[0]; npresent = 2;
    t("both: the seed first, then the player's list", warm_lists(cand, 2, exists, got) == 2 && got[0] == 0 && got[1] == 1);
    const char *nopath[2] = {"/s/pso_manifest.tsv", NULL};
    t("no player path (no save directory): the seed alone", warm_lists(nopath, 2, exists, got) == 1 && got[0] == 0);
    const char *noseed[2] = {NULL, "/u/pso_user.tsv"};
    t("no seed path (no shader map): the player's list alone", warm_lists(noseed, 2, exists, got) == 1 && got[0] == 1);
    printf("WARM_LIST %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
}
