// Tears of the Kingdom's environment defaults (games/totk/defaults.c): the translation thread on (plan item 61), the TBDR rewrites off on the Mac,
// and an explicit setting kept (DM_NVN_XLAT=0 stays 0 when the defaults run again). Without the translation thread's line
// (-DDM_TEST_TOTK_NO_XLAT): 1 fails.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "game.h"
const char *dm_game_ws_path(const char *rel, char *buf, size_t n) { snprintf(buf, n, "/nonexistent/%s", rel); return buf; }
#include "../../games/totk/defaults.c"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int is(const char *k, const char *v) { const char *e = getenv(k); return e && !strcmp(e, v); }
int main(void) {
    check("DM_NVN_XLAT=1 by default (the constructor ran before main)", is("DM_NVN_XLAT", "1"));
    check("DM_NVN_TBDR=0 on the Mac", is("DM_NVN_TBDR", "0"));
    setenv("DM_NVN_XLAT", "0", 1); totk_defaults();
    check("an explicit DM_NVN_XLAT=0 kept when the defaults run again", is("DM_NVN_XLAT", "0"));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
