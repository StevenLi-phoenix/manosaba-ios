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

Check the result: `make -C host/tests run`, `make -C gfx/tests run`, `make -C gfx/tests run-m3`, `make -C gfx/tex/tests run`,
`verify/regress.sh --fuzz`, then a Mac run (README, Getting started) and `ios/run.sh build`.

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

1. **The game's own memory leak.** Every save load builds a new set of Naninovel's `ScriptManager` and `ResourceProviderManager` (exactly one
   more of each per load, `DM_HEAP_WATCH`); the old sets stay referenced with their parsed scripts and UI objects (up to 12 copies of one
   script after 9 reloads). The collector's live data grows about 20 MB per load, the heap about 28 MB; `GC_FREE_SPACE_DIVISOR=8` saves 35–50 MB
   but not the rate. The Switch version is reported to end with errors in long sessions too (2.0.4's notes: "less likely"). The root reference
   was not found: the reference tracer sampled too sparsely, attributing a reference to an object was unreliable at object boundaries, and
   stacks were not scanned. Next: trace all old instances of the two managers (not samples), check each object's extent with its class's
   `instance_size` (`host/hle_svc.c` finds the offset at start), scan the guest stacks too; once the root is known, clear it from a
   `games/manosaba` hook after a load.
2. **Trial loading peak on 8 GB iPads.** Without the increased memory limit entitlement (a free account cannot sign it) an iPad mini 7 lets the
   app use about 3.35–3.4 GB. A signer whose profile has the entitlement (item 3) may lift that; not tried on the iPad mini yet. A trial's start adds about 1.4 GB within 0.2 s (about 116 character render targets, about 0.9 GB of Metal memory, of which the
   D32FS8 depth targets are about 0.44 GB); a fresh session keeps about 725 MB of headroom, a session of about two hours does not (the leak
   above). Render resolution does not help (the game fixes the target sizes). Memoryless depth would save most of the depth memory but the
   depth targets cross render passes that the layer splits at uniform buffer updates, so it needs pass merging first. Refusing guest memory
   does not help either: with the physical memory pool 700 MB smaller the game coped, 1000 MB smaller it broke.
