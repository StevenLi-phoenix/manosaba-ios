# Development notes

Where the work stands and how to pick it up again, on this machine or a new one. The repository holds all code, tools and notes; what it
does not hold (and must not: keys, dumps, anything derived from the game) is listed below with the way to get it back.

## Restoring a working copy

```sh
git clone https://github.com/iceorange-dev/manosaba-ios.git "manosaba ios" && cd "manosaba ios"   # work on main, which tracks origin/main
git remote add upstream https://github.com/jhaizhou-ops/d2r-mobile.git      # fetch only, never push there
```

Then the machine: [PORTING.md § 0](PORTING.md#0-machine-and-workspace) (Xcode, Homebrew packages, the Python venv, the shader translator).
This working copy uses `DM_ROOT=<repo>/work` (git ignores `work/`).

| Not in git | Where it lived | How to get it back |
|---|---|---|
| Console keys | `keys/` | From your own console; keep your own backup |
| Game dump (NSP, ExeFS, RomFS) | `dump/`, `extracted/2.0.4b/` | [PORTING.md § 1](PORTING.md#1-dump-the-game-from-your-console): `tools/game_extract.sh keys/prod.keys <out dir> dump/*.nsp` rebuilds ExeFS and RomFS from the two NSPs. `work/data/switch/manosaba-2.0.4b/RomFS` was a link to `extracted/2.0.4b/RomFS` |
| System fonts | `work/data/switch/sysfont` | PORTING.md § 1 (nxdumptool, system data titles 0100000000000810–814) |
| Console and SD card backups | `sd_backup_*/` | Only your own backup |
| Product (rewritten modules) | `work/build/cache/dm/out/…` | `tools/build_product.sh && verify/regress.sh` |
| Shaders, stage map | `work/build/…` (paths in `games/manosaba/game.json`) | PORTING.md § 5 |
| App icon | `workspace.icon` | Your own 1024×1024 PNG of the game's icon |
| Device saves | Device `Documents/save`; copies in `work/run/ios/*_save_*` | Files app → this app → `save`, or `devicectl device copy from` |
| iOS signing settings | `ios/local.env` | From `ios/local.env.example`. Keep the bundle ID of the installed app (`xcrun devicectl device info apps --device <id>`): another bundle ID is another app with an empty data container |

Keep `keys/`, the dump, the backups and the device saves in a separate private backup (for example an encrypted external disk); they stay
out of git by design.

Check the result: `make -C host/tests run`, `make -C host/tests run-ubsan`, `make -C gfx/tests run`, `make -C gfx/tests run-m3`,
`make -C gfx/tex/tests run`, `verify/regress.sh --fuzz`, `tools/audit_public.py`, then a Mac run (README, Getting started) and `ios/run.sh build`.

## Testing

- **Memory over repeated loads on the Mac**: `games/manosaba/reload_loop.sh [--stay] <run dir> <save dir> <cycles> [host binary] [VAR=value …]`
  loads a save's newest autosave every 40 s by tapping through the game's own menus; `tools/memstat_cycles.py <run dir>/mem.tsv` prints one
  row per load. With `DM_GUEST_ENV="GC_PRINT_STATS=1" DM_DBG=1` the IL2CPP collector logs every collection; `tools/gc_stats.py <run dir>/log.txt`
  summarizes it. The managed heap census (`DM_HEAP_CENSUS`, `DM_HEAP_WATCH`, `DM_HEAP_REFS`, …) is in [host/README.md](host/README.md).
- **A memory limit kill on iOS** leaves no crash report as a rule (JetsamEvent reports are rate-limited, the crash is "non-actionable"). With the
  device connected: `sudo /usr/bin/log collect --device-udid <UDID> --start '<yyyy-mm-dd hh:mm:ss>' --output kill.logarchive`, then
  `/usr/bin/log show kill.logarchive | grep -E 'ReportSystemMemory|killed by jetsam'`. `DM_MEMSTAT` (relative paths land in `Documents`) and the
  low-memory watch in `host/memstat.m` show what grew.
- **Pitfalls**: zsh has a `log` builtin (use `/usr/bin/log`); zsh does not split words (`set -- $var` does not do what bash does); make compares
  modification times, so two edits within one second can skip a rebuild; copy a host binary elsewhere before a long test if you will rebuild
  the host meanwhile; Apple's Metal HUD crashes with `CAMetalDisplayLink` on iOS 18.6 (keep it off there).

## Where the work stands (2026-10-16)

What works is in the README's status. Open, roughly by value:

1. **The game's own memory leak.** Every save load builds one more set of Naninovel's `ScriptManager` and `ResourceProviderManager`
   (`DM_HEAP_WATCH`); the old sets stay referenced. Live data grows about 20 MB per load, the heap about 28 MB (`GC_FREE_SPACE_DIVISOR=8` saves
   35–50 MB, not the rate). Root reference not found. Next: trace all old instances of the two managers (not samples), bound each object by its
   class's `instance_size` (`host/hle_svc.c`), scan the guest stacks too; then clear the root from a `games/manosaba` hook after a load.
2. **Trial loading peak on 8 GB iPads.** Without the increased memory limit entitlement (a free account signs it since 65) an iPad mini 7 gives about
   3.35–3.4 GB; a trial's start adds about 1.4 GB in 0.2 s (about 116 character render targets, 0.9 GB of Metal memory). A fresh session keeps about
   725 MB of headroom, a two-hour one does not (the leak). Render resolution does not help; memoryless depth needs pass merging first. A signer
   whose profile has the entitlement (3) may lift the limit; not tried on the iPad mini.
3. **Unsigned IPA** (`ios/run.sh ipa`, `DM_IPA_DATA=1`): run on an iPad Pro M4 (iOS 18.5) on 2026-10-16 signed with LCSign, which signs with
   every entitlement of its profile whatever the IPA carries (`increased-memory-limit`, `extended-virtual-addressing` true, `get-task-allow`
   false: the code remapping needs no debugger entitlement); 7.2 GB of headroom at 1 GB resident. An IPA's own entitlements (mush8's way) matter
   only for signers that read them (AltStore); not tried. Not tested: the iOS 18 build on iOS 26.
4. Smaller: 13 NVN functions looked up but unknown (NULL, never called); compute dispatch never seen; keyboard, mouse and controller untested
   with this game; Metal HUD on iOS 18.

Devices on 2026-10-16: iPhone 17 Pro (iOS 26.4) a build from before 1f54dad, iPad mini 7 (iOS 18.6.2) 68c048b, iPad Pro M4 (iOS 18.5) the
unsigned IPA (build 1792140837).

## Next game: Tears of the Kingdom (started 2026-10-17)

`games/totk` (1.4.3, program 0100F2C0115B6000, main build 277178b7dba1b6d4949a84778d4abc58), data from your NSPs with `tools/game_extract.sh`
into `work/data/switch/totk-1.4.3`. SDK 20.5.22; `sdk` and `subsdk0` have the same build IDs as nxnative's Breath of the Wild 1.9.0 (the same
system layer). What holds as of 2026-10-19 (later work: the plan below). On the Mac it plays the title, the prologue, the tutorial, the Ganon movie
and the first blood moon movie.

**Running it.** After `make -C host GAME=totk`:
`DM_GAME=totk DM_ROOT=$PWD/work DM_SHDUMP=$PWD/work/build/totk/shader_capture DM_SAVE=… DM_TEMP=… DM_CACHE=… DM_WATCH=120 host/dmhost` (stage
map and library from the profile; rebuild them with `shmap_from_capture.py` and `DM_GAME=totk dmsh_build.py`).
- The default product stays at 30 fps while the blood moon crash below is open; `work/build/totk/product60` carries the 60 fps rules
  (`DM_RENDER_RES=<w>x<h>` scene size, item 52; `DM_OUTPUT_RES=<w>x<h>` output, item 63; code options since item 57, `options.default` all of
  them, `DM_OPTIONS=none` none).
- RomFS overlay for 60 fps and up: `work/build/cache/dm/venv/bin/python -I games/totk/romfs_overlay.py work/data/switch/totk-1.4.3/RomFS
  work/build/totk/romfs_overlay` (quick menus' background, the game over menu's one-frame wait); served first when it exists
  (`games/totk/defaults.c` sets `DM_ROMFS_OVERLAY`; empty turns it off). Test: `games/totk/tests/t_romfs_overlay.py <RomFS>`.
- TBDR rewrites off on the Mac (`games/totk/defaults.c`; `DM_NVN_TBDR=1` on): GPU p50 8-10 % lower without them on the M5; mid-submission
  commits and layered passes need them off.
