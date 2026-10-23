// DM_CLEAR_SHADER_CACHE=1 (plan item 257, after AnySwitch ec152a8, MIT): the iOS app starts with the empty Metal shader cache of a fresh install, to
// measure a first play's pipeline compiles without reinstalling (which drops Documents, the game files and saves too). The system keeps it in
// Library/Caches/<bundle id>/: com.apple.metal, com.apple.gpuarchiver and com.apple.metalfe. Pair it with a fresh DM_PSO_USER, so no warm-up list
// learned in play hides the compiles either. Unit test tests/t_shadercache.c
#pragma once
#include <ftw.h>
#include <stdio.h>
#include <sys/stat.h>
static int shader_cache_rm1_(const char *p, const struct stat *st, int flag, struct FTW *f) { (void)st; (void)flag; (void)f; return remove(p); }
// Removes the three directories under dir (Library/Caches/<bundle id>); returns how many it removed, -1 when one could not be removed
static inline int shader_cache_clear(const char *dir) {
    static const char *const names[] = {"com.apple.metal", "com.apple.gpuarchiver", "com.apple.metalfe"};
    int n = 0, bad = 0;
#ifndef DM_TEST_SHADERCACHE_METAL_ONLY   // negative control of t_shadercache only: com.apple.metal alone
    for (int i = 0; i < 3; i++) {
#else
    for (int i = 0; i < 1; i++) {
#endif
        char p[1024]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        if (lstat(p, &st)) continue;
        if (nftw(p, shader_cache_rm1_, 16, FTW_DEPTH | FTW_PHYS)) bad = 1; else n++;
    }
    return bad ? -1 : n;
}
