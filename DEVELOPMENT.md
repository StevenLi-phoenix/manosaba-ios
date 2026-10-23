# Development notes

Where the work stands and how to pick it up again, on this machine or a new one. The repository holds all code, tools and notes; what it
does not hold (and must not: keys, dumps, anything derived from the game) is listed below with the way to get it back. Two more files:
[docs/sources.md](docs/sources.md) (the projects we draw from, how far each was read, what was taken and what not) and
[docs/plan-archive.md](docs/plan-archive.md) (plan items 1-259 with their measurements, as they stood on 2026-10-23; code comments and commit
subjects cite them as "plan item N").

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
| Product (rewritten modules) | `work/build/cache/dm/out/…`, `work/build/totk/product`, `work/build/totk/product60` | `tools/build_product.sh && verify/regress.sh` |
| Shaders, stage map | `work/build/…` (paths in `games/<id>/game.json`) | PORTING.md § 5 |
| App icon | `workspace.icon` | Your own 1024×1024 PNG of the game's icon |
| Device saves | Device `Documents/save`; copies in `work/run/ios/*_save_*` | Files app → this app → `save`, or `devicectl device copy from` |
| iOS signing settings | `ios/local.env` | From `ios/local.env.example`. Keep the bundle ID of the installed app (`xcrun devicectl device info apps --device <id>`): another bundle ID is another app with an empty data container |

Keep `keys/`, the dump, the backups and the device saves in a separate private backup (for example an encrypted external disk); they stay
out of git by design.

## Testing

Check a working copy with `make -C host/tests run`, `make -C host/tests run-ubsan`, `make -C gfx/tests run`, `make -C gfx/tests run-m3`,
`make -C gfx/tex/tests run`, the tool tests (`for t in tools/tests/t_*.py; do python -I "$t"; done` in the venv), `verify/regress.sh --fuzz`,
`tools/audit_public.py`, then a Mac run (README, Getting started) and `ios/run.sh build`.

