// The pipeline warm-up's lists (warm_start in nvn_mtl.m, unit test tests/t_warm_list.c): of the lists named, the ones that exist, in the order
// given (the order they are warmed in). Each list stands alone: the player's own list (pso_user.tsv, rows built on the submit thread in earlier
// runs) is warmed without a seed manifest too. Before this the warm-up stopped when the seed (pso_manifest.tsv, which no tool writes) was
// missing, so Tears of the Kingdom's 6,481 recorded rows were never read and the prologue built 698 pipelines on the submit thread (2,146 ms).
#pragma once

// cand: n paths, NULL for a list that has no path; exists: does the path exist. Fills out with the indices of the lists to warm; returns how many
static inline int warm_lists(const char *const cand[], int n, int (*exists)(const char *path), int out[]) {
    int k = 0;
    for (int i = 0; i < n; i++) if (cand[i] && exists(cand[i])) out[k++] = i;
#ifdef DM_TEST_WARM_NEED_SEED   // negative control of t_warm_list only: the old rule, nothing is warmed without the first list
    if (!(n > 0 && cand[0] && exists(cand[0]))) k = 0;
#endif
    return k;
}
