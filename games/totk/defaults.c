// Tears of the Kingdom's defaults: the TBDR rewrites off (DM_NVN_TBDR=1 turns them on), on the Mac and on iOS. They save tile loads and stores,
// which matter on the phones' memory bandwidth. Their trail behind Zelda's torch and glow over the prologue's cave on the iPhone 17 Pro was the store
// decision losing clears drawn as quads, fixed (plan item 62j); they stay off because on the phone's island the translation thread's CPU rose from
// 7.4-7.8 to 12.5-12.7 ms a frame and the rewrites that merge or defer passes (defer, fuse, hoist) rule out mid-submission commits: frames 66.6 against
// 33.4 ms (62j). With those three off the commits stay (228), still not faster: 55.1 against 56.0 fps. On an M5 the dive from the Great Sky Island
// measured GPU p50 8-10 % lower without them (2026-10-19). Set before main, ahead of the graphics layer's first look at it.
// The RomFS overlay games/totk/romfs_overlay.py writes (quick-menu backgrounds and the game over flow at 60 fps and up) is served first when it exists
// and DM_ROMFS_OVERLAY does not name another
#include <stdlib.h>
#include <sys/stat.h>
#include "game.h"

__attribute__((constructor)) static void totk_defaults(void) {
#ifndef DM_TEST_TOTK_TBDR_ON   // negative control of host/tests t_totk_defaults only
    setenv("DM_NVN_TBDR", "0", 0);   // 0: an explicit setting wins
#endif
#ifndef DM_TEST_TOTK_NO_XLAT   // negative control of host/tests t_totk_defaults only
    setenv("DM_NVN_XLAT", "1", 0);   // the translation thread (plan item 61): frame p95 46 → 34 ms at 30 fps, 15.6 → 11 ms at 120 Hz; an hour's soak with it ran clean
#endif
    char buf[1024]; struct stat st;
    if (!getenv("DM_ROMFS_OVERLAY") && !stat(dm_game_ws_path("build/totk/romfs_overlay", buf, sizeof buf), &st) && S_ISDIR(st.st_mode)) setenv("DM_ROMFS_OVERLAY", buf, 0);
}
