// RomFS parts (plan item 62e): a tree packed by tools/romfs_parts.py with 4 KiB parts (a 5,000-byte file a part by itself) and unpacked by
// host/runtime/romfs_parts.c: the same files with the same bytes, the parts, the index and the .partial folder gone; an older tree in the place replaced
// (its extra file gone); a part with one byte changed refused before anything is written; a part missing refused; carried on after a launch cut
// short (the first part unpacked and deleted, the folder still .partial); a part whose record path is "../evil" (with a matching index) refused and
// nothing written outside; no index: -1. The digest not checked (-DDM_TEST_PARTS_NO_SHA): 2 fail; paths taken as they are
// (-DDM_TEST_PARTS_ANY_PATH): 1 fails.
#include "../runtime/romfs_parts.c"
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;

static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static char g_root[1024], g_py[2048];
static void put(const char *dir, const char *rel, size_t size, int seed) {
    char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, rel); mkdirs(p);
    FILE *f = fopen(p, "wb"); for (size_t i = 0; i < size; i++) fputc((int)((i * 31 + (size_t)seed) & 255), f); fclose(f);
}
static int pack(const char *tree, const char *out) {
    char *argv[] = {"python3", g_py, "pack", (char *)tree, (char *)out, "--part-bytes", "4096", NULL}; pid_t pid; int st;
    int fd = open("/dev/null", O_WRONLY); posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa); posix_spawn_file_actions_adddup2(&fa, fd, 1);
    if (posix_spawnp(&pid, "python3", &fa, NULL, argv, environ)) return 0;
    waitpid(pid, &st, 0); close(fd); return WIFEXITED(st) && !WEXITSTATUS(st);
}
static int same_file(const char *a, const char *b) {
    FILE *x = fopen(a, "rb"), *y = fopen(b, "rb"); int ok = x && y;
    while (ok) { int c = fgetc(x), d = fgetc(y); if (c != d) ok = 0; if (c == EOF) break; }
    if (x) fclose(x); if (y) fclose(y); return ok;
}
static const char *const k_rel[] = {"a/b.txt", "a/c/d.bin", "e.dat", "f/big.bin", "f/g/h.txt"};
static const size_t k_size[] = {100, 3000, 10, 5000, 1};
static int tree_same(const char *a, const char *b) {
    for (int i = 0; i < 5; i++) { char x[2048], y[2048]; snprintf(x, sizeof x, "%s/%s", a, k_rel[i]); snprintf(y, sizeof y, "%s/%s", b, k_rel[i]); if (!same_file(x, y)) return 0; }
    return 1;
}
static int exists(const char *dir, const char *rel) { char p[2048]; struct stat st; snprintf(p, sizeof p, "%s%s%s", dir, rel ? "/" : "", rel ? rel : ""); return !stat(p, &st); }
static int nparts(const char *dir) { int k = 0; for (int i = 0; i < 100; i++) { char p[64]; snprintf(p, sizeof p, "romfs.%03d.dmrp", i); k += exists(dir, p); } return k; }
static void sub(char *out, const char *name) { snprintf(out, 2048, "%s/%s", g_root, name); mkdir(out, 0755); }

