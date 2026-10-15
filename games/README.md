# games — one directory per supported game

Everything that is specific to one game lives in `games/<id>/`. The core (`host/`, `gfx/`, `ios/`, `tools/`, `verify/`) reads only what is described
here, so adding a game means adding a directory, not editing the core. `tools/new_game.py` drafts a new directory from your dumped game files
(see [PORTING.md](../PORTING.md)).

| File | Required | Used by |
|---|---|---|
| `game.json` | yes | host build (`tools/gen_game.py` → `host/gen/<id>/game_profile.c`), `tools/*.sh`, `ios/run.sh`, `verify/regress.sh` |
| `rules.py` | no | the rewriter `tools/patch_nso.py` (via `tools/build_product.sh`) |
| `*.c`, `*.m`, `*.h` | no | built into the host and the iOS app (`host/Makefile` picks them up by wildcard) |

Select the game with `GAME=<id>` for `make -C host` and with `DM_GAME=<id>` for the scripts (default `manosaba`).

## game.json

| Field | Meaning |
|---|---|
| `id` | Directory name; also the executable name inside the Mac app |
| `title`, `version` | Shown in logs, the Mac app name and the iOS home screen |
| `engine` | Informational (`unity`, …); engine-specific tools live in `engines/<engine>/` |
| `program_id` | Title ID (hex), reported by `svc GetInfo` (ProgramId). `new_game.py` reads it from `main.npdm` |
| `modules` | NSO modules to load, in load order (`rtld` is never listed: the host plays its role) |
| `main_build_id` | Build ID of the supported `main` (NSO header bytes 0x40–0x4f). The loader refuses a product built from another `main` |
| `language` | Default `nn::oe::GetDesiredLanguage` answer (e.g. `en-US`, `ja`, `zh-Hans`); `DM_LANG` overrides it |
| `screen` | `[width, height]` the game renders at; the touch space until the game sets a window crop |
| `guest_heap_mb` | Heap region size on iOS (default 256); the rest of the address space reservation goes to the alias region |
| `romfs.files` | Key RomFS files and their exact sizes; checked before the game starts, which also catches the wrong game version |
| `romfs.tree`, `tree_files`, `tree_bytes` | A RomFS directory (`.` = all of it) whose file count and byte total must match: catches half-copied data |
| `save_sync` | Save files kept in iCloud sync (globs relative to the save root); empty = no sync |
| `aoc` | Owned add-on content indices reported by `nn::aoc` (omit when the game has no DLC) |
| `workspace` | Paths relative to `DM_ROOT` (default `~/dm-work`), see below |

`workspace` keys:

| Key | What is there | Who uses it |
|---|---|---|
| `exefs` | The dumped ExeFS (NSO modules, `main.npdm`) | `build_product.sh`, `verify/regress.sh` |
| `romfs` | The dumped RomFS | Mac host default `DM_ROMFS`, `ios/run.sh push-data` |
| `sysfont` | Switch system fonts (`*.bfttf`), shared by all games | Mac host default `DM_SYSFONT`, `ios/run.sh push-data` |
| `product` | Rewriter output (`<module>.patched.bin` and tables) | Mac host default product, `mac_app.sh`, `ios/run.sh build` |
| `shader_capture` | Captured shaders (`<key>.ctl` / `<key>.data`) | `gfx/tools/shmap_from_capture.py`, `dmsh_build.py` |
| `shader_map` | Shader stage map (`<key>\t<vs\|ps\|cs>`) | Mac host default `DM_NVN_SHMAP`, `mac_app.sh`, `ios/run.sh` |
| `c1` | List of shaders whose c1 index is not proven bounded (`gfx/tools/c1_guard.py`) | read next to the shader map |
| `msl_mac`, `msl_ios` | Shader library roots; libraries are under `b-<chain version>/` | Mac host default `DM_MSL_DIR`; `mac_app.sh`; `ios/run.sh` |
| `icon` | 1024×1024 opaque PNG app icon (game art: never committed) | `ios/run.sh build` |

Shader libraries are keyed by a hash of each shader's control section, so several games can share one `msl_mac` / `msl_ios` root.

## rules.py — rewriter rules

`tools/patch_nso.py` rewrites every module generically (x18, `tpidr_el0`, `tpidrro_el0`, `svc`, timer reads). `rules.py` adds game-specific
changes, keyed by the NSO build ID of the module they apply to; a module without an entry gets the generic rewrite only. The exact tuple formats
are at the top of `patch_nso.py`.

- `CUTS` — byte replacements `(offset, original hex, new hex, reason)`. The original bytes are asserted before rewriting. A cut may also put
  `svc #n` (n = 0x80–0xff) at a function entry (`svc ; ret`) or in place of a `bl`: the rewriter turns it into a host gate, and the game's C code
  handles it with `dm_host_call_register(n, fn)` (`host/hle.h`).
- `OBS` — observation gates: one instruction at an offset is replaced by "call the host, run the original instruction, continue". The game's C code
  attaches a read-only callback with `gate_obs_set(id, fn)` (`host/dm.h`).
- `PARAMS` — a constant built by a `movz`/`movk` pair is read from host thread slot `KPARAM0..3` instead; the game's C code sets the value
  through `g_keyparam[]` (`host/dm.h`).

Every rule is checked statically by `verify/structck.py` and dynamically by `verify/equiv.py`.

## C hooks

Sources in the game directory are compiled into the host. They may implement the weak hooks of `host/game.h`:

- `dm_game_boot()` — on the guest main thread, after the modules' constructors, right before `nnMain`
- `dm_game_mount_save(dir)` — before the save directory is mounted
- `dm_game_save_precommit(workdir)` — during `nn::fs::Commit`, before the working directory is swapped in; side files written into `workdir`
  are committed atomically with the game's own files (`dm_fs_save_poison(err)` refuses the commit)

They may also take over functions by mangled name with `HLE_FORCE(...)` / `HLE_FORCE_OPT(...)` (`host/hle.h`: every load-time reference
resolves to the host implementation, even when a module defines the symbol) and look up guest symbols with `guest_sym()` (`host/dm.h`).

## manosaba

`games/manosaba/` is 魔法少女ノ魔女裁判 (Switch, version 2.0.4b, Unity 6000.0.72f1 IL2CPP). Its cuts: one turns on Unity's touchscreen
player setting so the game receives native touch input; two make the witch book show its touchable "present evidence" button, which the game
hides on consoles (where A presents); one hands `Screen.SetResolution(int, int, bool)` to a host call, and `graphics.c` renders at most
`DM_RENDER_HEIGHT` lines (the iOS render resolution setting).