**How a change is made.** One plan item at a time, each with a unit test and a broken version of the change that must fail it. A batch of
items ends with the gfx tests (Metal 4 and Metal 3), the texture tests, the host tests and their sanitizer run, the public audit and the tool
tests; `verify/regress.sh --fuzz` when the rewriter, a game's rules or `verify/equiv.py` changed; both IPAs (`ios/run.sh ipa`, `DM_IOS_M3=1
ios/run.sh ipa`); a short Mac run of each game against the previous batch's screenshots and red lines; then one commit whose subject starts with
"Plan batch N". A change to `tools/patch_nso.py` or a game's `rules.py` changes the rewriter stamp in the products, so they are built again.
The repository is public: `tools/audit_public.py` checks the git index before every push.

- **Memory over repeated loads on the Mac**: `games/manosaba/reload_loop.sh [--stay] <run dir> <save dir> <cycles> [host binary] [VAR=value …]`
  loads a save's newest autosave every 40 s by tapping through the game's own menus; `tools/memstat_cycles.py <run dir>/mem.tsv` prints one
  row per load. With `DM_GUEST_ENV="GC_PRINT_STATS=1" DM_DBG=1` the IL2CPP collector logs every collection; `tools/gc_stats.py <run dir>/log.txt`
  summarizes it. The managed heap census (`DM_HEAP_CENSUS`, `DM_HEAP_WATCH`, `DM_HEAP_REFS`, …) is in [host/README.md](host/README.md).
- **A memory limit kill on iOS** leaves no crash report as a rule (JetsamEvent reports are rate-limited, the crash is "non-actionable"). With the
  device connected: `sudo /usr/bin/log collect --device-udid <UDID> --start '<yyyy-mm-dd hh:mm:ss>' --output kill.logarchive`, then
  `/usr/bin/log show kill.logarchive | grep -E 'ReportSystemMemory|killed by jetsam'`. `DM_MEMSTAT` (relative paths land in `Documents`) and the
  low-memory watch in `host/memstat.m` show what grew.
- **Performance on the iPhone**: compare builds in pairs run one after the other, each after a rest, and judge the sustained rate after the
  first minute (the phone allows a burst of about 60 s, see below). Capped frames lower the clocks, so GPU milliseconds at a held rate
  understate a saving (it shows as a lower clock); the sky over Tears of the Kingdom's island changes with the game's clock, so pin the frame step
  (`DM_FRAME_STEP_MS`) or use a check mode before blaming a picture difference on a render change.
- **Pitfalls**: zsh has a `log` builtin (use `/usr/bin/log`); zsh does not split words (`set -- $var` does not do what bash does); make compares
  modification times, so two edits within one second can skip a rebuild; copy a host binary elsewhere before a long test if you will rebuild
  the host meanwhile; Apple's Metal HUD crashes with `CAMetalDisplayLink` on iOS 18.6 (keep it off there).

## Where the work stands (2026-10-23)

What plays is in the README's status. Supported systems: macOS 26 and iOS 26 (Metal 4); iOS 18 and macOS 15 through the Metal 3 builds
(`DM_IOS_M3=1`, `DM_MAC_M3=1`); iOS 17 and iOS 27 are not supported. Devices used: iPhone 17 Pro (iOS 26.4, free developer account, the increased memory limit: 6.1 GB), a Mac with an M5; manosaba also on an
iPad mini 7 (iOS 18.6.2) and an iPad Pro M4 (iOS 18.5, the unsigned IPA) up to 2026-10-16.

**manosaba 2.0.4b** plays on the Mac and the iPhone. Open:
1. **The game's own memory leak.** Every save load builds one more set of Naninovel's `ScriptManager` and `ResourceProviderManager`
   (`DM_HEAP_WATCH`); the old sets stay referenced. Live data grows about 20 MB per load, the heap about 28 MB (`GC_FREE_SPACE_DIVISOR=8` saves
   35–50 MB, not the rate). Root reference not found. Next: trace all old instances of the two managers (not samples), bound each object by its
   class's `instance_size` (`host/hle_svc.c`), scan the guest stacks too; then clear the root from a `games/manosaba` hook after a load.
2. **A trial's loading peak on 8 GB iPads.** Without the increased memory limit an iPad mini 7 gives about 3.35–3.4 GB; a trial's start adds
   about 1.4 GB in 0.2 s (about 116 character render targets, 0.9 GB of Metal memory). A fresh session keeps about 725 MB of headroom, a two-hour
   one does not (the leak). A free account signs the increased memory limit since plan item 65 (the iPhone 17 Pro went from 3.4 to 6.1 GB), and
   `ios/tools/ipa_sign.py` signs the unsigned IPA with it (211); neither was tried on the iPad mini.
3. Smaller: 13 NVN functions looked up but unknown (NULL, never called); compute dispatch never seen; keyboard, mouse and controller untested
   with this game.

**Tears of the Kingdom 1.4.3** plays on the Mac and on the iPhone 17 Pro (the prologue, its movies, the dive, the Great Sky Island and after).
The iPhone build embeds `product60` and runs at 120 Hz; on the island it holds about 59.5 fps after the first minute at a 1280x720 scene (plan
item 239). What limits it there (230, measured with a plain load instead of the game): the phone allows a burst of about 60 s, then a sustained
power budget that goes to a busy GPU first, and the CPU's clock is cut (TotK: GPU 75-80 % busy, CPU 1.4-1.7 GHz). The frame is one chain (the
game waits every frame for the GPU to finish the frame it handed over), so only less energy a frame turns into fps: less GPU work gives the CPU
its clock back (239: 55.4 / 56.4 → 59.5 / 59.6 fps), saved translation cycles turn half into fps (225); keeping the GPU busy and the GPU a frame behind the
game were measured and dropped (the archive's Tier 9 notes, "Why the CPU's clock falls"). A 960x540 scene holds 59.6 fps with 98 % of frames at
16.7 ms; the iOS default stays 1280x720 (the user's choice; the app's settings offer 540p with MetalFX).

## Tears of the Kingdom

`games/totk` (1.4.3, program 0100F2C0115B6000, main build 277178b7dba1b6d4949a84778d4abc58), data from your NSPs with `tools/game_extract.sh`
into `work/data/switch/totk-1.4.3`. SDK 20.5.22; `sdk` and `subsdk0` have the same build IDs as nxnative's Breath of the Wild 1.9.0 (the same
system layer). The parts below are the reference; each plan item's details are in the archive.

**Running it.** After `make -C host GAME=totk`:
`DM_GAME=totk DM_ROOT=$PWD/work DM_SHDUMP=$PWD/work/build/totk/shader_capture DM_SAVE=… DM_TEMP=… DM_CACHE=… DM_WATCH=120 host/dmhost` (stage
map and library from the profile; rebuild them with `shmap_from_capture.py` and `DM_GAME=totk dmsh_build.py`).
- Products: `work/build/totk/product` stays at 30 fps on purpose (the blood moon crash, 72, is not retried with it yet); `work/build/totk/product60`
  carries the frame-rate rules and the code options (57): `fps60`, `realtime`, `scene` (the iPhone's set: `DM_PRODUCT=build/totk/product60
  DM_OPTIONS=fps60,realtime,scene ios/run.sh …`, 62l), `output` and `aspect`; `options.default` all of them, `DM_OPTIONS=none` none.
  `DM_RENDER_RES=<w>x<h>` sets the scene size (52), `DM_RENDER_HEIGHT` its lines at 16:9, `DM_OUTPUT_RES=<w>x<h>` the output (63); the command
  channel's `scene <w>x<h>` changes the scene size while the game runs (240).
- RomFS overlay for 60 fps and up: `work/build/cache/dm/venv/bin/python -I games/totk/romfs_overlay.py work/data/switch/totk-1.4.3/RomFS
  work/build/totk/romfs_overlay` (quick menus' background, the game over menu's one-frame wait); served first when it exists
  (`games/totk/defaults.c` sets `DM_ROMFS_OVERLAY`; empty turns it off). Test: `games/totk/tests/t_romfs_overlay.py <RomFS>`. On iOS
  `ios/run.sh push-data overlay` copies it in and the app serves it when it was made from the phone's RomFS (194).
- The TBDR rewrites are off on the Mac and on iOS (`games/totk/defaults.c`; `DM_NVN_TBDR=1` on): on the M5 the GPU p50 is 8-10 % lower without
  them, and on the iPhone they drew a glow over the prologue's cave (62j).
- Window: game.json `present_interval` "game" (the display-link wait takes the game's interval: it steps per frame), `window_unfocused`
  "in_focus" (OutOfFocus is the console's HOME menu); `DM_PRESENT_HZ=30..120` sets the display link's rate (mode 1).

**Rewrite** (`tools/patch_nso.py`; `verify/regress.sh --fuzz` OK for all three modules):
- `blr x18` whose x18 was loaded in an earlier block: the K18 slot holds it wherever it was loaded (nxnative's blr rewrite has no such check).
- Jump tables with explicit targets (`adr; ldrsw / ldrh / ldrb; add; br`): targets derived over the function's control flow to a fixpoint, then a
  compare chain with landing pads as nxnative's `site_br_tab` (`sub` + `cbz`, NZCV kept); an index outside stops at `brk #0x18`. Row `JT`.
