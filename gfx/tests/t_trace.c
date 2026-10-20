// The event trace (gfx/nvn/nvn_trace.c, plan item 61c): the DM_NVN_TRACE window and its file. A window spec is "<first>+<frames>:<path>" (a path
// with a space taken; no first, no '+', 0 or more than 1,000,000 frames, no path, refused); off, trace_now() is 0 and a span begun then is not
// kept; with DM_NVN_TRACE=3+2:<file> frames 1-2 record nothing, frame 3 starts, frame 5 stops and writes frames 3-4: its events sorted by start,
// kind names, the thread of each (two threads), nothing after the stop; with room for 2 events, a third is counted as not kept; stopping when not
// recording writes nothing. A span begun while off kept (-DDM_TEST_TRACE_KEEP_UNSTARTED): 3 fail.
#include "../nvn/nvn_trace.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static void *other(void *a) { (void)a; trace_end(TR_GPU, trace_now(), 7, 8); return NULL; }
static char g_file[4096];
static int slurp(const char *p) { FILE *f = fopen(p, "r"); if (!f) return 0; size_t k = fread(g_file, 1, sizeof g_file - 1, f); g_file[k] = 0; fclose(f); return 1; }
static int lines(void) { int c = 0; for (const char *p = g_file; *p; p++) c += *p == '\n'; return c; }

int main(void) {
    uint64_t first, frames; char path[256];
    check("\"300+120:/a b/t.tsv\": first 300, 120 frames, the path with its space",
          trace_parse("300+120:/a b/t.tsv", &first, &frames, path, sizeof path) && first == 300 && frames == 120 && !strcmp(path, "/a b/t.tsv"));
    const char *bad[] = {"", "abc", "+5:p", "5+:p", "5+0:p", "5+1000001:p", "5+5:", "5-5:p", "5+5p"};
    int refused = !trace_parse(NULL, &first, &frames, path, sizeof path);
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) refused &= !trace_parse(bad[i], &first, &frames, path, sizeof path);
    check("no first, no '+', 0 or 1000001 frames, no path, a '-', no ':': refused", refused);

    char file[256]; snprintf(file, sizeof file, "%s/t_trace_%d.tsv", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());
    char spec[300]; snprintf(spec, sizeof spec, "3+2:%s", file); setenv("DM_NVN_TRACE", spec, 1);
    uint64_t begun_off = trace_now();
    int quiet = !begun_off && !trace_frame(1) && !trace_frame(2) && !trace_now();
    check("off: trace_now() is 0, frames 1-2 record nothing", quiet);
    trace_frame(3);
    uint64_t t = trace_now();
    trace_end(TR_SUBMIT, t, 2, 41);
    trace_end(TR_WAIT, begun_off, 1, 1); trace_span(TR_WAIT, begun_off, t, 1, 1);   // begun while off: not kept
    pthread_t th; pthread_create(&th, NULL, other, NULL); pthread_join(th, NULL);
    trace_frame(4);
    trace_span(TR_GPU, t + 1000000000, t + 1000000004, 41, 1);   // a GPU span reported later, starting after the rest
    int wrote = trace_frame(5);
    trace_end(TR_PRESENT, trace_now(), 0, 5);
    check("frame 3 starts, frame 5 stops and writes; after it trace_now() is 0", t && wrote && !trace_now());
    int ok = slurp(file);
    check("the file: frames 3-4, 3 events (the span begun while off not kept), the header", ok && !strncmp(g_file, "# frames 3-4, 3 events, 0 not kept", 34) &&
          strstr(g_file, "\nt0_ns\tt1_ns\tkind\tthread\ta\tb\n") && lines() == 5);
    unsigned long long r[3][5] = {{0}}; char kind[3][16] = {{0}}; const char *p = strstr(g_file, "\tb\n") + 3; int rows = 0;
    for (; rows < 3 && sscanf(p, "%llu\t%llu\t%15s\t%llu\t%llu\t%llu", &r[rows][0], &r[rows][1], kind[rows], &r[rows][2], &r[rows][3], &r[rows][4]) == 6; rows++) p = strchr(p, '\n') + 1;
    check("sorted by start: submit (2 lists, fence 41), gpu from the other thread (7, 8), gpu (41, part 1)", rows == 3 && !strcmp(kind[0], "submit") && r[0][3] == 2 &&
          r[0][4] == 41 && !strcmp(kind[1], "gpu") && r[1][3] == 7 && !strcmp(kind[2], "gpu") && r[2][0] == t + 1000000000 && r[2][1] == t + 1000000004 && r[0][0] <= r[1][0] && r[1][0] <= r[2][0]);
    check("the threads: the other thread's event has its own", rows == 3 && r[0][2] && r[1][2] && r[0][2] != r[1][2] && r[0][2] == r[2][2]);
    check("after the window: no more frames start one", !trace_frame(6) && !trace_frame(3) && !trace_now());
    trace_start(2); uint64_t u = trace_now(); for (int i = 0; i < 3; i++) trace_span(TR_COMMIT, u + i, u + i + 1, 1, (uint64_t)i + 1);
    check("room for 2: the third counted as not kept", trace_stop(file, 0, 0) && slurp(file) && !strncmp(g_file, "# frames 0-0, 2 events, 1 not kept (room for 2)", 47) && lines() == 4);
    check("stopping when not recording: nothing written", !trace_stop(file, 0, 0));
    unlink(file);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