int main(void) {
    char cwd[1024]; if (!getcwd(cwd, sizeof cwd)) return 2;
    snprintf(g_py, sizeof g_py, "%s/../../tools/romfs_parts.py", cwd);
    snprintf(g_root, sizeof g_root, "%s/t_romfs_parts_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); mkdir(g_root, 0755);
    char tree[2048], src[2048], dest[2048], why[512];
    sub(tree, "tree"); for (int i = 0; i < 5; i++) put(tree, k_rel[i], k_size[i], i);
    sub(src, "src"); snprintf(dest, sizeof dest, "%s/romfs", g_root);
    check("packed with 4 KiB parts: three or more parts and an index", pack(tree, src) && nparts(src) >= 3 && exists(src, "romfs.dmri"));
    put(dest, "old/extra.txt", 5, 9);   // an older tree in the place
    int r = dm_romfs_parts_unpack(src, dest, why, sizeof why, NULL);
    check("unpacked: the same five files, byte for byte", r == 1 && tree_same(tree, dest));
    char partial[2100]; snprintf(partial, sizeof partial, "%s.partial", dest);
    check("... the parts, the index and the .partial folder gone; the older tree's extra file gone", !nparts(src) && !exists(src, "romfs.dmri") && !exists(partial, NULL) && !exists(dest, "old/extra.txt"));
    check("no index: -1", dm_romfs_parts_unpack(src, dest, why, sizeof why, NULL) == -1);

    char s2[2048], d2[2048], p1[2100]; sub(s2, "src2"); snprintf(d2, sizeof d2, "%s/romfs2", g_root); pack(tree, s2);
    snprintf(p1, sizeof p1, "%s/romfs.001.dmrp", s2); FILE *f = fopen(p1, "r+b"); fseek(f, 40, SEEK_SET); int c = fgetc(f); fseek(f, 40, SEEK_SET); fputc(c ^ 1, f); fclose(f);
    r = dm_romfs_parts_unpack(s2, d2, why, sizeof why, NULL);
    check("a part with one byte changed: refused before anything is written", r == 0 && strstr(why, "SHA-256") && !exists(d2, NULL) && nparts(s2) >= 3);
    unlink(p1);
    r = dm_romfs_parts_unpack(s2, d2, why, sizeof why, NULL);
    check("a part missing: refused", r == 0 && strstr(why, "missing"));

    char s3[2048], d3[2048], d3p[2100], p0[2100]; sub(s3, "src3"); snprintf(d3, sizeof d3, "%s/romfs3", g_root); snprintf(d3p, sizeof d3p, "%s.partial", d3); pack(tree, s3);
    snprintf(p0, sizeof p0, "%s/romfs.000.dmrp", s3); mkdir(d3p, 0755);
    long want0 = 0; { FILE *ix = fopen((snprintf(why, sizeof why, "%s/romfs.dmri", s3), why), "r"); char nm[64], sh[65]; long long b; long fl; fscanf(ix, "%*s %*d %*ld %*lld %*ld %63s %lld %ld %64s", nm, &b, &fl, sh); want0 = fl; fclose(ix); }
    int first = unpack_part(p0, d3p, want0, why, sizeof why); unlink(p0);   // a launch cut short after its first part
    r = dm_romfs_parts_unpack(s3, d3, why, sizeof why, NULL);
    check("carried on after a launch cut short: complete", first && r == 1 && tree_same(tree, d3));

    char s4[2048], d4[2048], pp[2100], ix[2100], sha[65]; sub(s4, "src4"); snprintf(d4, sizeof d4, "%s/inner/romfs4", g_root); sub(pp, "inner");
    snprintf(pp, sizeof pp, "%s/romfs.000.dmrp", s4); f = fopen(pp, "wb");
    const char *evil = "../evil"; uint32_t pl = (uint32_t)strlen(evil); uint64_t sz = 4; fwrite("DMRF", 1, 4, f); fwrite(&pl, 4, 1, f); fwrite(&sz, 8, 1, f); fwrite(evil, 1, pl, f); fwrite("bad!", 1, 4, f); fclose(f);
    sha_of(pp, sha); snprintf(ix, sizeof ix, "%s/romfs.dmri", s4); f = fopen(ix, "w"); fprintf(f, "dmri 1 1 4 1\nromfs.000.dmrp %d 1 %s\n", 4 + 12 + (int)pl + 4, sha); fclose(f);
    r = dm_romfs_parts_unpack(s4, d4, why, sizeof why, NULL);
    check("a record path \"../evil\": refused, nothing written outside", r == 0 && !exists(g_root, "inner/evil") && !exists(g_root, "evil"));
    nftw(g_root, rm_one, 32, FTW_DEPTH | FTW_PHYS);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
