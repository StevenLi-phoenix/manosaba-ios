# ios — iOS app

The iOS app is the host (`host/`, `gfx/`) plus a thin shell (`Sources/`): it finds the game data, starts the host on its own thread, and turns
UIKit touches, game controllers and the app lifecycle into what the host expects. The rewritten game modules are compiled into the app's
executable (their code in the signed `__TEXT` segment, remapped to the original layout at run time: iOS cannot load new executable code), and
the translated shaders are packaged with it. The game's RomFS is not in the app: you copy your own dump onto the device.

Requirements: iOS 26 (the graphics backend uses Metal 4; `DM_IOS_M3=1` builds for iOS 18 without it), an Apple account in Xcode, and
`xcodegen` (Homebrew).

## Setup

Copy `local.env.example` to `local.env` (ignored by git) and fill in: `DM_GAME`, the device (`DM_DEVICE` from `xcrun devicectl list devices`,
and `DM_IOS_UDID` for a free account), `DM_TEAM_ID`, your own `DM_BUNDLE_ID`, and `DM_FREE_ACCOUNT=1` for a free (personal team) account.

A free account cannot sign iCloud, increased memory limit, extended virtual addressing or sustained execution entitlements; the build leaves
them out. Its profiles expire after 7 days (rebuild and reinstall), and before the first launch you trust the developer in Settings → General →
VPN & Device Management.

## run.sh

```sh
export DM_ROOT=~/dm-work
ios/run.sh build            # generate Gen/ and the Xcode project, build (log: $DM_ROOT/run/ios/build.log)
ios/run.sh install          # install on DM_DEVICE
ios/run.sh push-data        # copy the profile's RomFS and system fonts into the app's Library/Application Support
ios/run.sh launch [s]       # launch, wait s seconds (default 30), fetch Documents/boot.txt
ios/run.sh log              # fetch the log now
```

`build` needs the game's product, shader stage map and iOS shader libraries (`b-<chain version>` under `workspace.msl_ios`) — see
[PORTING.md](../PORTING.md). `DM_DEVICE_FAMILY=1` builds for iPhone only (default iPhone and iPad). The app icon is the profile's
`workspace.icon` (1024×1024 PNG) when it exists.

What `build` puts into the app (`Gen/`, generated, not committed):

| Item | From |
|---|---|
| Host sources, include paths, frameworks | `host/Makefile`'s own compile command (`tools/gen_build.py`) |
| Game modules (`<module>.patched.bin` as assembly sections of the executable) and their `fixups.tsv` / `gates.tsv` | the product (`tools/gen_embed.py`) |
| `shader_map.tsv`, `c1_full.tsv` | the profile's workspace |
| `msl/b-<V>/` | iOS shader libraries of the current chain version |

## On the device

- **Game data**: RomFS in `Library/Application Support/romfs` (`push-data`) or `Documents/RomFS` (Files app; file sharing is on); system fonts in
  `.../sysfont` or `Documents/sysfont`. The RomFS is checked against the profile before the game starts; if something is missing the app shows
  what and where.
- **Saves**: `Documents/save` (transactional, committed atomically). With iCloud entitlements the profile's `save_sync` files are synced
  (`host/cloud.m`).
- **Log**: `Documents/boot.txt` (the last foreground session; the one before is `boot.previous.txt`). Settings app → this app → "Show log on
  screen" draws the last lines over the game while playing ("Only [red] lines", "Lines": 8 / 14 / 24; touches pass through). Launched with
  `DM_LOG_PORT=<port>`, the app also serves its log over USB: `iproxy <local> <port>`, then `tools/logport.py tail <local>`.
- **Graphics settings** (Settings app → this app → Graphics): frame rate 60 / 30 fps (`DM_FPS`; the display link and ProMotion follow), render
  resolution (`DM_RENDER_HEIGHT`, only for games whose hooks apply it — manosaba: `games/manosaba/graphics.c`; read at startup) and MetalFX
  spatial upscaling of the presented picture (`DM_UPSCALE=metalfx`, `gfx/nvn/nvn_mtl.m` `fx_apply`). Frame rate and MetalFX follow a change on
  returning to the game; a variable set at launch wins until the setting is changed. iOS has no V-Sync switch: `CAMetalLayer.displaySyncEnabled`
  is macOS-only, a layer always presents in step with the display.
- **Lifecycle**: in the background the guest clock, audio and graphics are suspended and pending iCloud uploads are flushed.

## Files

| File | What it does |
|---|---|
| `Sources/main.m` | App and scene delegates, log, finding the game data, graphics settings, starting the host |
| `Sources/front_ios.m` | The front end (`host/front.h`): Metal layer, touches, controllers, lifecycle |
| `Sources/log_overlay.m`, `Settings.bundle` | On-screen log; the Settings app page (graphics settings and the log switches) |
| `project.yml` | xcodegen spec; `tools/project_free.py` / `tools/project_m3.py` derive the free-account and iOS 18 variants |
| `run.sh`, `tools/local_env.sh` | Build / install / launch; reads `local.env` (or `~/.config/dm-ios/local.env`) |
| `tools/gen_build.py`, `tools/gen_embed.py` | Host sources from `host/Makefile`; the product embedded in the executable |
| `tools/logport.py` | Live log over USB |
