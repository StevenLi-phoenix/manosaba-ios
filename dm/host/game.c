// Default (no-op) game hooks; a game directory overrides them with strong definitions (game.h).
#include "game.h"
__attribute__((weak)) void dm_game_boot(void) {}
__attribute__((weak)) void dm_game_mount_save(const char *dir) { (void)dir; }
