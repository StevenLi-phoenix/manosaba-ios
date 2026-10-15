// The game this host is built for. The profile comes from games/<id>/game.json (tools/gen_game.py writes host/gen/<id>/game_profile.c); a game directory may
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
    struct { const char *romfs, *sysfont, *product, *shader_map, *msl; } ws;   // workspace paths relative to DM_ROOT (game.json "workspace"), 0 = none
} dm_game_profile;
extern const dm_game_profile dm_game;

// Workspace root: DM_ROOT, else ~/dm-work. dm_game_ws_path joins it with a relative path.
const char *dm_game_ws_path(const char *rel, char *buf, unsigned long n);
// Command-line defaults from the profile's workspace: DM_ROMFS, DM_SYSFONT, DM_NVN_SHMAP and DM_MSL_DIR are set only when unset (the iOS and Mac
// app front ends set their own before the host starts).
void dm_game_env_defaults(void);

// Hooks a game may implement (weak no-op defaults in game.c)
void dm_game_boot(void);                      // guest main thread, after the modules' constructors, right before nnMain
void dm_game_mount_save(const char *dir);     // before the save directory dir (the committed copy) is mounted
// nn::fs::Commit of the transactional save, before the working directory is swapped in: side files the hook writes into workdir are committed
// atomically with the game's own files. Called with the file system lock held: write with host file calls, not through the guest's nn::fs.
void dm_game_save_precommit(const char *workdir);

// Services for game hooks (hle_fs.c)
const char *dm_save_workdir(void);            // working directory of the transactional save (the copy the game reads and writes); NULL when none is mounted
void dm_fs_save_poison(int err);              // a side file failed to write: refuse this transaction's commit like a failed game write
