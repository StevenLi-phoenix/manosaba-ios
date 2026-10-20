// RomFS check (romfs_validate.c) against a profile of its own: two key files with exact sizes, and a tree "Data" of 3 files / 10 bytes; the marker
// (plan item 175) trusted after one walk, the key files checked every time. A marker trusted for the key files too (-DDM_TEST_ROMFS_MARKER_NO_KEYS): 1 fails.
#include "../romfs_validate.h"
#include "../game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const dm_game_file files[] = {{"Data/a.bin", 4}, {"Data/sub/b.bin", 3}, {0, 0}};
const dm_game_profile dm_game = { .id = "test", .title = "Test", .version = "1.0", .romfs_files = files, .romfs_nfiles = 2,
                                  .romfs_tree = "Data", .romfs_tree_files = 3, .romfs_tree_bytes = 10 };
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char R[512];
static void put(const char *rel, const char *txt) {
    char p[1024]; snprintf(p, sizeof p, "%s/%s", R, rel);
    for (char *s = p + strlen(R) + 1; (s = strchr(s, '/')); s++) { *s = 0; mkdir(p, 0755); *s = '/'; }
    FILE *f = fopen(p, "w"); fputs(txt, f); fclose(f);
}

int main(void) {
    const char *tmp = getenv("TMPDIR"); snprintf(R, sizeof R, "%s/t_romfs.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp(R)) { perror("mkdtemp"); return 1; }
    char why[512], rep[512];
    CHECK(!dm_romfs_validate(R, why, sizeof why) && !strcmp(why, "missing Data/a.bin"), "empty directory refused: %s", why);
    CHECK(dm_romfs_report(R, rep, sizeof rep) == 3 && strstr(rep, "2 of 2 key files"), "report on an empty directory: %s", rep);
    put("Data/a.bin", "abc");
    CHECK(!dm_romfs_validate(R, why, sizeof why) && strstr(why, "Data/a.bin is 3 bytes, should be 4"), "short key file refused: %s", why);
    put("Data/a.bin", "abcd"); put("Data/sub/b.bin", "xyz");
    CHECK(!dm_romfs_validate(R, why, sizeof why) && strstr(why, "Data has 2 files, 7 bytes; should be 3 files, 10 bytes"), "missing tree file refused: %s", why);
    put("Data/c.bin", "12");
    CHECK(!dm_romfs_validate(R, why, sizeof why) && strstr(why, "9 bytes"), "half-copied tree file refused: %s", why);
    put("Data/c.bin", "123");
    CHECK(dm_romfs_validate(R, why, sizeof why), "complete RomFS accepted: %s", why);
    CHECK(dm_romfs_report(R, rep, sizeof rep) == 0, "report of a complete RomFS: %s", rep);
    put("Data/.DS_Store", "finder");
    CHECK(dm_romfs_validate(R, why, sizeof why), "dot files are not counted: %s", why);
    // The marker (plan item 175): walked once, then trusted; the key files checked every time; a change in the tree directory, another version's
    // marker or DM_ROMFS_RECHECK=1 walk again; a failed check removes it
    char mk[600]; snprintf(mk, sizeof mk, "%s.marker", R); int w;
    CHECK(dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && w && !access(mk, F_OK), "marker: the first check walks the tree and writes it: %s", why);
    CHECK(dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && !w && strstr(why, "verified before"), "marker: the next one trusts it: %s", why);
    put("Data/a.bin", "abc");
    CHECK(!dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && strstr(why, "Data/a.bin is 3 bytes"), "marker: a key file cut short is still refused: %s", why);
    put("Data/a.bin", "abcd"); dm_romfs_validate_marked(R, mk, why, sizeof why, &w);
    put("Data/d.bin", "x"); usleep(10000);
    CHECK(!dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && w && strstr(why, "should be 3 files"), "marker: a file added to the tree directory walks it again (refused): %s", why);
    char rm[1200]; snprintf(rm, sizeof rm, "%s/Data/d.bin", R); unlink(rm);
    CHECK(access(mk, F_OK) && dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && w, "marker: removed by the failed check, written again by the next good one: %s", why);
    FILE *mf = fopen(mk, "w"); fputs("test 0.9 3 10 dev=0 ino=0 mtime=0.0\n", mf); fclose(mf);
    CHECK(dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && w, "marker: another version's marker walks again");
    setenv("DM_ROMFS_RECHECK", "1", 1);
    CHECK(dm_romfs_validate_marked(R, mk, why, sizeof why, &w) && w, "marker: DM_ROMFS_RECHECK=1 walks again"); unsetenv("DM_ROMFS_RECHECK"); unlink(mk);
    printf("t_romfs_validate: %d/%d passed\n", checks - fails, checks);
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", R); system(cmd);
    return fails != 0;
}
