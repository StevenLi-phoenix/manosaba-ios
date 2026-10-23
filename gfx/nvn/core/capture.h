// gfx/nvn/core/capture.h: what the console reads when the queue takes a submission, read at the submit call for a replay on the translation thread
// (plan item 61), and the submission's uniform snapshot (plan item 264 moved them here from the Metal backend). Checked without a GPU by
// gfx/tests/t_core_capture.c; with the backend by t_xlat.m and t_usnap.m
// The submit call's view, for the translation thread (plan item 61; MacNativeGames' mtl_sub_capture_ubos with TOTK_ASYNC_REPLAY, MIT, written anew):
// With DM_NVN_XLAT=1 a submission is replayed later, on the translation thread (hle_nvn.c), while the game goes on. What the console reads when the
// queue takes the submission is read at the submit call, on the game's thread (mtl_sub_capture): the handles of its deferred binds and the bytes of
// the uniform ranges it binds (Tears of the Kingdom rewrites both as soon as the call returns; MacNativeGames found the constant pools rebuilt by
// other threads), and the syncs of its FenceSync records get the submission's token. The replay installs the handles into the records
// (mtl_sub_install) and its snapshot takes the captured bytes instead of memory (mtl_sub_snapshot). A range that a GPU write of a submission not
// complete at the capture reaches is not taken: its bytes may predate that write, so it is read on the GPU's timeline as a range with a running
// write always was
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "usnap.h"

typedef struct { p_bind_handles *rec; int n; uint64_t h[32]; } cap_bh;
struct mtl_cap { uint64_t done, token; int us, nbh, cbh, nr; cap_bh *bh; us_rng *r; uint64_t *off; uint8_t *data; };   // done: the fence completed at the capture; r / off / data: the ranges taken and where their bytes are (off UINT64_MAX: read live)
extern mtl_cap *g_cap_cur;   // The capture of the submission being replayed (mtl_sub_install .. mtl_sub_commit)
mtl_cap *mtl_sub_capture(const uint64_t *h, int n, uint64_t token);   // The game's thread, in nvnQueueSubmitCommands
void mtl_cap_free(mtl_cap *c);
void mtl_sub_install(mtl_cap *c);   // The translation thread, after mtl_sub_begin: the deferred handles of the submit call
void mtl_sub_snapshot(const uint64_t *h, int n);   // hle_nvn.c nvnQueueSubmitCommands, after mtl_sub_begin and before the replay: the uniform snapshot, from the capture when there is one
void mtl_test_us_watch(uint64_t a); int mtl_test_us_seen(uint64_t *v);   // Tests (t_xlat): the 8 bytes at an address in the next snapshot (seen 1), or no copy holds it (0)
