// pchtxt patches at load (plan item 150a; after MacNativeGames 1b33531's TOTK_PCHTXT, MIT, written anew): DM_PCHTXT=<file>[:<file>...], emulator
// ExeFS patches in the pchtxt form community mods ship (an aspect-ratio patch, say). A file's @nsobid names the module it is for: its build ID, compared
// over the file's digits (at least 16 bytes' worth) with <module>.cuts.tsv's "# BUILD_ID" (patch_nso.py). The lines of its @enabled sections, up to
// @disabled or @stop, are "<offset> <hex bytes>"; as IPSwitch defines the format (3096/ipswitch example/README.md), an offset counts from the start
// of the uncompressed NSO file, its 0x100-byte header included, plus the latest "@flag offset_shift N" before it (0x100 in files written with .text
// addresses, as the community's are), so the module's image offset is offset + shift - 0x100 (plan item 197; before, every @flag was skipped and
// the shift taken as 0x100; MacNativeGames e55ad1c refuses any other). "@flag print_values" is taken, any other flag refuses the file; "#" lines are
// information (logged); "//" starts a comment. They are written before the code is sealed, after the options. A
// patch that touches a word the rewriter changed (<module>.sites.tsv's rewritten instructions, windows and regions; cuts.tsv; caves.tsv; every row
// of options.tsv, applied or not) or lies outside the module's original image refuses the whole file with a red line: there the product does not
// hold the original code the patch was made for. Mac only: iOS maps signed code (options.c). Checked by host/tests/t_pchtxt.c.
#include "dm.h"
#include <ctype.h>
#include <strings.h>

