# host — loader and platform services

The host runs a Switch game's own aarch64 code natively. It loads the modules the rewriter prepared (`tools/patch_nso.py`), plays the role of
Horizon's `rtld`, and provides the platform services the game calls: kernel system calls, the `nn::` service libraries (file system, input,
audio, time, …), and through `gfx/` the NVN graphics API. The same sources build the Mac command-line host `dmhost`, the Mac app
(`tools/mac_app.sh`) and the iOS app (`ios/`).

Every file starts with a comment saying what it does and what it is based on.

## Build and run

```sh
make -C host                      # dmhost for games/manosaba (GAME=<id> for another game)
make -C host dmhost_m3            # older-API build (no Metal 4), macOS 15+
make -C host/tests run            # unit tests

DM_ROOT=~/dm-work DM_SAVE=/tmp/save DM_TEMP=/tmp/temp host/dmhost
```

`dmhost [<product dir> [module...]] [-- game arguments...]`. Without a directory it runs the profile's `workspace.product` under `DM_ROOT`, with
the profile's module list; `DM_ROMFS`, `DM_SYSFONT`, `DM_NVN_SHMAP` and `DM_MSL_DIR` default to the profile's workspace paths. `DM_SAVE` (save
directory) is always required; `DM_TEMP` defaults to `$DM_ROOT/run/platform/temp`. By default the host runs headless with a silent audio sink;
`DM_WINDOW=1` opens a window with keyboard, mouse and gamepad input, `DM_AUDIO=1` plays sound.

## How a game starts

1. `main.c` → `loader.c`: map each module's `patched.bin`, check `main`'s build ID against the profile, write the key page (thread slot numbers),
   register the gates from `gates.tsv`, relocate across modules, resolve imports (host implementations by mangled name, `HLE` / `HLE_FORCE`,
   otherwise named stubs), set segment permissions.
2. On the guest main thread: the SDK initialization in the order of `nn::init::Start` (`nnosInitialize`, `nninitInitializeSdkModule`,
   `nninitStartup`), the modules' constructors, `dm_game_boot()`, then `nnMain`.
3. While the game runs, rewritten `svc` and timer reads enter `entry.S` → `gate_dispatch` (`hle.c`); `svc` numbers 0x80–0xff are host calls
   registered by game code (`dm_host_call_register`).

## Interfaces for game code

`game.h` (profile and hooks), `hle.h` (`HLE`, `HLE_FORCE`, host calls), `dm.h` (observation gates, parameter slots, guest symbols). See
[games/README.md](../games/README.md).

## Files