- Exclusive sections by control flow (`exclusive_regions`): TotK's compare-and-swap retries sit in cold blocks, giving sections in pieces copied
  by `site_region` (row `R`). Any other entry stops the rewrite. Load-exclusives become acquire loads (232).
- `mrs xN, ctr_el0` (SIGILL on Apple systems) → `movz xN, #4, lsl #16` in place, as nxnative (row `ctr`); also two words of manosaba's `sdk`.
- `HOSTREFS`: instructions that address a 16 KiB writable host page after the key page (fixups `HOST_PAGE_OFF`, only in modules that use it),
  resolved into cuts; the loader reads it (`dm_host_page`).
- Verifiers: `structck` ⑪ (windows, sections, explicit tables, single entry), ⑫ (`ctr`) and ⑭ (acquire loads); `equiv` runs sections from each
  piece and skips a trial reading below sp (the red zone); `fnfuzz` gives both images the `ctr` constant. Upstream PR #55 (x18 scan prefilter,
  n3crosis) is in.

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
  black after the movie). libvpx from Homebrew on the Mac, built for iOS with NEON into `$DM_ROOT/build/ios/libvpx` (62b, 62b2); pre-rendered
  movies are held at 30 fps (130). `t_movie` (`t_movie <file>` probes a movie).

**Shaders.**
- Keys are sha256(control section + code area) everywhere (`shader_key` in `hle_nvn.c`; the 0x880-byte control section alone collides).
- game.json `nvn_c2` (texture handles in c[2]: 0x20 for TotK, 0x168 for manosaba) and `depth_mode` (TotK `near_is_minus_w`: z' = z / 2 + w / 2
  before every vertex return); both in the translator job, the cache key and the reflection. `dmsh_build.py` needs `DM_GAME`, the translator
  `DOTNET_ROOT` with Homebrew's .NET (`/opt/homebrew/opt/dotnet/libexec`).
