// Run diagnostics (rundiag.h, plan item 106) in a scratch directory standing in for Documents: a session that left the foreground in order leaves
// nothing behind; one whose marker is still there at the next launch has its log kept as boot.interrupted.txt with the same bytes (the log itself
// untouched, an older kept log replaced), and the marker is gone afterwards; the marker follows the log's rename (boot.launch.txt → boot.txt); a
// marker naming a path outside the directory is refused. The report: a 9 MiB log gives its last 8 MiB exactly, with a note; a short log whole;
// a missing one says so; the header first. Expected bytes are made here. Without keeping the log (-DDM_TEST_RUNDIAG_NO_KEEP) 2 fail; the start of
// a long log instead of its end (-DDM_TEST_RUNDIAG_HEAD) 2 fail.
#include "../rundiag.h"
#include <stdlib.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static char g_dir[1024];
static void put(const char *name, const char *s, size_t len) { char p[1100]; snprintf(p, sizeof p, "%s/%s", g_dir, name); FILE *f = fopen(p, "w"); fwrite(s, 1, len, f); fclose(f); }
static char *get(const char *name, size_t *len) {   // the whole file, or NULL
    char p[1100]; snprintf(p, sizeof p, "%s/%s", g_dir, name); FILE *f = fopen(p, "r"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc((size_t)l + 1); *len = fread(b, 1, (size_t)l, f); b[*len] = 0; fclose(f); return b;
}
static int exists(const char *name) { char p[1100]; snprintf(p, sizeof p, "%s/%s", g_dir, name); return !access(p, F_OK); }
int main(void) {
    const char *td = getenv("TMPDIR"); snprintf(g_dir, sizeof g_dir, "%s/t_rundiag_%d", td ? td : "/private/tmp", (int)getpid()); mkdir(g_dir, 0755);
    char what[512]; size_t len;
    // A session in order: marked on entering the foreground, unmarked on leaving it
    put("boot.txt", "session 1\n", 10);
    CHECK(rundiag_mark(g_dir, "boot.txt") == 0 && exists(RUNDIAG_MARK), "entering the foreground writes the marker");
    rundiag_unmark(g_dir);
    CHECK(rundiag_launch(g_dir, what, sizeof what) == 0 && !exists(RUNDIAG_KEPT), "a session that left the foreground: the next launch keeps nothing");
    // An interrupted session: the marker is still there at the next launch
    put("boot.txt", "session 2: the last line before the kill\n", 41);
    rundiag_mark(g_dir, "boot.txt");
    int r = rundiag_launch(g_dir, what, sizeof what);
    char *k = get(RUNDIAG_KEPT, &len);
    CHECK(r == 1 && strstr(what, "boot.txt from 20"), "the next launch reports it interrupted, with the log and its time (got %d, \"%s\")", r, what);
    CHECK(k && len == 41 && !memcmp(k, "session 2: the last line before the kill\n", 41), "its log kept as boot.interrupted.txt, byte for byte");
    free(k); k = get("boot.txt", &len);
    CHECK(k && len == 41, "the log itself stays where it was (it rotates to boot.previous.txt as before)"); free(k);
    CHECK(!exists(RUNDIAG_MARK), "the marker is gone after the launch");
    CHECK(rundiag_launch(g_dir, what, sizeof what) == 0, "a second launch without a new marker: not interrupted");
    // Interrupted again, before the rename to boot.txt: the marker names the launch log, and the kept one is replaced
    put("boot.launch.txt", "session 3\n", 10);
    rundiag_mark(g_dir, "boot.launch.txt");
    r = rundiag_launch(g_dir, what, sizeof what); k = get(RUNDIAG_KEPT, &len);
    CHECK(r == 1 && k && len == 10 && !memcmp(k, "session 3\n", 10), "the log the marker names is kept, replacing the older kept log"); free(k);
    // A marker naming something outside the directory
    put(RUNDIAG_MARK, "../../etc/passwd\t2026-10-20T00:00:00Z\tpid 1\n", 44);
    CHECK(rundiag_launch(g_dir, what, sizeof what) == -1 && !exists(RUNDIAG_MARK), "a marker naming a path is refused and removed (%s)", what);
    // The report
    size_t big = 9u << 20; char *b = malloc(big); for (size_t i = 0; i < big; i++) b[i] = (char)('a' + (i * 7 + i / 4096) % 26);
    put("big.txt", b, big); put("small.txt", "a short log\n", 12);
    const char *logs[] = {"big.txt", "small.txt", "missing.txt"};
    char rp[1100]; snprintf(rp, sizeof rp, "%s/report.txt", g_dir);
    CHECK(rundiag_report(rp, "device: test\n", g_dir, logs, 3, 8 << 20) == 0, "the report is written");
    char *rep = get("report.txt", &len);
    CHECK(rep && !strncmp(rep, "device: test\n", 13), "the header comes first");
    const char *h = rep ? strstr(rep, "===== big.txt =====\n(the last 8388608 of 9437184 bytes)\n") : NULL;
    const char *body = h ? h + strlen("===== big.txt =====\n(the last 8388608 of 9437184 bytes)\n") : NULL;
    CHECK(h != NULL, "a long log says it is cut");
    CHECK(body && !memcmp(body, b + big - (8u << 20), 8u << 20), "... and gives its last 8 MiB exactly");
    CHECK(rep && strstr(rep, "===== small.txt =====\na short log\n"), "a short log whole");
    CHECK(rep && strstr(rep, "===== missing.txt =====\n(none: No such file or directory)\n"), "a missing log says so");
    CHECK(!exists("report.txt.tmp"), "no temporary file left");
    free(rep); free(b);
    char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir); if (system(cmd)) {}
    printf("t_rundiag: %d checks, %d failed\n", n, fail); return fail != 0;
}
