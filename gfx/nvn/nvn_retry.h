// Metal objects made through the system's compiler service, retried when that service was interrupted (pipelines in nvn_mtl.m pso_get and the compute
// dispatch; unit test tests/t_retry.m). Under memory pressure the service (MTLCompilerService) can be stopped in the middle of a compile and the call
// fails with "XPC_ERROR_CONNECTION_INTERRUPTED"; it restarts at the next request (MacNativeGames 39048ca, MIT: an iPad during its warm-up; it retries
// 4 times). Before, the pipeline was recorded as failed for the session and its draws were skipped for good.
#pragma once
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <unistd.h>

static inline int mtl_interrupted(NSError *e) {
    return e && ([e.localizedDescription containsString:@"XPC_ERROR_CONNECTION_INTERRUPTED"] || [e.localizedDescription containsString:@"interrupted connection"]);
}
// make() up to 4 times while it fails for an interruption, 100 / 200 / 300 ms apart; *gave_up = 1 when the last failure was still one (do not record
// the object as failed for good then: a later use tries again)
static inline id mtl_retry(id (^make)(NSError **), NSError **err, int *gave_up) {
    NSError *e = nil; id o = nil; *gave_up = 0;
    for (int attempt = 1; attempt <= 4; attempt++) {
        e = nil; o = make(&e);
#ifndef DM_TEST_RETRY_NONE   // negative control of t_retry only: one attempt, as before
        if (o || !mtl_interrupted(e)) break;
        if (attempt < 4) { fprintf(stderr, "[dm] [nvn-mtl] [red] the shader compiler service was interrupted (attempt %d): again in %d ms\n", attempt, attempt * 100); usleep((useconds_t)attempt * 100000); }
#else
        break;
#endif
    }
    if (!o) *gave_up = mtl_interrupted(e);
    if (err) *err = e;
    return o;
}
