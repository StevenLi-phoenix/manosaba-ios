// gfx/nvn/core/exec.h: the replay loop (plan item 264). nvn_exec (declared in nvn_obj.h with nvn_exec_rest) walks a command list in replay order:
// jumps and calls (the callers' next ops on the return stack, core/pass.h), the ops run before their place skipped, the deferred groups' P ends
// marked; each command that only sets state is applied to S / R (st_op), each FenceSync record fenced to this submission, and every other command
// handed to the backend (gfx/nvn/nvn_backend.h). Checked without a GPU by gfx/tests/t_core_exec.c
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"

extern uint32_t g_sub_work;   // draws, dispatches, copies and clears replayed in this submission so far (counter timestamps, nvn_ts.h; the backend zeroes it)