- Window: game.json `present_interval` "game" (the display-link wait takes the game's interval: it steps per frame), `window_unfocused`
  "in_focus" (OutOfFocus is the console's HOME menu); `DM_PRESENT_HZ=30..120` sets the display link's rate (mode 1).

**Rewrite** (`tools/patch_nso.py`; `verify/regress.sh --fuzz` OK for all three modules):
- `blr x18` whose x18 was loaded in an earlier block: the K18 slot holds it wherever it was loaded (nxnative's blr rewrite has no such check).
- Jump tables with explicit targets (`adr; ldrsw / ldrh / ldrb; add; br`): targets derived over the function's control flow to a fixpoint, then a
  compare chain with landing pads as nxnative's `site_br_tab` (`sub` + `cbz`, NZCV kept); an index outside stops at `brk #0x18`. Row `JT`.
- Exclusive sections by control flow (`exclusive_regions`): TotK's compare-and-swap retries sit in cold blocks, giving sections in pieces copied
  by `site_region` (row `R`). Any other entry stops the rewrite.
- `mrs xN, ctr_el0` (SIGILL on Apple systems) → `movz xN, #4, lsl #16` in place, as nxnative (row `ctr`); also two words of manosaba's `sdk`.
- `HOSTREFS`: instructions that address a 16 KiB writable host page after the key page (fixups `HOST_PAGE_OFF`, only in modules that use it),
  resolved into cuts; the loader reads it (`dm_host_page`).
- Verifiers: `structck` ⑪ (windows, sections, explicit tables, single entry) and ⑫ (`ctr`); `equiv` runs sections from each piece and skips a
  trial reading below sp (the red zone); `fnfuzz` gives both images the `ctr` constant. Upstream PR #55 (x18 scan prefilter, n3crosis) is in.

**Host** (most from nxnative, credited in the code): oe (`GetPreviousProgramIndex` = -1, launch parameter, startup logo,
`PopNotificationMessage`, volume balance, ARUID), profile field `device_api` (TotK 55.15, manosaba 53.311), hid mouse / vibration batch, the
in-process audio renderer (`host/hle_audren.c`, `host/hle_guestthread.c`, `dm_pcmout_*`), the hardware Opus C API, indexed cache storage
(`$DM_CACHE`), another application's save not existing (TotK looks for BotW's), svc GetInfo 20, play reports, amiibo (`host/hle_nfp.c`), clock
snapshots and steady / network clocks (from Ryujinx), NVN functions (13 names in `gfx/nvn/names.txt`).
- Cores (`host/hle_thread.c`): the job system keys per-worker output by `nn::os::GetTlsValue` = core + 1. Core mask calls follow the Horizon kernel
  (Atmosphère mesosphere: -1 don't care, -2 the ideal core, -3 keep it), ideal core and mask from main.npdm. Virtual cores (game.json
  `virtual_cores`, from MacNativeGames' runtime/threads.c): a guest thread holds one of three while it runs guest code; host-work threads
  (`dm_guest_thread_spawn`, the audio renderer) hold none.
- Command memory: a disabled or short callback continues in host segments. TotK calls cached 512-byte bind windows every frame, so `cmd_t`
  leaves +0x10 to the game (`guest_flags`: its NVN 55 inline wrappers store a dirty byte there), a host segment lives as long as the JUMP that
  links it, and the JUMP counts as used (`t_nvn` 5a).
- Movies: the 28 in `Event/Movie` are WebM, one VP9 track, no sound. `SetDataSource(path)` (`dm_fs_host_file`), a WebM / Matroska demuxer
  (`host/movie_webm.c`, RFC 9559 / 8794), a VP8 / VP9 decoder on libvpx with MacNativeGames' behaviour and pacing (NV12, at most one frame per
  game frame, late frames dropped); the second GetOutputBufferSize output is the alignment, 256; status codes from the SDK's `movie/Status.h`.
  End-of-stream inputs stay in the decoder until Flush / Reset / Configure, as ACodec (handed back, as MacNativeGames does, the game stayed
  black after the movie). libvpx from Homebrew, static when installed; no VP9 decoder in the iOS app yet. `t_movie` (`t_movie <file>` probes a movie).

**Shaders.**
- Keys are sha256(control section + code area) everywhere (`shader_key` in `hle_nvn.c`; the 0x880-byte control section alone collides).
- game.json `nvn_c2` (texture handles in c[2]: 0x20 for TotK, 0x168 for manosaba) and `depth_mode` (TotK `near_is_minus_w`: z' = z / 2 + w / 2
  before every vertex return); both in the translator job, the cache key and the reflection. `dmsh_build.py` needs `DM_GAME`, the translator
  `DOTNET_ROOT` with Homebrew's .NET (`/opt/homebrew/opt/dotnet/libexec`).
- `QueryHostSupportsShaderFloat64` false: Ryujinx's `DoubleToFloat` runs DMUL / DFMA in float (chain `ae97fd60e78b`). Integer pixel outputs:
  a failed pipeline writes `<DM_SHDUMP>/fout.tsv`, which `dmsh_build.py` passes to the translator (chain `530a8546d06c`). Vertex shaders padded
  for their pixel shaders (`vs_pad`).
- `engines/bnsh/shader_extract.py` reads the shipped BNSH (`Pack/Bootup`, `Lib/**`, `Shader/**`, as MacNativeGames' prepare_shader_seed.py),
  plus `Effect/**.esetb.byml.zs`, 0x88c-byte control sections and the sharcb archives, into `work/build/totk/shader_capture` (27077 shaders,
  27075 translate; SPIRV-Cross fails on one vertex shader of `AglShader.sharcb` and one compute shader of `agl_technique_pfx.sharcb`). Shaders
  in main stay run-time captures. `c1_guard`: 49 shaders whose c1 index is not proven bounded.

**Replay** (`gfx/nvn`):
- 8-bit index buffers widened to 16-bit; RGB10A2SN; a view swizzle replaces the texture's (`view_t` stays 10 bytes); views Metal cannot make are
  sampled as the texture.
- CPU-filled textures (as LASRecomp): a texture in a CPU-visible, non-compressible pool that the GPU has not written is uploaded once the CPU has
  filled it; the first GPU write makes it the GPU's (`tex_gpu_write`). Linear textures in CPU-visible pools live on the pool buffer
  (`tex_linear`, nxnative). Small textures are checked by content (06c80bd).
- Counters: reports other than samples passed write their counter half in command order (TIMESTAMP ones 0), their timestamp at completion;
  conditions are evaluated at replay from guest memory, look-aheads treat a render enable change conservatively; occlusion counting started inside a
  pass splits it. Counter timestamps run on the guest clock (`guest_ns`): `CLOCK_MONOTONIC` counts the Mac's sleep, and the game's frame-rate
  controller reads them.
- From MacNativeGames ([ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames), MIT, reviewed at
  41e1e0caf43d58c4af3c478ca2d1bafd9dba7093, credited in the code): a registration without a view clears the old view; partial clears as one
  triangle (`clear_quad` / `clear_ds_quad`); BC render targets through a one-texel-per-block view (`blk_aux`); storage buffers addressed from a
  uniform buffer (`sbuf_ubo`, `NVN_RB_SBUFU`, `pend_read`); the storage buffer write flag is bit 0.
- A draw without attachments gets a pass sized by the scissor. Also: `rt_dup_slot` (nxnative / tomodachi-native), inline uniform data in a host
  ring (nxnative), `BindProgram` as recorded, `SetRenderEnable`; `Read|WriteTexels`, `QueueFinish`, `SubmitCommands`, `PresentTexture`
  under one recursive lock.
- Uniform snapshots (`mtl_sub_snapshot`, MacNativeGames' usnap method, MIT, written anew): the game rebuilds per-object constant pools once a
  frame is submitted, so each submission copies the uniform ranges it binds into its argument arena when it starts (merged within 256 bytes, at
  most 1 MiB) and binds and reads the copies; a GPU write within the submission drops the copies it reaches. `DM_NVN_UBO_SNAPSHOT=0` reads live
  memory. About 0.9 ms per submission in the dive. `gfx/tests/t_usnap.m`.

**Frame rate** (from MacNativeGames' `game_patches_v143.c`, MIT):
- The controller (main 0x2db174) sets present interval clamp(int(max(CPU, GPU) × 60 + bias) + 1, 2, 3); `games/totk/rules.py` CUTS its seven
  words to allow 1. `nvnWindowSetPresentInterval` logs a backtrace on every change.
- Real-time step: the two producers (0x2daa54 ratio, 0x3e5638 ticks) load +0 (seconds × 30) and +4 (seconds × 60) from main's host page;
  `games/totk/frame_step.c` writes there at every present (`dm_game_present`) the mean of the last five present intervals on the guest clock;
  `DM_FRAME_STEP_MS` pins it. Checks: `games/totk/check_frame_rate.py <product> <main NSO>`, `tests/t_frame_step.c`.

**Diagnostics**: `DM_WATCHPOINT` (`host/diag.c`), `DM_CRASH_MEM` (`host/crash.c`, `*xN`), `DM_NVN_PASSDUMP_ALL`, `DM_NVN_GPUWATCH=<first>-<last>`,
`DM_NVN_LIVEROWS=<file>` (single wrong frames in window play), `DM_NVN_LIVESHOT=<first>,<count>:<path with %d>`, `DM_NVN_NOCULL`,
`DM_NVN_RTCLEAR=<serial>`, `DM_NVN_TEXSNAP`, `DM_NVN_TEXNZ`, `DM_NVN_TEXCLASS`, `DM_NVN_REUPLOG`. The cliff repro: a manual save at the cliff
edge, `DM_KEYS="A@26.5,A@31,A@35,LSU@f1380+240,X@f1400"`. Lesson: MacNativeGames' `nvn_mtl.m` described the dive's flicker and its fix; search
the references for a symptom before building probes, then measure to confirm the cause here.

**Open (2026-10-19, plan item 72):** after skipping the second blood moon movie (`DmT_SY_BloodyMoonDarkZelda`, stopped at frame 233) the game
crashed 3 s later in its resource control thread (0x1006, loop main+0x154bd0, which names `ClearCachesForKeepAllocSize` / `Unload(ForSync)`):
main+0xa2ef08 reads the resource at +0x28 of a binder of type 0xe (+0xc) after waiting for it (`WaitLightEvent` at 0xa2ee2c) and +0x28 is null;
registers name `Obj_TreeWhiteBirch…`. The SDK's `WaitLightEvent` trusts a 0 result of `WaitForAddress`, and our address arbiter only returns 0
from `SignalToAddress`. Not reproduced yet; the thread dump marks the thread that writes it.

Known gaps as of 2026-10-19: alpha test and depth-stencil (stencil) views are not modelled; an integer render target's partial clear is not drawn
(red line); iOS needs `DM_CACHE` and the large heap. Since taken up in the plan: the BC render target path for GPUs without BC (62c3), hid
six-axis and vibration (67a-67d), the 2K canvas (63b).

## Upstream and other ports to draw from

Reviewed on 2026-10-17, twice on 2026-10-19 and three times on 2026-10-20; every finding of those reviews is a plan item below (each tier says
which review it came from). Fetch the projects into a scratch repository, read only, never run their code; port by hand with a test per item,
check our code first (several fixes are already here in another form), and credit the source in the commit and the README. Each review's
findings with their evidence: `git show 66fa170:DEVELOPMENT.md`.

### Sources and how far they were read

The commits are the last ones read (dates in October 2026); the next sync starts from the last of each. Look for new projects in
d2r-port-radar first.

d2r-mobile and its derivatives (MIT, keeping jhaizhou's LICENSE; the derivatives use the old `dm/` layout):
- [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (upstream, fetch only): 21cf97b91823111ead237858ea79929afacf71a5 (10-17),
  9393b00 (10-19), 6996690 (10-20), 53e7c5b (10-20 evening, PRs #96, #97, #100). d2r-mobile/d2r-mobile is a rewritten copy of its history up to
  21cf97b with no code of its own.
- [seedds/nxnative](https://github.com/seedds/nxnative) (main): 20cba12c4299d77f05b6bbfd3dfa42d6c6035da4 (10-17), 64b79de, 2b31dbd and 19d7a70
  (10-19; 73-77), 7e2c741 (10-20; PR #11, digitalhivehub `nvn-block-height` a6b7191), b932024 (evening, PRs #11-#20). seedds/smbw-mobile is the
  same repository renamed. NSP → IPA; its README lists Super Mario Bros. Wonder, Fire Emblem Engage, Metroid Dread, 13 Sentinels, Super Mario 3D
  World + Bowser's Fury and Breath of the Wild 1.9.0 as playable on an iPhone (iOS 26).
- [Johnoooob/nx-port](https://github.com/Johnoooob/nx-port): one commit, 3e6366b86a510361d451618ffd7c45dea5549492, unchanged through 10-20.
- [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile): 95d6caa, 1539d80 (10-19), nothing new on 10-20; branch
  face-buttons-by-printed-glyph.
- [4ppp44/d2r-mobile-ios17](https://github.com/4ppp44/d2r-mobile-ios17), branch codex/ios17-a16-v1.0.17 (406028335f5f0fa65c5a421867abcde0987d0a9e,
  b638a1886b16f9a9d5c10eaa118231fb8544dfb9; 2a7b1c6 on 10-19, nothing new on 10-20): an iOS 17 / A16 Metal 3 build. iOS 17 is not supported here (59).
- TommyLau/botw-mobile: gone from GitHub (404 on 2026-10-17); its commits listed below can no longer be fetched.
- [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames) (main): 41e1e0caf43d58c4af3c478ca2d1bafd9dba7093 (10-18), 70729e9 in
  full and f5cc151 (10-19), 15b1afd (10-20; tvc97's fork 13 ahead), b75be8c (evening). Tears of the Kingdom 1.0.0 / 1.1.2 / 1.4.3 on the Mac, its
  NVN layer from d2r-mobile's (`games/totk/third_party/d2r_gfx`); its 1.4.3 notes give 83.9-110.2 fps averaged over 15 fixed-camera points on an
  M4 (docs/v1.4.3-port.md), about 27-30 fps with the game's own controller.
- [T5eng/nxnative_totk](https://github.com/T5eng/nxnative_totk) (MIT with a note): 1c22d16 (10-19), 057575d (10-20). Tears of the Kingdom 1.4.3 on
  an iPhone Air (iOS 27.0.1) as patches on MacNativeGames; `docs/TECHNICAL.md`, PERFORMANCE.md.
- [atone/nxnative](https://github.com/atone/nxnative) (MIT): 185cfda (10-19, unchanged on 10-20). Pro Controller motion (HID report 0x30) and HD
  rumble (report 0x10).
- [Nishikori-Yui/lordran-native](https://github.com/Nishikori-Yui/lordran-native) (MIT, from nxnative; Dark Souls Remastered): 8f5b96a (10-19),
  f7aed21 (10-20, a branch), main ecb74d5 (evening).
- [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile) (MIT), branches `codex/p5r-runtime` and `codex/p5r-shaders-verified` (Persona 5
  Royal): 8dd2950 (10-20), `codex/p5r-runtime` 639cf35 (evening).
- [enginefuture/bayonetta3-mobile](https://github.com/enginefuture/bayonetta3-mobile) (MIT; on blade2-mobile c981fcb): e5ce116.
  [enginefuture/dr3-mobile](https://github.com/enginefuture/dr3-mobile) (MIT; Diablo III, a patch on nxnative 9579030, with LDN): a31c36c.
- [huajiaoshu520/acnh-mobile](https://github.com/huajiaoshu520/acnh-mobile) (MIT; Animal Crossing on iOS 26, on upstream b94670c plus nxnative
  files): c764129, c2af194 (10-20), bd2e176 (evening).
- ha7rock/blade2-mobile (MIT; Blade II, UE4 4.17, on upstream f43480d): 43e760e. MZhao-ouo/Touchable (MIT; on upstream 2eb5758; Rhythm Heaven and
  Silksong): 5cab3ac (10-20 evening). tomodachi-native (MIT): 5587d1b (10-19), nothing new on 10-20. armyja/nxnative-aa-port (MIT; Ace Attorney:
  Unity 2017 / 2022 IL2CPP and RE Engine, on nxnative 280ead9): df997fa.
- dodola: 7ccbb3f (the newest write per 64-page block), `codex/zelda-echoes-of-wisdom-android` (an ordered NVN worker).
- Forks with pushes are mostly pull-request branches upstream has merged; the renamed ones (jeoichu-rgb/switch-mobile, Kelvin-Link/ns-mobile,
  wysl/-game-d2r-mobile) had no commits of their own on 2026-10-17.

Independent projects:
- [c22dev/mush8](https://github.com/c22dev/mush8) (**GPL-3.0: read it for ideas and behaviour only; no code goes into this MIT repository**):
  1b2e9b875f3beb94802624f055e7a041a473ee3c (10-16), 975040b (10-19), nothing new on 10-20; justr4mzi's fork, GPL too: ideas only. Mario Kart 8
  Deluxe on iOS and macOS in Rust (`nxpatch`, `nxhost`, `nxshader`, `nxfs`). Its IPA is ad-hoc signed with
  `com.apple.developer.kernel.increased-memory-limit` (`make-ios.sh --ipa`, `ios/Mush8-sideload.entitlements`) for signers that read the app's own
  entitlements (AltStore does, see [ios/README.md](ios/README.md)).
- peiikai/LASRecomp (MIT for its own code, with a note): main d23597d, `beta_5` 7597ec3 and 15 other branches (10-19); `beta_5` 44f5308 and 25
  other branches (10-20); `beta_5` f2c6fb4, `test-perf-7` 13f54af (evening). hjqcan's fork: its `multi` branch is now one squashed commit c404f1d.
- Apolinario-coder/SkyRecomp (MIT with a note; much of it is LASRecomp's): d876287 (10-19), 9722390 (10-20), c8de7ab (evening).
- BotWRecomp (MIT; `fps.cpp` read for ideas only): 2b84b82 (10-19), nothing new on 10-20.
- [OJZen/totk-mobile](https://github.com/OJZen/totk-mobile) (Apache-2.0, its `core/` upstream MIT and byte-identical to upstream b94670c): 510d95b
  (10-20, nothing new in the evening). Tears of the Kingdom 1.4.3 on a Mac and M1+ iPads, an iPhone 15 Pro to the title, independent of
  MacNativeGames and nxnative; its own work is `script/prepare_graphics.py` (exact-match replacements applied to `core/` at build time), `host/nvn_*`
  and 74 tests. Reimplement, or keep its licence and NOTICE.
- masagrator's FPSLocker-Warehouse (its Tears of the Kingdom 1.4.3 patch file, credited by totk-mobile): no licence, facts only.
- hyc1743/super-mario-odyssey-ios (MIT; its own rewrite): 6cd140f, `codex/nxnative-verify-once` ea19678 (evening). vecStardo/VA-11_ns2ios
  (GameMaker through GLES, no NVN): little to take.
- [catoncat/d2r-port-radar](https://github.com/catoncat/d2r-port-radar) (MIT): an index of derivative ports refreshed every few hours, with UE4 /
  CRIWARE findings: 13d100c (10-20, 260 projects), 7bffcbd (evening, 264 entries).
- New on the 10-20 evening, not reviewed: yokeyhh/switch-port-to-apple (a porting playbook on d2r-mobile and totk-mobile, MIT),
  DanielZhangyc/smbw-mobile (Super Mario Bros. Wonder on the Mac, calibrated Switch 2 Pro controller input and dual-band rumble, MIT),
  CODPHISH/ns2app (Silksong on iOS), WangTingYeYe/rtg-mobile, rootrd/dontstarve-harmony (NVN → Vulkan), two A12Z iPad Pro D2R ports with per-slot
  bindings (aerolithink, bluewater0121). hitsmaxft/nsp-dawrin (GPL-3.0): a synthetic NSP that CI could use.

Not worth porting: nxnative's prebuilt .NET DLLs, Android / Vulkan and MoltenVK runtimes (dodola, imink/hades-ios), upstream's D2R-only parts.

### Reviews and the plan items they produced

- 2026-10-17, upstream and the derivatives: ported that day (below).
- 2026-10-19, all references fetched again: ported the same day (below) but for three features, now 63 (the 2K canvas and MetalFX), 67 (gyro and
  rumble) and 62c (BC → ASTC).
- 2026-10-19, wider review (eight reviewers: new commits on every branch, every PR and issue, forks ahead of their parent, the code against ours,
  25 projects found by search): items 1-72, with 62c, 63 and 67 carried over; nxnative 2b31dbd..19d7a70 the same afternoon: 73-77.
- 2026-10-20, wider review (seven reviewers from the markers of 10-19, about 70 searches): 78-114.
- 2026-10-20, deep review (each project's own work against its base, and which of its tests could check ours): 115-141; every remaining finding
  of the three reviews, at the user's request: 142-148 and, seen but not planned before, 149-154.
- 2026-10-20 evening sync (only what was new since the morning): 155-180, and changes written into 62b, 67, 94a, 100, 131, 133, 134, 139 and 151.

Evidence the open items rely on:
- 94a: totk-mobile (`nvn_texture_rendered.inc`, `nvn_texture_writeback.inc`) writes back a GPU write after 30 frames without another, at the end of
  that submission, with per-page hashes; ours does only on `nvnMemoryPoolInvalidateMappedRange` unless `DM_NVN_WB_IDLE=1`. The Great Sky Island
  grass had a second, proven cause, the border colours of 161 (done): judge the write-back against the console footage with 161 in.
- 97: LASRecomp PR #203 (364b383): on an M3 Max a plain present every 33 ms let macOS drop the GPU to 338 MHz (frame 20 ms instead of 7, locked at
  30 fps); `presentDrawable:atTime:` kept the clock, `afterMinimumDuration` did not. Our keep-alive kernels are Mac only.
- 132: totk-mobile `nvn_wide_line.inc` expands the wave program's lines on the GPU.
- 61d: MacNativeGames' job queue sets the time-constraint policy on its translation thread: without it, replay 6.6 → 13 ms.
- 62 (a device run): T5eng: about 6 GiB of address space below 2^39 on iOS 27, arm64-iphoneos metallibs (MSL compiled on the device gave argument
  encoders of length 0), a translation thread (frame p50 22.9 → 16.67 ms on an iPhone Air), handheld 1280x720 (2-3 ms GPU and 100 MiB less), A/B
  runs need a cooldown (60 fps cool, 30 fps hot). totk-mobile: 12.5 GB reserved with extended-virtual-addressing on an iPhone 15 Pro and M1 iPads
  (exit if above 2^39), the stock application pool, and "do not push the large resource files with devicectl directly" (it can exhaust the Mac's
  memory and disk). Bayonetta 3 on an iPhone Air: 1568x720 → MetalFX 2736x1256, median 17.1 ms, GPU 9.4 ms, footprint 2.4 GB, thermal 1, no jetsam;
  a present buffer lost to GPU recovery at launch. MacNativeGames e099353: ~8,800 kept metallib mappings filled an A16 iPad's 11,400 vnodes (103).
- 62g: bayonetta3 exports the union of all fragment inputs from every vertex shader (2,045 libraries against a 14-location interface, no pipeline
  failure in 181 new pairs); TotK's fragment shaders use 24 locations.
- 62h: totk-mobile `nvn_texture_alias.inc`: Great Sky Island has 354 targets, 373 MiB of layouts in a 111 MiB pool; a heap of 1.25× the pool
  places all but 3 MiB.
- 64 (the next game; the open sub-items carry their own sources): from 10-20, add-on content mounts (upstream PR #69), UE4 / CRIWARE notes
  (d2r-port-radar), `Downsample` as a resolve, VideoToolbox VP9; the 47 NVN functions bayonetta3 has and we lack were split into 64a / 64a2.
- 65: BotWRecomp `ios/PORTING.md` (Xcode made a Personal Team profile with increased-memory-limit) and mush8 `make-ios.sh` (FREE=1 keeps it) say a
  free account can sign it; `ios/tools/project_free.py` strips it and `ios/README.md` says it cannot. MacNativeGames played 1.4.3 on an iPhone 14
  and an A16 iPad "signed with a personal development team in Xcode", with the entitlement (3.6 of 4 GB used). One signing attempt decides
  (`embedded.mobileprovision`, `os_proc_available_memory`). Decided on 2026-10-21: it can (65).
- 66: upstream 46cb318 (issue #57: a Switch Pro on iPadOS 27.0.1 recognised as Nintendo and still swapped); Kamisorara 55c6fba is the same fix.
- 68: MixWithOthers from upstream 9393b00, ours 46b63f4, never run on a device. 69: the overlay's game over flow is checked by bytes only
  (`games/totk/tests/t_romfs_overlay.py`). 70: its metallibs are built in `work/build/ios/dmsh_b/b-bab8d4746968`; the last device builds recorded
  are from 2026-10-16.
- 71c: BotWRecomp `fps.cpp` (ideas only) lists Breath of the Wild's 30 fps constants and the cutscenes that crash above 30 fps; FPSLocker says
  gameplay speed, physics, UI and logic follow its frame step alone.
- 72: none of the projects had anything on the blood moon crash (10-20). Leads: `WaitLightEvent` trusts a 0 result (svc 0x34 type 3, fixed in 1);
  Tears of the Kingdom makes threads with stacks as small as 4 KiB and 16 KiB host pages make 4 KiB guard pages writable (40); FPSLocker marks its
  cutscenes "must be blocked to certain framerate to not cause issues" and our crash follows a skipped pre-rendered movie (130's hold); the guarded
  backtrace (125) and the frames-stopped dump (107).

Not taken (and why):
- A Metal binary archive on macOS 26 without guarding `serializeToURL` (LASRecomp turned it off after crashes; PR #145 closed).
- Compiling shaders at launch and skipping draws until ready, async shader loading with skipped draws (content drawn once would stay missing).
- Present pacing to the screen refresh (games that step per frame would run fast; only TotK's real-time step can use it, `DM_PRESENT_HZ`).
- SignalToAddress −2 / LASRecomp's `value - 2` / justr4mzi's kind 2 (Atmosphère corrected it to −1 in 5f2d713; ours is right).
- nx-port's clamp of fragment outputs to ±65504 (breaks R32F); GirlNet's per-draw conditional render (a CPU stall).
- Equal or better here: the Opus table leak (we run the SDK's decoder), macOS 27 fixed reservations (ours are anywhere), a Metal 3 backend, one
  descriptor pool, lordran's deswizzle and in-place re-upload (ours waits for the fence / keeps a spare, 16), the audio mixers and renderers of
  LASRecomp, SkyRecomp, MacNativeGames, nx-port, totk-mobile and blade2 (our in-process SDK renderer is closer), saves (transactional), the condvar
  and WaitSynchronization order, totk-mobile's arbiter (it refuses type 3), per-axis stick clamp, jump tables and virtual pools, MacNativeGames'
  aliased buffer copies, serial history, run-time MSL caches, ultrawide / stretch (154 took the aspect patch), UIScene lifecycle, audio ring trim
  and shader list (its 492 1.4.3 survey shaders are all in our set), T5eng's touch controls (58), iOS metallib flags and `__TEXT` embedding,
  nxnative's torch light (77), render conditions, integer outputs, fast clears, depth clamp, attachment-less passes, MetalFX, uniform renaming,
  menu capture (the overlay), BC render targets (`blk_aux`), render scale (`DM_RENDER_RES` is absolute) and translator phi fix, upstream's D2R UI,
  device lab tools and PR #86, GirlNet's GPU primitive restart, swkbd trimming, linear R8 / RG8 views and atlas re-uploads, LASRecomp's
  16-sampler limit (argument buffers) and stale-program detector, SkyRecomp's 120 fps step, bayonetta3's two signed main images, VP9 as AVC and
  launcher page, acnh-mobile's packaged texture copy (the SDK says ignored), conditional ledger and pool cache, dr3's program snapshot,
  super-mario-odyssey-ios's audio ring and touch ABI, armyja's system clock context, Touchable's texture paths, fs, mixer and NCA extraction.

### Already ported

- Before the reviews: thread exit off the guest stack (014c5b3), crash report stderr trylock (1f1929e), dirty tracking of shared host pages
  (e4292e1), RomFS name index (af27ace), save full-disk robustness (eb86cfc, upstream b94670c), msl_retarget reflections (56fcd40, upstream 1784563).
- 2026-10-17, upstream: copy image stride (86b1315a84cec67cfef0b3eab125dbc3c4489cc4, `nvn_copy_pitch.h`, `t_copy_pitch`; d21348c), `UILaunchScreen`
  (21cf97b; ce466a9). Already here in the same form: weak imports stay NULL (b84657b25735d96a7263a1b4a7402159a17435a5), `DT_RELR`. Not applicable:
  D2R's Mac mouse (098d9f5e24407480da7cf0d21c5d88b871da6949); dc53e7a8eca1f170009b722d390fac7e0d050288 was reverted by 21cf97b.
- 2026-10-17, nxnative: pool lookup by last hit and a sorted index (`nvn_pool_find.h`, `t_pool_find`; d48fb4f), `tex_fit` views, 1D textures as
  one-row 2D, no write mask or blending for unwritten colour outputs (621bd6d), `DM_KEYS` timed by presented frames (9475f85);
  `tools/game_extract` after nx_extract (aa8cc5c…, LibHac 0.19.0 from NuGet, pinned by content hash; checked byte for byte with manosaba's NSPs;
  XCI input and a real personalized ticket not run).
- 2026-10-19 (each with a unit test and a broken version): shader chain 1e41705de87b (686b81e: per-stage driver segment reads, two translator
  fixes, vertex outputs from (0,0,0,1), `-fpreserve-invariance`, `-fmetal-math-mode=safe`, `gfx/tools/dmsh_mtlcache.py`); correctness 06c80bd
  (autorelease pool, c0 fills, block height, D16 clears, packaged texture data, primitive restart, 8-bit index widening, storage follow, textures
  sharing a storage, write-back on invalidate, QueueFinish, the GPU memory budget and pool checks); WaitSynchronization order 4dd984d;
  MixWithOthers 46b63f4; the RomFS overlay 601258f; pending writes across submissions, acquire sync, keep-alive, chunked commits, uniform
  snapshot and rename, the render-state filter, one compute encoder, upload staging, identical registrations, the dirty-tracking cost, layered
  passes. The encode stage is implemented and off (`DM_NVN_RENDER_STREAM=1`; it lost at frames 1800 / 2400; 61's thread took its place). The GPU
  times of that day are sums of command-buffer spans (5).
- Done from the list below: the AudioRenderer and deferred texture binds (a267ec1), VP9 movies (a8c3ff1, `host/movie_webm.c`), small guest stacks
  (40, `host/hstack.S`), the BAHS shader split (`engines/bnsh/shader_extract.py`, 70cb843, 17766ae, 36), a texture at two colour slots
  (`rt_dup_slot`, through tomodachi-native's variant), face buttons by printed glyph (2; Kamisorara 8429dc1 was the earlier, wrong variant).

### Port when a game needs it

- Graphics, nxnative d6cd8a812a1047dff8f660f604d0a46033752242: cube textures as 6-layer 2D arrays for cube-array views; compressed render targets
  skipped with a red line instead of a Metal abort. The attachment snapshot for shaders that sample what they write, per shader by a profile key,
  with `DM_NVN_MAGSCAN` (20cba12c4299d77f05b6bbfd3dfa42d6c6035da4, `nvn_mtl.m`); the submission handed to the GPU every 64 draws for games that
  wait for the GPU every frame (`DM_NVN_CUT`, ff74840…).
- Diagnostics: `DM_NVN_KEYDUMP` and sampler / swizzle output in PASSDUMP (734a286328d18404fd5a67785a1e05d2149c5986).
- nn::ro .nro modules: nx-port `dm/host/hle_ro.c` and loader changes (3e6366b…), with `R_AARCH64_TLSDESC` and symbol precedence.
- Older Unity bundles: nxnative `dm/gfx/tools/unityfs.py` (e3cd2ff8ff0eb84a62280ef70e90c60753e546e6).
- Tools, nxnative: nso_syms.py (aa8cc5c0cd8bd5d09bcacb019f7832df3a164452); nso_fnmap.py, carrying cuts to a new build of `main`
  (b5507f2aac7e1d1f5512de750f5810a550162b04, also for a manosaba update); build_ipa_from_nsp.py (0d763ec864344610cc41548a2ad01cd55def3182); DLC
  packing (e452f52b607bdf6f960eb843f20aeb29b803dec8); the app icon from the game's icon (1cd9bfb84b31fad5b413ab20baa977c16600f50a).
- An on-screen gamepad for games without touch: nxnative `dm/ios/Sources/DmGamepad.m` (dc8a49ad7471d3f0b767032a0b755f87a98c1912,
  d41fdd6ed1643ed7f97099f18ab6fc7360786a1b, 4a0f9acbcf8e5acad01b1d6b5919bfb3e3cd0998; the 3D layout b65c8b2cfe43d0ccbc2dcd24068d849ff4eb6baf).
- NVN / graphics from botw-mobile (no longer fetchable): depth and stencil sampling modes (b795ac15d620f42986df3d594e846bf03f83f1ea), mip import
  before partial texel writes (549fb9d7be4362e9b82138a674a5305a722ab1a5), native sampler defaults (767a276615a6c1c1d20a7b98aec17dd170d8e3b3),
  declining fast-clear hints (0d80bff4eec2668441ff4ddd075796e3ec674099), GPU timestamps (9eebd8d746621b851189a7b4679489388911e7b7), one dirty record
  per host page (1d69c8ae9cea2e83584314f8044b55c839c95c9e); nxnative: GPU timestamps on the guest clock (b6b3da202abbd9ca73f45057920c7a9fdfcc0eb6),
  depth / stencil clear mid-pass (7058f43497c6eb26eea351ec4e45db3bf0d417ae), Downsample as a Metal resolve (13ff4f51a10ff6c4207a1e909e430ddd9c0644d3),
  a per-game present interval (9745da2061b09fea0ea524c7d5369ad8e1357539), pool finalize dropping texture ID views
  (799df3c5edd00281c40ae8889a10c55cefe93616, the leak class of a593388); nx-port: geometry shader point-to-quad, alpha test, newer NVN object
  sizes (3e6366b…).
- An ad-hoc signature for LiveContainer (Kamisorara 95d6caa903bc19e602493bb5a5568650d39f52cf).

### Plan: all of the wider review, simple to complex (2026-10-19)

Every finding of the reviews above, done in this order. `[ ]` open, `[x] batch N` done in the commit whose subject starts with "Plan batch N", `[-]`
measured and not needed (with the number). Each item gets a unit test and a broken version that must fail it; each batch ends with the gfx tests
(Metal 4 and Metal 3), the host tests, a manosaba and a Tears of the Kingdom smoke run, then one commit. The full notes of each finished item
(sources, measurements, test details) are in `git show 66fa170:DEVELOPMENT.md` and in each batch's commit message.

What is left (2026-10-21): every item the Mac can do alone is done. The open items and the device checks of done ones, by what they need:

- The user at the Mac (a window, sudo, a display): 97 (`powermetrics`), 71b (the covered window), 71d (a display above 120 Hz), the window
  checks of 123 (the 30 fps setting) and 127.
- Play or saves: 72 (a blood moon), 69 (game over), 71c (product60's 30 fps systems), 132 (a save where the wave program draws), and the
  checks of 161 (grass), 163 (hair and cloth), 165 (`DM_NVN_FBX=split` over paved ground or decals), 167 (an overworld save for towns).
- The console: 94a (Link close to the camera, the Great Sky Island grass), 124 (stick diagonals).
- An iPhone or iPad: 70 (the on-screen controls), 71b's Metal 4 screen; the checks of 148, 153, 171 (done on 2026-10-21: 65, 68, 106,
  134, 135, 140, 173, 175, and manosaba's part of 142).
- A controller: 66, 67 (67b, 67c, 67d).
- Tears of the Kingdom running on iOS first (62): 61d, 62c2, 62g, 62h, the checks of 62a, 62b, 62e, 63b (`DM_APP_POOL_ADD_MB`), 112, 113, 142,
  170 (the game's text), 172, 67e (the phone's pulses after a trip to the background).
- The next game: 64's open sub-items.
- Decisions: `DM_FRAME_STEP=vsync` as iOS's default (133: 35 % less CPU on the Mac); `DM_PRESENT_LATENCY=auto` as the default (after 153's
  check).

Tier 1, small and local. Correctness:
- [x] batch 1 · 1 svc 0x34 type 3 (64-bit wait if equal), unknown types refused by 0x34 / 0x35, an arbiter race test (`host/tests/t_arbiter.c`)
- [x] batch 1 · 2 controller face buttons by the printed letter on Nintendo controllers (`host/gamepad_map.h`, `t_padmap`; the device check is 66)
- [x] batch 1 · 3 the BindProgram stage mask at the three scans (`defer_try` Q scan, `fuse_try` steps four and five; `t_defer_chain` mode 6)
- [x] batch 1 · 4 sampler LOD bias and min / max reduction (`gfx/tests/t_samp.m`; macOS / iOS 26, reported once before that)
- [x] batch 1 · 5 GPU time per frame as the union of command-buffer spans (`t_gputime`); `@gpu_ms` is the union from batch 1 on, `@gpu_sum_ms` the
  old sum, so GPU numbers above this plan are sums
- [x] batch 1 · 6 a fault guard around uniform snapshot copies (`t_usnap` section 5; without it SIGBUS)
- [x] batch 1 · 7 overlay directory listings without the 2048-name cap, duplicates found by hash (`t_fs`)
- [x] batch 1 · 8 the silent NVN no-ops red on first call; SetDepthBounds reported when enabled (`t_nvn` 7b)
- [x] batch 1 · 9 precise SleepThread (`host/tests/t_sleep.c`)
- [x] batch 1 · 10 rodata read-only after relocation (`host/rodata_seal.h`, `DM_RODATA_RW=1` leaves it), svc 0x02 with Horizon's checks; read / none
  reported, not applied: Tears of the Kingdom only asks for read-write
- [x] batch 1 · 11 the 8 / 16-bit UI2F / I2F vertex formats (`gfx/tests/t_vfmt.m`); Metal refuses 32-bit integers for a float input (measured), so
  the 32-bit and 10:10:10:2 kinds stay unmapped
- [x] batch 1 · 12 thread dumps list the handles of a WaitSynchronization with each one's state (`t_waitsync`)

Tier 1, performance:
- [x] batch 2 · 13 busy-yield back-off releasing the virtual core, a power line every 300 frames. Off by default (`DM_YIELD_BACKOFF=<µs>`): in
  Tears of the Kingdom's prologue it cost energy and frames; the power line shows manosaba at about 25,000 wakeups a second (see 76)
- [x] batch 2 · 14 targeted wakes (`t_waitsync`); upload waits on a condition variable (`t_upwait`); the audio renderer blocks on buffer releases
- [x] batch 2 · 15 replay lookups: per-thread pool cache, dirty range on the texture and `mprotect` over page runs, residency no-op skip
- [x] batch 2 · 16 a spare texture for re-uploaded CPU textures, dropped after 120 submissions unused (`t_texcheck` 4b)
- [x] batch 2 · 17 reflection summary per program, attachments walked once per pass (`t_zskip`)
- [x] batch 2 · 18 libvpx row multithreading (`DM_VPX_ROW_MT=0` off): the Ganon movie 1.02-1.03 → 0.95 ms a frame, frames identical

Tier 1, iOS and Mac:
- [x] batch 3 · 19 a Metal 4 launch check with a screen (Metal 3 for the iOS 18 build; `DM_TEST_NO_METAL4=1` shows it anywhere)
- [x] batch 3 · 20 game data excluded from backups (RomFS and sysfont outside the bundle; the boot log reads the value back)
- [x] batch 3 · 21 `GCSupportsGameMode` (iOS 18.0-18.5 read only this one; `LSSupportsGameMode` is from 18.6, Apple's reference)
- [x] batch 3 · 22 `env.txt` switches on the device: the bundle's (`DM_ENV_FILE` at build) then Documents' (`host/envfile.h`, `t_envfile`)
- [x] batch 3 · 23 audio notifier thread at USER_INTERACTIVE (`t_audio`)
- [x] batch 3 · 24 compressed files stored, not deflated, in the IPA (`.zs`: 1.66 s deflated into a larger archive, 0.26 s stored)
- [x] batch 3 · 25 iOS 17 code guards (`@available` around the residency set and `hasPerformanceProfile`; iOS 17 is not supported since, see 59)
- [x] batch 3 · 26 quit waits for a save in progress, 15 s on the Mac, 3 s on iOS (`dm_fs_save_wait`, `t_fs`)
- [x] batch 3 · 27 a covered Mac window discards presents (`DM_OCCL=0` off, `DM_TEST_COVER=<s>,<len>` to check). The window check with the display
  on is still to run (71b)

Tier 1, tooling and diagnostics:
- [x] batch 4 · 28 one game at a time on the Mac (`host/game_slot.c`: a flock(2) and a holder file; a run for the player, the .app or
  `DM_SLOT_OWNER=user`, stops an agent's run with SIGTERM, any other run waits; `DM_SLOT=0` off, `DM_SLOT_DIR`; `t_gameslot`)
- [x] batch 4 · 29 a stamp of the rewriter's sources in the product (`# REWRITER` in each fixups header, `tools/rewriter_stamp.py`; the loader says
  when a product differs). The Tears of the Kingdom default product is the 30 fps one kept on purpose since dba469f (its "frame step: main has no
  host page" red line is expected; `product60` carries the stamp)
- [x] batch 4 · 30 the rewriter stops on a system register outside NZCV, FPCR, FPSR and DCZID_EL0 (`tools/sysreg.py`); neither game has one
- [x] batch 4 · 31 the SDK's abort text in the log: the last 8 OutputDebugString lines and AbortImpl's return address printed at a Break
- [x] batch 4 · 32 the last 256 lock / condition variable calls in thread dumps (`DM_KTRACE=0` off); `DM_DEBUG_CV_LATE_WAIT=<µs>` (`t_ktrace`)
- [x] batch 4 · 33 upstream's `t_loader_weak` (`host/tests/t_loader_weak.c`: absent weak imports stay 0, our loader already did)
- [x] batch 4 · 34 `DM_MEMSNAP=frame[,frame...]:prefix` (`host/memsnap.c`) and `tools/memsnap.py` summary / diff / refs
- [x] batch 4 · 35 GitHub Actions (`.github/workflows/ci.yml`, macos-26): the hosts of every game (Metal 4 and Metal 3), the host tests and the tool
  tests
- Found while checking batch 4: `gfx/tests/t_ussort` failed about one run in ten (a block whose four probes find no place is collected again and
  the merge drops the repeats); the test now allows that. The host build has 34 compiler warnings, the same before batch 1

Tier 1, nxnative after 2b31dbd (reviewed through 19d7a70 on 2026-10-19 afternoon; MIT; batch 5, before Tier 2):
- [x] batch 5 · 73 the host races of nxnative 8530042: QueryMemory under the registry lock; a SystemEvent's binding ends with its write handle,
  the signal under one hold of G; audio output backends change under one lock, a second consumer reported red; CloseAudioRenderer joins the
  render thread (`dm_pcmout_close` frees the AudioOut slot)
- [x] batch 5 · 74 the yield back-off start, `DM_YIELD_BACKOFF=<cap µs>[:<start µs>]`, default 2 ms. Still off by default: no energy or frame
  difference in Tears of the Kingdom's prologue; manosaba's guest threads never yield
- [x] batch 5 · 75 the c1 guard by interval arithmetic over the whole index, every stage (`gfx/tools/c1_guard.py`); over chain b-1e41705de87b it
  lists the same 49 shaders as the old one, so nothing changes at run time
- [x] batch 5 · 76 two more power lines: render-target traffic a frame and guest CPU by Switch priority (`t_threadcpu`). manosaba's 25,000
  wakeups a second and full performance-core share come from host threads
- [x] batch 5 · 77 checked: the opening cave's stone blocks and steps beside the torch are lit warm here; not this port's bug

Tier 2, small code, one shader chain rebuild for all four:
- [x] Tier 2 · 36 the BAHS shaders: `engines/bnsh/shader_extract.py` reads every binary entry of a `.sharcb` (after nxnative's `bahs_shaders`):
  1,551 shaders new to the Tears of the Kingdom capture, 178 still capture-only. Not read yet: `Lib/NintendoSDK/TextureShader.bnsh`'s two 0x7d1-byte
  control sections and the Title layout's `bgsh`; compute code areas with the 0x12345679 header are translated from +0 (to skip to +0x100 when a
  game's header carries data)
- [x] Tier 2 · 37 FFMA / FMUL `.FMZ` (after tomodachi-native a0aa6c2). Neither game uses it
- [x] Tier 2 · 38 SHF wraps or clamps to the width, a shift of 0 no longer ORs both words; I2I `.SAT` in the source's signedness
  (`gfx/tests/t_xlate_ops.py`, LASRecomp d4c2ff6's vectors)
- [x] Tier 2 · 39 the alpha test, as a game profile option (`alpha_test` in game.json; after lordran-native a63dd76): fragment shaders end with
  the comparison under function constants (`gfx/tests/t_alpha.m`). Off for both games, which never call ColorStateSetAlphaTest
- Chain 1e41705de87b → bab8d4746968 for 36-39 (lock: the patched translator). Both games build every shader; the c1 list is the same 49

Tier 3, medium:
- [x] batch 6 · 40 host calls and svcs on a per-thread host stack (`host/hstack.S`, `hstack.c`; after MacNativeGames' hle_stack, MIT): imports,
  takeovers, NVN functions and svc gates run on the guest thread's own 1 MiB pthread stack; a frame record links back to the guest caller and gives
  GetThreadContext3 the guest's registers. Thread dumps show each thread's least free guest stack (a `[stack]` line below 1 KiB). `DM_HSTACK=0` off
- [x] batch 7 · 41 occlusion counts across submissions (after lordran-native 3ef6915, MIT): the open count's slots folded into a CPU subtotal before
  a visibility ring buffer is reused (`gfx/tests/t_visibility.m`); the overflow buffer of more than 8,192 counting passes is retired now
- [x] batch 8 · 42 draws that sample what their own pass draws (`DM_NVN_COUNTS=1` counts them): the replay copies only where NVN defines the read
  (a draw after a texture barrier that follows draws of its pass: `fbx_prepare` / `fbx_sub`); copying for every such draw put prologue shots outside
  the console footage's brightness ranges. `DM_NVN_FBX=all` copies for every such draw, `=0` never (`t_fbx`)
- [-] batch 8 · 43 checked: no two timestamp reports of one submission are subtracted (the game reads only the type 0 report, via
  `nvnDeviceGetTimestampInNanoseconds`, main+0x76920; a load watchpoint, `DM_NVN_COUNTS_WATCH=<n>` `DM_WATCHPOINT_LOAD=1`, saw no other read),
  so the command buffer is not split at reports
- [-] batch 8 · 44 counted: no ClearTexture in either game, 4 render passes a frame end without a draw in the prologue: keeping clears would save at
  most those
- [-] batch 8 · 45 counted: about 31,000-34,000 system counter reads a second in Tears of the Kingdom, about 1.5 ms of CPU a second in all: not
  worth an inline read
- [-] batch 9 · 46 counted: no pass of either game draws only a top-left part (also with timestamps made late, `DM_NVN_TS_LATE_US=<µs>`, new), so the
  render area stays the attachments' size (`gfx/tests/t_extent.m`)
- [x] batch 10 · 47 a live command channel, `DM_CMD=<file>` (`host/cmdchan.c`, after MacNativeGames' `TOTK_INPUT_FILE`): `key` / `down` / `up`,
  `wait`, `sleep`, `shot`, `passdump`, `threads`, `memsnap`, `hlecount`, `quit`; a line runs once its newline is written (`t_cmdchan`)
- [x] batch 11 · 48 sead heaps and list watch (`host/sead.c`, after MacNativeGames' TOTK_CRASH_HEAPFIND / HEAPWALK / LISTCHECK):
  `DM_CRASH_HEAP=<reg>,...`, the channel's `heap <addr>` and `heaps`, `DM_LIST_WATCH=[heap+]<addr>,...`; a use list counts as broken only in a heap class
  that has whole ones
- [x] batch 12 · 49 per-pass GPU time, `DM_NVN_PASSGPU=<tsv>` (after upstream 44a6770, MIT): Metal 4 counter heaps around each encoder and dispatch,
  Metal 3 stage-boundary samples, a ring of 8 invalidated before reuse; every 300 frames the five costliest kinds of pass (`gfx/tests/t_passgpu.m`)
- [x] batch 13 · 50 soak and fixed-scene measurement, `tools/soak.py` (after MacNativeGames' `mac_v143_soak.py` / `mac_v143_smoke.py`): `run` (memory
  slope after the warm-up, frame and GPU time early against late, late red lines) and `window` (N runs to a frame, `--clean`). Run-to-run GPU noise
  in the prologue is about 3 %
- [x] batch 13 · 51 lifecycle and swipe-away tests (upstream `ios/tools/lifecycle.sh`, `logred.sh`): `DM_TEST_BG=<s>,<len>` on the Mac too,
  `tools/lifecycle_check.py`, `ios/run.sh lifecycle`; the watchdog skips while the clock is frozen for the background; `host/tests/t_fs_kill.c`
  (saves killed at random stay whole). The home button and the app switcher need a phone (item 70)
- [x] batch 14 · 52 scene render size on 1.4.3, `DM_RENDER_RES=<w>x<h>` with the rules product (after MacNativeGames PR #19, MIT): four words at
  0x585e48-0x585e60 read the size from host page +16 (x17, not the PR's x3); `games/totk/frame_step.c` writes the game's own parameter there unless
  `DM_RENDER_RES` is set. `games/totk/check_render_size.py`; product60 rebuilt (`product60.prev` kept)
- [x] batch 15 · 53 frame pacing between 30 and 60 fps (after MacNativeGames' TOTK_UNCAPPED, MIT): a game whose step follows the measured frame time
  (`dm_game_realtime_step`) is paced by our clock at up to 120 fps (`gfx/nvn/nvn_pace.h` `nvn_uncapped_hz`; `DM_UNCAPPED=0` off,
  `DM_UNCAPPED_HZ=30..120` or `display`). `DM_RENDER_RES` above 1920x1080 is refused (crash at main+0x1c2f20; see 95). The 30 fps constants need
  play: 71c; a window run is part of 71b
- [x] batch 16 · 54 user mod folders, `DM_MODS=<dir>` (`host/mods.c`, after SkyRecomp's `runtime/mods.c`, MIT): each entry's RomFS is a layer in
  front of the game's (`<entry>/romfs`, `contents/<program id>/romfs`, `atmosphere/contents/<program id>/romfs`, loose files), in name order, `.` or
  `-` off; on iOS `Documents/mods`
- [-] batch 17 · 55 measured: the AudioUnit open takes 22-26 ms in the games here (tomodachi-native d7f52e1 moved it after a 40 s stall), so it stays
  where it is; the time and thread are logged (red above 1 s) for a device run (70). `DM_AUDIO_MUTE=1` feeds the sound card silence
- [-] batch 18 · 56 measured (justr4mzi's mush8 fork aafef22, ideas only): cold uploaded textures (`DM_NVN_COUNTS=1` census) are about 3 % of Tears of
  the Kingdom's Metal memory: not now; with the game on a device (62) the memory-low lines say whether to
- [x] batch 19 · 57 code patches chosen at launch, one product for 30 and 60 fps (after tomodachi-native 0428c80, MIT): rules.py `OPTIONS`, listed in
  `<module>.options.tsv`, applied by the Mac loader to the `DM_OPTIONS` names or the product's `options.default` (`host/options.c`, `dm_option_on`).
  Tears of the Kingdom: `fps60`, `realtime`, `scene`; `DM_OPTIONS=none` runs the game's code as shipped. The default 30 fps product
  (`work/build/totk/product`) is left as it is. iOS needs the changed pages embedded (tomodachi-native afd88e9), item 62
- [x] batch 20 · 58 on-screen controls with a drag-to-look camera (BotWRecomp `runtime/src/host/touch.cpp` at 2b84b82, MIT): `host/touchpad.c` for
  game.json `touch` "pad" (`DM_TOUCH=pad` / `screen`), iOS `DmPadView`, the eye's choice in `dm.touch_hidden`; `DM_TEST_TOUCHPAD=check:<s>` runs
  BotWRecomp's checks (17 of 17). The feel needs Tears of the Kingdom on iOS (62), or manosaba with `DM_TOUCH=pad` on a device (70)
- [-] 59 not done: iOS 17 is not supported (the user's decision, 2026-10-20); the apps stay at iOS 26 (Metal 4) and iOS 18 (Metal 3)
- [-] batch 21 · 60 measured: no game calls an indirect draw or dispatch yet (396 logs); implemented when a game calls them (item 64). `t_nvn` 7b
  holds them all to the stub

Tier 3b, from the wider review of 2026-10-20 (before Tier 4). Correctness, small:
- [x] batch 22 · 78 the pipeline warm-up from the player's own list without a seed list (`gfx/nvn/nvn_warm_list.h`); rows of an earlier
  translation skipped and counted. No title list: the warm-up is done long before the first frame that needs it
- [x] batch 22 · 79 the prediction table (after MacNativeGames PR #29, MIT): an untaken guess freed two submissions after its kick, each scan capped
  (8,192 commands, 32 keys, 2 ms), guesses only from loaded programs (`prog_peek`); a `pipeline prediction` power line. The cold case is a device's
- [x] batch 22 · 80 the present's command buffer committed with the residency set committed (MacNativeGames PR #30, open; `present_now`,
  `t_present_res`)
- [x] batch 22 · 81 a sync's fence stored before its flag, release / acquire (`nvn_obj.h` `sync_fence_store` / `sync_fence_load`; GirlNet f2faa5f)
- [x] batch 23 · 82 NVN texture layout per the SDK (`nvn_tex.c`, after nxnative PR #11, digitalhivehub bacf2cb, MIT; census `DM_NVN_TEXCENSUS=1`):
  cube array depth counts faces, 0x40 alone linear, MINIMAL_LAYOUT, the packaged block depth (`pkg_layout`); `t_tex_auto` with PR #11's vectors and
  totk-mobile's sizes (Apache-2.0, numbers only)
- [x] batch 23 · 83 the format's own swizzle in the texture views (`tex_by_id`; totk-mobile d3dd76c, Apache-2.0, reimplemented); red in
  `SetRenderTargets` if an R / B-swapped format is ever a render target (`t_fmt_swz`)
- [x] batch 23 · 84 socket errno from nn::socket's own values (the SDK's Errno: ENetDown 100, EAfNoSupport 97; `t_net`)
- [x] batch 23 · 85 sticks inside the circle of radius AnalogStickMax for every source (`hle_hid.c` `stick_circle`)
- [x] batch 23 · 86 the Mac app's first iCloud listing driven by a local run loop on the main thread (`cloud.m` `listing_query`, upstream 1ceaa6b, MIT)
- [x] batch 23 · 87 quit waits while the game is inside its exit-request section (`hle_oe.c` Enter / Leave, `hle_lifecycle.c` `quit_wait`;
  GirlNet 291a9cc)
- [-] batch 23 · 88 measured: SleepThread under 20 µs is rare (677 a minute in Tears of the Kingdom, none in manosaba). manosaba sleeps 20 µs-1 ms
  about 24,000 times a second (98 / later)
- [-] batch 23 · 89 measured: neither game reads the six-axis state (Tears of the Kingdom asks for its handles, never the state); a resting state
  belongs with the gyro (67)
- [x] batch 23 · 90 red lines: a second texture or sampler pool, packaged layout byte 1 not 0, a dual-source blend factor, an unimplemented import
  (`UNIMPL`, with its caller: `DM_UNIMPL_GUEST_RA`) and an unknown svc. None fires in either game
Tier 3b, Tears of the Kingdom:
- [x] batch 24 · 91 shrines at 60 fps: nxnative PR #11's five words (digitalhivehub 97172c1 / a6b7191, MIT) joined `fps60` (now 12 words): a shrine's
  forced fixed interval not taken, the lowering re-armed from 1 (`check_frame_rate.py`; `product60.prev91` kept). Not run in a shrine here: no save
  inside one
- [x] batch 24 · 92 the BNSH files inside the game's modules (`shader_extract.py --modules <product>`; MacNativeGames 2f9e707, MIT): main has 40
  shaders, capture-only shaders 178 → 138
- [-] batch 24 · 93 measured: the half-step selector (sys+0x1da bit 0) stayed 0 through 191 interval changes and the cliff save, so the step is never
  halved (T5eng's note does not hold here)
- [x] batch 24 · 94 GPU-rendered textures in CPU-cached pools counted (`DM_NVN_TEXCENSUS`); the experiment `DM_NVN_WB_IDLE=1` (after totk-mobile's
  write-back, Apache-2.0, reimplemented) writes back a texture idle for 30 submissions and hands it back to the CPU when the CPU writes it. It also
  makes Link close to the camera solid (by default a translucent ghost); which is the console's picture is open: off by default, 94a
- [ ] 94a the write-back's other half per region: what the RG8 256x256 texture the CPU rewrites every few submissions is (zeros each time; its
  hand-back changes Link's near-camera fade), console footage of Link close to the camera and of the Great Sky Island grass, then
  `DM_NVN_WB_IDLE` on by default or a narrower rule (hand back only what was written back, or only whole-texture copies)
- [x] batch 24 · 95 `DM_RENDER_RES` above the output size: `games/totk/heapdef.py <RomFS> <overlay> <w>x<h>` writes a HeapDef overlay with the
  graphics heaps grown by the scene's pixels (MacNativeGames' make_output_res_overlay.py, MIT; 1920x1080 matches totk-mobile's numbers) and a note of
  the size, up to which `frame_step.c` takes `DM_RENDER_RES` (3840x2160 at most)
- [x] batch 24 · 96 anisotropic filtering as a switch, off by default (`DM_NVN_ANISO=<2..16>`, `nvn_aniso.h`; tvc97's TOTK_ANISO): raised only on
  mipmapped samplers that minify linearly, without a depth compare or a reduction

Tier 3b, performance and power (measure first):
- [ ] 97 a present with a deadline against the GPU clock drop (`powermetrics`, the 30 fps product). Needs the user: the presents only happen with a
  window (headless runs have no drawable) and `powermetrics` asks for the password; mode 0 (`DM_PRESENT_LINK=0`, the rescue path) is the one
  without a deadline, the display link's default presents with one
- [x] batch 25 · 98 the audio renderer paced by its own clock (`host/audren_pace.h`, after SkyRecomp 0620de9, MIT): three silent frames queued
  first, a stall restarting the deadline; catch-up frames 3,731 → 10 in 75 s; a gap histogram every 2,000 frames
- [-] batch 25 · 99 measured: depth textures with or without lossless compression look the same (`DM_NVN_DEPTH_PLAIN=1`, LASRecomp PR #204, an A/B
  switch, off); the default stays compressed

Tier 3b, one shader chain rebuild for all three:
- [x] batch 26 · 100 the Ultrahand mask shader's integer output: `games/<id>/fout.tsv` read by `dmsh_build.py` (nxnative PR #11's shader_fout.tsv,
  MIT); its MSL declares `uint4` at colour 0
- [x] batch 26 · 101 the NoContraction helpers inlined (`gfx/tools/msl_helpers.py`): `spvFAdd` always-inline with FP contraction off in its body
  (totk-mobile's idea only), bit for bit the same (`t_fpcontract`); no measurable GPU gain on the M5. Chain bab8d4746968 → 4236b355e989 (with 100)
- [-] batch 26 · 102 not needed: 90's red line for dual-source factors fired in neither game

Tier 3b, iOS and tooling:
- [x] batch 26 · 103 metallibs copied into memory on iOS (`lib_from_file`; `DM_NVN_LIB_COPY=1` elsewhere; MacNativeGames e099353, MIT): no vnode
  held per library
- [x] batch 26 · 104 a pipeline retried after a compiler-service interruption (`nvn_retry.h`, after MacNativeGames 39048ca, MIT: up to 4 attempts
  for "XPC_ERROR_CONNECTION_INTERRUPTED"); not recorded as failed
- [x] batch 26 · 105 the command channel follows a replaced or truncated file (`cmdchan.c` `cmd_reopen`; T5eng's TOTK_INPUT_REOPEN)
- [x] batch 27 · 106 run diagnostics (lordran-native f5834c5 / 9567c94, MIT; the code is ours): UTC millisecond log stamps (`host/logstamp.h`,
  `DM_LOG_TIME=1` on the Mac), `Documents/boot.running` → red line and `boot.interrupted.txt` (`host/rundiag.h`), Export Diagnostics
  (`ios/Sources/diag_export.m`), the fps / GPU line (`gfx/nvn/nvn_perfline.h`; Settings: Show frame rate, `DM_PERF_LINE=1`).
  Checked on the iPhone 17 Pro (2026-10-21): a foreground kill's red line and `boot.interrupted.txt`, Export Diagnostics, Show frame rate
- [x] batch 27 · 107 the frames-stopped dump (justr4mzi's mush8 fork 979317e, GPL: the idea only): `DM_FREEZE_DUMP` seconds (6) without a present give
  one thread dump per freeze; thread dumps print after resuming (`hle_thread.c` `thr_bt`)
- [x] batch 27 · 108 the Metal 4 iOS build compiles for `apple-a14` (was a18; lordran 78dd33d): one IPA for every Metal 4 device (`t_ios_cpu.py`)
- [x] batch 27 · 109 touch camera sensitivity (0.25-3×, before the turn speed saturates) and X / Y inversion (lordran 42ad4cf, MIT: the idea;
  `touchpad.c` `tp_look_tuning`): Settings → Touch Camera, or `DM_TOUCH_LOOK=<s>[,x][,y]`
- [-] batch 27 · 110 not needed: `devicectl device copy to` already skips unchanged files and resumes (devicectl 642.16); `ios/run.sh push-data`
  says so when a copy fails
- [x] batch 27 · 111 `make -C host/tests run-ubsan` (the parsers of outside input, UBSan fatal; `t_ubsan_on`) and `tools/audit_public.py`
  (bayonetta3-mobile's idea, MIT: private paths, game data, keys and tokens, binary content; `--history`), both in CI; LDC.64 with the address
  register among its destinations (LASRecomp a179452's cases, MIT) in `t_xlate_ops.py`

Tier 3c, from the deep review of 2026-10-20 (after 3b, before Tier 4). Correctness, small:
- [x] batch 28 · 115 pipeline keys carry zeros for the blend state of targets whose blending is off (`nvn_pso_skey.h` `pso_key_blend_canon`, in
  `pso_key_fill` and when the warm-up list is read, folding rows that become one pipeline: Tears of the Kingdom's 729 keys → 397)
- [x] batch 28 · 116 SyncWait on a sync never fenced returns FAILED, as the SDK's NVNsyncWaitResult says (was TIMEOUT_EXPIRED), counted and logged;
  QueueWaitSync left as it is (the SDK gives it no failure)
- [x] batch 28 · 117 the Metal 3 buffer table grows and is indexed (`nvn_m3buf.h`, on `nvn_pool_find.h`; acnh-mobile c2af194's case, MIT): Tears of
  the Kingdom passes 513 buffers in the prologue, and the old 512 cap left s1800 black on `dmhost_m3` (the iOS 18 app)
- [x] batch 28 · 118 a write released through one range stamps every range on that host page (`nvn_dirty.c` `stamp_shared`, also in the fault handler:
  atomics only): two pools on one page each missed the other's writes; acnh-mobile's `t_dirty_shared.c` (MIT) as is
- [-] batch 28 · 119 not needed: with `DM_NVN_COUNTS=1` no draw sampled a texture that shares an attachment's storage under another serial (0 in the
  prologue, the cliff save and manosaba); the count stays in the counts mode (`nvn_obj.h` `tex_same_storage`)
- [x] batch 28 · 120 cull FRONT_AND_BACK drops polygons only (`nvn_obj.h` `nvn_prim_has_face`; points and lines were dropped too); neither game draws
  a point or a line under it (logged once if one does)
- [x] batch 28 · 121 a pass without attachments takes the viewport's extent when no scissor is set (`nvn_attless_size`; it was 1x1,
  bayonetta3-mobile's case), with a scissor its extent as before; samples stay 1 (none above 1 in either game, logged if one comes)
- [x] batch 28 · 122 the silent audio sink starts again from now when more than 100 ms behind (`audren_pace.h` `sink_step`; acnh-mobile, nxnative
  3bd1e9e): after a 2 s freeze 9 pulls, not 409
- [x] batch 28 · 123 the display-link step from the rate the link runs at (`nvn_pace.h` `nvn_link_step`): on the power-saving 30 Hz link interval 2
  takes one tick (30 fps; was 15). The window run on the device or the Mac with the 30 fps setting is for the user
- [x] batch 28 · 124 the sticks set NpadButton StickL / StickR Left, Up, Right, Down (bits 16-23, the SDK's names; `dm_input.h` `dm_stick_dirs`) at
  half the axis range or more (blade2-mobile 34755e9, MIT); whether the console also sets one on a diagonal outside the dead zone circle with both
  axes under half is not documented and not checked on a console
- [x] batch 29 · 125 crash and diagnostics fixes: the backtrace reads each frame with vm_read_overwrite (`crash.c` `dm_backtrace`; totk-mobile does
  so); the svc gate hands `svc_dispatch` the svc's address (`hle.c` `gate_dispatch`: red SVC lines and Break said "?"), Break names the module and the
  caller apart; each `RED_ONCE` tag one message (`tools/tests/t_red_once.py`); the shader capture writes `.data` then `.ctl`, each through a rename
  (`hle_nvn.c` `shdump_to`)
- [-] batch 29 · 126 not needed, and wrong for Tears of the Kingdom: it keeps its own fields right after a view (7250b79: a 12-byte `view_t` overwrote
  them), so its view is not Bayonetta 3's 16 bytes; every view both games hand over was made by our SetDefaults, none sets a depth / stencil mode
- [x] batch 29 · 127 the Mac window: a press on the title bar goes to AppKit with its drag (moving the window by its title bar did nothing), the black
  bars stay no touch (`host/mouse_route.h`); App Nap held off while a window runs (`beginActivityWithOptions`; blade2-mobile). The window checks are
  for the user
- [x] batch 30 · 128 the deep review's tests (their vectors as data, the code ours): `t_writetexels` (totk-mobile's regions), `t_vfetch` (dr3-mobile's
  UNORM8 and NaN payloads; `vfmt_for_float_input`), the svc vectors in `t_guest_mem` section 11 and `t_coremask` (with 143; the core table stays
  Atmosphère's where totk-mobile expects otherwise), `t_time` (acnh-mobile's zones), `t_relr` (144), the pool lookup (`nvn_pool_find.h`: a range
  wrapping past the end of the address space now in no pool), a NaN alpha reference in `t_alpha` (bayonetta3-mobile's)
- [x] batch 31 · 129 rewriter: a jump table whose add has the loaded entry first (`ldrsw x18, [xB, xI, lsl #2]; add x18, x18, xB; br x18`) found with
  the operands swapped; decoding goes past a word Capstone cannot decode (`decode_words`); equiv takes an exclusive store's success path and leaves
  out only 8-byte writes to x18's and the thread pointer's slot; `tools/tests/t_rewriter_synth.py` runs the rewriter, structck and equiv on a
  synthetic module in CI (smo's selftest words, bayonetta3-mobile's sequences) and on damaged products; CI installs lz4 and unicorn

Tier 3c, Tears of the Kingdom:
- [x] batch 32 · 130 pre-rendered movies held at 30 fps: the game extends the user inactivity time (`nn::oe::SetUserInactivityDetectionTimeExtended`)
  for every cutscene, so the oe HLE hands the call and the guest's return address (`DM_GUEST_RA`) to a game hook `dm_game_inactivity_fps`, and
  `games/totk/movie_fps.c` holds only the call returning to main+0x2b36104 (a pre-rendered movie, FPSLocker's flag) through `nvn_hold_interval`.
  `DM_INACTIVITY_FPS` replaces the held rate (0 = no hold, for 72); `DM_TEST_FPS_HOLD=<from>:<to>:<fps>` holds without a movie
- [x] batch 33 · 131 the game's GPU time is measured again: Tears of the Kingdom times its frame as the difference of two TIMESTAMP reports of one
  submission (main 0xa1a50), and every report took its completion time (it read 0-0.06 ms); now each report takes its place inside the submission by
  the share of the work before it (`gfx/nvn/nvn_ts.h`): the game reads p50 6.33 ms, the controller's view unchanged. `DM_NVN_GPU_TS=completion`
  restores the old timing, `DM_NVN_TS_SCALE=<x>` spreads the reports; `DM_PROBE_FP=1` adds s0..s7 to probes. The dynamic resolution itself never runs
  here (131a)
- [-] batch 61 · 131a what sets the dynamic resolution's enable bit (+0x1c bit 0 of one object per game, set up at 0x8c4eb0 as the constant 0x804;
  T5eng saw TotK's scene size change on an iPhone Air): nothing does; with the GPU reported 30 ms late the view size routine 0xd5c24 ran 789 times in
  60 s without the object, its paths never reached in 190 s, and no store in main sets the bit. T5eng's line is an inference. To look at again if a
  device log shows the scene's render size changing
- [ ] 132 the wave program's wide lines (2026-10-20: not reached: no `SetLineWidth` other than 1 in the prologue, on the cliff save or in the five
  other saves of `save_play` in 75 s each, all early game; the wave program's shaders 0cd9edc0a623c237 / e04d541253557805 are not in our capture;
  totk-mobile expands them by editing those shaders' source, so it needs both: a save where the game draws them, from the user)
- [x] batch 35 · 133 a frame step in whole vsyncs decided by the host, as a switch (`DM_FRAME_STEP=vsync`; `gfx/nvn/nvn_fstep.h`, nxnative's design
  and numbers written anew, MIT): each frame stays k vsyncs and the game steps k (paying back lost time, at most 15); 130's hold is its kmin. The 60
  fps product's prologue: frame p50 16.7 ms, 35 % less CPU than the mean step with uncapped pacing (the default, unchanged). Not needed: FPSLocker's
  three swapchain textures. Not measured: the day clock (main 0x68c15c), which needs a later save (71c)

Tier 3c, iOS:
- [x] batch 36 · 134 iOS audio: the session asks for 48 kHz and a 5 ms I/O buffer and logs what it got (nxnative 18c6143); underruns and the zeros
  filled counted, logged by a watchdog (after nxnative b301431, MIT) that moves a unit silent for 200 ms, or one whose Start the device refuses, to
  the silent sink and retries it (1, 2, 4, then every 8 s); the media services reset makes every unit again (`dm_audio_reset`). On the iPhone 17
  Pro (2026-10-21): 48000 Hz and 5.00 ms as asked, output latency 12.4 ms
- [x] batch 37 · 135 iOS: the frame rate setting held at 30 in Low Power Mode or from thermal state Serious up (`nvn_fps_policy` in
  `gfx/nvn/nvn_pace.h`, after super-mario-odyssey-ios's frame pacer, MIT), the reason logged; `ENABLE_DEBUG_DYLIB: NO` in `ios/project.yml`. On the
  iPhone 17 Pro (2026-10-21): Low Power Mode on and off logged "frame rate 30 fps (Low Power Mode)", then "frame rate 60 fps (the setting)"
- [x] batch 39 · 136 no option pages on iOS (iOS maps the signed executable's code): `ios/tools/gen_embed.py` builds the code options (DM_OPTIONS at
  build time, else the product's options.default) into the embedded image, each checked whole, and `options.c` `dm_options_embedded` reads them back,
  so `dm_option_on` answers as on the Mac; run-time choices stay data (the frame rate by the host's pacing, realtime and scene through the host page).
  manosaba has no options

Tier 3c, larger:
- [x] batch 40 · 137 pairs noted at bind time: `pso_get` writes each vertex / pixel pair to `<DM_SHDUMP>/pairs.tsv` (`nvn_mtl.m` `pair_note`), so
  BindProgram's pairs reach the next shader build's padding. No "not written by vertex shader" failure in 208 run logs since chain bab8d4746968, so
  the union-interface library moved to 62g
- [x] batch 41 · 138 render targets of virtual pools in a heap, a switch (`DM_NVN_RT_HEAP=1`; the placement rule is totk-mobile's
  `nvn_texture_alias.inc`, Apache-2.0, written anew in `gfx/nvn/nvn_rtheap.h`): a virtual, CPU-inaccessible pool gets a placement heap of 1.25 × its
  size, a target goes at its guest offset × 1.25 when it fits; Metal 4 only, with the TBDR rewrites off. The cliff save: GPU allocated about 2,120 →
  1,960 MB, timing unchanged. Off at first (Metal's layout is not Maxwell's); 166 made it the default on the Mac, 62h is iOS
- [x] batch 38 · 139 code caves in the rewriter (`CAVES` in a game's rules.py; totk-mobile's routines appended to the patch area, written anew): one
  instruction becomes `b <routine>`, the routine is emitted into the patch area from words and forms (`('orig',)`, `('adrp_host', d)`, `('b' | 'bl',
  target)`) and branches back; words that cannot run elsewhere (PC-relative, x18, svc, system registers, register branches) are refused;
  `<module>.caves.tsv` lists them, structck ⑬ checks them, fnfuzz leaves cave functions out
- [x] batch 42 · 140 FIFO frame consumption for a game that steps once per frame (game.json `present_consume` `"fifo"`: manosaba;
  `DM_PRESENT_CONSUME=newest|fifo` overrides; after acnh-mobile c2af194, MIT, written anew): the display link shows the oldest queued frame
  (`nvn_mtl.m` `link_take`), each at least its present period from the tick that showed it (`nvn_pace.h` `nvn_fifo_due`); uncapped pacing keeps the
  newest. On the iPhone 17 Pro (2026-10-21): the line is logged and steady play drops 0 stale frames; each return from the background drops 1-3
  (5 in 4 returns)
- [x] batch 43 · 141 IL2CPP class names in crash reports and memory snapshots (`host/il2cpp_obj.h`; armyja's nxnative-aa-port reads the same fields,
  MIT, written anew): a Unity game's crash report (`game.c` `dm_engine_il2cpp`) names the object each of x0-x28 points to; `tools/memsnap.py` `class`,
  `refs` and `diff` (manosaba from frame 600 to 2400 grew 29,776 System.String, 12,625 Naninovel.PlaybackSpot, 8,930 Naninovel.RawValuePart, 7,924
  Naninovel.UniTaskCompletionSource)

Tier 3c, smaller items listed at the user's request (2026-10-20: every finding of the three reviews as a to-do):
- [x] batch 44 · 142 the swapchain acquire against the GPU work of the frame last drawn into its image (after blade2-mobile 43e760e, MIT): the present
  records the newest submission's fence (`win_t` `rfence`) and the timing summary counts acquires before that work finished; `DM_NVN_ACQ_GPU=wait`
  waits. 0 on the Mac in both games, so the wait stays off. On the iPhone 17 Pro: 0 in manosaba (2026-10-21); Tears of the Kingdom's waits for 62
- [x] batch 30 · 143 svc details as Horizon (switchbrew's results; totk-mobile's vectors): SetHeapSize (2 MiB multiples up to 4 GiB, OutOfMemory
  0xD001, no shrink over a MapMemory source, fresh pages), MapPhysicalMemory's InvalidAddress / InvalidSize / InvalidMemoryRegion, GetInfo
  PriorityMask, RandomEntropy, SystemResourceSize (`game.json` "npdm") and Total / Used; `tools/new_game.py` reads the NPDM fields
- [x] batch 30 · 144 the DT_RELR table checked whole before any slot is touched (`loader.c` `relr_each`): a bad bitmap, slot or table refuses the
  module with a red line; both games' tables pass unchanged
- [x] batch 45 · 145 small diagnostics (bayonetta3-mobile's copy of blade2-mobile, MIT, written anew): the frame log (`DM_NVN_TSV`) stops at
  `DM_NVN_TSV_MAX_MB` (32 by default); `DM_PRESENT_LUMA=<file>` compares each present's guest image with its drawable, red once under 35 % (needs a
  window; `nvn_mtl.m` `pl_*`); `DM_HID_RECEIPT_LOG` deduplicates each (controller, style) on its own
- [x] batch 46 · 146 NCZ input in the game extraction (`tools/game_extract/Ncz.cs`; the layout as totk-mobile's `extract_romfs.py` reads it,
  Apache-2.0, written anew): an .nsz's NCZ is restored to its NCA and must hash to its file name; NCZBLOCK refused. ZstdSharp.Port 0.8.8 (MIT), pinned
  by content hash. Not run on a real .nsz here
- [x] batch 47 · 147 tests that run the game's own SDK code against the host (`host/sdktest.c`, `DM_SDK_TEST=time,font` with any run; after
  totk-mobile's `tests/test_time_sdk.c` and `test_font_sdk.c`, Apache-2.0, written anew; they need the game, not CI): at the 60th present a thread
  calls the SDK by name (`loader.c` `dm_sym_bound`), logs "[sdk-test]" lines and ends the run (exit 0 / 1); 8 of 8 in both games. Vibration waits for
  67
- [x] batch 48 · 148 an in-game settings panel and a layout editor for the on-screen controls (BotWRecomp's settings button and lordran-native's
  control editor, MIT, the ideas): `touchpad.c` moves and sizes controls (`dm.touch_layout`), opacity (`dm.touch_opacity`), the editor `tp_edit`; on
  iOS (`front_ios.m`) the panel opens from a menu button or a three-finger tap. On a device (the user): the panel from the button and from three
  fingers, a layout edited, saved and kept across a launch

Tier 3d, from the evening sync of 2026-10-20 (after 3c, before Tier 4). Correctness, small:
- [x] batch 49 · 155 the present viewport rounded before it is centred (`nvn_mtl.m` `present_vp`, now also the Metal 3 path's; Touchable 5cab3ac,
  MIT): it put the picture at -1 on 2868x1320 and 3456x2234 (the Mac's full screen), also 2796x1290 and 2556x1179
- [x] batch 49 · 156 dropped and slow frames judged against each frame's hold (`nvn_pace.h` `nvn_hold_ms` / `nvn_expect_ms` / `nvn_dropped` /
  `nvn_slow`, after nxnative e7a84c7, MIT); the frame interval line counts frames over 1.1 × their hold (it was > 16.7 ms: 299 of 300 at 30 fps);
  `[slowframe]` the same
- [x] batch 49 · 157 the oe notification event set and cleared under the queue lock (`hle_oe.c`, after nxnative ee51071, MIT)
- [x] batch 49 · 158 the user clock context in Horizon's layout (`hle_time.c`, after nxnative a35e964, MIT): the offset in seconds and the steady time
  point it was made at, made again when the clocks part by more than 2 s, once under `DM_FIXED_TIME`
- [x] batch 49 · 159 the Metal 4 feedback queue drains its autorelease pool per work item (`nvn_mtl.m`, lordran-native 6145d1d, MIT)
- [x] batch 49 · 160 GetInfo of an unknown type answers InvalidEnumValue 0xF001 (Atmosphère's GetInfo default; Touchable the same; ours succeeded with
  0), with a red line once per type and its caller

Tier 3d, Tears of the Kingdom:
- [x] batch 50 · 161 sampler border colours Metal lacks (after MacNativeGames b6231e6 / 874a32d, #49, MIT, written anew): the cliff save's grass
  interaction map uses (0.5, 0.5, 0.5, 0.5) and was drawn as transparent black; such a sampler clamps to its edge and a stage sampling through it gets
  a variant (`border_variant`, `dm_border_sample`) compiled from the `.metal` beside the library; an iOS app has none, so there it reads the edge
  colour (a red line once; for 62 those sources could ship). To check in a game (the user): grass that stands up away from Link and can be cut, in a
  grassy area of Tears of the Kingdom
- [x] batch 51 · 162 one pixel shader bound to float and to integer targets: every key in `fout.tsv` keeps its float library and gets an
  integer-output variant `<key>_i_b` (`dmsh_build.py`); `pso_desc` takes it for an integer colour format in a slot the shader writes (`fout_fn`).
  `games/totk/fout.tsv` lists the 23 keys of ours in MacNativeGames' shared list (MIT, keys as data). Chain ca5eb9623812
- [x] batch 52 · 163 hair and cloth at 60 / 120 fps (MacNativeGames PR #79, merged, MIT; its sites, routine words and Unicorn table as data, the code
  ours): the actor animation update builds its cloth and hair clock every frame as {seconds = factor / 30, ratio = factor}; `games/totk/rules.py`
  CAVES at the inlined copies 0x246eac, 0x345904 and the calls 0x24944c, 0x28bc98 to 0x2a0e40 scale it by host page +0 and +8 (`frame_step.c` writes
  +8), scratch x8 and s4. Caves can sit behind an option (`patch_nso.py`: a fifth item; the branch is an options.tsv row; caves.tsv has an option
  column), here realtime. `games/totk/check_cloth_clock.py <product> <main NSO>`. To check in a game (the user): hair and cloth in wind at 60 and 120
  fps against 30
- [x] batch 52 · 164 a mod that carries one of our RomFS overlay files (MacNativeGames #73, open): the mod's copy won without a word; now (`mods.c`
  `dm_mods_pin_overlay`, pinned through `hle_fs.c` `dm_rom_pin`) the SHA-256 of the mod's copy decides: the game's own file → the overlay's;
  `<mods>/-merged/<that SHA-256>/<path>` → that merge; otherwise the mod's copy with a red line naming the merge command (`romfs_overlay.py <RomFS>
  <mods> --merge <the mod's file>`). On iOS the merge is made on a Mac and copied to `Documents/mods/-merged`
- [x] batch 53 · 165 decals that read their own G-buffer pass, a switch (`DM_NVN_FBX=split`; MacNativeGames PR #67, open, MIT, written anew in
  `fbx_prepare`): no copies; the pass is stored and reopened before a draw that samples an attachment holding pixels newer than memory, a run of such
  draws sharing one split (the cliff save: 3 a frame, GPU time unchanged). Default off: the ghosting it fixes shows in motion where there are ground
  decals. To check (the user): a turning camera over paved ground or decals (Hyrule Castle courtyard in the PR) with and without the switch
- [x] batch 54 · 166 a three-hour soak of the 60 fps product before 151 (MacNativeGames #57, #72, #74; upstream #90; `tools/soak.py run --input`,
  `games/totk/soak_walk.cmd`): the cliff save at 120 fps stopped after 47 min (`FATAL GPU allocated 6555 MB, above the budget of 6553 MB`: 3,185
  render targets of virtual pools, each a Metal texture of its own); with DM_NVN_RT_HEAP=1 180.2 min, exit 0, footprint +77 MB/h. So the heap is the
  default where it can be (`rt_heap_decide`: Metal 4 with the TBDR rewrites off, Tears of the Kingdom on the Mac; DM_NVN_RT_HEAP=0 turns it off); iOS
  stays without it (62h). The memory pool table held 3,198 (peak 3,470) of its 4,096 entries
- [x] batch 53 · 167 a teleport command (`games/totk/survey.c`, `DM_SURVEY=1`; MacNativeGames d164868 `world_survey.c`, MIT: its 1.4.3 addresses,
  actor layout and bounds as data, written anew): `survey pos <tag>` / `survey move <tag> X Y Z <frames>` on the command channel (hooks
  `dm_game_command`, `dm_game_frame_sync` at main 0x2db958) through the game's forceSetMatrix (main 0x45ce70). An overworld save is still needed for
  towns (the user)
- [-] batch 53 · 168 measured: no second BC4 encoder in Tears of the Kingdom's runs (census `ttm_note`: 2D against 2D array, handled by `tex_fit`, and
  three pixel shaders reading a 4x4 RGBA8 texture as texture3d, no visible effect); a 2D-to-3D copy waits for a scene that shows it

Tier 3d, iOS, Mac and tooling:
- [x] batch 53 · 169 shader functional keys as a fallback (Touchable's internal/tools/shader_keys.py, MIT, written anew): sha256 of the stage, the
  code's layout and code (`shader_fkey`, kept by program serial in `mtl_prog_fkeys` so prog_t does not grow); `shmap_from_capture.py` writes it as the
  shader map's third column; a key not in the map takes the first with its functional key (`shmap_resolve`)
- [x] batch 53 · 170 the game language from the app's own language setting (Touchable's switch_language / CFBundleLocalizations, MIT, written anew):
  game.json `languages` → `CFBundleLocalizations` (`ios/tools/project_lang.py`); at launch the first preferred language maps through `host/lang.h` to
  DM_LANG when the game has it (Tears of the Kingdom without Portuguese: USpt's code is not in this SDK's list). On the iPhone 17 Pro (2026-10-21) the
  app's language follows the Language row; manosaba keeps the language chosen in the game, so the game's text is to check with Tears of the Kingdom (62)
- [x] batch 53 · 171 the touch controls below the iPadOS window controls and controllers (Touchable, MIT, written anew): `tp_layout` puts the left
  column below the window controls (iOS 26 `UIViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:`); a key pressed on a controller hides them
  (`host/gamepad.m` `dm_front_controller_key`), a touch or the last controller gone shows them (`tp_controller`). To check (the user): an iPad window,
  a controller
- [x] batch 53 · 172 ProMotion 120 Hz for a game that steps by real time (Touchable's SS_DISPLAY_HZ=120, MIT): `nvn_promotion_hz` (Tears of the
  Kingdom with realtime: 120 while a 120 Hz screen runs at 120; DM_PROMOTION=0 or DM_PRESENT_HZ leave it); manosaba steps per frame and stays at 60.
  To check (the user): a ProMotion iPhone's frame rate line in Tears of the Kingdom
- [x] batch 53 · 173 install checks (Touchable's launcher install, MIT, written anew; `ios/tools/install_check.py`): `run.sh install` refuses an
  expired signature or a profile without this device, warns within two days of expiry and backs the app's Documents up with a SHA-256 manifest first.
  Checked: installs to the iPhone 17 Pro (2026-10-21) with the expiry warning and the backup
- [x] batch 53 · 174 the iOS shader directory checked complete before packing (acnh-mobile 67afe52 check_invariance, MIT;
  `ios/tools/shader_check.py`): it found Tears of the Kingdom's profile naming manosaba's iOS directory; run.sh stops on it
- [x] batch 53 · 175 the game data verified once, then a marker (super-mario-odyssey-ios ea19678, MIT; `dm_romfs_validate_marked`): the key files
  every launch, the tree walked once while the marker matches; DM_ROMFS_RECHECK=1 walks again. On the iPhone 17 Pro (2026-10-21): 667 files walked in
  49 ms after an install, the marker in 1 ms after
- [x] batch 53 · 176 residency sets only on an Apple6-family GPU (GirlNet d2r-mobile 3e051f1, MIT): on a virtual GPU (GitHub's runners) Metal 3 relies
  on its useResource declarations and Metal 4 stops with a clear line; CI job `gfx-m3` (by hand: Actions → CI → Run workflow) runs the Metal 3 gfx
  tests there. Not run on GitHub yet
- [-] batch 53 · 177 measured first (LASRecomp dc1d28f: wait for the fence before the newest submission): an nvnSyncWait census: Tears of the Kingdom
  0.17-0.20 waits a frame on the newest still running, manosaba 1.00 (1.6-1.8 ms a frame), the pattern LASRecomp fixed. Not changed: whether manosaba
  misses frames on a phone decides it (97 / 142)
- [x] batch 53 · 178 the diagnostics export redacted (lordran 40a9a72's rules, MIT, written anew in `host/rundiag.h`): app container and bundle paths,
  a home folder's user, UUIDs, MAC, IPv4 and e-mail addresses; memory read every 60 frames (lordran 24740d4); the memory line gives the Metal 4
  command allocators
- [x] batch 54 · 179 tests to take: overlapping `UpdateUniformBuffer` writes (Touchable; `t_nvn` 5a2), a 512 re-upload soak (lordran; `t_upsoak`), the
  pass-continuation cases (GirlNet `t_p5r_rt_continue_gpu.m`: ten written anew in `gfx/tests/t_rt_continue.m` with an eleventh)
- [x] batch 34 · 180 screens judged by TypeSafe's Jev model drive the test runs (the d2r-mobile author's method; the key stays outside the repository
  in `~/.config/typesafe/api_key`): `tools/jevdrive.py` drives a running host's command channel (`DM_CMD`, `DM_NVN_SHOT_OUT=960x540`) until a goal's
  done condition holds on two frames in a row; goals in `games/<id>/jev.json` (questions in English). Tears of the Kingdom `load_save` about 25 s
  against about 50 s for the fixed keys; manosaba `first_dialogue`. Without a key it exits 2 and the fixed keys stay the fallback

Tier 4, large:
- [x] batch 55 · 61 NVN translation on a thread of its own (after 5, 47 and 49; `DM_NVN_XLAT`, hle_nvn.c, nvn_mtl.m `mtl_sub_capture`; D2R Mobile
  b94670c and MacNativeGames' TOTK_ASYNC_REPLAY, MIT, written anew): submissions, presents and the frame number go in order to one thread; the
  submit captures what the console reads there (deferred handles, bound uniform bytes, FenceSync records) and returns; a fence the game takes is a
  token (SYNC_TOKEN); the thread is USER_INTERACTIVE with a time constraint of half the present period (DM_NVN_XLAT_RT=0 drops it: 61d).
  `gfx/tests/t_xlat.m`. Tears of the Kingdom's prologue at 30 fps frame p95 46.3 → 33.9 ms, the 60 fps product at 120 Hz 15.6 → 11.0 ms, an hour's
  soak clean; manosaba, which waits for its newest submission every frame (177), worse (p95 17.8 → 20.5 ms; one freeze in 16 runs, inside the
  driver's submitCommandBuffers). So the default for Tears of the Kingdom (games/totk/defaults.c, DM_NVN_XLAT=0 turns it off), off for manosaba
  - [x] batch 54 · 61a LASRecomp's render-queue cases as a test first (`gfx/tests/t_rqueue.c`: nested lists, a fence in a nested list, two waiters,
    the four wait results, a reused ring slot); `nvnCommandBufferFenceSync` is now a record (OP_FENCE_SYNC) whose replay gives the sync its
    submission's fence. Completion may only be held after QueueFinish (the submit waits for one of the four occlusion-count buffers, `g_vis_ring`)
  - [x] batch 54 · 61b deferred bindings frozen at submit (read once a submission, stamped with its generation at `mtl_sub_begin`); binds of more
    than 32 handles split into records of 32, refused only past the replay's tables (textures and samplers 128, images 32).
    `gfx/tests/t_bindfreeze.m`
  - [x] batch 54 · 61c a per-frame critical-path trace (`gfx/nvn/nvn_trace.{h,c}`; totk-mobile's host/timeline.c, Apache-2.0, written anew):
    `DM_NVN_TRACE=<first>+<frames>:<path>` writes a TSV, `tools/trace_report.py` splits each frame into translation / sync waits / acquire / present
    / game with the GPU's busy, latency and starved time. Prologue: at 30 fps 297 of 299 frames wait for the display (61 does not raise 30 fps); the
    60 fps product at 120 Hz 16 % wait for translation (more on an iPhone's CPU: 61d). `gfx/tests/t_trace.c`, `tools/tests/t_trace_report.py`
  - [ ] 61d the translation thread's time-constraint policy (re-declared at most every 200 ms) against none, on the iPhone (with 61e's
    DM_NVN_PASSGPU count there: the single-draw passes' cost on a tile GPU)
  - [-] 61e full-screen single-draw passes as compute: not done on the Mac. DM_NVN_PASSGPU (item 49) on the prologue (M5): 84 single-draw passes a
    frame, 2.76 ms together, nearly all small downsample targets; idle gaps between encoders 0.19 ms a frame plus 0.84 ms waiting for the CPU to
    commit (61's matter). Compute versions need the translator to make kernels, to save at most the passes' own cost (under 0.9 ms a frame) on a GPU
    busy 6.7 of 33 ms. On an iPhone's tile GPU a dependent pass costs more (totk-mobile: 38-57 µs on an M4), so the DM_NVN_PASSGPU count joins 61d's
    device run
- [ ] 62 Tears of the Kingdom on iOS: guest memory layout, VP9 on iOS, BC → ASTC, a device run (after 19, 25, 59); the cold-texture census of 56 under its memory limit; code options (57) through embedded changed pages (tomodachi-native afd88e9)
  - [x] batch 54 · 62a guest memory reserved at the top of `main()`, before UIKit and Metal (`svc_reserve_early`; `host/guest_as.h`: the lowest free
    gap from 4 GiB, 2 MiB aligned, ending at or below 2^39, from 6 GiB down by 256 MiB to what the game needs; `vm_map` fixed, mmap's choice if that
    fails, the free gaps logged when short). Every platform: a block past 2^39 is moved below it (macOS 27 reserves about 24 GiB in
    0x70_0000_0000..2^39, MacNativeGames). Tears of the Kingdom: `guest_heap_mb` 2176, `ios_pool_add_mb` 0 (the console's pool; manosaba keeps 512).
    `host/tests/t_guest_as.c`. The size an iPhone gets is a device check
  - [x] batch 54 · 62b VP9 on iOS with libvpx (GirlNet dropped VideoToolbox's VP9 probe, which faulted at a movie's start):
    `ios/tools/build_libvpx.sh` builds 1.17.0 (SHA-256 pinned, the value totk-mobile pins) static for arm64 iOS 18.0+ into
    `$DM_ROOT/build/ios/libvpx`; `ios/run.sh` links it after `ios/tools/vpx_check.py`, hle_movie.c finds the headers through `DM_VPX_INC`; without
    it a line says movies will not play. `tools/tests/t_vpx_check.py`. A cutscene on the phone is a device check once Tears of the Kingdom runs
    there
  - [x] batch 54 · 62c BC1 → ASTC 4x4 on the GPU on GPUs without BC (MacNativeGames `nvn_bc1astc.c`, `nvn_bctc.metal`): `gfx/tex/nvn_bc1astc.c` and
    the kernel b1a (nvn_mtl.m), the three-colour midpoint and alpha exact; WriteTexels, texture → buffer copies and ReadTexels of BC textures
    without BC refused in red. `DM_NVN_BC1_ASTC=0`: decompressed as before. `gfx/tests/t_bc1astc.m`. Prologue at frame 3840 (DM_TEST_NO_BC=1): 2,314
    MB against 3,545 MB decompressed and 1,984 MB with BC
  - [ ] 62c2 the other BC formats on GPUs without BC: BC2 / BC3 / BC7 → ASTC 4x4 and BC4 / BC5 → EAC R11 / RG11 (MacNativeGames
    `nvn_bctc.metal`: decoded and encoded again, lossy); now decompressed (4x and 2x their size). After a run of Tears of the Kingdom on a GPU
    without BC shows what its memory comes to (62's census under the limit): lossy, and only those devices gain. With BC1 as ASTC the prologue
    still takes 330 MB more than with BC (2,314 against 1,984 MB at frame 3840): what the other formats cost
  - [x] batch 55 · 62c3 BC textures the GPU writes, on GPUs without BC (Apple's Metal Feature Set Tables of 2026-05-21: every iPhone with an A14,
    A15 or A16; `supportsBCTextureCompression` tells): the encoders' block draws go into the auxiliary block texture as with BC, and after the pass
    `blk_decode` decodes them into the texture (bcd; b1a for BC1 kept as ASTC; bcd3 for a 3D depth plane); BC6H / BC7 still left out, red once.
    `gfx/tests/t_blkunp.m`, t_mtlguard +1
  - [x] batch 54 · 62d handheld mode by default on iOS (1280x720): game.json `ios_mode` "handheld" (Tears of the Kingdom; manosaba stays docked);
    `DM_OPMODE` still decides when set. `host/tests/t_oe.c`
  - [x] batch 54 · 62e RomFS as 1 GiB parts with an index; a ready marker instead of the tree walk at every launch (the marker: item 175):
    `tools/romfs_parts.py pack <tree> <out>` writes the parts and `romfs.dmri` (sizes, files, SHA-256), `check` holds parts to it; copied into
    Documents, the next launch (`host/romfs_parts.c`, `romfs_locate`) checks them and unpacks part by part into Application Support/romfs.partial (a
    launch cut short carries on; absolute, "." and ".." paths refused). `host/tests/t_romfs_parts.c`. Copying Tears of the Kingdom's parts to a
    phone is a device check
  - [x] batch 54 · 62f `maxBufferLength` and the recommended working set on the "Metal device" line (mush8 PR #3); `DM_COOL_START=nominal` (or
    `fair`) waits before the game starts until the thermal state is at most that (looking every 5 s, at most 10 minutes; T5eng: the same scene at 60
    fps on a cool phone, 30 on a hot one): `host/coolstart.h`, `host/tests/t_coolstart.c`
  - [ ] 62g one varying interface as a fallback library (bayonetta3: every vertex shader exports the union of the pixel inputs) if a device's log
    shows a "not written by vertex shader" failure (item 137: none on the Mac since chain bab8d4746968; the red line names both keys)
  - [ ] 62h render targets of virtual pools in a heap on for iOS (`DM_NVN_RT_HEAP=1`, item 138: 140-190 MB less GPU memory on the cliff save)
    after screenshot comparisons over more scenes; with the TBDR rewrites on it needs them alias-aware (pass merges and content facts by heap
    range) or stays off
- [x] batch 56 · 63 the 2K canvas and MetalFX (veneers in the rewriter, HeapDef overlay, larger application pool; after 52 and 95)
  - [-] 63a MetalFX's docked guard against the scene's content size (MacNativeGames ff092e3): their fix is in their replacement of the game's
    upscale pass; our present-time MetalFX (`fx_apply`) already compares the content size with the size on screen (63c: the valid region from the
    pass's constants)
  - [x] 63b the output size ("native 2K", after MacNativeGames' game_patches_v143.c patch_output_res, MIT, reviewed at 41e1e0c): code option
    `output` (rules.py): the swapchain size built at main 0xbdb578 / 0xbdb584 is loaded from the host page +24, and a gated code cave at 0x3dcb28
    sets the canvas (x3 and display 0's +0x10) from +32 when that is not 0 (MacNativeGames stores unconditionally). `games/totk/frame_step.c` writes
    them from `DM_OUTPUT_RES` (1920x1080 / the game's own), up to the heap overlay's `.dm_output` note (`games/totk/heapdef.py ... --output
    <w>x<h>`, MacNativeGames' formula); on iOS the growth needs `DM_APP_POOL_ADD_MB` (device check). `games/totk/check_output_res.py`,
    `t_frame_step`, `tools/tests/t_heapdef.py`. 2560x1440: the UI sharper, GPU p50 6.2 against 4.7 ms; the post-processing size follows the canvas
    (canvas / 1.2, a multiple of 16). product60 rebuilt (previous: `product60.prev63`)
  - [x] 63c MetalFX in place of the game's own upscale pass (after MacNativeGames' mfxup_dock, MIT; `gfx/nvn/nvn_mtl.m` `fxp_dock`): with
    `DM_UPSCALE=metalfx` (the iOS setting) the draw of the profile's `upscale_pass` (game.json {fs, color}, `tools/gen_game.py`; Tears of the
    Kingdom's ac58fc18, FSR 1 EASU-style, HDR) is left out and MetalFX's spatial scaler writes the canvas from the valid region its constants give,
    only when that draw alone would write the whole canvas. Mean RGB within 0.2 of the game's own; GPU p50 6.30 against 6.20 ms at 2560x1440
  - [x] 63d MetalFX before the game's compose pass (after MacNativeGames' mfxup_draw, the "iOS handheld" case; `fxp_scene` / `fxp_sub`): in handheld
    the profile's `compose_pass` (0ac9a549) samples a MetalFX upscale of the scene instead of the scene (substituted by texture ID). 960x540
    handheld: the same brightness, smoother edges, GPU p50 4.25 against 3.36 ms bilinear. `gfx/tests/t_fxp.m`, `tools/tests/t_upscale_pass.py`
- [ ] 64 for the next game: add-on content, FenceSync, ClearBuffer and the other NVN functions, LDN (the list of 2026-10-20 too)
  - [x] batch 54 · 64a the 47 NVN functions bayonetta3-mobile has and we lack: 27 now (getters of colour, vertex, sampler and texture view state, a
    texture's address, the window crop; BindImage, BindTextures, the deferred image / separate texture / separate sampler binds read at the
    submission's start as 61b's; the queue's acquire; the checkpoint mask 1; colour reduction and ZCull compression accepted).
    `gfx/tests/t_nvn64a.m`
  - [ ] 64a2 the rest of 64a: ClearBuffer, DrawArrays / DrawElementsIndirect, the IndirectCount multi-draws, DispatchComputeIndirect and
    Downsample (Metal's indirect draws through `draw()`, whose rewrites read vertex counts on the CPU: deferral, chunks, coverage), the deferred
    uniform / storage buffer binds (the uniform snapshot walks only plain binds), SetPatchSize with 64f; left to the red-reporting stub: the
    subtile size calls, GetCommandMemorySize, GetSparseTileLayout (its layout is not in our header) and the integer border colour getters (no
    setters here)
  - [x] batch 54 · 64b CopyCommands as a copy made at the call (the SDK copies, as dr3-mobile read it; ours recorded a call): nested calls inlined,
    refused past 16 call levels, 1,000,000 records or 64 MiB, a cycle or a bad record; the replay follows calls 64 deep at most. No handle registry
    (Tears of the Kingdom's 1.28 million calls a run would grow it). `gfx/tests/t_copycmd.m`
  - [x] batch 54 · 64c depth bounds through `setDepthTestMinBound:maxBound:` (a record OP_DEPTH_BOUNDS; bounds clamped to 0..1, empty bounds skip
    the draw; the Metal 3 build behind @available), checked with bayonetta3's vectors (`gfx/tests/t_dbounds.m`). Neither game enables depth bounds
  - [x] batch 54 · 64d rasterizer discard (draws left out while on, reported once when the vertex stage may write memory), `SetScissors` /
    `SetDepthRanges` batches (entry 0 applied, others reported once), a second queue ordered on the one Metal queue; `QueueWaitSync` false on a sync
    never fenced. `gfx/tests/t_nvn64d.m`
  - [x] batch 54 · 64e several texture and sampler pools (bayonetta3's descriptors, lordran's checks): each pool an ID range from its descriptor
    memory's base (`hle_nvn.c pool_ns`; one pool unchanged), handles resolved at the base of the pool set last (`h_tid` / `h_sid`). Neither game
    sets a second pool. `gfx/tests/t_pools.m`
  - [ ] 64f tessellation through a Metal 3 command buffer between Metal 4 submissions. With the next game that tessellates: neither has a
    tessellation stage (control +0x714 of every capture is 0, 1 or 5: TotK 28,652 + 24,371, manosaba 2,740), ProgramSetShaders already reports one
    red, and it needs the translator's hull / domain stages as well
  - [ ] 64g HSETP2 `.H_AND` as LASRecomp and yuzu (5 vectors); translator call arguments kept local. With the next change of the translation
    chain: both are translator changes, so the chain version moves and every shader of both games is translated again (Tears of the Kingdom's
    25,320 and manosaba's 1,299, and the iOS libraries), while neither game has the instruction (the review counted 0 uses)
  - [x] batch 54 · 64h symbol resolution as Horizon's rtld (the SystemEvent: 64h2), by the ISC-licensed oss-rtld's rules: hidden / protected
    references bind in their own module, default ones to the first module in the order main, subsdk0-9, sdk that defines the name non-locally
    (`bind_sym`; it differs from ours only in the sdk's typeinfo and vtables: 801 manosaba, 782 TotK). Both games ran the same, so it is the
    default; DM_RTLD=0 binds by ours. `host/tests/t_rtld.c`
  - [ ] 64h2 a SystemEvent kept while its detached read end waits (blade2-mobile, UE4's audio renderer): neither game's main or subsdk0 imports
    `DetachReadableHandleOfSystemEvent` (only the sdk defines it); with a UE4 game
  - [x] batch 54 · 64i `getentropy` as HLE_FORCE, and the crypto / os random bytes from the host's entropy (deterministic with DM_DETCLOCK);
    `nn::pl::GetSharedFont` every shared font at once in type order (24-byte SharedFontData, read off manosaba's sdk); IL2CPP's `SystemNative_Stat`
    / `LStat` are not dynamic symbols in manosaba's main, so not taken (armyja patches them by offsets: with a Unity game that needs it).
    `host/tests/t_rng_font.c`
  - [x] batch 54 · 64j AudioOut volume and counters (Set / GetAudioOutVolume, GetAudioOutPlayedSampleCount, GetAudioOutBufferCount,
    ContainsAudioOutBuffer, FlushAudioOutBuffers; the SDK's own read through a null pointer for a host output) with the SDK reference's and yuzu's
    semantics (facts only), `host/tests/t_audio_extras.c`; FlushTexels as a CPU write unless WriteTexels wrote the texture since its last flush
    (Tears of the Kingdom pairs each of its 42 WriteTexels with one), `gfx/tests/t_flushtexels.m` (AVC / AAC from the bytes the game sends: 64j2)
  - [ ] 64j2 AVC / AAC decoded from the bytes the game sends (lordran acfb424: VideoToolbox from SPS / PPS, the reorder window from the SPS,
    AudioConverter for AAC; about 1,000 lines). Ours decodes the same track again with AVFoundation, and its Extractor hands the game that track's
    own samples, so for manosaba the bytes and the re-read agree. With the next game that demuxes itself or gives a memory source, or a desync seen
  - [x] batch 54 · 64k named time-zone rules (LoadTimeZoneRule, ToCalendarTime / ToPosixTime by the named zone through CFTimeZone;
    `host/tests/t_tzrule.c`); `IsSaveDataExisting` from $DM_SAVE, EnsureSaveData making it; OpenPreselectedUser; the NPDM's system resource size
    (done by 160); movie decoders reading the game's DecoderEvents when they signal (RegisterClientEvents keeping its pointer);
    ShowControllerSupport's result (`host/tests/t_hid_applet.c`); a separator before the mount name ("/rom:/…") and `\` between path components. [-]
    Unity shader layouts: our shaders come from the NVN calls (`shmap_from_capture.py`), not Unity's files
  - [x] batch 54 · 64l the control section length by GPU code version (0x838-0x880, at +0x20; bayonetta3-mobile's nvn_control.h as data): a shorter
    section is read to its own length and zeros to 0x880, so keys and .ctl files stay as dmsh_build.py checks; another version reported once. Both
    games' captures are all 1.16 / 0x880, so no key moves. t_nvn 5b2
  - [x] batch 54 · 64m guards against Metal aborts (super-mario-odyssey-ios 693bc6b / 356ab26, MIT, written anew): a non-depth format on the depth
    attachment left out, a cube view whose slices are not a multiple of 6 made 2D / 2D array, and (found with DM_TEST_NO_BC=1) a view of another
    format of a BC texture kept decompressed or as ASTC left out, red once (decoded since 62c3). `gfx/tests/t_mtlguard.m` (each case in a process of
    its own)
  - [x] batch 54 · 64n provisional profiles from the game files (LDN: 64n2): `tools/new_game.py --check` works the profile keys out again from each
    workspace and compares them with game.json, printing the engine markers (nxnative's profile_guess.py marker list, MIT, as data); both games
    agree. `tools/tests/t_new_game.py`
  - [ ] 64n2 LDN as dr3 (Bonjour, an HMAC-signed control protocol, a two-process test): neither game imports any `nn::ldn` function; with the next
    game that has local play
- [-] batch 55 · 112 conditional rendering on the GPU: not worth it on the Mac. The prologue makes 11,000-19,000 conditionals per 300 frames, 0-2 %
  on a samples-passed result still pending (at most about 1.3 draws a frame the console might have skipped); the GPU way needs GPU-written results,
  indirect draws and predicated clears (totk-mobile's nvn_query_gpu / nvn_conditional_draw). The same line (every 300 frames) on an iPhone decides
  again (device check)
- [-] batch 56 · 113 framebuffer fetch for in-pass reads of the current pixel (after 42): not done. In the prologue 16 fragment shaders sample what
  their own pass draws, all but one before any texture barrier, where the replay must read memory as the console does (42); the one after a barrier
  (fcd1a8d9) samples at a coordinate from its varyings, not its own pixel. What its attachment copies (three 1600x896 twice a frame docked) cost on
  an iPhone is a device check
- [x] batch 57 · 114 an iOS Simulator build for the touch controls, safe areas and lifecycle: `ios/run.sh sim-build` / `sim-run [s]` / `sim-stop`
  (ios/README.md). The Metal 3 build with MetalFX compiled out (`DM_METALFX` in `gfx/nvn/nvn_mtl.m`; the Simulator's SDK has no Metal 4 headers or
  MetalFX), unsigned, without libvpx; `ios/tools/sim_metal.py` compiles the map's MSL for the Simulator (`sim-run` sets `DM_NVN_TBDR=0`); the game
  data cloned into the container. Its pictures are not a device's. manosaba on an iPhone 17 Pro Simulator runs, `DM_TOUCH=pad` lays out in its safe
  area, `DM_SIM_BG=20,10` gives LIFECYCLE OK, `DM_SIM_ENV="DM_TOUCH=pad DM_TEST_TOUCHPAD=check:10"` 17 passed (`simctl` has no touch input).
  `tools/tests/t_sim_metal.py`

Tier 5, needs the user's devices, controller or account:
- [x] batch 63 · 65 increased-memory-limit with a free account (one signing attempt): a personal team signs it (Xcode made the profile with it on
  2026-10-21; with manosaba the iPhone 17 Pro's limit went from about 3.4 to 6.1 GB), so `ios/tools/project_free.py` keeps it (`t_ios_cpu`; dropping
  it as before fails 1). Extended virtual addressing is refused to personal teams ("do not support the Extended Virtual Addressing capability"),
  which bears on 62's address space with a free account
- [ ] 66 a Switch Pro controller check of 2
- [x] batch 59 · 67 gyro and rumble (implemented here; the checks with the controller and the phone are on the device list)
  - [x] batch 58 · 67a vibration handles per style, only the connected style forwarded, bool arguments masked: `host/hle_motion.c`. Handles as
    1.4.3's sdk makes them ({type, Npad ID's low byte, device index, 0}; type 3 full key, 4 handheld, 5 pair, 6 left, 7 right); only the connected
    controller's values reach the front end (`dm_vibration_take`, dm_input.h), each band at its peak since the last take (totk-mobile); off while
    not permitted or out of focus (LASRecomp's rule). The prologue now gets 2 vibration handles and 1 six-axis handle. `host/tests/t_motion.c`
    (totk-mobile's landing pulse as data)
  - [x] batch 59 · 67b the Pro Controller's IMU and HD rumble over IOHID on the Mac (atone): `host/procon_mac.m` opens the controller (0x057e /
    0x2009) beside the system driver, switches its IMU on and turns input report 0x30 into six-axis samples (`host/procon.h`, atone's axes); the
    game's vibration goes out as HD rumble (output report 0x10, dekuNukem's encoding) instead of CoreHaptics. `DM_PROCON=0` leaves it to
    GameController. `host/tests/t_procon.c`. Device checks: aiming and Ultrahand rotation by motion, the landing pulse felt
  - [x] batch 58 · 67c CoreHaptics with plain players (atone f6dba3d); phone haptics rate-limited and off at a serious thermal state:
    `host/gamepad.m` takes the values every 15 ms, an engine per handle, a new endless event per level change; `host/haptics_map.h` maps the bands
    to intensity and sharpness (dekuNukem's table). iOS without a controller: the phone's haptics as pulses with acnh-mobile's limits, none at a
    serious thermal state or with `DM_PHONE_HAPTICS=0`. `host/tests/t_haptics.c`. The feel is a device check with the user's controller and phone
  - [x] batch 58 · 67d gyro fusion with the rest bias learned (fed by a Pro Controller since 67b): the six-axis state from `dm_motion_set`'s
    samples, the orientation by atone/nxnative 185cfda's method (Mahony's filter, written anew), the zero point learned from still samples
    (BotWRecomp's idea; GyroscopeZeroDriftMode sets the threshold); `GetSixAxisSensorState` writes a not-connected state without a source.
    GameController's axes are to be measured first: `DM_PAD_MOTION_LOG=1` (device check: flat, tilted forward, turned left); then `gamepad.m` feeds
    `dm_motion_set`
  - [x] batch 62 · 67e the phone's haptics after a trip to the background: iOS stops the engine there and refused the new one made at once
    (AVAudioSession 'what', a red line on the iPhone 17 Pro), after which the phone stayed still for the session. Now made at the first pulse due
    in focus, tried again 2 s after a failed start, doubling up to 30 s (`haptics_map.h` `dm_phone_engine_due`, `gamepad.m`; `t_haptics`)
- [x] batch 62 · 68 MixWithOthers on an iPhone: another app's music plays on under the game (iPhone 17 Pro, 2026-10-21; "mixes with other apps: yes")
- [ ] 69 the game over flow with the overlay, in play
- [ ] 70 manosaba's iOS app with chain bab8d4746968 on the devices (was 1e41705de87b), with the host stack of 40 (both IPAs built with it); `ios/run.sh lifecycle` there, and the home button and the app switcher by hand (51); the log's "CoreAudio output opened in" time there (55); the on-screen controls of 58 with `DM_TOUCH=pad` (`DM_TEST_TOUCHPAD=check:30`, then by hand: the stick, the camera drag, the eye, a controller connected)
  2026-10-21 on the iPhone 17 Pro (chain ca5eb9623812): `ios/run.sh lifecycle` LIFECYCLE OK, the home button and the app switcher fine by
  hand, "CoreAudio output opened in 4.1 ms"; left: the on-screen controls of 58 with `DM_TOUCH=pad`
- [-] 71 not done: iOS 17 is not supported (59)
- [ ] 71b the covered-window check of 27 in a window (`DM_WINDOW=1 DM_TEST_COVER=30,10`, display on); the Metal 4 screen of 19 on a device
  (`DM_TEST_NO_METAL4=1` in Documents/env.txt); the backup exclusion of 20 in Settings → iCloud → Backups; uncapped pacing of 53 in a window (product60, `DM_WINDOW=1`)
- [ ] 71d displays above 120 Hz: macOS rounds a 60 Hz request to 72 on a 144 Hz panel (tvc97); needs such a display
- [ ] 71c in play with product60: the systems BotW had on 30 fps constants (stamina regeneration and drain, arrow speed, shield surfing, rune
  aiming, flurry rush) at 30 and at 60 fps, and cutscenes there (none listed for Tears of the Kingdom by MacNativeGames; BotWRecomp's BotW list
  as the guide)

Tier 6, optional (seen, not planned before; listed at the user's request 2026-10-20):
- [x] batch 60 · 149 face buttons by position for PlayStation and Xbox controllers, as a switch (MacNativeGames 31052d6, MIT, written anew):
  `DM_FACE_BUTTONS=position`; Nintendo controllers keep their printed letters. Unset, the system's letters as before (MacNativeGames' default is
  position; the user plays with an Xbox controller by its letters). `t_padmap`
- [x] batch 60 · 150 pchtxt patches at load and a cheat VM, refusing rewritten sites (MacNativeGames 1b33531, PR #22)
  - [x] batch 60 · 150a pchtxt at load (MacNativeGames 1b33531's TOTK_PCHTXT, MIT, written anew): `DM_PCHTXT=<file>[:<file>...]` (`host/pchtxt.c`,
    Mac): the `@enabled` sections go into the module its `@nsobid` names (against `<module>.cuts.tsv`'s `# BUILD_ID`) before the code is sealed; a
    patch touching a word the rewriter changed, or past the image, refuses the whole file in red. iOS: a red line. `host/tests/t_pchtxt.c`
  - [x] batch 60 · 150b a cheat VM (Atmosphère's cheat file format; MacNativeGames PR #22's split, MIT, open, written anew): `host/cheat_vm.c` runs
    Atmosphère's documented encodings (docs/features/cheats.md; facts only, its code is GPL); `host/cheats.c` (Mac): `DM_CHEATS=<file>` (main's
    build ID), `DM_CHEATS_ON=all` or names; store-only cheats into main's code at load (refused over the rewriter's words), the rest about 30 times
    a second on a host thread (mach_vm_read_overwrite / mach_vm_write). No cheat codes in the repository. iOS: a red line. `t_cheat_vm`, `t_cheats`
- [-] batch 60 · 151 the save-loading crash workaround at 1.4.3 main+0x1bd01c4 (MacNativeGames dd0e893): not seen, no log here has that address; to
  take if it ever shows up
- [-] batch 61 · 152 frame interpolation by replaying the frame (LASRecomp `frame-interp-rt`; peiikai/LASRecomp `runtime/nvn/interp.c`,
  dc1842b..777fe71, MIT, work in progress): not taken. product60 already presents at 120 Hz on the Mac (GPU p50 4.5-4.8 ms); on a phone a replay
  costs a frame's GPU work again, and what limits Tears of the Kingdom there is not measured yet. To look at again once it runs there, if the GPU
  has room and the CPU side is the limit
- [x] batch 61 · 153 frame latency 2 at 30 Hz (lfkdsk/switch-appletv-port-skill references/05-perf.md §5.3, MIT: present-to-present standard
  deviation 0.603 → 0.301 ms, for 33 ms more latency): `DM_PRESENT_LATENCY=auto` (`gfx/nvn/nvn_pace.h` `nvn_latency_setting` / `nvn_frame_latency`,
  `link_latency_sync`) takes 2 while the display link ticks at 30 Hz, 1 otherwise; a number fixes it; unset stays 1. The default waits for a device
  check (the present interval spread at the 30 fps setting with and without `auto`). `t_pace`
- [x] batch 61 · 154 the ultrawide aspect patch on the Mac (MacNativeGames `aspect_patch.h` at 1.4.3, MIT, written anew): option `aspect`
  (`games/totk/rules.py`, gated code caves at 0x628c74, 0xb43460, 0x1ff04f4, 0x253fbb0) multiplies the cameras' ratio by the host page +40, by 1
  while it holds 0 (a factor, not MacNativeGames' replacement), which `games/totk/frame_step.c` writes at every present. `host/aspect.c`:
  `DM_ASPECT=<w>:<h>`, `<w>x<h>`, a ratio from 1 to 4, or `window`; View → Wide Picture (⌘4) switches 21:9; the present pass stretches the picture
  (`mtl_present_stretch`). The HUD stretches with it, as in MacNativeGames; iOS refuses `DM_ASPECT` in red. `games/totk/check_aspect.py`,
  `t_aspect`, `t_present_vp`. product60 rebuilt (previous: `product60.prev154`), `options.default` = `fps60,realtime,scene,output,aspect`

Open:
- [ ] 72 the blood moon crash: after 1, the stack logging of 40 and 47, dump the threads right at the skip; with 130's movie hold, the skip with the
  hold, without it (`DM_INACTIVITY_FPS=0`) and with the 30 fps product (needs a blood moon in play: the user)