typedef struct { uint64_t lo, hi; } pch_rng;
typedef struct { pch_rng *r; int n, cap; } pch_set;
static void set_add(pch_set *s, uint64_t lo, uint64_t hi) {
    if (hi <= lo) return;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 1024; s->r = realloc(s->r, (size_t)s->cap * sizeof *s->r); }
    s->r[s->n++] = (pch_rng){lo, hi};
}
static int set_hits(const pch_set *s, uint64_t lo, uint64_t hi) { for (int i = 0; i < s->n; i++) if (s->r[i].lo < hi && lo < s->r[i].hi) return 1; return 0; }
static int hexlen(const char *h) { int n = 0; while (isxdigit((unsigned char)h[n])) n++; return n; }
// The words the rewriter changed in this module, from the product's tables
static void changed_words(const char *dir, const char *module, pch_set *s) {
    char p[1100], line[4096]; FILE *f;
    snprintf(p, sizeof p, "%s/%s.sites.tsv", dir, module);
    if ((f = fopen(p, "r"))) {
        while (fgets(line, sizeof line, f)) {
            char *t = line; uint64_t a = strtoull(t, &t, 0); while (*t == '\t' || *t == ' ') t++;
            if (*t == 'W' && t[1] == '\t') set_add(s, a, strtoull(t + 2, NULL, 0));
            else if (*t == 'R' && t[1] == '\t') { for (char *q = t + 2; *q && *q != '\n';) { uint64_t lo = strtoull(q, &q, 0); if (*q != '-') break; uint64_t hi = strtoull(q + 1, &q, 0); set_add(s, lo, hi); while (*q && *q != ' ' && *q != '\n') q++; while (*q == ' ') q++; } }
            else set_add(s, a, a + 4);
        }
        fclose(f);
    }
    snprintf(p, sizeof p, "%s/%s.cuts.tsv", dir, module);
    if ((f = fopen(p, "r"))) { while (fgets(line, sizeof line, f)) { if (line[0] == '#') continue; char *t; uint64_t a = strtoull(line, &t, 0); while (*t == '\t') t++; set_add(s, a, a + (uint64_t)(hexlen(t) / 2)); } fclose(f); }
    snprintf(p, sizeof p, "%s/%s.caves.tsv", dir, module);
    if ((f = fopen(p, "r"))) { while (fgets(line, sizeof line, f)) { if (line[0] == '#') continue; uint64_t a = strtoull(line, NULL, 0); set_add(s, a, a + 4); } fclose(f); }
    snprintf(p, sizeof p, "%s/%s.options.tsv", dir, module);
    if ((f = fopen(p, "r"))) { while (fgets(line, sizeof line, f)) { if (line[0] == '#') continue; char name[64], off[32], ob[64];
        if (sscanf(line, "%63s %31s %63s", name, off, ob) == 3) { uint64_t a = strtoull(off, NULL, 0); set_add(s, a, a + strlen(ob) / 2); } } fclose(f); }
}
// Whether [lo, hi) of a module's image holds a word the rewriter changed (cheats.c's code cheats too); the tables read once per module
int dm_rewrite_touches(const char *dir, const char *module, uint64_t lo, uint64_t hi) {
    static struct { char module[32]; pch_set s; } cache[8]; static int nc;
    int k = 0; while (k < nc && strcmp(cache[k].module, module)) k++;
    if (k == nc) { if (nc == 8) return 1; strlcpy(cache[nc].module, module, sizeof cache[0].module); cache[nc].s = (pch_set){0}; changed_words(dir, module, &cache[nc].s); nc++; }
    return set_hits(&cache[k].s, lo, hi);
}
static int module_build(const char *dir, const char *module, char *out, size_t n) {   // <module>.cuts.tsv's "# BUILD_ID <hex>"
    char p[1100]; snprintf(p, sizeof p, "%s/%s.cuts.tsv", dir, module);
    FILE *f = fopen(p, "r"); if (!f) return 0;
    char b[80] = ""; int ok = fscanf(f, "# BUILD_ID %79s", b) == 1; fclose(f);
    if (ok) strlcpy(out, b, n);
    return ok;
}
typedef struct { uint64_t off; uint8_t *b; int n; } pch_patch;
static char g_claimed[16][512]; static int g_nclaimed;   // files some module took (applied or refused): dm_pchtxt_report names the rest
static void claim(const char *path) { for (int i = 0; i < g_nclaimed; i++) if (!strcmp(g_claimed[i], path)) return; if (g_nclaimed < 16) strlcpy(g_claimed[g_nclaimed++], path, sizeof g_claimed[0]); }
// One file for this module: 1 applied, 0 not this module's, -1 refused
static int one_file(const char *path, const char *dir, const char *module, const char *build, uint8_t *img, size_t orig) {
    FILE *f = fopen(path, "r"); if (!f) return 0;   // (dm_pchtxt_report names it)
    char line[4096]; int enabled = 0, mine = -1, bad = 0, np = 0, cap = 0; pch_patch *p = NULL; char why[200] = ""; long long shift = 0;
    while (fgets(line, sizeof line, f)) {
        char *c = strstr(line, "//"); if (c) *c = 0;
        char *t = line; while (isspace((unsigned char)*t)) t++;
        if (!*t) continue;
        if (!strncasecmp(t, "@nsobid-", 8)) {
            int k = hexlen(t + 8), bl = (int)strlen(build);
            mine = k >= 32 && !strncasecmp(t + 8, build, (size_t)(k < bl ? k : bl));
            if (!mine) break;   // another module's (or another build's)
            continue;
        }
        if (!strncasecmp(t, "@enabled", 8)) { enabled = 1; continue; }
        if (!strncasecmp(t, "@disabled", 9)) { enabled = 0; continue; }
        if (!strncasecmp(t, "@stop", 5)) break;
#ifndef DM_TEST_PCHTXT_OLD_FLAGS   // negative control of t_pchtxt only: every @flag skipped and "#" not special, as before
        if (*t == '#') { if (mine == 1) LOG("DM_PCHTXT %s: %.*s", path, (int)strcspn(t, "\r\n"), t); continue; }   // information (IPSwitch prints it)
        if (!strncasecmp(t, "@flag", 5) && isspace((unsigned char)t[5])) {
            char name[40] = "", val[40] = "", *ve = NULL; int k = sscanf(t + 5, "%39s %39s", name, val);
            if (!strcmp(name, "offset_shift") && k == 2) { long long v = strtoll(val, &ve, 0); if (ve != val && !*ve) { shift = v; continue; } }
            else if (!strcmp(name, "print_values")) continue;   // IPSwitch prints what it writes; the log line below counts it
            snprintf(why, sizeof why, "a flag it cannot follow (%.40s)", t); bad = 1; break;
        }
#endif
        if (*t == '@' || !enabled) continue;
        char *e; uint64_t off = strtoull(t, &e, 16); while (*e == ' ' || *e == '\t') e++;
        int k = hexlen(e);
        if (e == t || !k || k % 2 || (e[k] && !isspace((unsigned char)e[k]))) { snprintf(why, sizeof why, "a line not \"<offset> <hex bytes>\" (%.40s)", t); bad = 1; break; }
        if (mine != 1) { snprintf(why, sizeof why, "patches before its @nsobid line"); bad = 1; break; }
#ifndef DM_TEST_PCHTXT_OLD_FLAGS
        long long io = (long long)off + shift - 0x100;   // the image offset: the NSO header's 0x100 bytes off the file offset
        if (io < 0) { snprintf(why, sizeof why, "%#llx with offset_shift %#llx is before %s's image (offsets count the NSO header)", (unsigned long long)off, shift, module); bad = 1; break; }
#else
        long long io = (long long)off; (void)shift;
#endif
        if (np == cap) { cap = cap ? cap * 2 : 64; p = realloc(p, (size_t)cap * sizeof *p); }
        p[np].off = (uint64_t)io; p[np].n = k / 2; p[np].b = malloc((size_t)p[np].n);
        for (int i = 0; i < p[np].n; i++) { unsigned v; sscanf(e + 2 * i, "%2x", &v); p[np].b[i] = (uint8_t)v; }
        np++;
    }
    fclose(f);
    int res = 0;
    if (mine == 1) claim(path);
    if (mine == 1 && !bad) {
        pch_set ch = {0}; changed_words(dir, module, &ch);
        for (int i = 0; i < np && !bad; i++) {
            if (p[i].off + (uint64_t)p[i].n > orig) { snprintf(why, sizeof why, "%#llx + %d is outside %s's image (%#zx)", (unsigned long long)p[i].off, p[i].n, module, orig); bad = 1; }
#ifndef DM_TEST_PCHTXT_NO_CHECK   // negative control of t_pchtxt only: written over the rewritten code
            else if (set_hits(&ch, p[i].off, p[i].off + (uint64_t)p[i].n)) { snprintf(why, sizeof why, "%#llx + %d touches a word the rewriter changed", (unsigned long long)p[i].off, p[i].n); bad = 1; }
#endif
        }
        free(ch.r);
        if (!bad) { int bytes = 0; for (int i = 0; i < np; i++) { memcpy(img + p[i].off, p[i].b, (size_t)p[i].n); bytes += p[i].n; }
                    LOG("DM_PCHTXT %s: %d patches, %d bytes in %s (build %s)", path, np, bytes, module, build); res = 1; }
    }
    if (bad) { LOG("[red] DM_PCHTXT %s: %s: the file not applied", path, why); res = -1; }
    for (int i = 0; i < np; i++) free(p[i].b);
    free(p);
    return res;
}
// The files of DM_PCHTXT (colon-separated) that are for this module, on its image (orig: its original part, before the patch area); returns how
// many were applied, -1 if one was refused
int dm_pchtxt_apply(const char *dir, const char *module, uint8_t *img, size_t orig, const char *files) {
    if (!files || !*files) return 0;
    char build[80]; if (!module_build(dir, module, build, sizeof build)) return 0;
    char buf[4096]; strlcpy(buf, files, sizeof buf); int applied = 0, refused = 0;
    for (char *sv, *t = strtok_r(buf, ":", &sv); t; t = strtok_r(NULL, ":", &sv)) {
        int r = one_file(t, dir, module, build, img, orig);
        if (r > 0) applied++; else if (r < 0) refused = 1;
    }
    return refused ? -1 : applied;
}
// After every module: a file no module took (not found, no @nsobid line, or for a build none of them is) in red
void dm_pchtxt_report(const char *files) {
    char buf[4096]; strlcpy(buf, files, sizeof buf);
    for (char *sv, *t = strtok_r(buf, ":", &sv); t; t = strtok_r(NULL, ":", &sv)) {
        int took = 0; for (int i = 0; i < g_nclaimed; i++) took |= !strcmp(g_claimed[i], t);
        if (!took) LOG("[red] DM_PCHTXT %s: for no module of this product (missing, without @nsobid, or another build): not applied", t);
    }
}
