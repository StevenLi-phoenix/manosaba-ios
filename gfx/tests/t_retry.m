// Metal objects retried after a compiler-service interruption (nvn_retry.h): a maker that fails twice with "XPC_ERROR_CONNECTION_INTERRUPTED" and then
// succeeds gives its object on the third attempt; another error is not retried; four interruptions give up, reported as such (the caller does not
// record the pipeline as failed then). One attempt only (-DDM_TEST_RETRY_NONE, as before) fails 2.
#import <Foundation/Foundation.h>
#include "../nvn/nvn_retry.h"
static int fail, total;
static void check(const char *name, int ok) { total++; fail += !ok; printf("%-72s %s\n", name, ok ? "✓" : "✗"); }
static NSError *err(NSString *m) { return [NSError errorWithDomain:@"MTLLibraryErrorDomain" code:3 userInfo:@{NSLocalizedDescriptionKey: m}]; }
int main(void) { @autoreleasepool {
    NSString *xpc = @"Compilation failed due to an interrupted connection: XPC_ERROR_CONNECTION_INTERRUPTED";
    __block int calls = 0; NSError *e = nil; int gave_up = -1;
    id o = mtl_retry(^id(NSError **er) { if (++calls < 3) { *er = err(xpc); return nil; } return @"pipeline"; }, &e, &gave_up);
    check("interrupted twice, then made: the object, three attempts", [o isEqual:@"pipeline"] && calls == 3 && !gave_up);
    calls = 0; o = mtl_retry(^id(NSError **er) { calls++; *er = err(@"Fragment input(s) `user(locn1)` mismatching vertex shader output type(s)"); return nil; }, &e, &gave_up);
    check("another error: one attempt, not reported as an interruption", !o && calls == 1 && !gave_up && e);
    calls = 0; o = mtl_retry(^id(NSError **er) { calls++; *er = err(xpc); return nil; }, &e, &gave_up);
    check("interrupted four times: given up, reported as an interruption", !o && calls == 4 && gave_up);
    printf("RETRY %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
} }