| File | What it does |
|---|---|
| `Makefile` | Builds `dmhost` / `dmhost_m3`; `hle_*.c` and the graphics sources are picked up by wildcard, the game directory's sources too |
| `main.c` | Bootstrap: load the modules, initialize the SDK runtime in the order of `nn::init::Start`, enter `nnMain` |
| `loader.c` | Multi-module loading (Horizon's `rtld` role): mapping, thread slots, gate registration, relocation, takeover by name, permissions |
| `entry.S` | The two common gate entries and the static stub table |
| `thread.c` | Guest thread environment: gate slot, x18 slot, `tpidr_el0` (ELF TLS), `tpidrro_el0` (Horizon ThreadLocalRegion) |
| `hle.c`, `hle.h` | Import resolution and gate dispatch; registration macros for host implementations |
| `hle_table.c` | libc passthrough table: C library functions with the same ABI, connected to Apple libc |
| `hle_svc.c`, `hle_thread.c` | Horizon kernel system calls: memory, heap, queries, debug output, timing, threads and synchronization |
| `hle_os.c` | Guest object side table, monotonic and guest clocks (freezing, `DM_DETCLOCK`), libc imports no module defines |
| `hle_sdkinit.c` | Service initialization called by `nninitInitializeSdkModule` (no Horizon service processes on the host) |
| `hle_fs.c` | `nn::fs`: mounts map to host directories (RomFS, save, temporary storage, RAM disk); transactional saves with atomic commit |
| `hle_hid.c`, `hle_touch.c`, `dm_input.h` | `nn::hid` controllers and touchscreen, built from what the front end publishes; scripted input `DM_KEYS` |
| `hle_audio.c` | `nn::audio` AudioOut on Core Audio |
| `hle_codec.c` | `nn::codec` hardware Opus decoder |
| `hle_movie.c`, `movie_av.m`, `movie_av.h` | The Nintendo movie library (`movie::` in subsdk0, used by Unity's VideoPlayer) on AVFoundation |
| `hle_oe.c`, `hle_lifecycle.c` | `nn::oe` operating environment (focus, operation mode, language); graceful exit |
| `hle_time.c` | `nn::time` from the system clock |
| `hle_account.c` | `nn::account`: one fixed local user |
| `hle_pl.c` | `nn::pl` shared system fonts from the dumped `*.bfttf` files (`DM_SYSFONT`) |
| `hle_swkbd.c` | Software keyboard applet → native text input |
| `hle_ro.c` | `nn::ro`: no code is loaded at run time; exception info for C++ unwinding |
| `hle_net.c`, `hle_friends.c`, `hle_misc.c` | Network, friends, shop, error applet, parental controls and other services, answered as "unavailable" / offline |
| `hle_diag.c`, `hle_count.c`, `hle_fdstat.c` | `nn::diag` module info; HLE call counts (`DM_HLECOUNT`); open file descriptor log (`DM_FD_LOG`) |
| `game.c`, `game.h`, `gen/<id>/game_profile.c` | The game profile (generated from `games/<id>/game.json`), workspace defaults, weak game hooks |
| `romfs_validate.c`, `romfs_validate.h` | Checks a copied RomFS against the profile (key files, tree size) |
| `front.h`, `front_mac.m` | Front end interface; the Mac front end (headless, or a window with `DM_WINDOW=1`) |
| `app_mac.m` | Mac app form: prepares what the command line would set when packaged as an `.app` |
| `gamepad.m` | Game controllers (GameController framework) → Switch controller input, shared by Mac and iOS |
| `cloud.m`, `cloud.h`, `cloudsync.c` | iCloud save sync of the profile's `save_sync` files (Mac app and iOS; file-level core in `cloudsync.c`) |
| `crash.c` | Crash handler: guest backtrace, thread dump, watchdog (`DM_WATCH`) |
| `diag.c` | Diagnostics: probes (`DM_PROBE`, `DM_THROWLOG`), boot timeline (`DM_BOOTPROF`), guest sampling (`DM_SAMPLE`), perf record |
| `memstat.m` | Memory accounting (`DM_MEMSTAT`), low-memory watch |
| `dm.h` | Common header |

## Common environment variables

| Variable | Effect |
|---|---|
| `DM_ROOT` | Workspace root for the profile's paths (default `~/dm-work`) |
| `DM_SAVE`, `DM_TEMP` | Save directory (required) and temporary storage |
| `DM_ROMFS`, `DM_SYSFONT` | RomFS and system font directories (default: the profile's workspace) |
| `DM_WINDOW=1`, `DM_AUDIO=1` | Open a window; play sound |
| `DM_KEYS` | Scripted input, e.g. `A@10+0.2` (press A at 10 s for 0.2 s), `T0.5:0.5@15+0.1` (touch at normalized x, y) |
| `DM_WATCH=<s>` | Stop after that many seconds (exit 142) |
| `DM_LANG` | Override the profile's language |
| `DM_MOVIE=1` (`2` = trace), `DM_TOUCH_LOG=1`, `DM_FS=1`, `DM_THROWLOG=1`, `DM_HLECOUNT=<file>` | Movie, touch, file system, managed exception and HLE call logging |
| `DM_MEMSTAT=<tsv>` | Memory accounting once a second: footprint, distance to the iOS memory limit, Metal allocated, kernel ledgers, memory by kind (the guest's alias region, stack region and heap, malloc, code, GPU, …), alias region mapped and the host pages it keeps in use; on iOS a relative name is under Documents |
| `DM_TEST_MEM_LIMIT_MB=<MB>` | Mac: stand in for an iOS memory limit (limit − footprint), so the `memory low:` lines below are written |
| `DM_APP_POOL_ADD_MB=<MB>` | Application pool reported to the game: the Switch's 3285 MB plus this (default 512, may be negative). As on the console, MapPhysicalMemory fails with LimitReached past what the pool leaves after the images and the heap |

On iOS the system ends a process that passes its memory limit without leaving a report of its own. The host reads the distance to the limit every
50 ms and logs a `memory low:` line each time it falls below 1024, 768, 512, 384, 256, 192, 128 or 64 MB, with what the footprint is made of and
the Metal objects by kind; below 1024 MB the periodic memory line is no longer capped.

The Mac app (`tools/mac_app.sh`) gets no shell environment when started from Finder; pass variables with `open --env DM_NVN_SHOT=… <app>` or run
`<app>/Contents/MacOS/<id>` directly. Graphics switches are listed in [gfx/README.md](../gfx/README.md).
