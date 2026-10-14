// The game this host is built for. The profile comes from games/<id>/game.json (tools/gen_game.py writes gen/game_profile.c); a game directory may
// also add C sources (games/<id>/*.c) that implement the hooks below or add HLE_FORCE takeovers of their own. The core reads only this interface.
#pragma once
#include <stdint.h>

typedef struct { const char *path; long long size; } dm_game_file;
typedef struct {
    const char *id, *title, *version, *engine;
    uint64_t program_id;                      // reported by svc GetInfo (ProgramId)
    const char *modules;                      // default module list, space separated ("main sdk subsdk0")
    const char *main_build_id;                // NSO build id of the supported main (hex)
    const char *language;                     // nn::oe::GetDesiredLanguage default (DM_LANG overrides)
    int screen_w, screen_h;                   // console output size the game renders at (touch space until the game sets a crop)
    unsigned guest_heap_mb;                   // heap region on iOS; the rest of the address space reservation goes to the alias region
    const dm_game_file *romfs_files; int romfs_nfiles;          // key RomFS files with their exact sizes (also tells the version)
    const char *romfs_tree; int romfs_tree_files; long long romfs_tree_bytes;   // RomFS directory whose file count and byte total must match
    const char *const *save_sync; int save_sync_n;              // save paths (glob, relative to the save root) kept in iCloud sync
    const int *aoc; int aoc_n;                                  // owned add-on content indices reported by nn::aoc (none = no DLC)
} dm_game_profile;
extern const dm_game_profile dm_game;

// Hooks a game may implement (weak no-op defaults in game.c)
void dm_game_boot(void);                      // guest main thread, after the modules' constructors, right before nnMain
void dm_game_mount_save(const char *dir);     // before the save directory dir (the committed copy) is mounted
