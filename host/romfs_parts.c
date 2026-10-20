// See romfs_parts.h.
#include "romfs_parts.h"
#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { char name[64], sha[65]; long long bytes; long files; } part_t;

static int sha_of(const char *p, char out[65]) {
    int fd = open(p, O_RDONLY); if (fd < 0) return 0;
    CC_SHA256_CTX c; CC_SHA256_Init(&c); static uint8_t buf[1 << 20]; ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0) CC_SHA256_Update(&c, buf, (CC_LONG)r);
    close(fd); if (r < 0) return 0;
    uint8_t d[32]; CC_SHA256_Final(d, &c); for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    return 1;
}
__attribute__((unused)) static int path_ok(const char *p) {   // relative, no empty / "." / ".." component
    if (!*p || *p == '/') return 0;
    for (const char *s = p;;) {
        const char *e = strchr(s, '/'); size_t n = e ? (size_t)(e - s) : strlen(s);
        if (!n || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
        if (!e) return 1;
        s = e + 1;
    }
}
static int mkdirs(char *p) {   // the parents of the file p
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; int r = mkdir(p, 0755); *s = '/'; if (r && errno != EEXIST) return 0; }
    return 1;
}
static int rm_one(const char *p, const struct stat *st, int flag, struct FTW *f) { (void)st; (void)flag; (void)f; return remove(p); }
static int unpack_part(const char *part, const char *to, long want, char *why, size_t n) {
    int fd = open(part, O_RDONLY); if (fd < 0) { snprintf(why, n, "%s: cannot open", part); return 0; }
    static uint8_t buf[1 << 20]; long got = 0; int ok = 1;
    for (;;) {
        uint8_t h[16]; ssize_t r = read(fd, h, sizeof h);
        if (r == 0) break;
        if (r != (ssize_t)sizeof h || memcmp(h, "DMRF", 4)) { snprintf(why, n, "%s: record %ld is not one", part, got + 1); ok = 0; break; }
        uint32_t pl; uint64_t size; memcpy(&pl, h + 4, 4); memcpy(&size, h + 8, 8);
        char rel[1024], dst[4096];
        if (pl == 0 || pl >= sizeof rel || read(fd, rel, pl) != (ssize_t)pl) { snprintf(why, n, "%s: record %ld has no path", part, got + 1); ok = 0; break; }
        rel[pl] = 0;
#ifndef DM_TEST_PARTS_ANY_PATH   // negative control of host/tests t_romfs_parts only: paths taken as they are
        if (!path_ok(rel)) { snprintf(why, n, "%s: record %ld's path %s leaves the tree: refused", part, got + 1, rel); ok = 0; break; }
#endif
        snprintf(dst, sizeof dst, "%s/%s", to, rel);
        if (!mkdirs(dst)) { snprintf(why, n, "%s: cannot make the folders of %s", part, rel); ok = 0; break; }
        int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { snprintf(why, n, "cannot write %s", dst); ok = 0; break; }
        for (uint64_t left = size; left && ok;) {
            ssize_t k = read(fd, buf, left < sizeof buf ? (size_t)left : sizeof buf);
            if (k <= 0 || write(out, buf, (size_t)k) != k) { snprintf(why, n, "%s: %s cut short", part, rel); ok = 0; break; }
            left -= (uint64_t)k;
        }
        if (close(out) && ok) { snprintf(why, n, "cannot write %s", dst); ok = 0; }
        if (!ok) break;
        got++;
    }
    close(fd);
    if (ok && got != want) { snprintf(why, n, "%s: %ld files, the index says %ld", part, got, want); ok = 0; }
    return ok;
}
int dm_romfs_parts_unpack(const char *src, const char *dest, char *why, size_t n, void (*line)(const char *)) {
    char p[4096], msg[512]; snprintf(p, sizeof p, "%s/romfs.dmri", src);
    FILE *f = fopen(p, "r"); if (!f) return -1;
    long files, nparts; long long bytes; char w0[8];
    if (fscanf(f, "%7s 1 %ld %lld %ld", w0, &files, &bytes, &nparts) != 4 || strcmp(w0, "dmri") || nparts < 0 || nparts > 100000) { fclose(f); snprintf(why, n, "%s is not an index", p); return 0; }
    part_t *pt = calloc((size_t)nparts + 1, sizeof *pt);
    for (long i = 0; i < nparts; i++)
        if (fscanf(f, "%63s %lld %ld %64s", pt[i].name, &pt[i].bytes, &pt[i].files, pt[i].sha) != 4 || strchr(pt[i].name, '/')) { fclose(f); free(pt); snprintf(why, n, "%s: part line %ld unreadable", p, i + 1); return 0; }
    fclose(f);
    char partial[4096]; snprintf(partial, sizeof partial, "%s.partial", dest);
    struct stat st; int resuming = !stat(partial, &st), left = 0;
    for (long i = 0; i < nparts; i++) {   // every part still there whole, before anything is written
        char pp[4096]; snprintf(pp, sizeof pp, "%s/%s", src, pt[i].name);
        if (stat(pp, &st)) { if (!resuming) { snprintf(why, n, "%s is missing (copy every part of the index)", pt[i].name); free(pt); return 0; } continue; }
        if (st.st_size != pt[i].bytes) { snprintf(why, n, "%s is %lld bytes, the index says %lld (not fully copied)", pt[i].name, (long long)st.st_size, pt[i].bytes); free(pt); return 0; }
#ifndef DM_TEST_PARTS_NO_SHA   // negative control of host/tests t_romfs_parts only: the digest not checked
        char sha[65]; if (!sha_of(pp, sha) || strcmp(sha, pt[i].sha)) { snprintf(why, n, "%s: SHA-256 differs from the index (damaged in the copy)", pt[i].name); free(pt); return 0; }
#endif
        left++;
    }
    if (mkdir(partial, 0755) && errno != EEXIST) { snprintf(why, n, "cannot make %s", partial); free(pt); return 0; }
    for (long i = 0, done = 0; i < nparts; i++) {
        char pp[4096]; snprintf(pp, sizeof pp, "%s/%s", src, pt[i].name);
        if (stat(pp, &st)) continue;
        if (!unpack_part(pp, partial, pt[i].files, why, n)) { free(pt); return 0; }
        unlink(pp); done++;
        if (line) { snprintf(msg, sizeof msg, "RomFS parts: %s unpacked (%ld of %d left this launch)", pt[i].name, done, left); line(msg); }
    }
    free(pt);
    if (!stat(dest, &st) && nftw(dest, rm_one, 32, FTW_DEPTH | FTW_PHYS)) { snprintf(why, n, "cannot remove the older %s", dest); return 0; }
    if (rename(partial, dest)) { snprintf(why, n, "cannot rename %s", partial); return 0; }
    unlink(p);
    snprintf(why, n, "%ld files, %.2f GB unpacked into %s", files, bytes / 1e9, dest);
    return 1;
}
