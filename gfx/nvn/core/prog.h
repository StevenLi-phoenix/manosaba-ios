// gfx/nvn/core/prog.h: the program bound per stage (plan item 264 moved it here from the Metal backend). Checked without a GPU by gfx/tests/t_core_prog.c
#pragma once
#include "../nvn_obj.h"

// BindProgram attaches a program per stage bit (NVN semantics: only the stages in stages are replaced, the others keep their binding; a program without a shader for that stage = that stage is unbound).
// Unity's programs each contain a single stage and vertex / pixel are bound in two calls; D2R binds all stages at once, which is why replacing the whole program used to happen to work.
// The replay layer represents the current program everywhere as a single prog_t: the per-stage keys are combined here into a composite prog_t (deduplicated by the 6 stage keys, kept alive, with its own serial), and the backend resolves it as usual.
// NULL when no stage is bound
const prog_t *prog_bind(const prog_t *cur, const prog_t *np, int stages);
