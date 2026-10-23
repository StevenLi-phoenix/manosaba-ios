// The Metal shader cache cleared at boot (host/shadercache.h, plan item 257): under a temporary Caches/<bundle id>, the three directories the system
// keeps (with files and a subdirectory inside) are removed, another directory stays; a second call finds nothing. com.apple.metal alone
// (-DDM_TEST_SHADERCACHE_METAL_ONLY): 2 fail.
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../shadercache.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-88s %s\n", what, ok ? "✓" : "✗"); }
static int exists(const char *dir, const char *name) { char p[1024]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, name); return !lstat(p, &st); }
static void mk(const char *dir, const char *rel) { char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, rel); mkdir(p, 0755); }
static void put(const char *dir, const char *rel) { char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, rel); FILE *f = fopen(p, "w"); if (f) { fputs("x", f); fclose(f); } }
int main(void) {
    char d[512]; snprintf(d, sizeof d, "%s/t_shadercache_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); mkdir(d, 0755);
    mk(d, "com.apple.metal"); mk(d, "com.apple.metal/sub"); put(d, "com.apple.metal/sub/a.data"); put(d, "com.apple.metal/b.db");
    mk(d, "com.apple.gpuarchiver"); put(d, "com.apple.gpuarchiver/c"); mk(d, "com.apple.metalfe"); put(d, "com.apple.metalfe/d"); mk(d, "keep"); put(d, "keep/e");
    int r = shader_cache_clear(d);
    check("three cache directories removed, files and subdirectories with them", r == 3 && !exists(d, "com.apple.metal"));
    check("... com.apple.gpuarchiver and com.apple.metalfe too [-DDM_TEST_SHADERCACHE_METAL_ONLY: left]", !exists(d, "com.apple.gpuarchiver") && !exists(d, "com.apple.metalfe"));
    check("another directory stays", exists(d, "keep/e"));
    check("a second call: nothing to remove", shader_cache_clear(d) == 0);
    char p[1024]; snprintf(p, sizeof p, "%s/keep/e", d); remove(p); snprintf(p, sizeof p, "%s/keep", d); rmdir(p);
    snprintf(p, sizeof p, "%s/com.apple.gpuarchiver/c", d); remove(p); snprintf(p, sizeof p, "%s/com.apple.gpuarchiver", d); rmdir(p);
    snprintf(p, sizeof p, "%s/com.apple.metalfe/d", d); remove(p); snprintf(p, sizeof p, "%s/com.apple.metalfe", d); rmdir(p); rmdir(d);
    printf("t_shadercache: %d checks, %d failed\n", n, fail); return fail != 0;
}