- `QueryHostSupportsShaderFloat64` false: Ryujinx's `DoubleToFloat` runs DMUL / DFMA in float. Integer pixel outputs: a failed pipeline writes
  `<DM_SHDUMP>/fout.tsv`, which `dmsh_build.py` passes to the translator. Vertex shaders padded for their pixel shaders (`vs_pad`). Early fragment
  tests where the control section asks for them (239). min / max / clamp with Maxwell's results for a NaN operand (`precise::`, 260). The chain in
  use: ae6a8bd1acdd.
- `engines/bnsh/shader_extract.py` reads the shipped BNSH (`Pack/Bootup`, `Lib/**`, `Shader/**`, as MacNativeGames' prepare_shader_seed.py),
  plus `Effect/**.esetb.byml.zs`, 0x88c-byte control sections and the sharcb archives, into `work/build/totk/shader_capture` (with the
  run-time captures 28,652 shaders; every one has a Mac and an iOS library, checked 2026-10-22). Shaders in main stay run-time captures.
  `c1_guard`: 49 shaders whose c1 index is not proven bounded.

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
- From MacNativeGames (MIT, credited in the code): a registration without a view clears the old view; partial clears as one triangle
  (`clear_quad` / `clear_ds_quad`); BC render targets through a one-texel-per-block view (`blk_aux`); storage buffers addressed from a uniform
  buffer (`sbuf_ubo`, `NVN_RB_SBUFU`, `pend_read`); the storage buffer write flag is bit 0.
- A draw without attachments gets a pass sized by the scissor. Also: `rt_dup_slot` (nxnative / tomodachi-native), inline uniform data in a host
  ring (nxnative), `BindProgram` as recorded, `SetRenderEnable`; `Read|WriteTexels`, `QueueFinish`, `SubmitCommands`, `PresentTexture`
  under one recursive lock.
- Uniform snapshots (`mtl_sub_snapshot`, MacNativeGames' usnap method, MIT, written anew): the game rebuilds per-object constant pools once a
  frame is submitted, so each submission copies the uniform ranges it binds into its argument arena when it starts (merged within 256 bytes, at
  most 1 MiB) and binds and reads the copies; a GPU write within the submission drops the copies it reaches. `DM_NVN_UBO_SNAPSHOT=0` reads live
  memory. `gfx/tests/t_usnap.m`.
- Threads: replay on a translation thread of its own (61), the render encoders on a second one on iOS (`DM_NVN_RENDER_STREAM`, 245).

**Frame rate** (from MacNativeGames' `game_patches_v143.c`, MIT):
- The controller (main 0x2db174) sets present interval clamp(int(max(CPU, GPU) × 60 + bias) + 1, 2, 3); `games/totk/rules.py` CUTS its seven
  words to allow 1. At a 120 Hz present rate the intervals 1 / 2 / 3 are up to 120 / 60 / 40 fps. `nvnWindowSetPresentInterval` logs a backtrace
  on every change.
- Real-time step: the two producers (0x2daa54 ratio, 0x3e5638 ticks) load +0 (seconds × 30) and +4 (seconds × 60) from main's host page;
  `games/totk/frame_step.c` writes there at every present (`dm_game_present`) the mean of the last five present intervals on the guest clock;
  `DM_FRAME_STEP_MS` pins it. Checks: `games/totk/check_frame_rate.py <product> <main NSO>`, `tests/t_frame_step.c`.

**Diagnostics**: `DM_WATCHPOINT` (`host/diag.c`), `DM_CRASH_MEM` (`host/crash.c`, `*xN`), `DM_NVN_PASSDUMP_ALL`, `DM_NVN_GPUWATCH=<first>-<last>`,
`DM_NVN_LIVEROWS=<file>` (single wrong frames in window play), `DM_NVN_LIVESHOT=<first>,<count>:<path with %d>`, `DM_NVN_NOCULL`,
`DM_NVN_RTCLEAR=<serial>`, `DM_NVN_TEXSNAP`, `DM_NVN_TEXNZ`, `DM_NVN_TEXCLASS`, `DM_NVN_REUPLOG`; the command channel `DM_CMD=<file>` (47); per-pass
GPU time `DM_NVN_PASSGPU` (49); the frame timeline `DM_NVN_TRACE` with `tools/trace_report.py`; on iOS, switches in `Documents/env.txt` (22). The
cliff repro: a manual save at the cliff edge, `DM_KEYS="A@26.5,A@31,A@35,LSU@f1380+240,X@f1400"`. Lesson: MacNativeGames' `nvn_mtl.m` described
the dive's flicker and its fix; search the references for a symptom before building probes, then measure to confirm the cause here.

Known gaps: depth-stencil (stencil) views are not modelled; an integer render target's partial clear is not drawn (red line); other iPhones and
iPads are not tried (GPUs without BC turn BC1 into ASTC and decompress the other BC formats, 62c / 62c2).

## Open work

The open plan items, shortened (their history: the archive under the same number), by what they need. New items continue from 260.

The codebase (2026-10-24, the user: the Metal backend in one 10,000-line file is the first thing to fix, before Android; what stays is mine to
decide):
- 263 the other switches off by default traced to their origin and measurement (`DM_NVN_RT_RANGE`, spread over ten parts with no recorded
  result, first), each kept as a diagnostic, kept as an option with its numbers written down, or removed.
- 264 the replay core and the Metal backend apart: what the replay decides (command walk, state, uniform snapshots, CPU-filled textures,
  counters, pass planning) in C behind an interface, the Metal calls behind it, so an Android Vulkan backend takes the same fixes.
  In steps, a module or two a batch, each into `gfx/nvn/core/` with its header and a test that needs no GPU: first the parts that use
  little Metal (store index done in batch 88, uniform snapshots in 89, the TBDR switches and validity ledger in 90; then occlusion, frame timing, the live
  texture, the submit capture, inline uniform renames, the replay loop); then the mixed parts (lookahead, tile merge, draw) split into what
  is decided and what is executed, 263's `DM_NVN_RT_RANGE` and `DM_NVN_DEPTH_PLAIN` going there (keeping what `t_rt_range --disabled`
  covers of the pass bookkeeping and unwritten DontCare); then the file-scope variables grouped by module; the Vulkan backend once the
  interface covers what manosaba needs.

The iPhone's frame rate (the current focus; the power split above decides what helps):
- 190 a scene size driven by dropped frames, now that the scene size can change while the game runs (240; 960x540 holds 59.6 fps on the island
  against 56.0). Upstream's controller counts only the first 3 frames of a run of dropped frames unless the run lasts 1 s; its GPU-time rule does
  not fit, since under the power split the frame looks CPU-bound.
- 131a the game's own dynamic resolution: two of four pass dumps on the iPhone (240) were at 0.8 of the page's size, which the Mac never showed.
  Nothing in main was found to set its enable bit (0x8c4eb0, +0x1c bit 0); look again from those dumps.
- 226 the submit call's capture on the game's thread: it walks every command of the frame (52 % of the call) and copies 1.5 MB of uniforms the
  snapshot copies again; snapshot lookups miss between draws binding several blocks; texture views are checked again in 6 % of the thread.
- 218 the G-buffer split by compute: two depth-probe dispatches (3c8e9ba061067c07_b, bc58d5dec860c367_b) sample the G-buffer's depth in its middle,
  and each split stores and reloads four colour targets and the depth (about 24 MB at 1024x576).
- 219 the TBDR rewrites that merge or defer passes, with mid-submission commits (228 did the others): on the phone GPU 24.3 → 22.8 ms and 15 %
  less traffic, but 66.6 ms frames without the commits. 186: the leads 62j left in those rewrites (shared storage tracked by serial, `attr_read`
  without `pend_hits`, the load probe's readback order on Metal 4, a run under Metal API validation).
- 229 the guest main thread's QoS: `DM_MAIN_QOS=ui` exists (batch 79); the A/B on the phone is not run.
- 220 MetalFX frame interpolation: the G-buffer has no motion target, so the camera's motion would come from the depth and two frames' matrices,
  and moving things would ghost.
- 221 fewer triangles through the game's own LOD and draw distance settings: the user's call on picture against frame rate.

Correctness that needs play, saves or the console:
- 72 the blood moon crash (Mac, 2026-10-19: the resource control thread read a null resource 3 s after the second blood moon movie was skipped).
  Skipped on the iPhone with the 60 fps product and the movie hold (130), no crash; left: a skip with the 30 fps product.
- 94a GPU write-back of idle textures (`DM_NVN_WB_IDLE=1`, totk-mobile's rule): it makes Link close to the camera solid instead of a translucent
  ghost. Needs console footage of Link close to the camera and of the island's grass, and what the RG8 256x256 texture the CPU rewrites is; then
  on by default or a narrower rule.
- 132 the wave program's wide lines (totk-mobile expands them by editing its shaders): needs a save where the game draws them.
- 71c product60's 30 fps systems in play (stamina, arrow speed, shield surfing, rune aiming, flurry rush; BotWRecomp's BotW list as the guide).
- Checks of finished items, for the user: grass that stands up and can be cut (161), hair and cloth in wind at 60 and 120 fps (163),
  `DM_NVN_FBX=split` with a turning camera over paved ground (165), an overworld save for towns (167), the game's text in the app's language (170),
  the settings panel on a device (148), stick diagonals against a console (124).

At the Mac or with hardware:
- 97 a present with a deadline against the GPU clock drop: needs a window and `powermetrics` (the password).
- 71b the covered window (`DM_WINDOW=1 DM_TEST_COVER=30,10`), the Metal 4 screen on a device (`DM_TEST_NO_METAL4=1` in env.txt), the backup
  exclusion in Settings, uncapped pacing in a window; the window checks of 123 and 127; 153's latency default (`DM_PRESENT_LATENCY=auto`) after
  a check of the 30 fps setting.
- 71d displays above 120 Hz (macOS rounds a 60 Hz request to 72 on a 144 Hz panel).
- 66 a Switch Pro controller with the printed-letter face buttons (2); 67 gyro and rumble with a controller and the phone.
- 62 other devices: the cold-texture census (56) under a device's memory limit; 62c2 BC2 / BC3 / BC7 → ASTC and BC4 / BC5 → EAC on GPUs without
  BC (now decompressed; with BC1 as ASTC the prologue still takes 330 MB more than with BC); 62g one varying interface as a fallback library if
  a device's log shows a "not written by vertex shader" failure.

The user's choices: 192 touch controls after Touchable (its 8-way D-pad, Liquid Glass, face-button slides and layout; the stick, the camera
drag and opacity are open); 221 above; `DM_FRAME_STEP=vsync` as iOS's default (133: 35 % less CPU than the mean step on the Mac).

With the next game (64): 64a2 ClearBuffer, the indirect and IndirectCount draws and dispatches, Downsample, the deferred uniform / storage binds;
64f tessellation; 64g HSETP2 `.H_AND` and translator call arguments (with the next change of the translation chain); 64h2 a SystemEvent kept while
its detached read end waits (UE4); 64j2 AVC / AAC decoded from the game's bytes; 64n2 LDN. Also [docs/sources.md § Port when a game needs
it](docs/sources.md#port-when-a-game-needs-it).

## Plan items from 260

Finished items from 260 on, in the archive's form (`[x] batch N` is the commit whose subject starts with "Plan batch N").

- [x] batch 85 · 260 min / max / clamp with Maxwell's results for a NaN operand on every GPU (seedds/nxnative 56704a0, MIT, written anew):
  spirv-cross writes the translator's FMin / FMax / FClamp as `fast::min` / `max` / `clamp`, which compile to `air.fast_fmin` / `fast_fmax` /
  `fast_clamp` in every Metal math mode, safe included, and leave a NaN operand's result to the GPU. The M5 happens to agree with Maxwell (the
  check of 2026-10-23 evening), an A20 Pro does not (nxnative: Super Mario Galaxy 2's `max(0, NaN)` lighting turned half of Mario white).
  `gfx/tools/msl_helpers.py` `precise_minmax` writes them as `precise::` (IEEE `fmin` / `fmax`), applied by `dmsh_build.py` after spirv-cross;
  every plain min / max / clamp left is an integer one (no fast intrinsic call in a 300-shader sample). Chain f9e7d3ecf49e → ae6a8bd1acdd
  (Tears of the Kingdom 28,675 shaders, 25,081 rewritten; its c1 list 5 as before), 217's 48 kernels made again. `tools/tests/t_msl_helpers.py`
  +7 (the rewrite, and a library compiled with iOS's flags calling `air.fmin` / `fmax` / `clamp` and no fast one); `T_MSL_PRECISE_OFF=1` fails 3.
  Smoke: Tears of the Kingdom's two red lines as before and its prologue frames the same by eye, manosaba's shots identical. The iPhone 17 Pro's
  (A19 Pro) own `fast::` results were not measured.
- [x] batch 86 · 261 the Metal backend split by subject: `gfx/nvn/nvn_mtl.m` (10,113 lines, 914 KB, 356 file-scope variables) keeps the
  device, queues, residency and deferred releases (295 lines) and includes 38 parts from `gfx/nvn/mtl/` in the original order, each a
  contiguous run of the old file cut where conditionals, braces and `@implementation` are closed (`work/scripts/split_mtl.py`, not in git).
  One translation unit as before: preprocessed (`clang -E -P`) for the Mac on Metal 4 and Metal 3 and for iOS 26 and iOS 18, the old file and the
  split give the same text but for blank lines and the 19 `__LINE__` values of 217's `KP_NO` (a pass dump's line numbers). The parts' quoted
  includes go one directory up; `host/Makefile` and `gfx/tests/Makefile` list the parts as headers, `t_red_once.py` reads them; the file
  table of `gfx/README.md` names each part. `tools/tests/t_mtl_parts.py` (6 checks: each part included once and none left out, each opening
  with its name, closing every conditional, brace and `@implementation` it opens, its includes resolving, both Makefiles rebuilding on a
  change); a part with an `#if` left open (`T_MTL_PARTS_BROKEN=1`) fails 1. Batch tests as in batch 85; smoke: the same red lines, the
  frames as batch 85's but for the animation's timing (looked at).
- [x] batch 87 · 262 the single-draw kernel passes of 217 removed: off by default, and on the iPhone they saved no GPU time. Out: the part
  `mtl/kpass.inc`, its hooks in `tex_desc` (shader-write usage on small targets), `rp_end`, `NvShader`, `bind_args`, `draw` and the
  `hle_nvn.c` statistics line, `nvn_kpass.h`, `gfx/tools/dmsh_kernel.py`, `t_kpass`, `t_kpnvn` with `gfx/tests/kpass/`, `t_dmsh_kernel.py`,
  `games/totk/kernel_shaders.txt` and the switches' rows in `gfx/README.md`; the kernels left in the workspace's chain folders went too. The
  backend is the one before 217 again: preprocessed for the four configurations (Metal 4 and Metal 3 on the Mac and iOS), it is the same text
  as 55de9bf's `nvn_mtl.m` with today's headers but for blank lines, and `hle_nvn.c` the same file. Git history keeps all of it (b921a1e).
- [x] batch 88 · 264, its first module: the store lookahead index in plain C. `gfx/nvn/core/` starts: its own translation units, no Metal,
  Objective-C or Grand Central Dispatch, a header for all the backend may use of a module and, under "The backend provides", what the module
  asks of the backend. `core.h` / `core.c` hold `MLOG`, `MDIE` and `RED_ONCE` with `g_red_once` (the backend's, the same lines);
  `rt.h` a mip level's extent, an attachment's subresource and whether a scissor covers a target; `prog.c` `prog_bind` (the program per
  stage); `store_index.c` the index (the scan, the buckets, the replay order check `sdx_seq`, the decision `sdx_first`), its state in
  `g_sdx`. The backend gives it a program's reflection per stage (`nvn_be_prog_refl`) and a binding record's texture serials
  (`nvn_be_bind_sers`); `mtl/store_index.inc` keeps the build at the submit's start and the ledger's check, `hoist_try` moves to
  `mtl/lookahead.inc`, `tile_merge` reads `g_sdx`. Both Makefiles build `core/*.c` (the iOS app follows the host's command). Tests:
  `gfx/tests/t_core_store_index.c` (35 checks on synthetic streams, the backend faked; a set-before-the-end attachment counting as set again,
  `-DDM_TEST_SDX_ANY_RTSEQ`, fails 2), `t_core_prog.c` (11; the whole program replaced, `-DDM_TEST_PROG_BIND_WHOLE`, fails 6),
  `tools/tests/t_core.py` (6: the files compile as plain C and include no framework or backend part, both Makefiles take them, each module
  has its test; a module importing Metal, `T_CORE_BROKEN=1`, fails 2); `t_red_once.py` reads `core/`. The backend's warnings are HEAD's.
  Batch tests as in batch 87. Smoke: the same red lines and frames (manosaba's title by eye: particles and the fade's timing); the store
  statistics of manosaba's log moved within what equivalent builds show run to run (b85 → b86: 5 of 13 lines).
- [x] batch 89 · 264, uniform snapshots: `gfx/nvn/core/usnap.c` takes `mtl/usnap.inc` (the walk, the deferred handles once a submission with
  `bh_fresh` / `g_bh_gen`, the radix sort, the fault-guarded copies) and the snapshot half of `mtl/capture.inc` (`mtl_sub_snapshot`'s body as
  `us_snapshot`, `us_find`, the GPU-write notes and drops, `us_report`); its state is `g_us` (`g_us.ent`, `.n`, `.gw`, the counters), and the
  merge, the running-write filter and a copy's place in the arena are shared helpers that the capture now calls too (`us_merge`,
  `us_gw_keep`, `us_gw_reaches`, `us_add`, `us_cap_copy`). The backend provides whether a range lies in one pool, the arena, worker threads
  for the copies in parts (`dispatch_apply_f`; core has no Grand Central Dispatch) and the completed fence; `us_drop` stays the backend's
  (it asks for the arguments to be encoded again). `core.h` gains `core_now_ns` (the uptime clock on Apple's systems, monotonic elsewhere).
  `gfx/tests/t_core_usnap.c` (31 checks with the backend faked, pools of host memory and a page made unreadable; the last copy found taken
  unchecked, `-DDM_TEST_US_LAST_UNCHECKED`, fails 7); `t_usnap.m` / `t_ussort.m` read `g_us.*`, and their broken variants that now live in
  core still fail as written (`DM_TEST_US_GUARD_EACH` 1, `DM_TEST_US_LAST_UNCHECKED` 9, `t_bindfreeze`'s `DM_TEST_BH_EACH_READ` 2).
  Batch tests as in batch 88 (host `t_sleep`'s short-sleep count failed once in the batch, as twice before, and passed three runs alone).
  Smoke: the same red lines; Tears of the Kingdom's frames the same scenes with the cutscene further on (looked at), its snapshot
  statistics the same at frames 300 and 600 and within timing after, each snapshot 2-10 µs slower (0.020-0.103 against 0.018-0.099 ms);
  manosaba's shots as batch 88's.
- [x] batch 90 · 264, the TBDR switches and the validity ledger: `core.h` / `core.c` take the pass dump (`PD`, `passdump` inline,
  `mtl_passdump_next`) with the frame number `g_cur_frame` and `g_stat_pass` / `g_stat_draw`; `gfx/nvn/core/tbdr.c` takes from
  `mtl/tbdr_xv.inc` the switches (`tbdr_on` / `tbdr_part` inline over `g_tbdr` / `g_tbdr_off`, the `TBP_*` parts, the ruler's
  `g_ruler_truth`, `udc_on`) and the ledger (its state `g_xv.*`, `xv_*`), and from `mtl/store_index.inc` the ledger's check against the
  index (`xv_forward`). The part keeps the store decisions' attachments, load elision, continuation and layered passes. Fixed on the way:
  `DM_NVN_TBDR_OFF=all` set the mask to ~0, which is the "not read yet" -1, so every call read the variable again and logged it; it is the 12
  parts' mask now. `gfx/tests/t_core_tbdr.c` (30 checks: the switches, the ledger with `DM_XV_FILE` read back, `xv_forward` against a faked
  submission; a full clear of another level counting, `-DDM_TEST_XV_ANY_LEVEL`, fails 1); `t_xv.m` reads `g_xv.*`. The pass dump of
  `t_rt_continue` and `t_defer_chain` (`DM_NVN_PASSDUMP=0-1000000`) is batch 89's line for line but for two command buffer addresses. Batch
  tests as in batch 88; smoke: the same red lines and frames.
