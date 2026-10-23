// The program bound per stage without a GPU (gfx/nvn/core/prog.c prog_bind, plan item 264): a program bound with all its stages is itself (D2R's
// whole-program bind); Unity's vertex and fragment programs bound in two calls combine into one composite with both stages, in its own serial
// range, the same object for the same combination; a later bind replaces only the stages it names (a stage named that the program lacks is
// unbound); unbinding every stage gives NULL; a program without keys replaces the whole.
// The whole program replaced, the stages outside the mask unbound (-DDM_TEST_PROG_BIND_WHOLE, the broken variant): 6 fail.
#include "../nvn/core/prog.h"
#include <stdio.h>
#include <string.h>

static int n, fail;
static void check(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-96s %lld (want %lld) %s\n", name, got, want, ok ? "✓" : "✗"); }
static prog_t one(int id, int stage) { prog_t p = {0}; p.nstage = 1; p.kstage[0] = (uint8_t)stage; p.key[0][0] = (uint8_t)id; p.serial = 500 + (uint64_t)id; return p; }
static int key_at(const prog_t *p, int stage) { if (!p) return -1; for (unsigned i = 0; i < p->nstage; i++) if (p->kstage[i] == stage) return p->key[i][0]; return 0; }   // the shader id at a stage, 0 none

int main(void) {
    prog_t v1 = one(1, 0), f1 = one(2, 1), f2 = one(3, 1), v2 = one(4, 0), keyless = {0};
    prog_t vf = {0}; vf.nstage = 2; vf.kstage[0] = 0; vf.kstage[1] = 1; vf.key[0][0] = 5; vf.key[1][0] = 6; vf.serial = 600;
    check("a program bound with all its stages (vertex + fragment bits) is itself", 1, prog_bind(NULL, &vf, 0x3) == &vf);
    const prog_t *a = prog_bind(NULL, &v1, 0x1);
    check("a vertex program bound by the vertex bit alone is itself", 1, a == &v1);
    const prog_t *b = prog_bind(a, &f1, 0x2);
    check("then a fragment program by the fragment bit: a composite with both stages", 2, b ? (long long)b->nstage : -1);
    check("... vertex from the first, fragment from the second", 12, key_at(b, 0) * 10 + key_at(b, 1));
    check("... in the composite serial range (top bits)", 1, b && (b->serial >> 62) == 1);
    check("the same combination again is the same object", 1, prog_bind(prog_bind(NULL, &v1, 0x1), &f1, 0x2) == b);
    const prog_t *c = prog_bind(b, &v2, 0x1);
    check("rebinding the vertex stage keeps the fragment one", 42, key_at(c, 0) * 10 + key_at(c, 1));
    const prog_t *d = prog_bind(b, &f2, 0x1);
    check("a fragment program bound by the vertex bit: vertex unbound, the fragment stage kept", 2, key_at(d, 0) * 10 + key_at(d, 1));
    check("unbinding every stage gives NULL", 1, prog_bind(b, NULL, 0x3f) == NULL);
    check("a program without keys replaces the whole", 1, prog_bind(b, &keyless, 0x1) == &keyless);
    const prog_t *e = prog_bind(&vf, &f1, 0x2);
    check("binding the fragment stage over a whole program keeps its vertex stage", 52, key_at(e, 0) * 10 + key_at(e, 1));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