3. **Unsigned IPA** (`ios/run.sh ipa`, `DM_IPA_DATA=1`): signed and installed with an on-device signer (LCSign) on an iPad Pro M4 (iOS 18.5)
   on 2026-10-16; the game data was read from the app bundle, no red lines. LCSign signs with every entitlement of its profile, whatever the
   IPA carries (read from the signature of its output: `increased-memory-limit` and `extended-virtual-addressing` true, `get-task-allow`
   false): the app ran without `get-task-allow` (the code remapping needs no debugger entitlement), and the memory readout showed 7.2 GB of
   headroom at 1 GB resident (how much of that is the entitlement and how much the device is not separated: the iPad mini was never signed
   this way). So an IPA carrying entitlements of its own (mush8's way) matters only for signers that read them (AltStore); not
   tried. Not tested: the iOS 18 build on iOS 26.
4. Smaller: 13 NVN functions the game looks up that the layer does not know (returned as NULL, never called); compute dispatch never seen at
   run time; keyboard, mouse and controller input untested with this game; Metal HUD on iOS 18; generic upstream changes not ported yet
   (present pacing to the screen refresh, Mac occlusion, audio notifier QoS, `hle_fs` robustness).

Devices on 2026-10-16: iPhone 17 Pro (iOS 26.4) runs a build from before 1f54dad (the copy encoder fix), iPad mini 7 (iOS 18.6.2) a build of
68c048b, iPad Pro M4 (iOS 18.5) the unsigned IPA above (build 1792140837) with the iPad mini's save of 2026-10-16 17:10.

## Next game: Tears of the Kingdom (started 2026-10-17)

`games/totk` (1.4.3, program 0100F2C0115B6000, main build 277178b7dba1b6d4949a84778d4abc58), data from your NSPs with
`tools/game_extract.sh` into `work/data/switch/totk-1.4.3`. SDK 20.5.22; its `sdk` and `subsdk0` have the same build IDs as nxnative's Breath of
the Wild 1.9.0, so the system layer is the same binary and most host work could be taken from nxnative.

**Rewrite: done, `verify/regress.sh --fuzz` OK for all three modules** (main: 129 windows, 5 sections in pieces, 6 explicit jump tables with 49
targets, 17 equally spaced tables, 8 `blr x18`; manosaba's equivalence numbers are unchanged). What `tools/patch_nso.py` learned:

- `blr x18` whose x18 was loaded in an earlier block: the K18 slot holds it wherever it was loaded (nxnative's blr rewrite has no such check).
- Jump tables with explicit targets (`adr xA, anchor; ldrsw / ldrh / ldrb xT, [xB, xI, lsl #s]; add xD, xA, xT[, lsl #k]; br xD`): the targets
  are derived over the function's control flow (table base and anchor from every reaching definition, the index bound from a compare + branch
  on every path, other tables' targets as edges, iterated to a fixpoint and re-derived strictly), then a compare chain with landing pads as
  nxnative's `site_br_tab`, comparing with `sub` + `cbz` so NZCV stays as `br` leaves it. An index outside the table stops at `brk #0x18`.
  New `sites.tsv` row `JT`.
- Exclusive sections by control flow (`exclusive_regions`): every instruction on a path from an exclusive load to a store (not through another
  exclusive load, a call or `clrex`), plus the status branch. Contiguous sections entered at their load are windows as before (manosaba's 17
  come out identical); TotK's compiler moves compare-and-swap retries into cold blocks that jump back to the main path's `stxr`, giving sections
  in pieces, copied by `site_region` (row `R`: entry, then `lo-hi@copy` per piece). Any other entry stops the rewrite.
- `mrs xN, ctr_el0` (SIGILL on Apple systems, read by `nn::os::FlushDataCache`) → `movz xN, #4, lsl #16` in place, as nxnative (row `ctr`; 64-byte
  lines, at most the real 128). This also changes two words of manosaba's `sdk`, a latent SIGILL there.
- Verifiers: `structck` ⑪ checks windows, sections and explicit tables word by word against the original and enforces the single entry
  independently, ⑫ the `ctr` constants; `equiv` runs sections in pieces from each piece (the cold retry block is reached only through the status
  branch that `deexcl` removes), draws explicit table targets, and does not compare a trial whose original instruction reads below sp (the rewrite
  blocks' red zone; one such draw in TotK main); `fnfuzz` gives both images the `ctr` constant. Seven deliberately broken products were all
  caught by `structck`, five also by `equiv`.

**First runs on the Mac (headless, empty shader map):** the game runs its main loop at 30 fps, renders and captures shaders (6202 so far), its
audio renders with sound, saves and cache storage work. Host additions on the way, most from nxnative (credited in the code):
oe (`GetPreviousProgramIndex` = -1 per switchbrew, launch parameter, startup logo, `PopNotificationMessage`, volume balance, ARUID), the NVN API
version as a profile field `device_api` (TotK needs 55.15; manosaba keeps 53.311), hid mouse / vibration batch, the in-process audio renderer
(`host/hle_audren.c`, with host guest threads `host/hle_guestthread.c` and `dm_pcmout_*`), the hardware Opus C API on the SDK's software decoder,
fs read priority / access log, indexed cache storage (`$DM_CACHE`, from the SDK reference), another application's save not existing (TotK looks
for BotW's), the user exception context area (svc GetInfo 20), play reports, amiibo (`host/hle_nfp.c`), clock snapshots and the steady / network
clocks (layout and service semantics from Ryujinx), NVN functions (`MapVirtual`, `ProgramSetShadersExt`, `BindTexture`, deferred texture binds,
`GetCommandMemoryUsed`, ...; 13 names added to `gfx/nvn/names.txt`, as nxnative's table), and command memory: a disabled callback or one that
cannot supply enough continues in host segments that grow on demand and are reclaimed when the game gives the recording's memory out again.

**With its shaders (2026-10-18):** the title screen draws as on the console (the lit stone mural, the green wisps, the logo and the menu text;
compared with a console screenshot), and the save selection after "Continue" shows its text over the blurred title, as on the console. Its
background is not drawn live: `TitleMenuBG_00` (layout archive `Title.Product.140`) has a capture pane (`C_Capture_00`, R10G10B10A2,
multi-filter `SystemWindow_00`) that copies the framebuffer once when the screen opens, and `LoadList.module` hides the `Main` layer. The
title's camera loop fades to black for about a second (frames 900–970 in our runs), so pressing A then captures only the logo and the menu
(what `DM_KEYS="A@48"` did; `A@26.5` captures the lit mural). The black thumbnail of the old save was written while the mural still drew
black. What it took, in the order found:

- Core numbers (`host/hle_thread.c`): the job system picks a per-worker output block by `nn::os::GetTlsValue` = current core + 1, and the host
  answered core 0 for every thread, so ModuleSystemWorker0..2 and the main thread filled one block at once (the crash at main+0x84f5c, an
  object pointer overwritten by a neighbour). `SetThreadCoreMask` / `GetThreadCoreMask` / `GetCurrentProcessorNumber` / `CreateThread` now follow
  the Horizon kernel (Atmosphère mesosphere: -1 don't care, -2 the process's ideal core, -3 keep it and it must be in the mask; the running core
  moves into the mask), with the process's ideal core and core mask from main.npdm (core 0, cores 0..2 in both games). Found with a new
  hardware store watchpoint (`DM_WATCHPOINT`, `host/diag.c`) and crash-time memory dumps (`DM_CRASH_MEM`, `host/crash.c`).
- NVN: `DrawArraysInstanced`, timestamps in ns, render enable / rasterizer discard / tiled cache logged or no-ops (nxnative); Metal layer entries
  (`Read|WriteTexels`, `QueueFinish`, `SubmitCommands`, `PresentTexture`) take one recursive lock (TotK writes texels from worker threads).
- Shader build per game: `game.json` `nvn_c2` (where the shaders read texture / sampler / image handles in c[2]: textures at 0x20 for TotK as for
  nxnative's games, 0x168 for manosaba; it reached 2090 shaders with unbound textures) and `depth_mode` (TotK sets `near_is_minus_w`: the
  translator's own z' = z / 2 + w / 2 before every vertex return; without it every full-screen pass at z = -w was clipped). Both travel in the
  translator job, the cache key and the reflection, and the host refuses a reflection built for another layout. manosaba's 2740 shaders come out
  byte-identical (MSL and reflection) under the new chain. `dmsh_build.py` needs `DM_GAME`, and the translator needs `DOTNET_ROOT` when .NET
  comes from Homebrew (`/opt/homebrew/opt/dotnet/libexec`).
- Replay: 8-bit index buffers widened to 16-bit (most of TotK's quads; they had been skipped), vertex format RGB10A2SN, texture view swizzle
  (a set view property replaces the texture's; `view_t` stays 10 bytes, because a 12-byte one overwrote the game's fields after its views),
  views whose format or level / layer range cannot be a Metal view of the texture are sampled as the texture (they had aborted the process),
  compressed render targets are left unattached (BC4 written through an RGBA16UI view, as nxnative found in BotW).

The title picture was wrong for three separate reasons, each found by following the data from the screen back to its source (pass dumps,
snapshots after each pass, then the texture and memory behind the bad value):

- **Red background: the wrong shader code.** Shader keys hashed the 0x880-byte control section only, which holds sizes and resource counts but
  no hash of the code. 807 of the game's 3667 captured shaders share their control section with another one (313 sections); the capture kept
  the first code seen per key and every other shader under that key ran it. The bloom upsampler (`sample × c[3][0x60]`) ran a shader that adds
  `c[3][0x20]`, which held a matrix row (2, 0, 0, 0) of an earlier pass: red. The key is now sha256(control section + code area) in the host,
  the capture and the tools (`shader_key` in `hle_nvn.c`; nxnative had made the same change for Mario, whose control sections collide too).
  Both games were rebuilt: manosaba's 2740 shaders map one to one (MSL byte-identical, only key names change); TotK's captures were renamed and
  recaptured (3706 shaders). The uniform-buffer experiments on the way (per-binding copies instead of memory semantics) only hid this: they
  broke manosaba and were reverted; NVN's documentation and Ryujinx both give memory semantics in command order, which the host keeps.
- **Black mural: assets never uploaded.** The game gives no packaged layout to its asset textures (26 `SetPackagedTextureLayout` against 757
  registered textures); it writes their block-linear data into CPU-cached pools and flushes the ranges. Only packaged textures were uploaded
  from the pool, so the mural's albedo was black in Metal while its storage was 90-99 % filled. As in LASRecomp (a texture is uploaded from
  guest memory until it is rendered into), a texture in a CPU-visible, non-compressible pool that the GPU has not written is uploaded once the
  CPU has filled it (non-zero storage at first use, or dirty pages later), created render-target capable; the first GPU write (render target,
  copy / clear destination, image store) makes that MTLTexture the GPU's (`tex_gpu_write`). Linear textures in CPU-visible pools are made on
  the pool buffer (`tex_linear`, from nxnative): that is the glyph cache (IMAGE | LINEAR), and the menu text appeared with it.
- **Black lighting: every conditional clear ran.** The lighting buffers are cleared once per frame with `SetRenderEnableConditional(not
  equal, X)`, the clear, then `ReportCounter(TIMESTAMP_TOP, X)`. NVN documents that TIMESTAMP and TIMESTAMP_TOP reports write 0 as the
  counter value, so the next conditional clear of the frame compares X+0 = 0 with X+16 = 0 and is skipped, and the second lighting pass adds
  to the first. The host wrote the timestamp into the counter half and did not evaluate conditions at all, so the second pass cleared the
  first pass's light. Counter reports other than samples passed now write their counter half (0) in command order at replay; conditions are
  evaluated at replay from guest memory (rendering stays on while either value is a samples-passed result still on the GPU); the store,
  clear-hoisting, fusion and deferral look-aheads treat a render enable change conservatively.

Also ported on the way: one attachment per texture level / layer attached at several color slots (`rt_dup_slot`, nxnative / tomodachi-native),
inline uniform data copied into a host ring at replay (nxnative), `BindProgram` keeping the program's shaders as recorded, `SetRenderEnable`,
vertex shaders padded for the pixel shaders the game pairs them with (`vs_pad`, pairs written by the host while capturing; link failures
45 → 2).

**The prologue (2026-10-18):** an empty save starts it without input (corridor with Zelda's torch, her dialogue, then the controllable cave
with the HUD; the first autosave at about 88 s carries a correct thumbnail). Zelda was mostly missing (her torch and a flat-shaded head only)
because the run-time capture only had the title's shaders: 87 more were captured in the prologue (3706 → 3793; some appear only in some runs,
so it took two capture runs), and every "not in the shader map" line went away. 7 of them are vertex shaders doing DMUL / DFMA on uniform data,
and Metal has no `double`: `dmsh_xlate` now answers `QueryHostSupportsShaderFloat64` false, so Ryujinx's own `DoubleToFloat` pass runs them
in float with IEEE double ↔ float helpers for the register pairs (chain `ae97fd60e78b`; all 3793 TotK shaders build, manosaba's 2740 and the
other TotK shaders come out byte-identical in MSL and reflection).

**30 fps, the shaders from the RomFS, MacNativeGames' fixes (2026-10-18):**

- **20 fps was the game's own choice.** TotK's frame-rate controller (main 0x2db174, called from 0x2daa20) sets present interval
  clamp(int(max(CPU, GPU) × 60 + bias) + 1, 2, 3). Its GPU time is `nn::os::ConvertToTick(nvnDeviceGetTimestampInNanoseconds(last report))`
  minus the frame's start tick from `nn::os::GetSystemTick` (main 0x2b3cff8, stored at sys+0x128, the larger of the two at sys+0x118). Our
  counter timestamps were `CLOCK_MONOTONIC`, which counts the Mac's sleep; the guest's system tick is `mach_absolute_time`, which does not. After
  25 hours of sleep the "GPU time" was 25 hours and the game stayed at interval 3 with the process idle 99 % of the time (a `sample` of the
  cave). Counter timestamps and `nvnDeviceGetCurrentTimestampInNanoseconds` now run on the guest clock (`guest_ns`, the system tick's source);
  the game keeps interval 2 (frame time p50 33.3 ms) and its GPU time reads 5-14 ms. Found with a backtrace on every present interval change
  (kept in `nvnWindowSetPresentInterval`) and a hardware watchpoint on sys+0x118 (`DM_WATCHPOINT`).
- **Shaders from the RomFS:** `engines/bnsh/shader_extract.py` reads the BNSH files the game ships, as MacNativeGames' prepare_shader_seed.py
  does (`Pack/Bootup`, `Lib/**`, `Shader/**`: 8290 shaders, its count for 1.4.3), plus the effect sets `Effect/**.esetb.byml.zs`, whose VFXB
  carries two BNSH files (GRSN graphics, GRSC compute; not in MacNativeGames' set), and 0x88c-byte control sections taken as their first 0x880
  bytes (`Shader/cave_quadmesh`, which MacNativeGames skips: 30 vertex shader records, 21 distinct, all captured at run time under those bytes): 24371
  shaders and 19669 vertex / pixel pairs. 3451 of the 3793 run-time captures are among them, all byte-identical, and all 2272 run-time pairs of
  those are BNSH program pairs. The other 342 come from agl / gsys / Terrain shader archives (`Lib/**.sharcb`) and from main, whose code areas
  are not next to their control sections (not parsed: they stay run-time captures). The extractor writes into the capture directory
  (`work/build/totk/shader_capture`, 24713 shaders) and adds only what is missing.
- **From MacNativeGames** ([ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames), MIT, reviewed at
  41e1e0caf43d58c4af3c478ca2d1bafd9dba7093; ported by hand, credited in the code):
  - a texture registration without a view clears the slot's old view, and only a registered view changes format / levels / layers / target
    (`tex_by_id` read the old view whenever the texture had a swizzle);
  - clears inside a pass, with a partial channel mask, under a partial scissor or with a partial stencil mask are drawn as one full-target
    triangle (`clear_quad` / `clear_ds_quad`); they had been dropped, and a scissored depth clear or a partial stencil mask had cleared the whole
    plane. The store look-ahead and the clear-pass hoist now count a clear only under a full scissor;
  - BC render targets written through a view of one texel per block (BC4 through RGBA16UI) render into an auxiliary texture loaded with the old
    blocks and copied back through a buffer when the pass ends (`blk_aux`); they had been left out;
  - storage buffers whose address the shader reads from a uniform buffer (21 vertex shaders): `dmsh_build.py` kind `sbuf_ubo` (uniform slot
    = cb slot − 3, byte offset), reflection kind `NVN_RB_SBUFU`, read at bind time with the inline uniform updates recorded so far in the
    submission applied (`pend_read`); argument blocks of those shaders are not reused;
  - the storage buffer write flag is Ryujinx's `BufferUsageFlags.Write` = bit 0 (we tested bit 1, so none was writable).
- **Also found in the prologue:** the BC4 encoder's pixel shader writes an RGBA16UI target, but shaders were built with float outputs (pipeline
  creation failed). When a pipeline fails, the host writes the integer colour attachments the pixel shader writes to `<DM_SHDUMP>/fout.tsv`
  ("key target uint|sint"); `dmsh_build.py` passes them to the translator's fragment output types (Ryujinx answers them from the render target
  formats the same way) and keys the build on them. A draw with no colour and no depth target (1x1 scissor) had been dropped ("none of the
  render targets can create an MTLTexture"): it now gets a Metal pass without attachments, sized by the scissor.
  Chain `530a8546d06c`. Tests: `t_nvn` (timestamps on the guest clock, scissored / masked clears, BC4 render target), `t_refl` (new),
  `t_ub` (`pend_read`), `t_mip_extent` (the scissored clear now writes its two texels; a pass without attachments); each fails with the old
  code.

**Playing it in a window (2026-10-18):** the window front end presented one frame per display tick (60 fps) whatever the game's
interval, and the game's step is per frame, so it ran twice as fast; with game.json `present_interval` "game" the display-link wait takes as many
ticks as the game's interval (30 fps). game.json `window_unfocused` "in_focus" keeps the game in focus while its window is visible but not key
(OutOfFocus is the console's HOME menu: the game stopped whenever the player clicked another window). Frames alternated between two states
(Zelda's cloak and hood, the cave walls, torn models): found with consecutive-frame captures, present already in the morning's build.
- Counter reports wrote their timestamp at replay, before the GPU ran them; the timestamp now appears only at completion (the counter half
  stays in command order for conditional rendering). The large alternations (walls, whole objects) went away (16-frame captures, 14/14 → 0/14
  alternating triples at frames 1500-1515). `t_nvn` checks that the timestamp is not there while the GPU is busy.
- Occlusion counting that starts inside a pass without a visibility buffer splits the pass (it counted nothing; `t_reorder_chain`).
- Virtual cores (game.json `virtual_cores`, host `hle_thread.c`, from MacNativeGames' runtime/threads.c): a guest thread holds one of three
  cores while it runs guest code and gives it up in waits, sleep, yield, exit and blocking NVN calls; TotK keys per-core data by
  GetCurrentProcessorNumber without locks and its main thread shares core 0 with ModuleSystemWorker0. Host-work threads
  (`dm_guest_thread_spawn`, the audio renderer) hold none: the audio renderer kept core 0 and the boot hung.
- What was left, the cloak and the face (every other frame Zelda lost her cloak or her face, or the cloak changed shape): cached command
  handles lost their tails. Tears of the Kingdom records the binds of each draw once per frame parity, with the memory callback off, into a
  512-byte window at slot + GetCommandMemoryUsed of the previous recording, and calls those handles every frame until it records them again.
  Three faults in our command memory combined:
  - its NVN 55 inline wrappers (775 sites in main, gated by the SDK's `nvnPrivate0`, a .bss word that stays 0 here) store a dirty byte at
    command buffer +0x10 before every call, and +0x10 was our `end`: windows ended up to 255 bytes early (…8b0 → …801), so recordings that
    fit went on in host segments (1336 by frame 1300; 4 now);
  - a host segment was reclaimed when the game gave out memory overlapping its recording, but the 512-byte windows reach into the next slots'
    live recordings: a later recording took the segment and its binds became the tail of a cached handle. In even frames Zelda's G-buffer
    draws bound another model's skeleton buffer while the shadow pass bound hers;
  - used did not count the JUMP into a host segment: with only END there, the game's next slot began 8 bytes before the JUMP's end.

  Now `cmd_t` leaves +0x10 to the game (`guest_flags`), a host segment lives as long as the JUMP that links it (in game memory inside a live
  pool, or in a live host segment) and is reused once that JUMP is gone, and the JUMP counts as used. Over a run: 1.28 million CallCommands,
  none to a handle a later recording had written over; 16-frame captures at frames 1290, 1790 and 2090 have no pixels alternating (per pixel
  |N − (N+1)| against |N − (N+2)|; 2.4–6.2 % of the pixels before), and every skinned mesh binds the same skeleton in both passes. Found with
  the pass dump's op trace (`DM_NVN_PASSDUMP_ALL`, `o` lines): the even frames' bind handles jumped into host segments holding another draw's
  binds. The earlier note here that the two frames' draw lists differed was wrong: same draws, different bindings. MacNativeGames avoids the
  same with another design (records in host memory, copies owned by control memory, usage reported as the console encodings would take).
  `t_nvn` 5a checks the dirty byte, the JUMP in used, a host segment surviving an overlapping window and its reuse once its JUMP is
  overwritten; each fails with the old code.

**The first cutscene (2026-10-18):** after the tutorial the game plays its first pre-rendered cutscene, `Event/Movie/DmT_OP_GanonWakeUp_PreRender.webm`,
and crashed: `Extractor::SetDataSource(path)` was not implemented (only the stream-reader source Unity uses), and the game went on to call
`nn::os::LockMutex` on +0x178 of a null object (main+0x2b45c68 → sdk+0x13f5b8). The 28 movies in `Event/Movie` are WebM with one VP9 track (1920x1080, 30 fps,
no sound track: the game plays the cutscene's sound itself). Now:
- `SetDataSource(path)` opens the guest path in place through the fs mount table (`dm_fs_host_file`), and `movie_av.m` hands EBML files to a
  WebM / Matroska demuxer (`host/movie_webm.c`, element layout from RFC 9559 and RFC 8794; every block is indexed when the file is opened and
  read from the mapped file);
- a VP8 / VP9 decoder decodes the bytes the game sends with libvpx, with MacNativeGames' behaviour and pacing (NV12 output described as
  MediaCodec does, the format change with the first frame, at most one frame per game frame on the guest clock, late frames dropped when the game
  runs below the movie's rate so the picture keeps time with the sound). The game ignores both outputs of GetOutputBufferSize (main+0x2b44d68);
  the second is the alignment (SDK docs), 256. Status codes follow the SDK's `movie/Status.h`. libvpx comes from Homebrew and is linked
  statically when installed (`host/Makefile`); the iOS app has no VP9 decoder yet;
- `t_movie` (host tests) encodes VP9 with libvpx, muxes a WebM with a Void element, a BlockGroup with a ReferenceBlock and an unknown-sized
  Cluster, and checks extraction (bytes, times, sync flags, seek), decoding (NV12 values exact, events, end of stream, output format) and pacing
  (one frame per game frame at 30 fps; at 10 fps late frames are dropped and the picture keeps time); each fails with its fault put back.
  `t_movie <file>` probes a real movie: all 6421 frames of the Ganon cutscene decode without an error, 1.0 ms per frame;
- the player draws with an NV12 → RGB shader (`nv12decode`) from agl's `agl_common.sharcb`, which the extractor did not read:
  `engines/bnsh/shader_extract.py` now reads the sharcb archives (12 in TotK: agl, gsys, Terrain, application; layout in the script),
  2478 more shaders, among them 141 that had come only from run-time captures (all byte-identical) and 20 of the 27 that were missing in the
  first play session (all 27 were captured by `DM_SHDUMP` there). The capture holds 27077 shaders and the library was rebuilt from it: 27075
  translate; SPIRV-Cross fails on two of the new ones (a vertex shader of `AglShader.sharcb`, "nullptr", and a compute shader of
  `agl_technique_pfx.sharcb` using subgroup builtins where SPIRV-Cross does not allow them), neither of which has run so far. `c1_guard`: 49
  shaders whose c1 index is not proven bounded (47 before).

**After the movie (2026-10-19):** with the picture there, the game stayed black after the Ganon movie's last frame (30 fps, two draws a frame,
for good). Its player (main+0x2b43xxx) sends every input buffer it can take back with the end-of-stream flag once the extractor is at the end
(main+0x2b45338), and its decoder thread waits with `WaitAny` on holders linked quit, input, output, format (main+0x2b44690). We handed each
end-of-stream input straight back with input available, so input was always signalled, `WaitAny` always returned the input holder and the
output handler (main+0x2b45564), which takes the end-of-stream output and sets the player's end flag (+0x708, read by main+0x2b43ba8), never
ran: a `sample` of the stuck process had 2377 samples in the input handler and none in the output handler. The library's codec does not hand
input back after the end: ACodec's `postFillThisBuffer` (subsdk0 0x2bd214) returns at once while the input port is at end of stream (+0x248, set
in `onInputBufferFilled`). Now the end-of-stream input and every input after it stay in the decoder until Flush / Reset / Configure (MacNativeGames
hands them back as we did). `t_movie` (31 checks): after the end of stream, inputs sent back stay, no input event, Flush gives all four back; the
first fails with the old code. Played: the Ganon movie and the first blood moon movie end and the game goes on.

**60 / 90 fps (2026-10-19):** from MacNativeGames' `game_patches_v143.c` (MIT; their seven controller words and their real-time step method),
in our rules and host:
- `games/totk/rules.py` CUTS: the seven words of the frame-rate controller (main 0x2db174) that lower its minimum interval from 2 to 1. The
  controller still raises the interval to 2 or 3 when frames do not fit.
- Real-time step: the game's step was 60 / (the controller's fps) at its two producers (0x2daa54: ratio, stored at 0x3a22128; 0x3e5638: ticks
  before a 0.5 / 1 selector). New rewriter rule kind `HOSTREFS` (`tools/patch_nso.py`): instructions that address a 16 KiB writable host page
  after the key page (fixups `HOST_PAGE_OFF`, only in modules that use it; the others keep their layout byte for byte), resolved into cuts so
  structck / equiv check them as cuts. The producers now load ratio (+0 = seconds × 30) and ticks (+4 = seconds × 60) from main's page;
  MacNativeGames puts a veneer at 0x3e5658, we do the same in place (x9 free between 0x3e562c and 0x3e5644). `games/totk/frame_step.c` writes the
  mean of the last five present intervals on the guest clock (each 1/120 .. 1/15 s; above 0.25 s or below 1 ms restarts) at every present
  (new game hook `dm_game_present`); `DM_FRAME_STEP_MS` pins it. The loader reads `HOST_PAGE_OFF` (`dm_host_page`).
- `DM_PRESENT_HZ=30..120`: the display link (mode 1) now ticks at the present rate, so one present interval is 1 / that rate.
- `games/totk/check_frame_rate.py <product> <main NSO>` (17 checks): the controller run in Unicorn reaches interval 1 on 10 ms frames, 2 on 25 ms,
  3 on 40 ms and back, sys+0x250 = 60 / interval; the original main never picks 1 (negative control); both producers read the page (the original
  computes 60 / 30 and ignores it); `tests/t_frame_step.c` (the mean, the clamps, the page layout). `verify/regress.sh` OK (main cuts 16).
- The product with these rules is `work/build/totk/product60` (`host/dmhost work/build/totk/product60`); the default product was left at 30 fps
  while the blood moon crash below is open. A 75 s prologue run: interval 1, frame time p50 16.8 ms.

**Flicker in the dive from the Great Sky Island (2026-10-19):** many objects flickered while falling from the first cliff, and some frames were
wrong over the whole screen (rock walls in front of the camera, a flat ground). Repro: a manual save at the cliff edge, the dive scripted with
`DM_KEYS="A@26.5,A@31,A@35,LSU@f1380+240,X@f1400"`, and `DM_NVN_LIVEROWS=<file>` (16 rows of every presented picture; a present that differs
from both neighbours more than they differ from each other is a wrong frame): wrong frames at presents ~1719 / 1893 / 1913 in two of four runs.
- Ruled out by measurement, with probes since removed: occlusion queries (every samples-passed report forced large: still wrong frames; the
  occlusion table that replaced a 256-entry ring stays, it is right but was not the cause), CALLs of re-recorded command memory (each recording's
  hash checked at every CALL: 0 mismatches), sync waits that time out (none), GPU lag across frames (the previous frame had completed at every
  submit), the present waiting for the GPU (wrong frames with it on).
- Cause: `DM_NVN_GPUWATCH=<first>-<last>` copies the uniform ranges a submission binds at its commit and compares them when it completes: in the
  dive 1041 (frame 1814), 662 (1817), 128 (1895) and 197 (1915) ranges changed meanwhile, 0 in the other frames, all vertex stage slot 4, 256
  bytes (the objects' transforms). Once a frame is submitted the game's workers rebuild those per-object constant pools; on the console the GPU
  has read them by then, here the submit replays for 11-17 ms first. MacNativeGames found the same (main+0x87a604 from main+0x874f50, "rock walls
  while jumping"; their `usnap`). (A first, page-granular version of the watch also flagged pages that main+0x8cbbf4 writes next to the ranges.)
- Fix (method from MacNativeGames' usnap, MIT, written anew in `gfx/nvn/nvn_mtl.m` `mtl_sub_snapshot`): when a submission starts, every uniform
  range it binds is copied into its argument arena (ranges up to 256 bytes apart in one pool merged, at most 1 MiB, each at its offset modulo
  256); uniform bindings (graphics and compute) and replay-time reads (`pend_read`, `ubo_float`) use the copy. A GPU write within the submission
  (inline update, buffer copy, texture-to-buffer copy, a writable storage buffer, the storage buffer table c0) drops the copies it reaches from
  then on; a range a GPU write of an earlier submission still running reaches is not copied. `DM_NVN_UBO_SNAPSHOT=0` reads live memory. Cost in
  the dive: ~12,200 bindings in ~1,200 copies, 2.4 MB, 0.9 ms per submission on the submit thread (MacNativeGames copies on helper threads).
- Checks: `gfx/tests/t_usnap.m` (29, also in run-m3): a draw reads the bytes of the submit call although the CPU rewrites them during the replay,
  and the rewritten ones with snapshots off (negative control); after an inline update the draws before it read the copy, those after it the
  update; merging and alignment; a running GPU write keeps its range out. In the game, snapshots off: wrong frames at presents 1719 and 1893, 707
  changed ranges read from memory at frame 1814; three runs with snapshots (one with the present not waiting): no wrong frame, the ranges that
  changed (866 / 529 / 723 at frames 1814 / 1815 / 1817) all read from the copies, none from memory. Frame times unchanged (30 fps, p50 33.3 ms).
- Diagnostics kept: `DM_NVN_GPUWATCH=<first>-<last>` (above), `DM_NVN_LIVEROWS=<file>` (window play: finds single wrong frames without pausing the
  game) and `DM_NVN_LIVESHOT=<first>,<count>:<path with %d>` (those presents in full).
- Lesson: MacNativeGames' `nvn_mtl.m` describes this symptom and its fix in the comment above its `usnap`, and the clone was at hand. Search the
  references for a symptom (its words, the game, the area of code) before building probes; then measure to confirm the cause here.

**Open (2026-10-19):** after skipping the second blood moon movie (`DmT_SY_BloodyMoonDarkZelda`, stopped at frame 233) the game crashed 3 s later in
its resource control thread (0x1006, loop main+0x154bd0, which names `ClearCachesForKeepAllocSize` / `Unload(ForSync)`): main+0xa2ef08 reads the
resource at +0x28 of a binder of type 0xe (+0xc) after waiting for it (`WaitLightEvent` at 0xa2ee2c) and +0x28 is null; registers name
`Obj_TreeWhiteBirch…`. The SDK's `WaitLightEvent` trusts a 0 result of `WaitForAddress`, and our address arbiter only returns 0 from
`SignalToAddress`. Not reproduced yet. The thread dump now marks the thread that writes it, and `DM_CRASH_MEM` takes `*xN` (one dereference).
Also seen: `nn::os::WaitForAddress(long const*, long)` (sdk 0x146fb0) uses arbitration type 3 (64-bit wait if equal), which svc 0x34 treats
as type 0; main and subsdk0 do not import it. The vertex shader `1eee2726aae7493e` (SPIRV-Cross "nullptr") is now drawn with.

Known gaps: two 4x4 textures are re-uploaded every frame (the CPU rewrites their pages; MacNativeGames hashes small textures instead); alpha
test and depth-stencil (stencil) views are not modelled; an integer render target's partial clear is not drawn (red line); GPUs without BC
(iOS) do not get the BC render target path; iOS needs `DM_CACHE` and the
large heap; hid six-axis is a stub. Diagnostics added on the way: `DM_NVN_NOCULL`, `DM_NVN_RTCLEAR=<serial>`,
`DM_NVN_TEXSNAP` (ordered on the replay's encoder, `@WxH` and ranges), `DM_NVN_TEXNZ`, `DM_NVN_TEXCLASS`, `DM_NVN_REUPLOG`; the pass dump prints
cull / depth range / index type, the inline uniform data, the first vertices and the counters seen by conditional rendering. Run after
`make -C host GAME=totk`:
`DM_GAME=totk DM_ROOT=$PWD/work DM_SHDUMP=$PWD/work/build/totk/shader_capture DM_SAVE=… DM_TEMP=… DM_CACHE=… DM_WATCH=120 host/dmhost` (the
stage map and library directory come from the profile; rebuild them with `shmap_from_capture.py` and `DM_GAME=totk dmsh_build.py`).

Upstream PR #55 (x18 scan prefilter, n3crosis) is in `tools/patch_nso.py` (76 s → 16 s for manosaba's main).

## Upstream and other ports to draw from

Reviewed 2026-10-17. Fetch them into a scratch repository, read only, never run their code; port by hand with a test per item, check our
code first (several fixes are already here in another form), and credit the source in the commit and the README.

### Upstream: [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)

Reviewed through 21cf97b91823111ead237858ea79929afacf71a5 (2026-10-17).

- Ported 2026-10-17: buffer ↔ texture copies honour `nvnCommandBufferSetCopyImageStride` (86b1315a84cec67cfef0b3eab125dbc3c4489cc4, SepheTT;
  `gfx/nvn/nvn_copy_pitch.h`, `t_copy_pitch`; manosaba never calls it; d21348c); `UILaunchScreen: {}` instead of an empty `UILaunchStoryboardName`
  (21cf97b91823111ead237858ea79929afacf71a5, BossaMelon; upstream saw iPhones start in the legacy compatibility size, this app already rendered at
  native size on the iPhone 17 Pro with iOS 26 and both iPads; ce466a9).
- Already here in the same form: unresolved weak imports stay NULL (b84657b25735d96a7263a1b4a7402159a17435a5, `host/hle.c`), `DT_RELR`
  (`host/loader.c`). Not applicable: D2R's Mac mouse (098d9f5e24407480da7cf0d21c5d88b871da6949). dc53e7a8eca1f170009b722d390fac7e0d050288
  was reverted by 21cf97b.

### Derivatives of d2r-mobile

All MIT and keep jhaizhou's LICENSE; they use the old `dm/` layout.

- [seedds/nxnative](https://github.com/seedds/nxnative) (main, reviewed through 20cba12c4299d77f05b6bbfd3dfa42d6c6035da4): NSP → IPA; its
  README at that commit lists Super Mario Bros. Wonder, Fire Emblem Engage, Metroid Dread, 13 Sentinels, Super Mario 3D World + Bowser's
  Fury and Breath of the Wild 1.9.0 as tested on an iPhone and playable (iOS 26); the BotW profile's own header still says "in bring-up".
- [Johnoooob/nx-port](https://github.com/Johnoooob/nx-port) (main, single commit 3e6366b86a510361d451618ffd7c45dea5549492, unchanged).
- [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile) (main: only upstream merges since 95d6caa; branch
  face-buttons-by-printed-glyph).
- [4ppp44/d2r-mobile-ios17](https://github.com/4ppp44/d2r-mobile-ios17) (branch codex/ios17-a16-v1.0.17): an experimental iOS 17 / A16
  Metal 3 build (406028335f5f0fa65c5a421867abcde0987d0a9e, b638a1886b16f9a9d5c10eaa118231fb8544dfb9); its author reports one short manual
  test on an iPhone 14 Pro Max with iOS 17.0. For running on iOS 17 devices.
- TommyLau/botw-mobile: gone from GitHub (404 on 2026-10-17). Its commits listed below can no longer be fetched from there; nxnative now
  carries Breath of the Wild.
- [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames) (main, reviewed at 41e1e0caf43d58c4af3c478ca2d1bafd9dba7093,
  2026-10-18): Tears of the Kingdom 1.0.0 / 1.1.2 / 1.4.3 on the Mac, its NVN layer from d2r-mobile's (`games/totk/third_party/d2r_gfx`).
  Ported (see the TotK section): offline BNSH shaders, stale texture views, quad clears, BC render targets, storage buffers addressed from uniform
  buffers, the storage buffer write flag. Not yet: the real-time frame step and the 60 fps controller patch (`game_patches_v143.c`), the 2K canvas
  and MetalFX, uniform rename (inline updates without ending the pass), submission split every 60 / 75 draws, the acquire sync not fenced to the
  newest submission, GPU keep-alive, small-texture hashing, the state filter. With those its 1.4.3 notes give 83.9-110.2 fps averaged over 15
  fixed-camera points on an M4 (docs/v1.4.3-port.md), and about 27-30 fps with the game's own controller.

### Independent: [c22dev/mush8](https://github.com/c22dev/mush8)

main 1b2e9b875f3beb94802624f055e7a041a473ee3c (first commit, 2026-10-16): Mario Kart 8 Deluxe on iOS and macOS with LAN, online
(lan-play) and wireless multiplayer; its own implementation in Rust (`nxpatch` rewriter, `nxhost`, `nxshader` Maxwell → MSL, `nxfs` NSP / NCA
extraction). **GPL-3.0: read it for ideas and behaviour only; no code goes into this MIT repository.** Worth reading: the audio renderer
(`crates/nxhost/src/audio`), touch controls (`gfx/touch.rs`), WebM movies, NSP extraction (`crates/nxfs`), and its IPA, which is ad-hoc
signed with `com.apple.developer.kernel.increased-memory-limit` (`make-ios.sh --ipa`, `ios/Mush8-sideload.entitlements`) for signers that
read the app's own entitlements (AltStore does, see [ios/README.md](ios/README.md)).

### Already ported

Thread exit off the guest stack (014c5b3), crash report stderr trylock (1f1929e), dirty tracking of shared host pages (e4292e1), RomFS name
index (af27ace), save full-disk robustness (eb86cfc, from upstream b94670c), msl_retarget reflections (56fcd40, from upstream 1784563).

On 2026-10-17, from nxnative: address → memory pool lookup by last hit and a sorted index (`gfx/nvn/nvn_pool_find.h`, `t_pool_find`, d6cd8a8…; d48fb4f);
a compatible view when a shader declares another texture type than the bound texture has (`tex_fit`, with the declared type now exact for
multisample textures), 1D textures as one-row 2D, a zero write mask and no blending for colour outputs the fragment shader does not write
(42a50ef…; 621bd6d); `DM_KEYS` timed by presented frames (`A@f3600`, d6cd8a8…; the left stick keys were already here; 9475f85). Each has a unit test with a
broken version that fails it (`t_pool_find`, `t_tex_fit` on the GPU, `t_hid_script`). On the Mac a trial scene loaded with the old and the new
host gave no red lines and equal screenshots after the load; no texture type mismatch happens there, so `tex_fit` has only run in its test.

On 2026-10-17, after nxnative's nx_extract (aa8cc5c…, LibHac 0.19.0): `tools/game_extract` (`prod.keys` + `.nsp` / `.xci` → ExeFS and
the merged RomFS), our own code with XCI input, Horizon split-file folders, only the keys LibHac classes as common, and a check for
personalized tickets added. LibHac's GitHub repository is gone (404); the NuGet package is unlisted but still served, MIT, pinned by content
hash. Checked with manosaba's base and update NSPs: ExeFS and all 667 RomFS files equal to the console's extraction by SHA-256, and
negative controls (base game only differs; a damaged NSP fails the hash check and leaves no output; a ticket marked personalized, a split
folder with a missing part, a key file without the header key and its sources are refused; a split folder of the base NSP gives the same
RomFS; no key value in any output). Not run yet: XCI input (no XCI dump here) and a real personalized ticket.

### Port when a game needs it

- **AudioRenderer** (first-party games): nxnative `dm/host/hle_audren.c` (50a9c65b5f402b796cc7e7f0b011e11b2015f30f, the SDK renderer
  in-process; SDK 20 rendering through the locked path ff74840187d5ca4dda914bb80ffbd86cb0cce96b); nx-port's own PCM16 / DSP-ADPCM mixer
  (3e6366b…); mush8 (ideas only). Ours has AudioOut only.
- **Deferred texture binds** read when the command executes, not when it is recorded (`nvnCommandBufferBindTexturesDeferred`): nxnative
  d6cd8a812a1047dff8f660f604d0a46033752242. Not ported yet: every reader of a bind record in `nvn_mtl.m` (about eight places: replay, lookahead,
  deferral, merging) must refresh the handles first, and manosaba, which looks the name up and gets NULL, would get a function and might take
  another path; wants a game that uses it to check against.
- **Graphics**, nxnative: a texture attached at two colour slots is attached once, cube textures as 6-layer 2D arrays for cube-array views,
  compressed render targets skipped with a red line instead of a Metal abort (d6cd8a812a1047dff8f660f604d0a46033752242; the first touches the
  attachment setup and the pipeline key in several places); a snapshot of
  the attachment for shaders that sample what they write in the same pass, per shader by a profile key, with the device self-check
  `DM_NVN_MAGSCAN` (the `nvn_mtl.m` part of 20cba12c4299d77f05b6bbfd3dfa42d6c6035da4); the submission handed to the GPU every 64 draws for games
  that wait for the GPU every frame (`DM_NVN_CUT`, ff74840…).
- **Small guest stacks**: host work on a per-thread host stack (`HLE_FORCE_BIG`, `hle_bigstack.c`, 42a50effdda418fd03d9e15c569d75a3a466d1d4: a
  save thread with 8 KB; NVN functions other than command recording, d6cd8a8…). Our HLE functions run on the calling guest thread's stack too
  (imports resolve straight to them), and the file system ones keep path buffers on it. Not ported yet: an assembly trampoline that only
  carries register arguments, and it cuts the guest backtrace of crash reports at the switch; wants a game whose small stacks overflow.
- **Diagnostics**: `DM_NVN_KEYDUMP` (every input of one fragment shader's draws) and sampler / swizzle output in PASSDUMP
  (734a286328d18404fd5a67785a1e05d2149c5986).
- **nn::ro .nro modules**: nx-port `dm/host/hle_ro.c` and loader changes (3e6366b…).
- **VP9 movies** with libvpx: nxnative a9e4f18ed17b17bb5cd8eef422464e94a017654b (decoder hooks), 5e84062fad1f07e95f59c1cc6a94918c463d077e
  (libvpx).
- **Shaders** from Nintendo's containers: nxnative `dm/gfx/tools/shader_split.py` (Yaz0 SARCs, BNSH, BAHS in 42a50ef…); older Unity bundles:
  `dm/gfx/tools/unityfs.py` (e3cd2ff8ff0eb84a62280ef70e90c60753e546e6).
- **Tools**: nso_syms.py (aa8cc5c0cd8bd5d09bcacb019f7832df3a164452); nso_fnmap.py, which carries cuts to a new build
  of `main` (b5507f2aac7e1d1f5512de750f5810a550162b04, also for a manosaba update); build_ipa_from_nsp.py
  (0d763ec864344610cc41548a2ad01cd55def3182); DLC packing (e452f52b607bdf6f960eb843f20aeb29b803dec8); the app icon from the game's icon
  (1cd9bfb84b31fad5b413ab20baa977c16600f50a).
- **On-screen gamepad** for games without touch: nxnative `dm/ios/Sources/DmGamepad.m` (dc8a49ad7471d3f0b767032a0b755f87a98c1912,
  d41fdd6ed1643ed7f97099f18ab6fc7360786a1b, 4a0f9acbcf8e5acad01b1d6b5919bfb3e3cd0998; 3D layout with D-pad, right stick and L3 / R3,
  b65c8b2cfe43d0ccbc2dcd24068d849ff4eb6baf).
- **NVN / graphics** from botw-mobile (no longer on GitHub): depth and stencil sampling modes (b795ac15d620f42986df3d594e846bf03f83f1ea),
  mip import before partial texel writes (549fb9d7be4362e9b82138a674a5305a722ab1a5), native sampler defaults
  (767a276615a6c1c1d20a7b98aec17dd170d8e3b3), declining fast-clear hints (0d80bff4eec2668441ff4ddd075796e3ec674099), GPU timestamps
  (9eebd8d746621b851189a7b4679489388911e7b7), dirty tracking with one record per host page (1d69c8ae9cea2e83584314f8044b55c839c95c9e); nxnative
  GPU timestamps on the guest clock (b6b3da202abbd9ca73f45057920c7a9fdfcc0eb6), depth/stencil clear mid-pass
  (7058f43497c6eb26eea351ec4e45db3bf0d417ae), Downsample as a Metal resolve (13ff4f51a10ff6c4207a1e909e430ddd9c0644d3), per-game present
  interval (9745da2061b09fea0ea524c7d5369ad8e1357539), pool finalize dropping texture ID views (799df3c5edd00281c40ae8889a10c55cefe93616, same
  leak class as a593388); nx-port geometry shader point-to-quad, alpha test, newer NVN object sizes (3e6366b…).
- **iOS 17 devices**: 4ppp44/d2r-mobile-ios17 (above).
- Small: an ad-hoc signature for LiveContainer (Kamisorara 95d6caa903bc19e602493bb5a5568650d39f52cf), gamepad face buttons by printed glyph
  (8429dc1727cbca079c60427e3b59258683f1c924).

Not worth porting: nxnative's prebuilt .NET DLLs, Android/Vulkan and MoltenVK runtimes (dodola, imink/hades-ios), the upstream D2R-only
parts. Forks of d2r-mobile with their own pushes are mostly branches for pull requests that upstream has merged; on 2026-10-17 the renamed
ones (jeoichu-rgb/switch-mobile, Kelvin-Link/ns-mobile, wysl/-game-d2r-mobile) had no commits of their own.
