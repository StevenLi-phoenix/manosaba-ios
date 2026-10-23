// gfx/nvn/core/prog.c: the program bound per stage, see prog.h
#include "prog.h"
#include "../nvn_hmap.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

const prog_t *prog_bind(const prog_t *cur, const prog_t *np, int stages) {
    if (np && !np->nstage) return np;   // a program without keys (unit-test objects that register an NvProg straight into the serial table): replaced as a whole, as before
#ifdef DM_TEST_PROG_BIND_WHOLE   // negative control of t_core_prog only: the whole program replaced, the stages outside stages unbound
    stages = 0x3f;
#endif
    uint8_t k[6][8] = {{0}}; uint8_t have[6] = {0};
    if (cur) for (unsigned i = 0; i < cur->nstage && i < 6; i++) { unsigned st = cur->kstage[i]; if (st < 6 && !(stages & (1 << st))) { memcpy(k[st], cur->key[i], 8); have[st] = 1; } }
    if (np) for (unsigned i = 0; i < np->nstage && i < 6; i++) { unsigned st = np->kstage[i]; if (st < 6 && (stages & (1 << st))) { memcpy(k[st], np->key[i], 8); have[st] = 1; } }
    if (np) {   // the composite is exactly np's own key set (all its stages bound at once, other stages empty): use np itself, the same object as D2R's whole-program bind
        int same = 1; unsigned cnt = 0; for (unsigned st = 0; st < 6; st++) cnt += have[st];
        if (cnt != np->nstage) same = 0;
        for (unsigned i = 0; same && i < np->nstage; i++) { unsigned st = np->kstage[i]; if (st >= 6 || !have[st] || memcmp(k[st], np->key[i], 8)) same = 0; }
        if (same) return np;
    }
    uint8_t hk[6 * 8 + 6]; memcpy(hk, k, 48); memcpy(hk + 48, have, 6);
    static nvn_hmap cache; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    if (!cache.klen) nvn_hm_init(&cache, sizeof hk);
    prog_t *c = nvn_hm_get(&cache, hk);
    if (!c) {
        static uint64_t nc; c = calloc(1, sizeof *c); c->magic = MAGIC('P', 'R', 'G', 'C'); c->serial = (1ull << 62) | ++nc;   // composite programs have their own serial range (top bits), never colliding with the texture / program serials from nvn_new_serial
        for (unsigned st = 0; st < 6; st++) if (have[st]) { memcpy(c->key[c->nstage], k[st], 8); c->kstage[c->nstage] = (uint8_t)st; c->nstage++; }
        nvn_hm_put(&cache, hk, c);
    }
    pthread_mutex_unlock(&mu);
    return c->nstage ? c : NULL;
}
