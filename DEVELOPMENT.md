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
   (Mac occlusion, audio notifier QoS: see "Wider review of 2026-10-19"; present pacing to the screen refresh does not apply, see there).

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
- The product with these rules is `work/build/totk/product60` (`host/dmhost work/build/totk/product60`; `DM_RENDER_RES=<w>x<h>` sets its scene
  render size, item 52 below; since item 57 its changes are code options, `options.default` turning on all three and `DM_OPTIONS=none` none);
  the default product was left at 30 fps
  while the blood moon crash below is open. A 75 s prologue run: interval 1, frame time p50 16.8 ms.
- RomFS overlay for 60 fps and up (601258f): `work/build/cache/dm/venv/bin/python -I games/totk/romfs_overlay.py work/data/switch/totk-1.4.3/RomFS
  work/build/totk/romfs_overlay` builds it from the game's own RomFS (the quick menus' background was black at 60 fps; the game over menu would
  wait forever on a one-frame wait with the real-time step); the host serves it first when it exists (`games/totk/defaults.c` sets
  `DM_ROMFS_OVERLAY`; empty `DM_ROMFS_OVERLAY=` turns it off). Test: `games/totk/tests/t_romfs_overlay.py <RomFS>`.
- On the Mac Tears of the Kingdom runs with the TBDR rewrites off (`games/totk/defaults.c`, `DM_NVN_TBDR=1` turns them on): on the M5 the dive
  measured GPU p50 8-10 % lower without them at the same submission time, and mid-submission commits and layered passes need them off.

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
Also seen: `nn::os::WaitForAddress(long const*, long)` (sdk 0x146fb0) uses arbitration type 3 (64-bit wait if equal), which svc 0x34 treated
as type 0 (fixed in plan batch 1, item 1); main and subsdk0 do not import it. The vertex shader `1eee2726aae7493e` (SPIRV-Cross "nullptr") is now drawn with.

Known gaps: alpha test and depth-stencil (stencil) views are not modelled; an integer render target's partial clear is not drawn (red line); GPUs
without BC (iOS) do not get the BC render target path; iOS needs `DM_CACHE` and the large heap; hid six-axis and vibration are stubs; no 2K
canvas (both in the 2026-10-19 review, not done yet). The two 4x4 textures that were re-uploaded every frame are checked by content now
(small-texture hashing, 06c80bd). Diagnostics added on the way: `DM_NVN_NOCULL`, `DM_NVN_RTCLEAR=<serial>`, `DM_NVN_TEXSNAP` (ordered on the
replay's encoder, `@WxH` and ranges), `DM_NVN_TEXNZ`, `DM_NVN_TEXCLASS`, `DM_NVN_REUPLOG`; the pass dump prints cull / depth range / index type,
the inline uniform data, the first vertices and the counters seen by conditional rendering. Run after
`make -C host GAME=totk`:
`DM_GAME=totk DM_ROOT=$PWD/work DM_SHDUMP=$PWD/work/build/totk/shader_capture DM_SAVE=… DM_TEMP=… DM_CACHE=… DM_WATCH=120 host/dmhost` (the
stage map and library directory come from the profile; rebuild them with `shmap_from_capture.py` and `DM_GAME=totk dmsh_build.py`).

Upstream PR #55 (x18 scan prefilter, n3crosis) is in `tools/patch_nso.py` (76 s → 16 s for manosaba's main).

## Upstream and other ports to draw from

Reviewed 2026-10-17 and again 2026-10-19 (candidates in "Review of 2026-10-19" below, then a wider pass the same day: "Wider review of 2026-10-19"). Fetch them into a scratch repository, read only, never run their code; port by hand with a test per item, check our
code first (several fixes are already here in another form), and credit the source in the commit and the README.

### Upstream: [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)

Reviewed through 21cf97b91823111ead237858ea79929afacf71a5 (2026-10-17), then through 9393b00 (2026-10-19).

- Ported 2026-10-17: buffer ↔ texture copies honour `nvnCommandBufferSetCopyImageStride` (86b1315a84cec67cfef0b3eab125dbc3c4489cc4, SepheTT;
  `gfx/nvn/nvn_copy_pitch.h`, `t_copy_pitch`; manosaba never calls it; d21348c); `UILaunchScreen: {}` instead of an empty `UILaunchStoryboardName`
  (21cf97b91823111ead237858ea79929afacf71a5, BossaMelon; upstream saw iPhones start in the legacy compatibility size, this app already rendered at
  native size on the iPhone 17 Pro with iOS 26 and both iPads; ce466a9).
- Already here in the same form: unresolved weak imports stay NULL (b84657b25735d96a7263a1b4a7402159a17435a5, `host/hle.c`), `DT_RELR`
  (`host/loader.c`). Not applicable: D2R's Mac mouse (098d9f5e24407480da7cf0d21c5d88b871da6949). dc53e7a8eca1f170009b722d390fac7e0d050288
  was reverted by 21cf97b.

### Derivatives of d2r-mobile

All MIT and keep jhaizhou's LICENSE; they use the old `dm/` layout.

- [seedds/nxnative](https://github.com/seedds/nxnative) (main, reviewed through 20cba12c4299d77f05b6bbfd3dfa42d6c6035da4, then through 64b79de on 2026-10-19; seedds/smbw-mobile points at the same commits): NSP → IPA; its
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
  2026-10-18, and in full at 70729e9 on 2026-10-19): Tears of the Kingdom 1.0.0 / 1.1.2 / 1.4.3 on the Mac, its NVN layer from d2r-mobile's (`games/totk/third_party/d2r_gfx`).
  Ported (see the TotK section): offline BNSH shaders, stale texture views, quad clears, BC render targets, storage buffers addressed from uniform
  buffers, the storage buffer write flag; then the real-time frame step and the 60 fps controller words (dba469f), uniform snapshots (146354a),
  and the 2026-10-19 review items ("Ported 2026-10-19" below). Not yet: the 2K canvas and MetalFX scene / output upscaling, frame interpolation,
  gyro and rumble, BC → ASTC transcoding. With those its 1.4.3 notes give 83.9-110.2 fps averaged over 15
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
  (3e6366b…); mush8 (ideas only). Ported for Tears of the Kingdom in a267ec1 (`host/hle_audren.c`, the SDK renderer in-process).
- **Deferred texture binds** read when the command executes, not when it is recorded (`nvnCommandBufferBindTexturesDeferred`): nxnative
  d6cd8a812a1047dff8f660f604d0a46033752242. Ported for Tears of the Kingdom in a267ec1 (the readers refresh the handles, `bh_fresh` in
  `nvn_mtl.m`).
- **Graphics**, nxnative: a texture attached at two colour slots is attached once (ported through tomodachi-native's variant, `rt_dup_slot`), cube textures as 6-layer 2D arrays for cube-array views,
  compressed render targets skipped with a red line instead of a Metal abort (d6cd8a812a1047dff8f660f604d0a46033752242; the first touches the
  attachment setup and the pipeline key in several places); a snapshot of
  the attachment for shaders that sample what they write in the same pass, per shader by a profile key, with the device self-check
  `DM_NVN_MAGSCAN` (the `nvn_mtl.m` part of 20cba12c4299d77f05b6bbfd3dfa42d6c6035da4); the submission handed to the GPU every 64 draws for games
  that wait for the GPU every frame (`DM_NVN_CUT`, ff74840…).
- **Small guest stacks**: host work on a per-thread host stack (`HLE_FORCE_BIG`, `hle_bigstack.c`, 42a50effdda418fd03d9e15c569d75a3a466d1d4: a
  save thread with 8 KB; NVN functions other than command recording, d6cd8a8…). Our HLE functions run on the calling guest thread's stack too
  (imports resolve straight to them), and the file system ones keep path buffers on it. Not ported yet: an assembly trampoline that only
  carries register arguments, and it cuts the guest backtrace of crash reports at the switch; wants a game whose small stacks overflow.
  MacNativeGames' version keeps the backtrace: see "Wider review of 2026-10-19". Done in plan item 40 (`host/hstack.S`).
- **Diagnostics**: `DM_NVN_KEYDUMP` (every input of one fragment shader's draws) and sampler / swizzle output in PASSDUMP
  (734a286328d18404fd5a67785a1e05d2149c5986).
- **nn::ro .nro modules**: nx-port `dm/host/hle_ro.c` and loader changes (3e6366b…).
- **VP9 movies** with libvpx: nxnative a9e4f18ed17b17bb5cd8eef422464e94a017654b (decoder hooks), 5e84062fad1f07e95f59c1cc6a94918c463d077e
  (libvpx). Done our own way in a8c3ff1 (`host/movie_webm.c`, libvpx, MacNativeGames' semantics).
- **Shaders** from Nintendo's containers: nxnative `dm/gfx/tools/shader_split.py` (Yaz0 SARCs, BNSH, BAHS in 42a50ef…; ours:
  `engines/bnsh/shader_extract.py`, 70cb843 and 17766ae); older Unity bundles:
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
  (8429dc1727cbca079c60427e3b59258683f1c924; an earlier variant, the fix is 55c6fba / upstream 46cb318, see "Wider review of 2026-10-19").

### Review of 2026-10-19

All references fetched again (blobless, read only). New since the last review: upstream 1 commit, nxnative 33 (9579030..64b79de), MacNativeGames
3 plus a full pass over its TotK code, 4ppp44 1, LASRecomp `main` 17 (README) and its `beta_5` / `multi` branches (300-500 commits: other
games, Vulkan, randomizer), SkyRecomp 9 (PC / Vulkan: NIS scaling, LayeredFS mod manager), mush8 / tomodachi-native / Kamisorara only
small or merges; d2r-mobile/d2r-mobile is a rewritten copy of upstream's history up to 21cf97b with no code of its own. Checked against our
code; "seen" = the symptom is in our logs or shader cache.

Correctness, small (in this order):
- Retired textures and buffers kept alive by autorelease: `mtl_sub_begin` runs `defer_drain` / `fin_drain` / `warm_claim` before it pushes the
  submission's pool, on a guest thread without one (nxnative 0fc0ce4, Metroid Dread's GPU memory grew by gigabytes).
- Driver segment c0 for vertex and compute shaders (nxnative 9579030, MacNativeGames `drv_fillable` / `driver_bank_fill`): `dmsh_build.py`
  `drv_reads` matches `fp_c0` only, so 229 vertex and 146 compute shaders of the TotK library read an all-zero c0 (seen: storage buffer table
  `cp_c0[53]`, dispatch size `c0[32]`, point size limit `c0[97]`). Fill 0x200 (thread groups), 0x210 + 16 r (compute uniform table), 0x610
  (2047.9375) too.
- Automatic block height `(ah + ah / 2) / 8`, ours `+ 7` (`nvn_tex.c`; MacNativeGames cd85f5b, 16 of 40 boundary cases wrong against the SDK;
  check manosaba's screenshots). D16 load clears quantised like fragment depth (MacNativeGames 3d90e41).
- iOS audio session `AVAudioSessionCategoryOptionMixWithOthers` (upstream 9393b00): other apps' sound keeps playing.
- The two shaders that fail to translate (nxnative 771f62f: register texel offsets, four-offset gathers; subgroup ballot lowered as
  MacNativeGames does); seen: `1eee2726aae7493e` fails to load in play.
- `nvnTextureBuilderSetPackagedTextureData`, `nvnCommandBufferSetPrimitiveRestart` (both have them; seen: TotK calls both).

Correctness, medium:
- A registration whose serial was finalized redirected to the texture living at that storage now (MacNativeGames `live_put` /
  `nvn_tex_live_at`); seen: 56 "created again" red lines in play logs.
- Clears on a left-out duplicate attachment slot (nxnative 7dde056 `clear_color_tri`); seen once (red line 75). A growable dirty-range table
  (same commit; TotK peaks at 3067 of 4096).
- One shader rebuild for: `-fpreserve-invariance` (nxnative f1cc6be: depth pre-pass vs G-buffer blocks), vertex outputs starting at (0,0,0,1)
  (e81d66f), precise math for the integer-division idiom MacNativeGames found broken (`MTLMathModeSafe`; costs speed, measure).
- 8-bit indices: restart 0xFF → 0xFFFF, and widening on the GPU when a GPU write earlier in the submission made them.
- GPU results written back to CPU-visible pools on `InvalidateMappedRange` (MacNativeGames: black terrain blocks after travel).
- Textures sharing one storage (MacNativeGames `stor_*`: black geometry, full-frame flashes from NaN depth); large.
- WaitSynchronization order as Horizon (signaled, timeout 0, then cancelled; LASRecomp 1144aea checked against Atmosphère); do not take its
  `value - 2` for SignalToAddress mode 2.
- `nvnQueueFinish` without waiting for queued presents (nxnative 3b72aea, a 78 s → 6 s load on iPhone); recheck our stall note first.

Performance (TotK at 60 fps; MacNativeGames' numbers): acquire sync not fenced to the newest submission, which needs the pending-write table
kept across submissions (theirs 16384 entries, ours 256 reset per submission); GPU keep-alive (GPU p50 12.1 → 9.1 ms, costs power); commits
every 60 / 75 draws (11.66 ms vs 12.50), at odds with our TBDR passes; dirty-tracking cost (pool scan ~14 % and checks 4.5 % of the replay,
small-texture hashes); a render-state filter (~30,000 redundant encoder calls a frame); uniform rename (~80 copy encoders, ~23 split passes a
frame); one compute encoder for consecutive dispatches; an upload staging pool; identical texture registrations skipped; an encoding thread
(replay −18 %); layered / cube-face passes.

Features: the 2K canvas and MetalFX, RomFS overlays for 60 fps (black quick-menu backgrounds), gyro and rumble, BC → ASTC for iOS, a GPU
memory budget and pool range checks (MacNativeGames added them after four macOS kernel panics).

Not to do: a Metal binary archive on macOS 26 without guarding `serializeToURL` (LASRecomp turned it off after crashes).

### Ported 2026-10-19

Every item above but three features (below), each ported by hand with a unit test and a broken version that fails it; gfx tests (Metal 4 and Metal 3) and host
tests pass at the end; Tears of the Kingdom's dive (`fl/trial.sh`, frames 1500-2400) shows no wrong frame; manosaba's four screenshots stay within
run-to-run variance (s600 / s2400 differ about 0.15 % between two runs of the same build, s1500 / s3300 identical). Numbers are from that dive on an
M5 (16 GB, macOS 26), 30 fps product, two runs where stated.

- Shader chain 1e41705de87b (686b81e): driver segment reads per stage, two translator fixes (shader `1eee2726aae7493e` builds), vertex outputs
  start at (0,0,0,1), `-fpreserve-invariance`, `-fmetal-math-mode=safe`, a Metal compile cache (`gfx/tools/dmsh_mtlcache.py`).
- Correctness (06c80bd): autorelease pool (`t_retire`), c0 fills (`t_drvseg`), block height and D16 clears (`t_tex_auto`, `t_d16clear`),
  SetPackagedTextureData / SetPrimitiveRestart and GPU widening of 8-bit indices (`t_restart`), storage follow (`t_live`), duplicate-slot
  clears and the growable dirty table, write-back on invalidate (`t_wb`), QueueFinish (`t_nvn`). WaitSynchronization order in
  4dd984d (`host/tests/t_waitsync.c`). MixWithOthers in 46b63f4 (not yet run on a device). Invariance, vertex output defaults and safe math are part of the chain.
- Textures sharing a storage (06c80bd, `t_stor`): measured first, 139 reads of an aliased texture after its partner was rendered (564x81 R8,
  25x25 pairs, CPU_NO_ACCESS pool). With one MTLTexture per (storage, layout, format) the transient render targets re-initialized every frame
  keep their texture: submission time p50 3.41 / 7.96 / 4.30 / 6.65 → 2.41 / 6.49 / 3.38 / 5.38 ms (frames 1500 / 1800 / 2100 / 2400), GPU
  allocated 2989 → 2300 MB, RSS 4615 → 3921 MB at frame 1500.
- The "created again" red lines: the loading screen samples two finalized BC4 textures (IDs 13963 / 13973) through their descriptors; making them
  again from their storage is what the console reads, so it is an info line now unless the content is lost, and the texture stays 600
  submissions (it was made again every 16).
- Pending writes across submissions + acquire sync (`t_pend`), keep-alive (`t_keepalive`), chunked commits (`t_chunk`) and a cheaper
  uniform snapshot (radix sort, repeated bindings once, copies in parts; 0.34-0.76 → 0.23-0.43 ms a submission; `t_ussort`), render-state
  filter (`t_esfilter`), one compute encoder (`t_drvseg`), upload staging pool (`t_uppool`), identical registrations (`t_live`).
- Dirty-tracking cost (`t_texcheck`, `t_pool_find`): check once per submission, content hash for textures up to 4 KiB, views kept across
  serial-table steps for other serials and (GPU textures) across guest writes, contiguous pool index + address cache. Submission time p50 −12..14 %;
  the title's re-uploads 1605 → 11 and write-protect faults 967 → 2 by frame 900; `DM_NVN_DIRTYCHECK=1` finds no missed change.
- Uniform rename (`t_ubren`): about 100 inline updates a frame no longer end their render pass; GPU p50 7.97 → 6.78 ms, p95 11.31 → 7.92 ms.
  The first version dropped the snapshot when it wrote the updates, after which every update of the range was written before its draw again; the
  private copy is now the snapshot with every update of the submission.
- `fin_drain` 0.69 → 0.04 ms a submission (released copies in due order, released views in one walk; `t_snapreap`, `t_findrain`).
- Encode stage (`gfx/nvn/nvn_render_stream.h`, `t_rstream`): implemented and off (`DM_NVN_RENDER_STREAM=1`): two runs each, submission time
  p50 lower in light stretches (3.1 against 3.4 ms) and 0.3-0.5 ms higher at frames 1800 / 2400 (the replay thread waits for the encode thread before every copy and dispatch).
- Layered passes (`t_layer`): the cube filter's per-layer passes merged through the vertex amplification view mapping (no shader patching;
  a GPU probe showed the offset works without a layer output and without a topology class, which would refuse vertex shaders writing point
  size); about ten merges a frame in the title and menus (GPU p50 −13 % there), none in the dive. MacNativeGames' six-attachment cube groups:
  none in our scenes.
- Found on the way: `pso_predict_rest` restored only the draw state, so the draw that asked for the prediction took the last scissor walked over
  (first draw of new pipelines; `t_ubren` case 0).
- RomFS overlay (601258f), GPU memory budget and pool range checks (06c80bd, `t_poolcheck`; no pool refused in Tears of the Kingdom or
  manosaba).
- Not done: 2K canvas and MetalFX (code patches with a veneer, which the rewriter cannot place yet, plus a HeapDef overlay and a larger
  application pool), gyro and rumble (needs a controller to check), BC → ASTC for iOS (Tears of the Kingdom does not run on iOS yet).

Not worth porting: nxnative's prebuilt .NET DLLs, Android/Vulkan and MoltenVK runtimes (dodola, imink/hades-ios), the upstream D2R-only
parts. Forks of d2r-mobile with their own pushes are mostly branches for pull requests that upstream has merged; on 2026-10-17 the renamed
ones (jeoichu-rgb/switch-mobile, Kelvin-Link/ns-mobile, wysl/-game-d2r-mobile) had no commits of their own.

### Wider review of 2026-10-19 (to do)

Eight reviewers in parallel, one group of references each, read only (nothing of theirs built or run): new commits on every branch, all pull
requests and issues, forks with commits of their own (forks pushed after creation, compared with their parent through the API), and the code as
it stands against ours. Reviewed through: upstream 9393b00 (39 PRs, 47 issues; 1086 forks, 19 ahead, 157 branches compared), nxnative 2b31dbd (4
PRs, 5 issues, 4 forks ahead; then through 19d7a70 the same afternoon, items 73-77), MacNativeGames f5cc151 (13 PRs, 7 issues, 2 forks
ahead), LASRecomp main d23597d, beta_5 7597ec3 and 15 other
branches (131 PRs, 63 issues, 6 forks ahead; hjqcan's fork carries a `multi` branch 205 commits past beta_5), mush8 975040b (GPL-3.0, ideas only;
3 PRs, 2 forks ahead), tomodachi-native 5587d1b, nx-port 3e6366b, Kamisorara 1539d80, 4ppp44 2a7b1c6, SkyRecomp d876287, BotWRecomp 2b84b82, and
25 projects found by search. "Checked here" = the gap was confirmed in our code when the reports were merged; the rest is the reviewer's reading,
to confirm before porting.

Carried over from "Review of 2026-10-19" (not done there):
- The 2K canvas and MetalFX scene / output upscaling (MacNativeGames): code patches with a veneer, which the rewriter cannot place yet, a HeapDef
  overlay and a larger application pool. The scene resolution patch below (PR #19) is a smaller first step.
- Gyro and rumble: needs a controller to check; the references below.
- BC → ASTC for iOS: waits until Tears of the Kingdom runs on iOS (iOS items below).
- Checks not run yet: MixWithOthers on a device (46b63f4); the game over flow with the overlay in play (checked by bytes only,
  `games/totk/tests/t_romfs_overlay.py`); manosaba's iOS app rebuilt with chain bab8d4746968 (Tier 2; before it 1e41705de87b) and run on the
  devices (its metallibs are built in `work/build/ios/dmsh_b/b-bab8d4746968`; the last device builds recorded are from 2026-10-16).
- Open: the blood moon crash ("Open (2026-10-19)" above); svc 0x34 type 3 and the host stack item below bear on it.
- The encode stage stays off (`DM_NVN_RENDER_STREAM=1`); the translation thread below takes its place as the plan.

New references:
- [T5eng/nxnative_totk](https://github.com/T5eng/nxnative_totk) (MIT, 1c22d16): Tears of the Kingdom 1.4.3 on an iPhone Air (iOS 27.0.1) as
  patches on MacNativeGames: boot, saves, play, audio and touch. Its `docs/TECHNICAL.md`: about 6 GiB of address space below 2^39 on iOS 27 (heap
  and alias reserved anywhere, then checked against 2^39), arm64-iphoneos metallibs (MSL compiled on the device gave argument encoders of length
  0), opus and libvpx cross-built for iOS, a translation thread (frame p50 22.9 → 16.67 ms).
- [Nishikori-Yui/lordran-native](https://github.com/Nishikori-Yui/lordran-native) (MIT, from nxnative, Dark Souls Remastered, 8f5b96a): alpha
  test, occlusion counts across submissions, command-buffer FenceSync, sampler reduction, each with GPU tests.
- [atone/nxnative](https://github.com/atone/nxnative) (MIT, 185cfda): Pro Controller motion (HID report 0x30) and HD rumble (report 0x10).
- [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile) branches `codex/p5r-runtime` and `codex/p5r-shaders-verified` (Persona 5 Royal
  bring-up): sampler LOD bias and reduction, NVN functions we lack.
- [catoncat/d2r-port-radar](https://github.com/catoncat/d2r-port-radar) (MIT): an index of derivative ports refreshed every few hours, with UE4 /
  CRIWARE findings; start the next review there.

Correctness, small:
- **svc 0x34 type 3** (checked here): `host/hle_thread.c` treats every type but 2 as a 32-bit "less than". Atmosphère's address arbiter has type
  3 = wait if equal on a 64-bit value (8-byte aligned) and refuses unknown types for svc 0x34 and 0x35; the SDK's `nn::os::WaitForAddress(long
  const*, long)` (sdk 0x146fb0) uses type 3. With a race test as LASRecomp's `tools/test_condvar.c` (a user-space adder against the kernel's
  updates; a plain store instead of the compare-and-swap must fail it). Bears on the open blood moon crash (`WaitLightEvent` trusts a 0 result).
- **Controller face buttons swapped twice on Nintendo controllers** (checked here): `host/gamepad.m` swaps by position when `is_nintendo`, which
  its own comment says comes from the header convention only. Upstream 46cb318 (issue #57: a Switch Pro on iPadOS 27.0.1 recognised as Nintendo
  and still swapped) maps buttonA..Y straight and has an 18-case `t_padmap.c`; Kamisorara 55c6fba is the same fix (8429dc1 under "Small" above
  was its earlier, wrong variant).
- **Three scans ignore the BindProgram stage mask** (checked here): the Q scan of `defer_try` and steps four and five of `fuse_try` take the
  program as is instead of `prog_bind`; with Unity's two-call binds (manosaba) a deferral can miss a sampled attachment and step five never
  fuses. Upstream PR #70 (open, SepheTT) covers every site with tests.
- **1,568 Tears of the Kingdom shaders missing from the offline extraction** (checked here: all 27,101 of our keys are among nxnative's 28,669):
  BAHS `.sharcb` entries share control sections but point at different code, and `engines/bnsh/shader_extract.py` takes one code area per control
  section. nxnative `dm/gfx/tools/shader_split.py` `bahs_shaders` reads the entry table (`agl_technique_pfx` 777, `agl_technique_shdw` 300,
  `agl_common` 174, `agl_technique_lght` 99, ...), plus the Title layout's `bgsh` and the older control layout of
  `Lib/NintendoSDK/TextureShader.bnsh`. None was missed in our runs so far (title, prologue, dive); later areas and iOS (no capture at run time)
  need them.
- **FMZ multiplies** (checked here: `Fmz` is decoded and used nowhere under `Instructions/`): Maxwell gives 0 for 0 × Inf / NaN with FMZ, IEEE
  math gives NaN, which blurs spread (tomodachi-native a0aa6c2). Shader chain bump.
- **SHF with a clamped or zero shift, I2I.SAT across signedness** in the Ryujinx translator (LASRecomp d4c2ff6, PR #134: 29 hand-worked vectors,
  15 failed on their old translator; ours read, not run). Run their vectors through our chain; scan the TotK library for those forms.
- **Sampler LOD bias and reduction** (checked here: `lod_bias` is stored and printed, never applied): `MTLSamplerDescriptor` `lodBias` /
  `reductionMode` (macOS / iOS 26); TotK calls SetLodBias 861 times. GirlNet 0293081, lordran 0a35532 (292 GPU checks).
- **GPU time per frame is a sum of command-buffer spans** (checked here: `gpu_tag_done` adds GPUEndTime − GPUStartTime): Apple GPUs overlap
  command buffers, so the union is what the frame costs (LASRecomp PR #197: summed 13-17 ms against a union of 6.5-7 ms). The GPU p50 numbers
  above are inflated by an unknown amount; the direction of each change holds.
- **A snapshot copy of an unmapped page ends the process** (checked here: `us_copy_range` is a plain memcpy, also on worker threads):
  MacNativeGames guards the copy with a SIGSEGV / SIGBUS fault guard and drops that copy (a helper died once in about 12 sessions).
- **Overlay directory listings stop at 2048 names** without a word (checked here: `dir_snapshot` in `host/hle_fs.c`, with a quadratic duplicate
  check). TotK has directories of 75,940 / 29,356 / 15,067 entries and the overlay is on by default; whether the game lists them is not known.
- **Silent no-ops** (checked here): SetPatchSize, MultiDraw*IndirectCount, DispatchComputeIndirect and DownsampleTextureView do nothing without a
  line; red on first call (upstream 44a6770), the indirect ones implemented when a game calls them (nxnative's DispatchComputeIndirect /
  DrawElementsIndirect).
- **Occlusion counts across submissions** (lordran 3ef6915: 64 samples, then 32 in the next submission, read 64): our visibility ring and slot
  numbering look the same; not checked whether TotK counts across a submission.
- Smaller: precise SleepThread against macOS timer slack (nxnative a9c56fd: 33 ms sleeps took 36.4 ms); rodata read-only and svc 0x02 honoured so
  a wild write faults at the writer (MacNativeGames `loader.c` `seal` / `check_permissions`); RG8_UI2F / UI2F / I2F vertex formats (jiangjifeng's
  nxnative fork ae9082d); `R_AARCH64_TLSDESC` and symbol precedence for .nro modules (nx-port).

Correctness, medium:
- **Host calls and svcs on a per-thread host stack** (MacNativeGames `games/totk/runtime/gate.S`, `hle_stack.S`, `hle_stack.c`: 4096 static
  thunks and a frame record that links back to the guest caller, so backtraces stay whole; mush8 has the same idea). `host/entry.S` puts 800
  bytes on the guest stack and host functions run there (a RomFS open with the overlay stacks about 5.6 KB of path buffers); their note says
  Tears of the Kingdom makes threads with stacks as small as 4 KiB, and 16 KiB host pages make 4 KiB guard pages writable. A possible cause of
  the blood moon crash (unproven): log the least free stack per thread at gate entry first. Replaces "Small guest stacks" above; manosaba's
  stop-the-world reads host frames on the guest stack and would need the guest sp the switch saves. Done: plan item 40.
- **A draw that samples the attachment it draws into** (tomodachi-native 4dbd30f + da08d11 `fbx_hazard`: every draw, mip aware, copies from a
  pool of 32, views held until completion): we split only merged continuations (`draw_samples_rt`). Count such draws in the dive and in manosaba
  first.
- **Counter timestamps where the report sits on the GPU** (LASRecomp PR #93 splits the command buffer at each ReportCounter): ours give every
  report of a submission one completion time. Check whether TotK subtracts two reports of one submission.

Performance:
- **NVN translation on a thread of its own**, now in four places with numbers: upstream b94670c (+ `t_nvn` case 8; dropped frames per 300 85 → 56
  on an iPhone 17 Pro Max), LASRecomp `runtime/nvn/rqueue.c` (PR #199; game-thread CPU 5.20 → 3.02 ms a frame on an M3 Max), MacNativeGames
  `TOTK_ASYNC_REPLAY` (`hle_nvn.c`, in the 70729e9 tree), T5eng on an iPhone Air (frame p50 22.9 → 16.67 ms). A fence is the submit counter
  ("translated and GPU done"); Finish, texel reads, write-back and destroys drain the queue; presents are queued in order. The uniform snapshot
  stays at the submit call. Large; the way to take replay off the game thread, rather than our encode stage (which lost at frames 1800 / 2400).
- **Busy yields** (nxnative e06a8ec): after 200 µs of back-to-back SleepThread(0) each yield sleeps an eighth of the run (10-250 µs); Three
  Houses: Mac CPU energy 3.5 → 0.5 W at equal frame times; a `power_line` logs TASK_POWER_INFO_V2 every 300 frames. With virtual cores the sleep
  has to release the core.
- **Targeted wakes** (nxnative e06a8ec): an event wakes only the waiters whose handle set holds it (ours wakes every WaitSynchronization waiter);
  semaphores instead of `usleep` polls in `host/hle_audren.c` and three places in `nvn_mtl.m`.
- **Replay lookups without the lock or a walk** (nxnative e06a8ec): a per-thread pool cache with a generation, the dirty range kept on the
  texture (our `find_span` walks about 3,000 ranges), residency adds skipped when nothing changes, page runs unprotected with one `mprotect`;
  dodola 7ccbb3f keeps the newest write per 64-page block.
- **Re-uploaded CPU textures alternate with a kept spare** (nxnative `NvPkg` spare; Link's Awakening 1.64 → 0.37 ms a submission); ours makes a
  new MTLTexture per re-upload. Check cutscenes with `DM_NVN_REUPLOG`.
- **Clears kept on the texture, ClearTexture included** (LASRecomp TexDefer, PRs #96 / #129: passes 42 → 33 and clear-only passes 12 → 3 in one
  scene); ours opens a store-only pass for every ClearTexture. Count them first.
- Smaller: the reflection summary cached per program and attachment events once per pass (upstream 44a6770 / afad65c); `mrs cntpct_el0` read
  inline with a key-page multiplier instead of a gate (mush8 idea; 43 sites in TotK's sdk; count the reads first; the background freeze and
  `DM_DETCLOCK` must keep working); the render area limited to the drawn part (LASRecomp 59a7507); libvpx row multithreading (nxnative
  `VP9D_SET_ROW_MT`).

iOS:
- **A launch check for Metal 4** (upstream b94670c, issue #41): iOS 26 runs on A13, Metal 4 needs A14 / M1, and ours stops with "failed to create
  MTL4CommandQueue". Show a screen instead (and offer the Metal 3 build).
- **Tears of the Kingdom's guest memory on iOS**: nxnative 2b31dbd (a 5 GB reservation without extended addressing, a 2.375 GB alias region, a
  2.125 GB heap region because the game calls SetHeapSize(0x86000000); ours has `guest_heap_mb` 256 → 0xD401 on iOS) and T5eng (iOS 27: about 6
  GiB below 2^39; log our reservation base against 2^39).
- **VP9 on iOS**: nxnative vendors the VP9-decoder part of libvpx v1.17.0 (71 files and the generated config, compiled into the iOS app); T5eng
  cross-builds it.
- **increased-memory-limit with a free account**: BotWRecomp `ios/PORTING.md` (Xcode made a Personal Team profile with it) and mush8
  `make-ios.sh` (FREE=1 keeps it) say yes; `ios/tools/project_free.py` strips it and `ios/README.md` says a free account cannot sign it. One
  signing attempt decides (`embedded.mobileprovision`, `os_proc_available_memory`); it would lift the 3.4 GB limit of item 2 at the top.
- **iOS 17 / Metal 3 devices**: the only unguarded iOS 18 APIs are the residency set (`nvn_mtl.m`) and `hasPerformanceProfile` (`front_ios.m`)
  (4ppp44 compiles the set out; `@available` keeps one binary); deployment target 17.0 in `ios/tools/project_m3.py` and `gfx/tools/ios_metal.py`
  (rebuild the metallibs).
- **Game data excluded from backups** (`NSURLIsExcludedFromBackupKey`, BotWRecomp); TotK's RomFS is 17 GB.
- **On-screen controls with drag-to-look** for TotK (BotWRecomp `runtime/src/host/touch.cpp`, MIT, with a self-test hook); nxnative's
  `DmGamepad.m` is the alternative listed above.
- Smaller: `env.txt` switches on the device (nxnative 5aabb96); the audio notifier thread at USER_INTERACTIVE (upstream afad65c: late wakes 16 →
  0 in 240 s); CoreAudio opened off the calling thread (tomodachi-native d7f52e1, one open took about 40 s there); cold CPU-uploaded textures
  dropped under memory pressure (justr4mzi's mush8 fork aafef22, ideas only); `GCSupportsGameMode` for iOS 18-25; already-compressed files
  stored, not deflated, in the IPA (nxnative `zip_store`); code patches chosen at launch, so one product serves 30 and 60 fps (tomodachi-native
  0428c80 / afd88e9).

Features (Tears of the Kingdom):
- Gyro and rumble, references now: atone/nxnative (Pro Controller IMU and HD rumble, MIT), mush8 (CoreMotion / CoreHaptics mapping, ideas only),
  LASRecomp `docs/rumble.md` (clamping, mixed by maximum, renewal, stop on focus loss), smbw-mobile `front_haptics_mac.m`, SkyRecomp's resting
  six-axis state.
- Scene render resolution on 1.4.3 (MacNativeGames PR #19: four loads at main+0x585e48.. from the host page; a `HOSTREFS` rule can express it).
- Frame pacing between 30 and 60 fps: with our real-time step, the interval pinned to 1 presents frames as they finish (BotWRecomp `fps.cpp`,
  ideas only). Its list of BotW's 30 fps constants and of cutscenes that crash above 30 fps says what to check in TotK, first whether the blood
  moon crash happens with the 30 fps product.
- User mod folders as overlay layers (SkyRecomp `runtime/mods.c`, with the Atmosphère layout).
- Quit waits for a save in progress (MacNativeGames `frontend.m`, up to 15 s); a covered Mac window discards presents (upstream afad65c).
- Alpha test: lordran a63dd76 (8 comparisons in the fragment epilogue, function constants, 54 GPU cases) or nx-port's; TotK never called
  ColorStateSetAlphaTest in our logs, so the known gap above is not hit there.
- For the next game: add-on content mounts (upstream PR #69), command-buffer FenceSync (lordran c6af84b), ClearBuffer / ClearTexturei /
  SetScissors / SetDepthRanges / Downsample as a resolve / depth bounds / rasterizer discard (GirlNet's P5R branches), LDN / lan-play (mush8's
  design), UE4 / CRIWARE notes (d2r-port-radar).

Diagnostics and tooling:
- **A live command channel** (MacNativeGames `input.c`, a polled file: keys, shots, pass dump, graphics flags without a restart, thread dump,
  host-call trace, frame step).
- **One game at a time on the Mac** (MacNativeGames `game_slot.c`, a flock with a queue; two games used 13 of 16 GB and the GPU driver panicked
  the kernel four times there).
- **Continuous integration** now that the repository is public: a macOS runner that builds the host and runs the host tests that need no GPU
  (GirlNet's nxnative fork bfc0376, LASRecomp `ci.yml`; a synthetic NSP as in hitsmaxft/nsp-dawrin, GPL-3.0, would go further).
- Smaller: thread dumps list the handles of a WaitSynchronization (SkyRecomp `kernel.c`); a ring of the last 256 condvar / mutex operations and a
  delay injected into condvar waits (LASRecomp: a 1-in-6 hang reproduced 3 times of 3); sead heap walk and list watch at a crash
  (MacNativeGames); `DM_MEMSNAP` raw guest memory at chosen frames for the manosaba leak (tomodachi-native a71964e); per-pass GPU time with
  MTL4CounterHeap (upstream 44a6770); soak and fixed-camera measurement (MacNativeGames `mac_v143_soak.py` / `mac_v143_smoke.py`); a stamp of the
  rewriter's sources in the product, the rewriter refusing system registers it does not know, the SDK's abort text in the log (mush8 ideas); the
  lifecycle and swipe-away tests back (upstream `Lifecycle.swift`, `lifecycle.sh`, `logred.sh`); upstream's `t_loader_weak`.

Checked, not better or already here: the Opus table leak (we run the SDK's decoder in the game's storage), macOS 27 fixed reservations (ours are
anywhere), a Metal 3 backend (we have one), compiling shaders at launch and skipping draws until ready (not taken: content drawn once would stay
missing), separate texture and sampler descriptor pools (one pool in our games), present pacing to the screen refresh (games that step per frame
would run fast; only TotK's real-time step can use it, through `DM_PRESENT_HZ`), SignalToAddress −2 (Atmosphère corrected it to −1 in 5f2d713;
ours is right), Metal binary archives (LASRecomp PR #145 closed; still "not to do"), nx-port's clamp of fragment outputs to ±65504 (breaks R32F),
lordran's deswizzle change (same offsets as ours), the audio mixers of LASRecomp, SkyRecomp, MacNativeGames and nx-port (our in-process SDK
renderer is closer), saves (ours are transactional), the condvar and WaitSynchronization order (ours is closer to Horizon than theirs).

### Wider review of 2026-10-20 (to do)

The second wide review, as the first: seven reviewers in parallel, read only (nothing of theirs built or run), from the markers of 2026-10-19:
new commits on every branch, pull requests and issues, forks with commits of their own, the code against ours, plus a search for new projects.
Reviewed through: MacNativeGames 15b1afd (110 commits after f5cc151; 34 PRs, 17 issues; 255 forks, 3 ahead: tvc97 +13), T5eng/nxnative_totk
057575d (MIT with a note; 9 commits), nxnative 7e2c741 (13 commits after 19d7a70; PR #11 digitalhivehub `nvn-block-height` a6b7191, 39 commits;
160 forks, 4 ahead; seedds/smbw-mobile is the same repository renamed), atone/nxnative 185cfda, tomodachi-native 5587d1b (nothing new), upstream
6996690 (3 commits; 47 PRs, 51 issues; 1126 forks, 22 ahead), GirlNet `codex/p5r-runtime` 8dd2950 (121 commits, 26 since 10-19), Kamisorara /
nx-port / 4ppp44 (nothing new), LASRecomp `beta_5` 44f5308 and 25 other branches (MIT with a note; 146 PRs, 67 issues; hjqcan's `multi` is now one
squashed commit c404f1d), lordran-native f7aed21 (25 commits on a branch), SkyRecomp 9722390 (MIT with a note; much of it is LASRecomp's), BotWRecomp
and mush8 (nothing new; justr4mzi's mush8 fork 10 commits, GPL: ideas only), d2r-port-radar 13d100c (260 projects) and about 70 searches. New
projects: [OJZen/totk-mobile](https://github.com/OJZen/totk-mobile) (Apache-2.0, its `core/` upstream MIT; 510d95b: Tears of the Kingdom 1.4.3 on
a Mac and M1+ iPads with saves and play, an iPhone 15 Pro to the title; independent of MacNativeGames and nxnative; 76 tests, many Metal read-backs;
reimplement or keep its licence and NOTICE), [enginefuture/bayonetta3-mobile](https://github.com/enginefuture/bayonetta3-mobile) (MIT, e5ce116),
[enginefuture/dr3-mobile](https://github.com/enginefuture/dr3-mobile) (MIT, a31c36c, Diablo III on nxnative with LDN),
[huajiaoshu520/acnh-mobile](https://github.com/huajiaoshu520/acnh-mobile) (MIT, c764129, Animal Crossing on iOS 26). "Checked here" = confirmed in
our code when the reports were merged (2026-10-20); the rest is the reviewer's reading, to confirm before porting. Nothing new on the blood moon
crash in any of them.

Correctness, small:
- **Pipeline warm-up never runs** (checked here): `warm_start` (`gfx/nvn/nvn_mtl.m`) returns when `pso_manifest.tsv` is missing next to the shader
  map, before the player's own `pso_user.tsv` is used; no tool writes `pso_manifest.tsv` (upstream's D2R build shipped one, 2eb5758). Tears of the
  Kingdom's `pso_user.tsv` has 6,481 rows never read; the prologue's boot profile: 698 pipelines built on the submit thread in 2,146 ms, "warm-up
  claimed 0"; frames 217 / 218 built 160 pipelines and 182 libraries, 206-278 ms. MacNativeGames 15b1afd `warm_start` (title, seed, player and shared
  lists; the first frame waits only for the title list, 3a6424f: first frame 5.8 → 2.0 s; libraries on 8 threads, 35606fe: cold 856 → 187.5 s).
- **The pipeline prediction table fills with guesses nobody takes** (checked here, the mechanism): a slot of the 384 is freed only when its pipeline
  is taken; kicks stop when none is free. The prologue's first 20 logged scans handed off 420; the taken count exists (`g_pp_taken`) but is not
  logged. MacNativeGames PR #29 (`pso_pend_reclaim`: an untaken guess freed 2 submissions later; scans bounded to 8,192 commands, 32 keys, 2 ms;
  guesses only from programs already loaded): before, 4,450 kicked against 841 taken; after, worst frame 1,151 → 682 ms.
- **The present command buffer commits without the residency set** (checked here): `present_now` may create the source texture (`tex_mtl` →
  `res_add`) and commits without `res_commit`. MacNativeGames PR #30 (open): a GPU page fault in 4 of 14 boots when nothing had rendered into the
  presented texture yet. Not seen in our logs.
- **A sync's flag is published before its fence value** (checked here): `nvnQueueFenceSync` and the acquire sync write `fenced = 1` before `fence`,
  `nvnSyncWait` reads both plainly: a poller can see a fenced sync with fence 0 and report ALREADY_SIGNALED early (GirlNet f2faa5f: fence, then
  flag, release / acquire).
- **NVN texture layout rules against the SDK** (checked here): `gfx/nvn/nvn_tex.c` counts a CUBEMAP_ARRAY's builder depth as cubes (`depth * 6`;
  also `nvn_mtl.m` array length and upload), while the NVN guide (the user's SDK docs) says it is the number of faces; LINEAR_RENDER_TARGET (0x40)
  without LINEAR (0x10) is laid out block-linear, while the enum docs say either bit means a 2D array of texels; MINIMAL_LAYOUT (0x200) is not
  handled; packaged 3D textures ignore the block height and depth nibbles. nxnative PR #11 (digitalhivehub bacf2cb, `nvn_layout_diff.py` runs the
  SDK's TextureBuilder in Unicorn; 15 driver vectors; "TotK has 16" linear render targets, RG8 260x260 minimal-layout terrain maps) and GirlNet
  d030a3b (16x16 RGBA8 stride 128 flags 0x44: 2,048 bytes, ours 1,024). A census of which of these our games create comes first.
- **The format's own swizzle is dropped** (checked here): `nvn_fmt_to_mtl` returns an R/B swap for the RGB 16-bit family and alpha 1 for the
  RGBX family, DXT1 RGB, BGR5 and BGRX8, and no caller uses it (`tex_desc`, `tex_by_id`); the table's comment says the bit order was never checked
  with real data. totk-mobile d3dd76c (`nvn_texture_swizzle.inc`, 786,432 GPU read-backs over 0x3a / 0x3b / 0x3c, RGBX8 alpha 1).
- **Offline sockets report the wrong errno** (checked here): `host/hle_net.c` sets 50 as "ENETDOWN", the FreeBSD number; the SDK's `nn::socket`
  enum has ENetDown = 100 and ENoCsi = 50 (dr3-mobile fixes the same).
- **Stick values outside the Switch's circle** (checked here): `host/gamepad.m` clamps each axis, the Mac keyboard and the script keys send
  diagonals at (±32767, ±32767), length 46,340; the SDK docs put stick input inside a circle of radius AnalogStickMax (SkyRecomp issue #16: Link
  "SUPER fast" on diagonals). A radial clamp in `hle_hid.c` for every source.
- **The Mac app never gathers the first iCloud listing** (checked here): `dm_cloud_boot` runs on the main thread before a run loop, and
  `first_listing_ok` (`host/cloud.m`) sends the query to the main queue and waits: always a timeout, the sync skipped, about 5 s at every launch.
  Upstream 1ceaa6b (PR #88): start the query in place and run the run loop until gathered.
- **Quit does not wait for the game's exit-request section** (checked here): `nn::oe::Enter/LeaveExitRequestHandlingSection` are no-ops; manosaba
  brackets its saves with them; `dm_fs_save_wait` covers only the first write to the commit. Count the depth (GirlNet 291a9cc) and wait while > 0.
- **SleepThread of under 20 µs** (checked here): any positive time leaves and retakes the virtual core under `G`, and a 100 ns poller spins on it
  (nxnative baf5677: under 20 µs is a yield with the back-off; Galaxy's pollers 199 → 5 s of CPU).
- **The six-axis state is never written** (from B, checked here: `hle_hid.c` six-axis HID_STUBs write nothing, so the singular
  `GetSixAxisSensorState` leaves the caller's struct as it was; TotK calls `GetSixAxisSensorHandles`): a resting state in the 0x60 layout with
  newest-first sampling numbers (atone/nxnative `hle_hid.c:138-217`; SkyRecomp's resting state), before any gyro work (67).
- **Red lines for what the next game would hit silently**: a second sampler pool (`OP_SET_SAMP_POOL` is ignored; Dark Souls switches 12 within a
  frame, lordran b826e34), packaged layout byte 1 ≠ 0 (bayonetta3; 0 in all our 2,134 logged layouts), SRC1 blend factors (dual-source: the
  translator is always told `dualSourceBlendEnable` false, checked here), an unimplemented svc or import with its caller (`DM_UNIMPL_ABORT` in bayonetta3-mobile's copy of blade2-mobile).

Tears of the Kingdom:
- **Shrines run at a fixed interval 2** (nxnative PR #11, 97172c1 / a6b7191 `patch_nso.py:169-189`, 1.4.3): shrines force the fixed interval
  (main+0xd28e2c sets the flag, 0xd28f1c applies it): 0xb00f4c and 0x1ff6104 `tbz w9/w10, #0` → `b`; leaving the fixed mode re-arms the step-down
  only when floor < current: 0x2b3d268, 0xb00f64, 0x1ff6138 `ldr w10` → `mov w10, #1`. Our `fps60` option has none of the five. "Shrine route:
  game picks 60, GPU p90 13.6 ms." Check in Unicorn as `check_frame_rate.py`, then Ukouh Shrine.
- **GPU-rendered terrain tiles moved by CPU copies** (totk-mobile `nvn_texture_rendered.inc` / `nvn_texture_writeback.inc`, with a test): the game
  renders each terrain tile's RG8 normal map into a CPU-cached pool while loading and later moves tiles in that pool with CPU copies; with the
  texels only in the Metal texture the moved tiles read zeros: grass flat, no relief shading. Theirs writes back the first GPU write after 30
  frames without one, at the end of that submission, with per-page hashes. Ours writes back only on `nvnMemoryPoolInvalidateMappedRange` (checked
  here: called 4 times in a TotK run, no write-back line). Count GPU-written textures in CPU-cached pools, compare the grass with the console footage.
- **The Ultrahand mask shader writes R8Uint** (nxnative PR #11 `shader_fout.tsv`): `e3fb0bb58a691e10 0 uint` is not in our `fout.tsv` (checked
  here; only `0544282c162305db`); without it Metal refuses the pipeline when Ultrahand is used. A shader chain rebuild of one shader.
- **The two BNSH files inside `main`** (MacNativeGames 2f9e707 `prepare_shader_seed.py` scans the module images): 40 keys, none in our offline
  index, all among our 178 capture-only shaders (`engines/bnsh/shader_extract.py` says "Not covered: shaders built into main", checked here). For
  iOS, which captures nothing at run time.
- **The half-step selector** (T5eng TECHNICAL.md / PERFORMANCE.md): `sys+0x1da` bit 0 (×0.5 / ×1, kept by our `realtime` option) said to follow
  the frame-rate mode, so the world speed changes when the interval flips 1 ↔ 2. Measure: log the bit with the interval under `DM_TEST_WORK_MS=14`.
- **`DM_RENDER_RES` above the output size** (tvc97/MacNativeGames cea76085, MIT): the crash at main+0x1c2f20 (our 53 cap) is the dynamic texture
  allocator's single pool running out (constructor 0x8cfc9c; size at +0x2298 = `DynamicTextureAllocatorMem`): scale it in
  `Product.Nin_NX_NVN.HeapDef.byml.zs` by 1.35 × the pixel ratio and add the same to the application pool (2560x1440 ran with 384 MiB). Also for 63.
- **Anisotropic filtering** (tvc97 `TOTK_ANISO`): about 6,900 TotK samplers all ask for 1; 16 on mipmapped linear samplers without compare or
  reduction. A switch, off by default (the brightness checks against the console footage stay valid).

Performance and power, measure first:
- **A present with a deadline keeps the GPU clock up** (LASRecomp PR #203, 364b383): `presentDrawable:atTime:`; on an M3 Max a plain present every
  33 ms let macOS drop the GPU to 338 MHz (frame 20 ms instead of 7, locked at 30 fps); `afterMinimumDuration` did not move the clock, and our mode 0
  and the rescue path use it (checked here). Our keep-alive kernels are Mac only. Measure with `powermetrics`, TotK's 30 fps product.
- **The audio renderer paced in bursts** (SkyRecomp 0620de9: before 53 of 79 event gaps under 1 ms, after none): ours renders whenever the output
  hands back a buffer, and a large CoreAudio callback frees several at once. Count renderer events against `RequestUpdateAudioRenderer`, gap histogram.
- **`[[clang::optnone]]` arithmetic helpers in our MSL** (totk-mobile `nvn_shader_math.h`, bit-exact test): 3,088 of 31,392 files of chain
  bab8d4746968, 8,641 call sites, are real GPU function calls. A chain change; GPU p50 A/B.
- **Depth textures left uncompressed** (LASRecomp PR #204: ambient occlusion vanished with compression, cause unknown): ours never sets
  `allowGPUOptimizedContents`. An A/B switch and the shot brightness check.

iOS (before or with 62):
- **Kept metallib mappings use up the vnode table** (MacNativeGames e099353): ~8,800 libraries filled an A16 iPad's 11,400 vnodes, jetsam
  "vnode-limit"; on iOS copy into memory and unmap (about 110 MB for all). Ours keeps the mapping (`lib_from_file`, checked here).
- **A pipeline failed by an interrupted compiler service stays failed** (MacNativeGames 39048ca: XPC_ERROR_CONNECTION_INTERRUPTED under memory
  pressure, 4 retries): ours stores every failure for the session (`g_pso_fail`).
- **The command channel after the file is replaced** (T5eng `TOTK_INPUT_REOPEN`: an AFC push is a new inode): reopen on inode or size change.
- **Run diagnostics** (lordran f5834c5 / 9567c94): a marker of an interrupted run that keeps its log, a share-sheet export (device, Metal, memory,
  thermal, the last 8 MiB of each log), an fps / GPU line, UTC milliseconds on log lines.
- **Thread dump when frames stop, without ending the game** (justr4mzi's mush8 fork 979317e, GPL: idea): 6 s without a present, one dump per
  freeze; ours dumps only under `DM_WATCH` / `DM_WATCH_STALL`, which exit. Also for 72.
- **`-mcpu=apple-a14` for the Metal 4 iOS build** (lordran 78dd33d; Metal 4 runs from A14): ours a18 (`ios/project.yml`, checked here); the IPA has
  no BF16 / I8MM / SME / WFxT instruction today, so this only rules out a future one and lets one IPA cover every Metal 4 device.
- **Touch camera sensitivity and inversion** (lordran 42ad4cf / 03e24ab, with tests): 0.25-3×, X / Y inversion, before saturation.
- **Resumable data transfer to the device** (dr3 `transfer_ios_assets.py`, totk-mobile `ios_transfer.py`): skip files already there, a journal;
  Tears of the Kingdom's RomFS is 17 GB, ours copies whole trees.
- For 62: MacNativeGames `services.c` (4.75 GiB + alias stepped 16 → 4 GiB in a found gap, a gap log on failure) and T5eng (3 + 3 GiB, reserved in
  `main()` before UIKit and Metal, checked against 2^39), totk-mobile (12.5 GB with extended-virtual-addressing); ours `guest_heap_mb` 256 would
  refuse the game's 2,118+ MB heap on iOS (`DM_APP_POOL_ADD_MB` 512 on top): at least 2,176 and the pool add 0 there. Code options applied at export
  and verified on the device (MacNativeGames `game_patches_v143.c`, T5eng). VP9: libvpx cross-built (MacNativeGames `build_deps.py` v1.16.0, T5eng
  1.15.0, totk-mobile 1.17.0 with a pinned hash) or VideoToolbox's supplemental VP9 decoder on iOS 26.2+ (GirlNet f548a07 .. 8dd2950, libvpx as
  fallback). Handheld mode by default on iOS (1280x720: 2-3 ms GPU and 100 MiB less, T5eng). Cooldown between A/B runs (T5eng: 60 fps cool, 30 fps
  hot). BC → ASTC on the GPU (MacNativeGames `nvn_bc1astc.c`, `nvn_bctc.metal`). Log `maxBufferLength` at boot (mush8 PR #3).

Larger:
- **Conditional rendering on the GPU** (totk-mobile `nvn_conditional_draw.inc`, tests): a kernel turns the comparison into indirect draw arguments
  with count 0, conditional clears as predicated quads; ours renders unconditionally when the result is still on the GPU (the red line in every TotK
  run).
- **Framebuffer fetch for in-pass reads of the current pixel** (totk-mobile `nvn_framebuffer_fetch.inc`: G-buffer decals reading c1 / c3 at their
  own pixel, about 65 split passes a frame in the cave): ours copies after barriers (42); `dmsh_fetch.py` builds fetch variants for merged passes only.
- **An iOS Simulator build** (lordran 9814125): the touch controls, safe areas and lifecycle across iPhone and iPad sizes without devices.
- For 61: LASRecomp's render-queue cases (hjqcan c404f1d `test_render_queue.c`: nested CallCommands kept after handles are recycled, a fence in a
  nested list published when queued, two waiters, the four wait results, a fence whose ring slot was reused; upstream's prescan read the wrong record
  slot), MacNativeGames' job queue (64 jobs, a 4,096-entry fence ring, texture releases as jobs, the time-constraint policy on the translation thread:
  without it replay 6.6 → 13 ms), per-pass costs on an M4 (a dependent render pass 38-57 µs, a copy → render switch about 30 µs).
- For 63: MetalFX's docked guard compares the scene's content size, not the allocated texture (MacNativeGames ff092e3).
- For 64: `CopyCommands` copies at call time (the NVN guide; ours replays the source like a CALL), HSETP2 `.H_AND` as LASRecomp and yuzu (0 uses
  in our games; ours ANDs the halves first), depth bounds on Apple10 (`setDepthTestMinBound:maxBound:`), rasterizer discard with void vertex
  variants, two queues, `SetScissors` / `SetDepthRanges` batches (GirlNet), `getentropy` as HLE_FORCE and `nn::pl::GetSharedFont` (GirlNet),
  AVC / AAC from the bytes the game sends (lordran acfb424), the control section length by GPU code version (bayonetta3-mobile `nvn_control.h`), LDN as dr3 (Bonjour, an
  HMAC-signed control protocol, a two-process test), VideoToolbox VP9.
- For 67: the vibration handles per style with only the connected style forwarded (digitalhivehub 7db9248: games send zeros to the others), the
  Pro Controller's IMU and HD rumble over IOHID on the Mac (atone; GameController reports its motion as zeros, and an 8BitDo in Pro mode too,
  MacNativeGames 37d55cb), CoreHaptics with plain players (advanced players drop the connection on macOS 26), phone haptics rate-limited (acnh).
- For 65: MacNativeGames played 1.4.3 on an iPhone 14 and an A16 iPad "signed with a personal development team in Xcode", with
  increased-memory-limit in its entitlements (3.6 of 4 GB used).
- Tooling: UBSan for the C parsers in CI (MacNativeGames; ASan hangs in dyld on macOS 26), a public-content audit in CI (bayonetta3
  `audit_public.py`; the repository is public), LDC.64 vectors in `t_xlate_ops.py` (LASRecomp a179452; ours is right, 2,069 TotK shaders use the form).

Seen, not planned: face buttons by position for PlayStation and Xbox controllers (MacNativeGames 31052d6, a preference); pchtxt patches and a cheat VM
(MacNativeGames 1b33531, PR #22); a save-loading crash workaround at 1.4.3 main+0x1bd01c4 (MacNativeGames dd0e893; never seen here: to take if it
shows); displays above 120 Hz (tvc97: macOS rounds a 60 Hz request to 72 on a 144 Hz panel; no such display here); frame interpolation by replaying
the frame (LASRecomp `frame-interp-rt`); provisional profiles from the game files (nxnative `profile_guess.py`); the IL2CPP object class at a crash
(armyja); frame latency 2 at 30 Hz (lfkdsk's notes).

Checked, not better or already here: MacNativeGames' aliased buffer copies, serial history, async shader loading with skipped draws, its run-time
MSL caches and shared lists, ultrawide / stretch, Metal 3 and HUD PRs; T5eng's touch controls (ours, 58), iOS metallib flags (ours have safe math
and invariance), `__TEXT` embedding (ours keeps one executable segment); nxnative's torch light (ours, 77), render conditions, PR #11's integer
outputs, fast clears, depth clamp, attachment-less passes, MetalFX, uniform renaming, menu capture (ours: the overlay); upstream's D2R UI and device
lab tools, PR #86; GirlNet's per-draw conditional render (a CPU stall), GPU primitive restart, swkbd trimming; LASRecomp's 16-sampler limit (ours:
argument buffers), stale-program detector (ours keys by hash), condvar order and Break result (ours); justr4mzi's SignalToAddress kind 2 (do not
take: ours is current Atmosphère); lordran's in-place re-upload (ours: a spare texture, 16); totk-mobile's audio renderer and virtual pools (ours
closer or equal).

### Deep review of 2026-10-20 (to do)

A third review the same day, at the user's request ("totk-mobile and the other games can all be included; learn from where others do better"):
five reviewers, read only, each project's own work against its base, and which of its tests could check ours. Reviewed: OJZen/totk-mobile 510d95b
(two reviewers; Apache-2.0, its `core/` byte-identical to upstream b94670c; its own work is `script/prepare_graphics.py`, 1,164 lines of exact-match
replacements applied to `core/` at build time, 6,211 lines of `host/nvn_*`, kernel / services / iOS app, 74 tests; it credits masagrator's
FPSLocker-Warehouse TotK 1.4.3 patch file, which has no licence: facts only), enginefuture/bayonetta3-mobile e5ce116 (MIT; on blade2-mobile c981fcb,
62 changed or new files, about 50 tests, `docs/UPSTREAM_CHANGES.md` items 1-56; 308 NVN entry points against our 296, none of the 47 extra called by
our games), huajiaoshu520/acnh-mobile c2af194 (MIT; on upstream b94670c plus nxnative files of 10-15; 72 tests), enginefuture/dr3-mobile a31c36c
(MIT; a 3,087-line patch on nxnative 9579030; 27 tests), ha7rock/blade2-mobile 43e760e (Blade II, UE4 4.17; on upstream f43480d), vecStardo/VA-11_ns2ios
(GameMaker through GLES, no NVN, not Unity: little to take, its AudioOut is wrong), hyc1743/super-mario-odyssey-ios 6cd140f (its own rewrite),
armyja/nxnative-aa-port df997fa (Ace Attorney: Unity 2017 / 2022 IL2CPP and RE Engine, on nxnative 280ead9). Correction to the section above: the
`DM_UNIMPL_ABORT` strict mode and the control length by GPU code version are bayonetta3-mobile's (its vendored blade2 copy), not blade2's. "Checked
here" = confirmed in our code when merging; the rest is the reviewer's reading.

Correctness, small:
- **Pipeline keys carry the blend state of targets whose blending is off** (checked here: `pso_key_fill` copies `blend[i]` for every target with a
  format, the descriptor reads it only when `blend_en` has the bit). Measured on our own `pso_user.tsv`: Tears of the Kingdom's 729 unique keys
  become 397 with disabled slots zeroed (older keys 3,628 → 2,441); manosaba 32 → 32. About 45 % of TotK's pipeline builds are duplicates (super-mario-
  odyssey-ios 693bc6b had the idea).
- **SyncWait on a sync never fenced** (checked here): ours returns TIMEOUT_EXPIRED ("as on a real device", and `t_nvn` asserts it); the SDK's enum
  docs: "Waiting on a sync object that has not been signaled with a fence command is an error and will produce a result of FAILED" (dr3 follows them,
  also for QueueWaitSync). Count such waits in both games first: a loop waiting until "not timeout" spins forever on ours.
- **The Metal 3 buffer table stops at 512** (checked here: `M3_BUFS` 512, `m3_buf_add` drops the rest; a miss skips the bind or the indexed draw
  with one red line). TotK has 1,294 live pools, so the Metal 3 build (`dmhost_m3`, the iOS 18 app) would lose draws; acnh-mobile c2af194 fixed missing
  houses and bridges this way (`t_m3_buffers.m`, 1,537 buffers). Index it like `nvn_pool_find.h`.
- **Dirty-page state kept per pool range, not per host page** (acnh-mobile `nvn_dirty.c` page records, `t_dirty_shared.c` drop-in with our API): a slot
  reused on the same 16 KiB page loses a CPU write by reading; `DM_NVN_DIRTYCHECK` saw none in our runs (latent; the old botw-mobile 1d69c8ae idea).
- **The self-sampling guard compares serials** (bayonetta3 compares storage ranges): our storage sharing gives two serials one texture, so a merged
  pass may sample a partner attachment without a split (item 42's count may be low).
- **Cull FRONT_AND_BACK drops points and lines** (checked here: both draw paths return for every primitive); the SDK says only polygons have a face.
- **A pass without attachments is sized from the scissor only, at one sample** (bayonetta3: the larger of viewport and scissor, samples from the
  multisample state; `test_attachmentless.m`: a 4x3 viewport gives 12 fragments). TotK's known case has a 1x1 scissor.
- **The silent audio sink bursts after an iOS suspension** (checked here: `null_sink` adds 5 ms per pull and never resets; acnh-mobile and nxnative
  3bd1e9e restart when more than 100 ms behind). Our lifecycle test simulates the background in-process and cannot see it.
- **The display-link step ignores the applied rate** (checked here by reading: power-saving mode runs the link at 30 Hz while the step still counts
  60 Hz ticks × the game's interval, so TotK at interval 2 with the 30 fps setting shows 15 fps).
- **Stick directions are never set as NpadButton bits 16-23** (checked here; the SDK names StickLLeft = 16 … StickRDown = 23, dead zone a circle of
  AnalogStickMax / 2; blade2: UE4's tutorial card never closed without them).
- **Crash and diagnostics bugs of ours**: the backtrace dereferences frame pointers inside the signal handler unguarded (a corrupt x29, as in a
  memory-corruption crash like 72, loses the rest of the report; totk-mobile reads each frame with `mach_vm_read_overwrite`); the svc stub log runs
  `module_of` on a module-relative address and always prints "?", Break mixes two modules; 13 of 74 `RED_ONCE` tags are shared by unrelated messages
  (checked here), so the first to fire silences the others; the shader capture writes `.ctl` before the code (checked here: a crash between leaves a
  capture later runs trip on; bayonetta3 writes atomically).
- **Our texture view is 10 bytes, the SDK's 16** (bayonetta3 `nvn_view.h` with the SDK's byte vector; TotK reports API 55.15 as Bayonetta's 55.12):
  `SetDefaults` leaves bytes 10-15 as they were and a view's depth / stencil mode is not stored. Check TotK's own setters in Unicorn first.
- **The Mac mouse monitor swallows title-bar clicks** (checked here: a mouse-down outside the game rectangle returns nil); App Nap is never held off
  (blade2: a hidden window without sound turned a 10 ms sleep into 200 ms).

Tears of the Kingdom:
- **MINIMAL_LAYOUT and LINEAR_RENDER_TARGET sizes against the SDK** (deepens 82): totk-mobile's rule (`prepare_graphics.py:1112-1133`: with 0x200 and
  no packaged block height, the whole mip chain with one-GOB blocks when smaller; 0x40 alone is pitch-linear, "the game sets only 0x40 plus an
  explicit stride and writes the rows with the CPU") and eight sizes from TotK 1.4.3's SDK (`test_nvn_objects.c:81-128`), six of which ours gets
  wrong by a model of `nvn_tex.c`: 320x320 0x3b 204,800 (ours 245,760), RG8 260x260 with 9 levels — the terrain normal maps — 214,528 (332,800),
  3D 64x64x13 212,992 (262,144), a 320x320x6 array 1,228,800 (1,474,560); the grass height maps (R16 260x260, 0x40, stride 640) 166,400 against our
  221,184. Their cave note: two 200 KiB textures right before vertex data, "both CPU writes must stay inside"; our `mtl_write_texels` re-swizzles a
  whole level in our larger layout and would write past the slot (unverified in the game). Also: super-mario-odyssey-ios compares each BNTX's own
  total size with the layout code (TotK has 14,921 BNTX files to check ours against); blade2 found 8-GOB blocks for BC1 / BC3 at 512² where ours gives 16.
- **Pre-rendered cutscenes held at 30 fps** (FPSLocker's TotK file and totk-mobile `game_timing.py:84-96`): a movie flag set at main 0x2b36100 and
  cleared at 0x2b33918 (FPSLocker stores the bool passed to `nn::oe::SetUserInactivityDetectionTimeExtended` there); FPSLocker marks TotK's cutscenes
  "must be blocked to certain framerate to not cause issues". Ours paces them like play. Our blood moon crash follows a skipped pre-rendered movie: a
  hypothesis to test (the skip with the cap, without it, and with the 30 fps product). The cap can come from our own oe HLE, no code change
  (checked in item 130: not alone, the call is made for every cutscene; the hold keys on its caller).
- **Dynamic resolution's time budget stays at 30 fps** (FPSLocker: main 0xA1B94 loads a factor of target / 30000; README "must be adjusted to target
  other FPS"): at 60 fps the game still budgets 33 ms, so it never lowers the resolution to hold 60 (and item 46 never saw it move). A HOSTREFS pair.
- **The HeapDef values for a 1080p scene** (for 95): totk-mobile patches `Product.Nin_NX_NVN.HeapDef.byml.zs` (source SHA b84a6a64…) at decompressed
  offsets 2732 (DynamicTextureAllocatorMem 113,664 → 163,840 KiB) and 2920 (Graphics 201,728 → 251,904), with the stock application pool.
- **The wave program draws wide lines** (totk-mobile `nvn_wide_line.inc`: vertex 0cd9edc0a623c237 / fragment e04d541253557805, expanded on the GPU):
  ours draws 1 px (and no run of ours has reached it).
- **Render targets of virtual pools in a heap** (totk-mobile `nvn_texture_alias.inc`: Great Sky Island has 354 targets, 373 MiB of layouts in a 111 MiB
  pool; a heap of 1.25× the pool places all but 3 MiB): ours shares a texture only for identical storage, so about 3× the memory; for iOS (62).
- **A different frame step** (FPSLocker: one routine replaces the controller body at 0x2db18c, step = clamp(elapsed, 1/target, 1/15), the fps as float at
  sys+0x224 read by the two producers; totk-mobile repays short frames): the interval never flips. An alternative to measure against ours, not proven
  better. FPSLocker also says gameplay speed, physics, UI and logic follow this step alone (evidence for 71c), "remove double buffer" at 0xBE14A8
  (measure whether our acquire ever waits first), and the world clock's per-frame increment at 0x68c15c (a probe for 71c).
- **Patches always applied, chosen by data on the page** (totk-mobile: one patched main for Mac and iPad, the ceiling, render scale and day rate as
  floats in the host page): `fps60`'s `mov w8, #1` as a load from the page (2 = the original) would leave iOS no option pages to embed (57, 62).
- **Code caves in the rewriter** (totk-mobile `patch_module.py`: routines appended to the patch area, originals checked, Unicorn tests that registers
  and flags are kept): what item 63's veneers need.

Shaders (one chain rebuild with 100-102):
- **One varying interface as a fallback** (bayonetta3: every vertex shader exports the union of all fragment inputs, 2,045 libraries against a 14-location
  interface, no pipeline failure in 181 new pairs): ours pads a vertex shader only for pixel shaders recorded together in `pairs.tsv`, which
  `ProgramSetShaders` alone writes (checked here: pairs made through BindProgram masks are never recorded). Older TotK runs had 19-45 "not written by
  vertex shader" failures each (none in the latest 5); iOS captures nothing. TotK's fragment shaders use 24 locations. A fallback library for pairs not
  recorded, and pairs noted at bind time.

iOS:
- **The audio session asks for nothing** (checked here: no preferred sample rate or I/O buffer): RemoteIO then pulls about 1,024 frames a callback
  against our 4 × 240 queued (nxnative 18c6143: every callback underran, crackling; with 48 kHz / 5 ms none); an underrun counter in AudioOut (ours
  zero-fills silently); the unit rebuilt after a media services reset (super-mario-odyssey-ios d30e994).
- **Low Power Mode or thermal Serious → 30 fps automatically** (super-mario-odyssey-ios); `ENABLE_DEBUG_DYLIB: NO` (a Debug build moves the embedded
  images into a debug dylib; blade2).
- For 62: iPhone Air numbers from Bayonetta 3 (1568x720 → MetalFX 2736x1256, median 17.1 ms, GPU 9.4 ms, footprint 2.4 GB, thermal 1, no jetsam; a
  present buffer lost to GPU recovery at launch, the class of 80), totk-mobile's 12.5 GB reservation with extended addressing on an iPhone 15 Pro and
  M1 iPads (exit if above 2^39) and the stock application pool, RomFS as one image or 1 GiB parts with an index (totk-mobile, acnh-mobile), "do not
  push the large resource files with devicectl directly" (totk-mobile: it can exhaust the Mac's memory and disk).

Tests to take (vectors are data; code reimplemented): the layout rows and WriteTexels regions (totk-mobile, bayonetta3), the format swizzle
read-backs (totk-mobile), `t_dirty_shared.c` and `t_m3_buffers.m` (acnh-mobile), the vertex-fetch GPU vectors (dr3: UNORM8 {0, 64, 128, 255} →
{0, 0x3e808081, 0x3f008081, 0x3f800000}, a NaN payload kept; our `main0_vi` has no GPU test), the svc vectors of totk-mobile `test_kernel.c` (our
`svc_dispatch` takes the same register array: SetHeapSize granularity, a regrow reading zeros, MapPhysicalMemory, QueryMemory, the core table, event
errors; two fail ours), the time-zone cases (acnh-mobile, without its invalid-date case: the SDK returns 0 results with success), RELR vectors and a
pool-lookup overflow case, NaN references for `t_alpha`, depth-bounds pixels (bayonetta3), the rewriter's word sequences (bayonetta3
`test_rewriter.py`, super-mario-odyssey-ios's synthetic NSO), the vibration pulses for 67 (totk-mobile's captured TotK landing pulse, dr3's envelopes).

For 61: deferred bindings frozen at submit and binds of more than 32 handles split (bayonetta3 `nvn55_snapshot.h`; ours re-reads them at each walk and
drops binds over 32), a per-frame critical-path trace (totk-mobile `timeline.c`), per-pass costs and full-screen draws as compute (totk-mobile: a
dependent pass 38-57 µs on an M4).
For 64: the 47 NVN functions bayonetta3 has and we do not (deferred binds, ClearBuffer, indirect draws, depth bounds, patch size, ZCull, subtiles,
getters), several descriptor pools, tessellation through a Metal 3 command buffer, CopyCommands as a staged copy with a handle registry (dr3, 64
checks), symbol resolution as Horizon's rtld (default-visibility symbols through the global list: UE4 overrides malloc; ours takes the module's own
first), a SystemEvent kept while its detached read end waits (blade2, UE4's audio renderer), IL2CPP's `stat` and the crypto RNG (armyja), AudioOut
volume and counters, named time-zone rules, `IsSaveDataExisting` from the directory.
For 67: TotK's vibration calls (`nn::hid::detail::` permit / send / enqueue + flush / actual; handles 3 and 0x10003; bool arguments not extended by
Switch callers, so mask them) and the landing pulse {0.1, 198.7 Hz, 0, 299.9 Hz}.
For 72: the cutscene cap above, the guarded backtrace, and a thread dump when frames stop (107).
For manosaba: IL2CPP class names at a crash (armyja: class at +0x10 / +0x18; our `il2cpp.json` has 106,577 methods), FIFO frame consumption for a
game that steps per frame (acnh-mobile; manosaba on the iPhone dropped 14 stale frames by tick 1,549).

Checked, not better or already here: totk-mobile's audio renderer, saves, arbiter (it refuses type 3), per-axis stick clamp, rewriter jump tables,
D-pad background fix (ours: the overlay); bayonetta3's two signed main images, VP9 transcoded to AVC, launcher page; acnh-mobile's packaged texture
copy (the SDK says ignored), conditional ledger, pool cache; dr3's program snapshot, socket facts (to 84 / 64); blade2's HLE mixer; VA-11 as a whole;
super-mario-odyssey-ios's audio ring and touch ABI; armyja's system clock context (ours better).

### Evening sync of 2026-10-20 (to do)

At the user's request ("before going on, sync the upstreams so we do not build what they have just built"): every reference clone fetched again
in the evening, and only what was new since the morning's reviews read (four read-only reviewers; reports in the session scratchpad
`review5/`). New since the morning: seedds/nxnative 7e2c741..b932024 (15 commits, PRs #11-#20), ly05010419/MacNativeGames 15b1afd..b75be8c (6,
40 PRs / 25 issues updated), jhaizhou-ops/d2r-mobile 6996690..53e7c5b (PRs #96, #97, #100), huajiaoshu520/acnh-mobile ..bd2e176, Apolinario-coder/
SkyRecomp ..c8de7ab, catoncat/d2r-port-radar ..7bffcbd (264 entries), side branches of GirlNet/d2r-mobile (`codex/p5r-runtime` 639cf35),
hyc1743/super-mario-odyssey-ios (`codex/nxnative-verify-once` ea19678), Nishikori-Yui/lordran-native (main ecb74d5) and peiikai/LASRecomp (MIT for its
own code; `beta_5` f2c6fb4, `test-perf-7` 13f54af); and a new project, MZhao-ouo/Touchable 5cab3ac (MIT, on upstream 2eb5758; Rhythm Heaven and
Silksong). OJZen/totk-mobile had nothing new after 510d95b. "Checked here" = confirmed in our code when merging; the rest is the reviewer's
reading.

Changes to planned items:
- **131**: the two values main 0xa1b8c subtracts come from one NVN function pointer (main+0x396e420) called with the device and a counter
  address, are multiplied by 0.001 (ns → µs, main+0x2d02608) and divided by 16,666.666 µs (main+0x2d02094): the game's GPU time as a share of a
  60 Hz frame (checked here). Our counter timestamps are the submission's completion time for every report in it and `DM_NVN_TS_LATE_US` adds
  one constant to all, so the difference can read about 0 and item 46 did not test this path. Probe s0 / s1 at 0xa1bb8 (and the multiplier
  after main 0xd5d80, nxnative's render-scale site) first; if the input is 0, timestamps at the command buffer's GPU start / end (nxnative
  `rep_ticks`, its BotW mode) before the budget factor. GPU times taken while held at 30 fps read high from the clock drop (LASRecomp 4e77c26:
  15.9 against 9-11 ms). nxnative does not touch 0xA1B94.
- **133**: nxnative now decides TotK's step on the host in whole vsyncs and holds each frame exactly that long (`nvn_fstep.h`, `hle_nvn.c`
  `fstep_tick`, `t_fstep.c` about 19 checks: hysteresis, a trial at k−1 after 30 frames that fit, debt paid back): the step equals the time on
  screen, and its kmin follows the movie hold. Ours steps by a mean of five intervals and, uncapped, renders up to 120 fps on a 60 Hz panel
  (half never shown) or lets the game's controller flap on 120 Hz displays (543 interval changes in 120 s, item 53). Same producers (our
  `realtime` HOSTREFS), so it is a switch beside ours; theirs stops at 60 fps.
- **134**: nxnative b301431: an AudioOut with no device callback for 200 ms moves to the silent sink and retries after 1-8 s, a failed Start
  falls back instead of dropping the unit for good (ours `host/hle_audio.c:244`), and the media-services reset rebuilds every unit.
- **94a**: the Great Sky Island grass has a second, proven cause (border colours below, 161): fix that first, then judge the write-back.
- **100**: integer outputs belong to (shader, target kind), not to the shader (162).
- **62b**: libvpx as the iOS VP9 path; GirlNet removed VideoToolbox's VP9 probe, which faulted in movie start on its iOS target (76ca341;
  libvpx 1.15.2 recipe in 4fc3114).
- **67**: Touchable's design (`ss_haptics.c`, `Haptics.m`, `test_haptics.c`: the stronger side per band, intensity √(low²+high²), sharpness from
  log2(f/40)/5, values cleaned, suspend reasons and an epoch so old pulses never replay, a muted 30 s looping player on the phone, a plain player
  on `GCController.haptics`, device-confirmed on a DualSense); its handles are made up and it has no batch send, which TotK uses.
- **151**: two more long-play crashes at the same site (MacNativeGames #57: main+0x1561c through 0x1bd01c4 after 72 min and 2 h 13 min), plus
  GPU memory at a 5 GB budget after 2 h 43 min (#72) and a footprint of 3.4 → 6.5 GB in 110 min (#74): run our own long soak first (166).
- **139**: hair and cloth at 60 fps (163) is a second TotK user of code caves.

New references (port radar and the reviewers): yokeyhh/switch-port-to-apple (a porting playbook on d2r-mobile and totk-mobile, MIT),
DanielZhangyc/smbw-mobile (Super Mario Bros. Wonder on the Mac, calibrated Switch 2 Pro controller input and dual-band rumble, MIT),
CODPHISH/ns2app (Silksong on iOS), WangTingYeYe/rtg-mobile, rootrd/dontstarve-harmony (NVN → Vulkan), two A12Z iPad Pro D2R ports with per-slot
bindings (aerolithink, bluewater0121), dodola's `codex/zelda-echoes-of-wisdom-android` and its ordered NVN worker (61). TypeSafe's Jev model
(d2r-mobile's author drives multi-game iPhone tests with it: which key to press on each screen; not published yet) is noted for the test
automation question.

Checked, not better or already here: nxnative's BC render targets (our `blk_aux`), attachment-less passes, render scale (our `DM_RENDER_RES` is
absolute and keeps the dynamic multiplier), translator phi fix (no texture-array binding in our 31,392 reflections); MacNativeGames' UIScene
lifecycle, audio ring trim, shader list (its 492 1.4.3 survey shaders are all in our set); SkyRecomp's 120 fps step (ours does the same) and jump
tables; Touchable's texture paths, fs, audio mixer, Python NCA extraction; GirlNet's linear R8 / RG8 views (it rounds unaligned strides),
compressed atlas re-uploads; lordran's in-place re-upload (reverted; ours waits for the fence).

### Plan: all of the wider review, simple to complex (2026-10-19)

Everything above is to be done, in this order. `[ ]` open, `[x] batch N` done in the commit whose subject starts with "Plan batch N", `[-]` measured and not needed (with the number). Each item gets a
unit test and a broken version that must fail it; each batch ends with the gfx tests (Metal 4 and Metal 3), the host tests, a manosaba and a Tears
of the Kingdom smoke run, then one commit. Sources and evidence per item are in "Wider review of 2026-10-19" above.

Tier 1, small and local. Correctness:
- [x] batch 1 · 1 svc 0x34 type 3 (64-bit wait if equal), unknown types refused by 0x34 / 0x35, an arbiter race test (`host/tests/t_arbiter.c`; the
  race also with kernel increments needing 177,412 retries against 200,000 user-space adds)
- [x] batch 1 · 2 controller face buttons by the printed letter on Nintendo controllers, a pure mapping function and an 18-case table test
  (`host/gamepad_map.h`, `host/tests/t_padmap.c`; the device check is 66)
- [x] batch 1 · 3 the BindProgram stage mask at the three scans (`defer_try` Q scan, `fuse_try` steps four and five; `t_defer_chain` mode 6 covers the
  first: the old code deferred and read stale pixels; no test reaches `fuse_try`, whose two sites take the same call)
- [x] batch 1 · 4 sampler LOD bias and min / max reduction (`gfx/tests/t_samp.m`, on the GPU; macOS / iOS 26, reported once before that)
- [x] batch 1 · 5 GPU time per frame as the union of command-buffer spans (`t_gputime`); `@gpu_ms` is the union from batch 1 on, `@gpu_sum_ms` the old
  sum, so GPU numbers above this plan are sums
- [x] batch 1 · 6 a fault guard around uniform snapshot copies (`t_usnap` section 5, on this thread and on the workers; without it SIGBUS)
- [x] batch 1 · 7 overlay directory listings without the 2048-name cap, duplicates found by hash, kept compact (`t_fs`: 3005 names)
- [x] batch 1 · 8 the silent NVN no-ops red on first call; SetDepthBounds reported when enabled (`t_nvn` 7b)
- [x] batch 1 · 9 precise SleepThread (`host/tests/t_sleep.c`: mean oversleep 0.004 ms, one nanosleep 1.19 / 4.12 ms on 5 / 33.3 ms)
- [x] batch 1 · 10 rodata read-only after relocation (`host/rodata_seal.h`, `DM_RODATA_RW=1` leaves it), svc 0x02 with Horizon's checks; read / none
  reported, not applied: Tears of the Kingdom only asks for read-write (`t_guest_mem` 7, 8)
- [x] batch 1 · 11 the 8 / 16-bit UI2F / I2F vertex formats (`gfx/tests/t_vfmt.m`); Metal converts 8 / 16-bit integers to their value for a float input
  and refuses 32-bit ones (measured), so the 32-bit and 10:10:10:2 kinds stay unmapped
- [x] batch 1 · 12 thread dumps list the handles of a WaitSynchronization with each one's state (`t_waitsync`)

Tier 1, performance:
- [x] batch 2 · 13 busy-yield back-off releasing the virtual core, a power line every 300 frames (`t_sleep`: 20 ms of yields 354 against 81,062
  plain). Off by default, `DM_YIELD_BACKOFF=<µs>` turns it on: in Tears of the Kingdom's prologue on the M5 it cost energy (two runs, 0.27-0.38 W
  against 0.23-0.24 W a 300-frame window; wakeups 2,700-3,800 against about 580 a second; the work moved to performance cores, and frames
  2100-2400 fell to 23.5 / 25.4 fps both times). The power line shows manosaba at about 25,000 wakeups a second, all on performance cores: a
  thing to look at
- [x] batch 2 · 14 targeted wakes (`t_waitsync`); upload waits on a condition variable (`t_upwait`: 0.015 against 1.03 ms after completion); the audio
  renderer blocks on buffer releases (`t_audio` scenario 3)
- [x] batch 2 · 15 replay lookups: per-thread pool cache (`t_poolcache`), dirty range on the texture and `mprotect` over page runs (`t_dirty_slots`),
  residency no-op skip (`t_poolcache`)
- [x] batch 2 · 16 a spare texture for re-uploaded CPU textures, dropped after 120 submissions unused (`t_texcheck` 4b)
- [x] batch 2 · 17 reflection summary per program, attachments walked once per pass (`t_zskip`)
- [x] batch 2 · 18 libvpx row multithreading (`DM_VPX_ROW_MT=0` off): the Ganon movie 1.02-1.03 → 0.95 ms a frame, frames identical

Tier 1, iOS and Mac:
- [x] batch 3 · 19 a Metal 4 launch check with a screen (Metal 3 for the iOS 18 build; `DM_TEST_NO_METAL4=1` shows it anywhere)
- [x] batch 3 · 20 game data excluded from backups (RomFS and sysfont outside the bundle; the boot log reads the value back)
- [x] batch 3 · 21 `GCSupportsGameMode` (iOS 18.0-18.5 read only this one; `LSSupportsGameMode` is from 18.6, Apple's reference)
- [x] batch 3 · 22 `env.txt` switches on the device: the bundle's (`DM_ENV_FILE` at build) then Documents' (`host/envfile.h`, `t_envfile`)
- [x] batch 3 · 23 audio notifier thread at USER_INTERACTIVE (`t_audio`)
- [x] batch 3 · 24 compressed files stored, not deflated, in the IPA (130 MB of `.zs`: 1.66 s deflated into a larger archive, 0.26 s stored;
  `run.sh ipa` reports the count)
- [x] batch 3 · 25 iOS 17 code guards (`@available` around the residency set and `hasPerformanceProfile`): no unguarded API left for an iOS 17
  target (`clang -fsyntax-only -target arm64-apple-ios17.0 -DDM_NVN_M3=1 -Wunguarded-availability`)
- [x] batch 3 · 26 quit waits for a save in progress, 15 s on the Mac, 3 s on iOS (`dm_fs_save_wait`, `t_fs`)
- [x] batch 3 · 27 a covered Mac window discards presents (`DM_OCCL=0` off, `DM_TEST_COVER=<s>,<len>` to check). The window check is still to
  run with the display on: during the 2026-10-19 run the display went to sleep (11:25), the display link stopped and no occlusion change came

Tier 1, tooling and diagnostics:
- [x] batch 4 · 28 one game at a time on the Mac (`host/game_slot.c`: a flock(2) held for the life of the host and a holder file; a run for the
  player, the .app or `DM_SLOT_OWNER=user`, stops an agent's run with SIGTERM, any other run waits; `DM_SLOT=0` off, `DM_SLOT_DIR`; `t_gameslot`)
- [x] batch 4 · 29 a stamp of the rewriter's sources in the product (`# REWRITER` in each fixups header: `tools/rewriter_stamp.py`, patch_nso.py and
  the game's rules.py; the host is compiled with the same value and the loader says when a product differs; `tools/tests/t_rewriter_stamp.py`).
  It found the manosaba product made by an older rewriter (rebuilt: modules identical, stamps added), and it shows that the Tears of the Kingdom
  default product is the 30 fps one kept on purpose since dba469f (its "frame step: main has no host page" red line is expected; `product60`,
  the 60 fps one, now carries the stamp)
- [x] batch 4 · 30 the rewriter stops on a system register outside NZCV, FPCR, FPSR and DCZID_EL0 (`tools/sysreg.py`, `tools/tests/t_sysreg.py`); neither
  game has one
- [x] batch 4 · 31 the SDK's abort text in the log: the last 8 OutputDebugString lines and AbortImpl's return address printed at a Break
  (`t_guest_mem` 9)
- [x] batch 4 · 32 the last 256 lock / condition variable calls in thread dumps, timed waits left out (`DM_KTRACE=0` off); `DM_DEBUG_CV_LATE_WAIT=<µs>`
  starts every condition variable wait that late (`t_ktrace`)
- [x] batch 4 · 33 upstream's `t_loader_weak` (`host/tests/t_loader_weak.c`: absent weak imports stay 0, our loader already did)
- [x] batch 4 · 34 `DM_MEMSNAP=frame[,frame...]:prefix` (`host/memsnap.c`: resident readable pages of the guest block and the modules' data;
  `t_memsnap`) and `tools/memsnap.py` summary / diff / refs (`tools/tests/t_memsnap.py`)
- [x] batch 4 · 35 GitHub Actions (`.github/workflows/ci.yml`, macos-26): the hosts of every game (Metal 4 and Metal 3), the host tests and the tool
  tests. Its steps ran here on a clean copy (all pass; on a copy with a build error, a failing host test and a failing tool test each step
  failed); the first run on GitHub comes with the next push
- Found while checking batch 4: `gfx/tests/t_ussort` failed about one run in ten (600 bindings counted as 607 or 614): a block whose four probes in
  the seen table find no place is collected again at each later binding, as the code says, and the merge drops the repeats; the test now allows
  that (and still fails when nothing is deduplicated). The host build has 34 compiler warnings, the same before batch 1

Tier 1, nxnative after 2b31dbd (reviewed through 19d7a70 on 2026-10-19 afternoon; MIT; batch 5, before Tier 2):
- [x] batch 5 · 73 the host races of nxnative 8530042. MapMemory / UnmapMemory were already serialized here (`g_mapmem_mu`), so of the map fix only
  QueryMemory's unlocked reads were missing: now under the registry lock (`t_guest_mem` 10: 3,000,000 queries of a thread's own stack while two
  threads unmap and map 512 others; unlocked, 32-123 of them wrong in every run). A SystemEvent's binding ends with its write handle, the signal
  looks up and signals under one hold of G (`t_sysevent`: before, the signal went to a decoy on the freed numbers). Audio output backends change
  under one lock with the backend recorded, a second consumer on one output is counted and reported red (`t_audio` scenario 4: before, 14-58
  second consumers or a hang in every run). CloseAudioRenderer joins the render thread, which closes its output (`t_audren`; `dm_pcmout_close`
  frees the AudioOut slot, `t_audio`)
- [x] batch 5 · 74 the yield back-off start, `DM_YIELD_BACKOFF=<cap µs>[:<start µs>]`, default 2 ms (`t_sleep`: a 1.5 ms run does not back off; the old
  200 µs start fails it). Still off by default: in Tears of the Kingdom's prologue (30 fps product, two runs each, 120 s) 0.206-0.209 W with it
  against 0.204-0.212 W without, wakeups 588-603 against 581-583, the same frame times; it barely fires there now (one window of 284 sleeps).
  manosaba's guest threads never yield, so it does nothing there. `DM_YIELD_BACKOFF=250` no longer costs frames in Tears of the Kingdom
- [x] batch 5 · 75 the c1 guard by interval arithmetic over the whole index, every stage (`gfx/tools/c1_guard.py`, `tools/tests/t_c1_guard.py`: the old
  decision fails 9 of 15, both ways: it passed a literal past the data and compute reads, and it listed `_5 & 3u`). Over chain b-1e41705de87b it
  lists the same 49 shaders as the old one (all 277 compute c1 reads proven), so nothing changes at run time
- [x] batch 5 · 76 two more power lines: render-target traffic a frame and guest CPU by Switch priority with the busiest threads (`t_threadcpu`). manosaba's
  guest threads use 53-103 ms/s in all: its 25,000 wakeups a second and full performance-core share come from host threads
- [x] batch 5 · 77 checked: the opening cave's stone blocks and steps beside the torch are lit warm here (prologue frames 1500, 1600 and 2100, 30 fps
  product); not this port's bug

Tier 2, small code, one shader chain rebuild for all four:
- [x] Tier 2 · 36 the BAHS shaders: `engines/bnsh/shader_extract.py` reads every binary entry of a `.sharcb` (after nxnative's `bahs_shaders`),
  not only the one 0x60 bytes before each control section: 28,474 shaders offline instead of 26,849 (`tools/tests/t_shader_extract.py`; the
  old reader finds 1 of its 2), 1,551 of them new to the Tears of the Kingdom capture, which now has 28,652 (all build); 74 that only run-time
  captures had are now found offline, 178 are still capture-only (shaders built into main). Not read yet: the two 0x7d1-byte control sections
  of `Lib/NintendoSDK/TextureShader.bnsh` and the Title layout's `bgsh` (nxnative's extraction has 17 more keys). The per-shader cache key has
  no gap of the kind 8530042 records: the pairs reach it through the vertex shader padding, the profile rows the build reads (`nvn_c2`,
  `depth_mode`, now `alpha_test`) are in it, and the Ryujinx libraries are in the lock file. Checked on the way: compute code areas with the
  0x12345679 header (241) are translated from +0 although their instructions start at +0x100 (nxnative skips to it); harmless here, the header
  is zero past the magic in all of them and their one indirect jump is relative; to skip when a game's header carries data
- [x] Tier 2 · 37 FFMA / FMUL `.FMZ` (a zero factor gives 0, also against infinity and NaN; after tomodachi-native a0aa6c2). Neither game uses it:
  every FFMA / FMUL of both is `.FTZ`
- [x] Tier 2 · 38 SHF wraps or clamps to the width, and a shift of 0 no longer ORs both words (on the GPU it gave 0xffffffff for 0x89abcdef :
  0x76543210); I2I `.SAT` compares in the source's signedness, narrowing keeps the low bits. `gfx/tests/t_xlate_ops.py` runs 47 cases through the
  translator, SPIRV-Cross and the GPU (inputs from a constant bank so nothing folds them; LASRecomp d4c2ff6's vectors plus shifts of 0 and 32):
  23 fail on the old translator, 0 now. manosaba has 18 shaders with a register SHF, Tears of the Kingdom 35; the I2I forms occur in neither
- [x] Tier 2 · 39 the alpha test, as a game profile option (`alpha_test` in game.json; after lordran-native a63dd76): fragment shaders end with the
  comparison under function constants, specialized with ALWAYS at load and per comparison and reference for draws that need it
  (`gfx/tests/t_alpha.m`: 8 comparisons x 3 alphas, the early return, reference changes, a shader without alpha; broken host key, unbraced
  blocks or no rewrite fail it). Off for both games, which never call ColorStateSetAlphaTest: Metal builds no pipeline from an unspecialized
  function, the first specialization costs 2-18 ms per shader on the M5, and reading the values from a buffer would leave a discard in every
  fragment shader (no hidden surface removal)
- Chain 1e41705de87b → bab8d4746968 for 36-39 (lock: the patched translator). Both games build every shader; the c1 list is the same 49;
  Tears of the Kingdom's prologue and manosaba's shots are within run-to-run variance (the prologue's s1300 differs 22-28 % between two runs of
  one build, from the cutscene's timing); both IPAs carry the new libraries

Tier 3, medium:
- [x] batch 6 · 40 host calls and svcs on a per-thread host stack (`host/hstack.S`, `hstack.c`; after MacNativeGames' hle_stack, MIT). Imports,
  takeovers, the NVN functions, the movie vtables, the audio renderer's thread entry and the unimplemented stubs run on the guest thread's own
  pthread stack (1 MiB), svc gates too; the guest stack holds 32 bytes at an svc and none at a host call, where a RomFS open with the overlay kept
  about 5.6 KB of path buffers (read from the code) and a first `localtime_r` 4,504 bytes (measured). Tears of the Kingdom has four threads with 4
  KiB guest stacks (VibrationThread, two DecompThread, gmd::GameDataMgr). A record at the top of the host stack is a frame record back to the guest
  caller (crash, sampler and thread dump walks take the one step between the stacks; `DM_GUEST_RA` for the NVN diagnostics) and gives
  GetThreadContext3 the guest's sp, x19-x28 and the call's arguments: manosaba's 307 GC context fetches in a run all reported an sp on the guest
  stack. `host/tests/t_hstack`: the switch with 64 KiB used on a 16 KiB guest stack, svc registers, nested calls, stubs, three paused contexts, the
  frame walk; `DM_HSTACK=0` faults on the guard page, the context as before fails all three pauses. The libc memory, string and math functions stay
  direct (each at most 144 bytes of stack, measured by the test); no host function the guest calls takes stack arguments (`tools/hle_args.py`, 427
  functions). Thread dumps show each thread's least free guest stack at svc entries (Tears of the Kingdom's prologue: 3,808 of 4,064 bytes left at
  the least), a `[stack]` line below 1 KiB. A wrapped call costs 5.3 ns more on the M5; both games' frame times, energy and shots are unchanged.
  `DM_HSTACK=0` turns it off
- [x] batch 7 · 41 occlusion counts across submissions (after lordran-native 3ef6915, MIT): before a visibility ring buffer is used again, the slots
  of the open samples-passed count in it are added into a CPU subtotal that reports include and a reset clears; before, both slot entries read the
  reused slot (`gfx/tests/t_visibility.m`, 23 checks on the GPU with API validation, Metal 4 and Metal 3: 64 then 32 samples reported 64, not 96;
  without the fold 6 fail). Tears of the Kingdom resets before every report (24-206 a frame in the prologue) but about 39 slots a frame belong to a
  count still open when their buffer comes round again: those counts read overwritten slots before (the prologue's torch glow and a lens flare
  differ between the builds; no console capture to say which is right). manosaba makes no occlusion queries. Found on the way: the overflow buffer
  of a submission with more than 8,192 counting passes was never retired (the check read `g_vis` after it was cleared; the test fails on the old
  line). The power summary has an occlusion line (reports, resets, open slots, folded slots a frame)
- [x] batch 8 · 42 draws that sample what their own pass draws (`DM_NVN_COUNTS=1` counts them): Tears of the Kingdom about 59 a frame in the
  prologue, full-screen passes at 1600x896 / 800x448 / 400x224 sampling the one level and layer they draw (most the first draw of their pass, 6 a
  frame after a texture-invalidating barrier that follows draws of the pass); manosaba none. On the M5 such a draw reads the attachment as it was
  before the Metal pass (the earlier draws are in tile memory, a pending clear is not in memory); measured in `gfx/tests/t_fbx.m`. Copying the
  attachment for every such draw (after tomodachi-native 4dbd30f / da08d11, MIT: earlier draws and clears visible) put two prologue shots outside
  the console's brightness ranges taken from the video of the opening the user found (youtu.be/IyCrvjFhkRE, the shots' frames matched by structure:
  s1800 left third 39.0 against 44.2-54.5, s1300 right 65.9 against 68.2-84.2; GPU p50 +2.4 ms, 112 copies a frame), while the M5's own reads and
  copies only after such a barrier stay inside them in every third. So the replay copies only where NVN defines the read (a draw after a texture
  barrier that follows draws of its pass: the pass ends, the whole texture is copied, the copy is bound in its place, `fbx_prepare` / `fbx_sub`): 2
  draws and 6 copies a frame, within run-to-run timing. `DM_NVN_FBX=all` copies for every such draw, `=0` never. `t_fbx`: 10 checks in each mode on
  the GPU with API validation; with `=0` the barrier cases fail
- [-] batch 8 · 43 checked: Tears of the Kingdom writes 43-80 timestamp reports a frame, one submission a frame holds two or more (a dozen
  TIMESTAMP_TOP to a pool); a load watchpoint on four of them (`DM_NVN_COUNTS_WATCH=<n>`, `DM_WATCHPOINT_LOAD=1`, new) saw no read in 15-30 s, while
  the type 0 report of that submission is read every frame by `nvnDeviceGetTimestampInNanoseconds` (main+0x76920, the frame-rate controller, which
  compares it with the CPU's frame start tick). No two reports of one submission are subtracted: the command buffer is not split at reports
- [-] batch 8 · 44 counted: no ClearTexture in Tears of the Kingdom's prologue or manosaba, and 4 render passes a frame end without a draw in the
  prologue (0 in manosaba): keeping clears on the texture would save at most those
- [-] batch 8 · 45 counted: about 31,000-34,000 system counter reads a second in Tears of the Kingdom (manosaba 4,500-9,800); a gate round trip
  costs 13-25 ns on the M5 (`t_hstack` prints it), so about 1.5 ms of CPU a second in all with the lookup and the clock: not worth an inline read
  that would have to keep the background freeze and `DM_DETCLOCK`
- [-] batch 9 · 46 counted (`DM_NVN_COUNTS=1`: how far each render pass's draws reach right and down, scissor and viewport, clear quads
  included): no pass of Tears of the Kingdom's prologue (90-122 with draws a frame) or of manosaba (2.5-5.5) draws only a top-left part, with or
  without a clear on load, so the render area stays the attachments' size. The console's dynamic resolution (900p, toward 720p while the camera
  moves: Digital Foundry) did not show here; counter timestamps reported 8, 15 or 25 ms late (`DM_NVN_TS_LATE_US=<µs>`, new: the game sees a
  slower GPU) changed the game's present interval between 2 and 3 (3, 5 and 16 times in 120 s against once) and still no pass drew a part.
  `gfx/tests/t_extent.m`: 12 checks on the GPU with API validation (after LASRecomp 59a7507's render area); with the viewport and clear quads
  left out 3 fail
- [x] batch 10 · 47 a live command channel, `DM_CMD=<file>` (`host/cmdchan.c`, after MacNativeGames' `TOTK_INPUT_FILE`): lines appended to
  the file run while the game runs: `key` / `down` / `up` with the DM_KEYS names, `wait <frames>`, `sleep <ms>`, `shot <png>`, `passdump
  <frames>` (now settable while running), `threads`, `memsnap <path>`, `hlecount`, `quit` (the orderly exit, code 0). A line runs once its
  newline is written (a reader stopping at the end of the file, as fgets does, would run half a line). `host/tests/t_cmdchan.c`: 24 checks (a
  table of good and malformed lines; a file a writer appends to); run as far as written, the line written in two parts fails it (4 fail);
  `t_hid_script`: the keys reach GetNpadState (2 fail without them). In Tears of the Kingdom: wait 300, shot, passdump 1 (frame 301 only),
  threads, key A (read by the game at 11.2 s), an unknown line skipped in red, quit: exit 0
- [x] batch 11 · 48 sead heaps and list watch (`host/sead.c`, after MacNativeGames' TOTK_CRASH_HEAPFIND / HEAPWALK / LISTCHECK):
  `DM_CRASH_HEAP=<reg>,...` on a crash and the channel's `heap <addr>` print the heaps holding an address, outermost first, and the block of the
  innermost one it is in; `heaps` lists every heap; `DM_LIST_WATCH=[heap+]<addr>,...` dumps the threads once when a watched list gets a null next.
  MacNativeGames' layout holds in Tears of the Kingdom 1.4.3 (names RootHeap, GraphicsSystem, Resource, Actor read back): at frame 900, 7,508
  heaps found in 1.3 s, in five classes by vtable (the game has no C++ type names): main+0x3900d18 (266) and main+0x39011d0 (53) all with whole
  use lists, main+0x3900e40 (7,178), +0x39013f0 (7) and +0x3945240 (4) with none at +0xc8, so a use list counts as broken only in a class that
  has whole ones (counted naively, 6,127 of 6,450 looked broken). An address in the Actor heap: RootHeap → sead::TaskMgr → ModuleSystem → Actor
  and its block. `host/tests/t_sead.c`: 19 checks on heaps built by hand (a child heap, a class without a use list, three kinds of break, a list
  watch); taking a heap without checking it reaches the address fails 2; `t_crash_stderr`: DM_CRASH_HEAP gets the register's value at a crash
- [x] batch 12 · 49 per-pass GPU time, `DM_NVN_PASSGPU=<tsv>` (after upstream 44a6770, MIT): Metal 4 writes timestamps into an
  MTL4CounterHeap around each render encoder and each dispatch (precise: dispatches share the copy encoder), Metal 3 samples at stage boundaries;
  a heap per submission in a ring of 8, invalidated before reuse (upstream's is not: a slot left unwritten would read an older submission's
  time). One row per pass; every 300 frames a line with the GPU time a frame and the five kinds of pass (size, formats, loads) that took the
  most. Tears of the Kingdom's prologue: 139-147 passes and dispatches a frame, 5.9-6.3 ms of increments against a GPU p50 of 6.8 ms (the union
  of the command buffers, copies included); most: 1600x896 with five colour attachments and depth 0.78 ms (5 a frame), 800x448 with eight 0.72
  ms (2), 1600x896 format 92 0.43 ms (1), 1920x1080 format 92 0.43 ms (2). Summed by a pass's index in the frame, as upstream logs it, the same
  run came to 13 ms (an index is no identity when frames have 100-260 passes). Passes overlap: on Metal 3 a light pass after a heavy one spans 13
  ms (its vertex stage starts during the heavy one's fragments) and adds 0. `gfx/tests/t_passgpu.m`: 8 checks on the GPU with API validation,
  Metal 4 and Metal 3; without the end timestamps no row comes out (both fail)
- [x] batch 13 · 50 soak and fixed-scene measurement, `tools/soak.py` (after MacNativeGames' `mac_v143_soak.py` / `mac_v143_smoke.py`; their
  fixed camera teleports a played save to world coordinates, which a new save cannot): `run` keeps the host going for M minutes with
  `DM_MEMSTAT` and screenshots and a quit through the command channel, then reports the memory slope after the warm-up (a quarter of the run, at
  least 2 minutes), frame and GPU time early against late, and red lines first seen late; `window` runs N times to a frame and gives the frame
  and GPU time over a frame window, with `--clean` emptying the save before each run. Tears of the Kingdom, 20 minutes from a new save:
  footprint −1 MB/h and Metal +0 after the first 5 minutes (124 → 2,648 → 2,638 MB), frame time p50 33.69 → 33.74 ms, GPU 7.18 → 7.12, no late red
  line; manosaba: +4 MB/h, 16.66 ms throughout, no red line. Frames 1800-3300 of the prologue, 3 runs: frame time p50 33.72-33.74 ms, GPU p50
  7.13-7.33 ms (the noise a GPU change there has to beat, about 3 %). `tools/tests/t_soak.py`: 13 checks; counting the warm-up in the slope
  fails 2 (a flat run reads as growth)
- [x] batch 13 · 51 lifecycle and swipe-away tests (upstream `ios/tools/lifecycle.sh` criteria, `logred.sh`): `DM_TEST_BG=<s>,<len>` on the Mac
  too (`host/front_mac.m`, the order and lines of the iPhone's scene events), `tools/lifecycle_check.py` judges a log (suspended while away,
  the guest clock the same back, audio paused and resumed, presenting goes on, no new kind of red line), `ios/run.sh lifecycle` runs it on a
  device with the background simulated in the app. Found on the way: the no-progress watchdog counted the background, so a run with DM_WATCH
  was killed 15 s into it; it now skips while the clock is frozen for the background (`t_watchdog`: 3 cases; counting it fails "away"). Tears of
  the Kingdom and manosaba with `DM_TEST_BG=40,20`: LIFECYCLE OK (the guest clock 217236.755 s at the background and at the return). A save
  killed at any moment (an app swiped away while suspended gets SIGKILL): `host/tests/t_fs_kill.c` kills a process committing saves 60 times
  at random (222 commits, 22 kills after a commit not yet reported): every file one whole version, the last reported or the next; a commit that
  removes the committed save before cloning in its place (`-DDM_TEST_FS_COMMIT_INPLACE`) tears it 14 times of 60. `t_lifecycle_check.py`: 10
  checks; not comparing the guest clock fails 1. The home button and the app switcher themselves need UI automation on a phone (item 70)
- [x] batch 14 · 52 scene render size on 1.4.3, `DM_RENDER_RES=<w>x<h>` with the rules product (after MacNativeGames PR #19, merged
  2026-10-19, MIT): main 0x585da0 reads the size from the graphics parameter into w11 / w10 and stores it as floats at renderer +0xcc4 /
  +0xcc8, from which every scene target is sized. The four words of the PR checked in our main: 0x585e48 `scvtf s0, w11` → `adrp x17, host
  page` (HOSTREFS), 0x585e50 → `ldp s0, s1, [x17, #16]`, 0x585e54 → `str s0, [x19, #0xcc4]`, 0x585e60 → `str s1, [x19, #0xcc8]` (CUTS). x17,
  not the PR's x3: the function (0x585da0-0x586000, 152 instructions) uses neither, and x3 is an argument register of the calls after it. A
  probe at 0x585e4c (now printing x9-x12) read the parameter once at boot: docked 1600x900 with w9 = 1920 output, handheld 1280x720 with 1280;
  `games/totk/frame_step.c` writes that at host page +16 unless `DM_RENDER_RES` is set, so the rules product looks as before (its pass sizes
  in one frame the same as the previous product60's). With 960x540 the scene passes are 960x536, 480x268, 240x134, the UI stays sharp; GPU
  p50 5.93 against 6.30 ms in the window to frame 3600. Without the host's value (an older host) the scene is 0x0 and the passes 8x8 and
  smaller. `games/totk/check_render_size.py <product> <main NSO>`: 8 checks in Unicorn (the page's size stored, w9's output size and x9 kept,
  the argument registers as before; the original stores the parameter's); with x9 as the MacNativeGames 1.0 code has it, the output size is
  overwritten (2 fail). `t_frame_step`: 22 checks with the size choice. `work/build/totk/product60` rebuilt with these rules (main cuts 20,
  sdk and subsdk0 byte for byte the same; the previous one is `product60.prev`); `verify/regress.sh` OK for it and for manosaba, after its
  gate bench got the host stack sources it needs since item 40 (it did not build since batch 6)
- [x] batch 15 · 53 frame pacing between 30 and 60 fps (after MacNativeGames' TOTK_UNCAPPED, MIT; the user pointed out MacNativeGames already has
  the 60 / 90 unlock, which product60 has since dba469f, and its uncapped pacing is the rest of this item): for a game whose step follows the
  measured frame time (`dm_game_realtime_step`, Tears of the Kingdom's rules product), the game no longer waits for the display: our clock paces
  it at up to 120 fps (its present interval × 1/120 s a frame) and the display link shows the newest finished frame at each refresh, which ours
  already did. `gfx/nvn/nvn_pace.h` `nvn_uncapped_hz`: off with `DM_UNCAPPED=0`, with `DM_PRESENT_HZ` and on a display of 120 Hz or more;
  `DM_UNCAPPED_HZ=30..120` or `display` (MacNativeGames' iPhone default, for heat). With the frame's own work made to take 14 ms
  (`DM_TEST_WORK_MS=14`, a new test switch at the frame's first submission, which the controller counts): before, frame time p50 32.7 ms and 64 %
  of the frames at 30-36 ms (the controller flapping between intervals 1 and 2, 543 changes in 120 s); now 17.8 ms, 66 % under 18 ms, 33.5 % at
  18-30, 0.6 % at 30-36, and 6,300 frames in 120 s against 4,200; a shot 75 s after start shows the same game moment in both (the step keeps the
  speed). Unhindered, the prologue runs at about 115 fps. `gfx/tests/t_pace.c`: 13 more checks; uncapping a game that steps per frame
  (`-DDM_TEST_UNCAPPED_ANY_STEP`) fails one. Found on the way: `DM_RENDER_RES` above the output size crashes the game (2560x1440 and 3200x1800,
  main+0x1c2f20, in its first seconds): now refused above 1920x1080 (`t_frame_step`: 26 checks). The 30 fps constants and the cutscenes that
  crash above 30 fps (BotWRecomp's list for BotW: stamina, arrows, shield surfing, Rito bridge...) need play: 71c. A window run (the display
  link path) is part of 71b
- [x] batch 16 · 54 user mod folders, `DM_MODS=<dir>` (`host/mods.c`, after SkyRecomp's `runtime/mods.c`, MIT; MacNativeGames and nxnative have
  none): each entry's RomFS becomes a layer of `DM_ROMFS_OVERLAY` in front of the game's (and of the game's own overlay, as Tears of the
  Kingdom's 60 fps one): `<entry>/romfs`, `<entry>/contents/<program id>/romfs` (an SD card's `atmosphere` folder copied as is),
  `<entry>/atmosphere/contents/<program id>/romfs` (a mod as shipped), or loose files; in name order, the first winning; names starting with
  `.` or `-` off; other games' folders and code (exefs, exefs_patches) left out with a line. The program ID matched without regard to case,
  and paths built with each folder's own spelling (the Mac's file system ignores case, iOS's does not). The log lists each layer, how many of
  the game's files it replaces and the files an earlier mod also has. On iOS `Documents/mods` when it exists. In Tears of the Kingdom a mod
  folder with two of the game's files copied unchanged (one as `01_test/romfs`, one as `atmosphere/contents/0100f2c0115b6000/romfs`): both
  opened from the mods (`DM_FS=1` now prints the host path too), the 60 fps overlay still after them, the prologue as before.
  `host/tests/t_mods.c`: 12 checks on every layout; paths built with the program ID as asked fail one
- [-] batch 17 · 55 measured (tomodachi-native d7f52e1 moved the AudioUnit's creation to a queue after CoreAudio once took about 40 s there):
  on the M5 the open takes 22-26 ms in the games (manosaba on its main thread, Tears of the Kingdom on the audio renderer's), a process's first
  AudioUnit 47 ms in a probe and 0.5-1 ms after; nothing here near a stall, so the open stays where it is and the moving's races with Start / Stop
  / suspend / Close are not taken on. The time and the thread are logged now (red above 1 s), so a device run shows the iPhone's (70).
  `DM_AUDIO_MUTE=1`: the sound card opened and fed, silence out (measuring at night); `t_audio` checks the blocks are still taken while muted
  (muting without taking them fails it)
- [-] batch 18 · 56 measured (the low-memory texture trim of justr4mzi's mush8 fork aafef22, ideas only): `DM_NVN_COUNTS=1` now counts the
  uploaded (CPU-filled) textures by how many submissions ago they were last used, every 300 submissions on the submit thread. Tears of the
  Kingdom's prologue: 364 of them, about 300 MB used in the last 30 submissions and 64 MB not used for more than 1,800 (of 2.0 GB Metal allocated,
  footprint 2.6 GB); manosaba has 40 MB of such textures in all. Dropping the cold ones would free about 3 % at the price of a re-upload when they
  come back: not now; with Tears of the Kingdom on a device (62) the memory-low lines say whether to. `t_uppool`: the census (one texture used
  again in the first bucket, eleven in 301-1800); with a later use not noted 2 fail
- [x] batch 19 · 57 code patches chosen at launch, one product for 30 and 60 fps (after tomodachi-native 0428c80, MIT): rules.py `OPTIONS`
  (`tools/patch_nso.py`: entries as CUTS or as HOSTREFS forms, checked against the original, not written into the image, listed in
  `<module>.options.tsv` with their final bytes; asserted not to overlap a cut and to be left alone by the rewrite), applied by the Mac loader
  before it seals the code to those `DM_OPTIONS` names, or else the product's `options.default` (`host/options.c`: originals checked again, an
  option whole or not at all, unknown names in red, `dm_option_on`). Tears of the Kingdom's changes are now the options `fps60` (the 7
  controller words), `realtime` (the frame step) and `scene` (the scene size); `games/totk/frame_step.c` and the uncapped pacing follow them.
  `work/build/totk/product60` is rebuilt this way with `options.default` = `fps60,realtime,scene`: with all three applied its main is byte for
  byte the previous product60's (sdk and subsdk0 too), `DM_OPTIONS=none` runs the game's code as shipped (frame time p50 33.8 ms, its own step),
  the default 120 fps uncapped (p50 8.3 ms); `check_frame_rate.py` (17) and `check_render_size.py` (8) apply the product's options
  (`verify/nsoimg.py product_image`). The default 30 fps product (`work/build/totk/product`) is left as it is. The manosaba product rebuilt with the
  new rewriter: byte for byte the same images, only the stamp (`.prev57` kept). `host/tests/t_options.c`: 9 checks; written without checking the
  originals, an option is half applied (1 fails). iOS maps the signed executable's code: options there need the changed pages embedded
  (tomodachi-native afd88e9), item 62
- [x] batch 20 · 58 on-screen controls with a drag-to-look camera (BotWRecomp `runtime/src/host/touch.cpp` at 2b84b82, MIT: its layout, feel
  constants, finger rules and input checks, in C): `host/touchpad.c` for a game with game.json `touch` "pad" (Tears of the Kingdom; `DM_TOUCH=pad`
  / `screen` overrides it on iOS): a floating stick on the left half whose base follows a thumb that leaves its ring, the D-pad (sliding changes
  direction), ABXY, L / R / ZL / ZR, - / +, L3 / R3 and the eye that collapses them; the right half outside the buttons is the camera (the finger's
  speed smoothed with τ 45 ms, 420 pt/s for full deflection, at least 0.12 once moving, 0 at rest). Each finger owns its control until lifted; one
  stick and one camera finger at a time; a press shorter than the time between two reads is still seen once; the first read after a pause gives
  the camera no spike; a layout with the same size and safe area keeps the fingers. `hle_hid.c` ORs it into the controller (its stick after the
  front end's, before a game controller's). iOS (`front_ios.m` `DmPadView`): over the whole screen, laid out in the safe area, drawn after each
  touch, off while a controller with an extended profile is connected, the eye's choice in the user defaults (`dm.touch_hidden`).
  `DM_TEST_TOUCHPAD=check:<s>` (any game, the Mac too) runs BotWRecomp's checks against the controller the game read: Tears of the Kingdom 17 of
  17 in the prologue and on the Great Sky Island cliff save; `hide` 2 of 2; `look` only drags a camera finger: shots of frames 1432-1462 show the
  camera turning around Link, who stays where he is, and still from frame 1468 when the finger rests. `t_touchpad`: 33 checks (BotWRecomp's 17
  on a fake clock at 30 reads a second, then ownership, the short tap, the eye, off, the pause, the relayout, every button's centre); a finger
  that takes the button it slides onto (`-DDM_TEST_TP_REHIT`) fails 1. `t_hid_script`: 6 more (the merge, the last state read). How it feels on a
  phone needs Tears of the Kingdom on iOS (62), or manosaba with `DM_TOUCH=pad` on a device (70)
- [-] 59 not done: iOS 17 is not supported (the user's decision, 2026-10-20); the apps stay at iOS 26 (Metal 4) and iOS 18 (Metal 3)
- [-] batch 21 · 60 measured: no game calls an indirect draw or dispatch yet. The red-reporting stub logs every unimplemented NVN function on its
  first call (it is why `SetPrimitiveRestart` shows in 42 logs); in the 396 logs of Tears of the Kingdom and manosaba here, play up to frame
  27,000 among them (the prologue, the Great Sky Island, the dive), not one of DrawArraysIndirect, DrawElementsIndirect, MultiDraw*IndirectCount
  or DispatchComputeIndirect. Implemented when a game calls them (nxnative has DispatchComputeIndirect and DrawElementsIndirect; item 64).
  `t_nvn` 7b now holds all of them to the stub, the two plain indirect draws too (the check was on five functions; one added to the silent
  no-ops fails it)

Tier 3b, from the wider review of 2026-10-20 (sources and evidence in that section; before Tier 4). Correctness, small:
- [x] batch 22 · 78 the pipeline warm-up from the player's own list without a seed list (`gfx/nvn/nvn_warm_list.h`: the lists that exist, each
  on its own; before, a missing `pso_manifest.tsv` ended the warm-up). Rows of an earlier translation are skipped and counted, not reported
  in red (Tears of the Kingdom's list: 3,628 of its 4,357 render rows have an earlier key layout, 224 libraries are not in this chain). The
  prologue with the same list, before → after: pipelines built on the submit thread 698 / 129 ms → 1 / 0.2 ms, libraries loaded there 443 → 2,
  the warm-up 1,841 shaders, 729 render and 43 compute pipelines in 0.3-1.2 s in the background; frame 217 (the scene's first frame) 425 →
  131 ms, long frames 82 → 51, first frame 1.05 → 0.89 s; footprint at 75 s 2,613 → 2,596 MB. manosaba, a second run on the list of the
  first: 36 libraries and 23 pipelines on the submit thread → 2 and 1 (the warm-up 4 ms), the same pictures. The title list is not made: with the warm-up
  done by 1.2 s here, long before the first frame that needs its pipelines (9 s), there is nothing for a title list to win on this Mac; with a
  cold system cache (a new install) the list's own order puts the title's rows first (they were recorded first). `t_warm_list`: 6 checks;
  the old rule (`-DDM_TEST_WARM_NEED_SEED`) fails 2
- [x] batch 22 · 79 the prediction table (after MacNativeGames PR #29, MIT): a guess no draw took is freed two submissions after its kick once
  its compile has finished (one still compiling keeps its slot), each scan stops at 8,192 commands, 32 keys or 2 ms, its likely keys are
  kicked as they are found, and it guesses only from programs whose shaders are loaded (`prog_peek`: before, a scan could read libraries from
  disk for a guess). The power line has a `pipeline prediction` line: the prologue without the warm-up, frames 300-900, 15 scans, 186
  kicked, 65 taken, 120 freed untaken, 0 refused (the old table freed a slot only on a take, so the untaken attach modes filled its 384 for
  good). Its pipelines are cheap on this Mac with the system cache warm (698 in 39-129 ms), so the worst frame does not move (166.9 against the
  old 165.6 ms); the cold case is a device's. `t_ppred`: 13 checks; a slot freed only by a take (`-DDM_TEST_PP_NO_RECLAIM`) fails 4
- [x] batch 22 · 80 the present's command buffer committed with the residency set committed (MacNativeGames PR #30, open: a GPU page fault in 4
  of 14 boots when nothing had rendered into the presented texture yet): `present_now` may make the source texture itself. `t_present_res`: a
  present into an offscreen layer of a texture made by it, 6 checks, Metal 4 and Metal 3; without the commit 1 fails
- [x] batch 22 · 81 a sync's fence stored before its flag, release / acquire (`nvn_obj.h` `sync_fence_store` / `sync_fence_load`; GirlNet
  f2faa5f): `nvnQueueFenceSync`, the acquire sync and `nvnSyncWait`. `t_sync_order`: a million fences against a poller on another thread, none
  seen with a fence other than its own; the old order (`-DDM_TEST_SYNC_FLAG_FIRST`) is seen with fence 0 in 15,547-19,636 of them
- [x] batch 23 · 82 NVN texture layout per the SDK. The census first (`DM_NVN_TEXCENSUS=1`, kept: one line per new shape of each kind): Tears of
  the Kingdom has a cube array (RGBA8 4x4 depth 6, the size of six cubes here), 734 and more LINEAR_RENDER_TARGET textures without LINEAR (1x1 to
  260x260, the grass height maps among them), 117 and more MINIMAL_LAYOUT ones on the Great Sky Island (the RG8 260x260 terrain normal maps with 9
  levels, RGB5A1 / RGB565 320x320) and a packaged 3D BC6H 64³ (byte 0 0x40); manosaba one cube array (1x1 depth 6); no linear block-compressed
  texture in either. Then the rules (`nvn_tex.c`, after nxnative PR #11, digitalhivehub bacf2cb, MIT): a cube array's depth counts faces (Metal's
  array length = depth / 6), 0x40 alone is linear and a linear size counts pixel rows, MINIMAL_LAYOUT keeps the automatic block only when it pads
  no level, else one GOB, and the packaged byte 0's high nibble is a 3D texture's block depth (`pkg_layout` replaces `pkg_block_h`). `t_tex_auto`:
  the 15 vectors PR #11 took from the SDK's TextureBuilder in emulation (12 of them differed before) and totk-mobile's eight TotK 1.4.3 SDK sizes
  plus its grass height map (Apache-2.0, numbers only; 6 and the map differed before), 0 differ now; MINIMAL_LAYOUT ignored
  (`-DDM_TEST_TEX_NO_MINIMAL`) fails 25 checks. The prologue and the cliff save look the same as before (shots within run-to-run noise)
- [x] batch 23 · 83 the format's own swizzle in the texture views (`tex_by_id`: the guest's selectors go through the format's components;
  totk-mobile d3dd76c, Apache-2.0, reimplemented). Sampled with one: Tears of the Kingdom's RGB5A1 / RGB565 terrain textures (packaged 210x210,
  minimal-layout 320x320); none is ever a render target in either game (census), which the R / B swap would read crossed: red in
  `SetRenderTargets` if one is. No X format in these runs. `t_fmt_swz`: RGB565 / RGB5A1 texels in NVN's order (red in the low bits) read where
  they were written, RGBX8 alpha 1, a guest swizzle composed, through a compute kernel (9 checks); the swizzle dropped
  (`-DDM_TEST_FMT_SWZ_DROP`) fails 6
- [x] batch 23 · 84 socket errno from nn::socket's own values (the SDK's Errno: ENetDown 100, EAfNoSupport 97; 50 is its ENoCsi). `t_net`: 5
  checks; the FreeBSD numbers (`-DDM_TEST_NET_BSD_ERRNO`) fail 4
- [x] batch 23 · 85 sticks inside the circle of radius AnalogStickMax (the SDK's hid documents), for every source (`hle_hid.c` `stick_circle`): the
  keyboard diagonal (32767, -32767) reads (23169, -23169). `t_hid_script`: 3 more checks; per-axis clamping (`-DDM_TEST_HID_NO_CIRCLE`) fails 3;
  `t_hid_receipt` records that diagonal on the circle
- [x] batch 23 · 86 the Mac app's first iCloud listing: from the main thread the query starts in place and a local run loop drives it
  (`cloud.m` `listing_query`, upstream 1ceaa6b, MIT); before, the block it sent to the main queue never ran while it waited the whole limit.
  `t_cloudlist`: the query started before the call returned (0.07 s here, without an iCloud account); sent to the main queue it never starts (1
  fails)
- [x] batch 23 · 87 quit waits while the game is inside its exit-request section (`hle_oe.c` counts Enter / Leave, never below 0;
  `hle_lifecycle.c` `quit_wait` waits for the pending save and the section, at most 15 s / 3 s; GirlNet 291a9cc). `t_quitwait`: 5 checks, the save
  alone (`-DDM_TEST_QUIT_NO_SECTION`) fails 3; `t_oe` 4 more, `t_fs` checks the pending flag the wait reads
- [-] batch 23 · 88 measured: SleepThread under 20 µs is rare here. Tears of the Kingdom: 677 in a minute of the prologue (989 ns, threads 0x1001
  and 0x1033), against 1.4 million yields; manosaba none. Nothing to win, left as it is. Seen on the way: manosaba sleeps between 20 µs and 1 ms
  about 24,000 times a second (1.45 million in 60 s), each one leaving and retaking its virtual core (98 / later)
- [-] batch 23 · 89 measured: neither game reads the six-axis state. Tears of the Kingdom asks for its handles (`GetSixAxisSensorHandles` in 280 of
  325 logs here), gets 0 and never calls `GetSixAxisSensorState(s)`; manosaba does not ask. A resting state belongs with the gyro (67)
- [x] batch 23 · 90 red lines: a second texture or sampler pool (the replay keeps one ID space), packaged layout byte 1 not 0, a dual-source
  blend factor (the shaders have no second output), an unimplemented import (`UNIMPL`, now with its caller: `DM_UNIMPL_GUEST_RA`, `t_hstack` 4)
  and an unknown svc (with the svc's module and offset). None fires in Tears of the Kingdom's prologue or the cliff save, nor in manosaba
Tier 3b, Tears of the Kingdom:
- [x] batch 24 · 91 shrines at 60 fps: nxnative PR #11's five words (digitalhivehub 97172c1 / a6b7191, MIT; at 1.4.3 their originals all match)
  joined `fps60` (now 12 words): the apply step 0xb00f4c and its inlined copy 0x1ff6104 never take the forced fixed interval a shrine sets (owner
  +0x4f8 bit 0, +0x508 = 2: 30 fps there however light the frame), and leaving a fixed interval re-arms the lowering from the minimum 1 in the
  helper 0x2b3d268 and both inlined copies (0xb00f64, 0x1ff6138; before, from 2 it never tried 1 again). `check_frame_rate.py`: 25 checks, 8 new
  in Unicorn (a shrine is not forced, the re-arming in all three places), the original main the negative control, and the previous product60
  fails the 4 patched ones. `work/build/totk/product60` rebuilt (`tools/build_product.sh`): the images and every other table byte for byte the
  same, only the option table and the rewriter stamp differ; the previous one is `product60.prev91`; the prologue as before (120 fps uncapped,
  p50 8.3 ms). Not run in a shrine here: no save inside one (the first is Ukouh Shrine, after the cliff save)
- [x] batch 24 · 92 the BNSH files inside the game's modules (`shader_extract.py --modules <product>`, the `*.patched.bin` images; MacNativeGames
  2f9e707, MIT): Tears of the Kingdom 1.4.3's main has two, 40 shaders (11 vertex, 29 pixel) and 45 pairs, all before only from run-time captures
  (the 45 pairs were known from them); the workspace's capture-only shaders 178 → 138. `tools/tests/t_shader_module.py`: a synthetic image with an
  embedded BNSH, 5 checks; the RomFS walk's visit in place of the scan fails 3
- [-] batch 24 · 93 measured: the half-step selector (sys+0x1da bit 0, ×0.5 in the step) does not follow the interval. A probe reading it with
  the interval at every present (the system object from the controller's saved registers when it sets the present interval): the prologue on
  product60 with `DM_TEST_WORK_MS=14`, 191 interval changes between 1 and 2 in 90 s, and the Great Sky Island cliff save (7,800 frames at
  intervals 1-3, 14 changes): the byte stayed 0 throughout, so the step is never halved; nothing to change (T5eng's note does not hold here)
- [x] batch 24 · 94 GPU-rendered textures in CPU-cached pools, counted (`DM_NVN_TEXCENSUS`): on the cliff save 107 (the RG8 260x260
  minimal-layout terrain normal maps among them), 6 of whose storage the CPU then wrote, and what it wrote was zeros (0.0 % non-zero), but for one.
  The experiment `DM_NVN_WB_IDLE=1` (after totk-mobile's write-back, Apache-2.0, reimplemented): a texture the GPU left alone for 30 submissions
  is written back to its storage, and one whose storage the CPU writes afterwards is handed back to the CPU (its next fetch uploads it). With it
  the terrain slots the CPU writes hold 84 % non-zero bytes: the game does move GPU-rendered tiles with CPU copies, which the default (the CPU's
  writes to a GPU-written texture ignored) never shows. But it also hands back an RG8 256x256 texture the CPU rewrites every few submissions
  (1,300 times in a minute), and Link close to the camera, a translucent ghost by default (the game's near-camera fade), is drawn solid with it.
  Which is the console's picture is open: off by default, 94a. `t_wb --idle`: the write-back after 30 idle submissions and the hand-back, 6
  checks; without the switch 3 fail
- [ ] 94a the write-back's other half per region: what the RG8 256x256 texture the CPU rewrites every few submissions is (zeros each time; its
  hand-back changes Link's near-camera fade), console footage of Link close to the camera and of the Great Sky Island grass, then
  `DM_NVN_WB_IDLE` on by default or a narrower rule (hand back only what was written back, or only whole-texture copies)
- [x] batch 24 · 95 `DM_RENDER_RES` above the output size: `games/totk/heapdef.py <RomFS> <overlay> <w>x<h>` writes a HeapDef into a RomFS
  overlay with DynamicTextureAllocatorMem and Graphics grown by the shipped 113,664 KiB × the scene's pixels over 1600x900, rounded up to whole MiB
  (found by name in the BYML, as MacNativeGames' make_output_res_overlay.py reads it, MIT; 1920x1080 gives exactly totk-mobile's 163,840 /
  251,904 KiB, its source asset the same SHA b84a6a64…), and notes the size beside it, up to which `frame_step.c` then takes `DM_RENDER_RES`
  (3840x2160 at most; without the note 1920x1080 as before). The host's application pool already has 512 MiB over the console's. On the cliff
  save through the inventory and system menus: 1920x1080 needs no overlay; 2560x1440 with it runs (no red line), with only the note (the heap as
  shipped) it crashes at main+0x1c2f20 as before. `t_frame_step`: 5 more checks (31), the note ignored (`-DDM_TEST_SCENE_NO_HEAP`) fails 1;
  `tools/tests/t_heapdef.py`: 9 checks on a synthetic BYML; fixed offsets in place of the lookup by name end it in an error
- [x] batch 24 · 96 anisotropic filtering as a switch, off by default (`DM_NVN_ANISO=<2..16>`, `nvn_aniso.h`; tvc97's TOTK_ANISO): raised only on
  mipmapped samplers that minify linearly, without a depth compare or a reduction, never lowered. `t_aniso`: 10 checks; raising every sampler
  (`-DDM_TEST_ANISO_ALL`) fails 3

Tier 3b, performance and power (measure first):
- [ ] 97 a present with a deadline against the GPU clock drop (`powermetrics`, the 30 fps product). Needs the user: the presents only happen with a
  window (headless runs have no drawable) and `powermetrics` asks for the password; mode 0 (`DM_PRESENT_LINK=0`, the rescue path) is the one
  without a deadline, the display link's default presents with one
- [x] batch 25 · 98 the audio renderer paced by its own clock (`host/audren_pace.h`, after SkyRecomp 0620de9, MIT): a frame every frame period, three
  silent frames queued first as the device's buffering, at once only when the queue is empty, a stall restarting the deadline. Measured with the
  sound card (`DM_AUDIO=1 DM_AUDIO_MUTE=1`, the prologue): before, 55 % of the gaps between frames under 1 ms and 44 % at 7-12 ms (pairs every
  10 ms), a catch-up frame before 27 % of the game's updates (3,731 in 75 s); after, 89 % at 3-7 ms, under 1 ms 0-7 per 2,000, catch-ups 10 (as
  with the silent sink, whose pacing was already even). The renderer logs the gap histogram every 2,000 frames now. `t_audren_pace`: a
  simulated 10 ms sound card, 5 checks; paced by the output (`-DDM_TEST_AUDREN_PACE_OUTPUT`) 2 fail
- [-] batch 25 · 99 measured: depth textures with or without the GPU's lossless compression look the same. `DM_NVN_DEPTH_PLAIN=1` (an A/B switch,
  off) describes depth / stencil textures with allowGPUOptimizedContents NO (LASRecomp PR #204). The prologue: shots within run-to-run noise
  (s1300 28 %, s1800 7.5 % against 32 / 11 % between two default runs), brightness inside the console footage's ranges both ways (s1300 mean 53.0
  / 50.9 against 43.3-56.0, s1800 79.8 / 79.1 against 74.3-81.6), GPU p50 6.33 / 6.72 ms; the default stays compressed. `t_depth_plain`: 3
  checks; without the switch 2 fail

Tier 3b, one shader chain rebuild for all three:
- [x] batch 26 · 100 the Ultrahand mask shader's integer output: `games/<id>/fout.tsv`, read by `dmsh_build.py` beside the capture directory's,
  holds the outputs known without a failed run (Tears of the Kingdom: the BC4 encoder and `e3fb0bb58a691e10 0 uint`, nxnative PR #11's
  shader_fout.tsv, MIT); its MSL now declares `uint4` at colour 0 (was `float4`, which Metal refuses on an R8Uint target)
- [x] batch 26 · 101 the NoContraction helpers inlined (`gfx/tools/msl_helpers.py`, in the chain's files): SPIRV-Cross's `[[clang::optnone]]
  T spvFAdd` (the only such helper here: 3,047 of Tears of the Kingdom's files, 41 of manosaba's) becomes always-inline with FP contraction off in
  its body (totk-mobile turns it off for the whole file and the math mode with it; the idea only). Bit for bit: `t_fpcontract` (65,536 random
  triples and 1,024 crafted ones where a fused sum gives other bits: the same as the optnone helper; measured on the way, our safe math mode fuses
  not even a plain a * b + c, and the inlined fma(1, l, r) stays unfused under fast math too; a plain l + r under fast math, the broken variant,
  fails 1), `tools/tests/t_msl_helpers.py` (5; an empty rewrite fails 3). GPU p50 over the prologue, two runs each, per 300-frame window: old
  5.4-7.4 ms, new 5.1-6.9 ms, inside the run-to-run spread: no measurable gain on the M5. Chain bab8d4746968 → 4236b355e989 (with 100): every
  shader of both games builds, the c1 list the same 49, shots and brightness inside the console footage's ranges
- [-] batch 26 · 102 not needed: 90's red line for dual-source factors fired in neither game (the prologue, the cliff save, manosaba)

Tier 3b, iOS and tooling:
- [x] batch 26 · 103 metallibs copied into memory on iOS (`lib_from_file`; `DM_NVN_LIB_COPY=1` elsewhere; MacNativeGames e099353, MIT): the mapping is
  dropped at once, so a library holds no vnode (theirs filled an A16 iPad's 11,400 with about 8,800 libraries; ours keeps 1,841 after item 78's
  warm-up of the prologue's list). `t_libcopy`: the library works and no memory region maps the file; the mapping path (the Mac's) fails 1
- [x] batch 26 · 104 a pipeline retried after a compiler-service interruption (`nvn_retry.h`, after MacNativeGames 39048ca, MIT: up to 4 attempts,
  100 / 200 / 300 ms apart, for "XPC_ERROR_CONNECTION_INTERRUPTED"; render pipelines in `pso_get` and compute ones): one that still fails for an
  interruption is not recorded as failed, its next use tries again. `t_retry`: 3 checks; one attempt (`-DDM_TEST_RETRY_NONE`) fails 2
- [x] batch 26 · 105 the command channel follows a replaced file (a new inode, as a copy pushed to a device; T5eng's TOTK_INPUT_REOPEN) or one cut
  shorter than what was read from its start again (`cmdchan.c` `cmd_reopen`, at the end of the file). `t_cmdchan`: 26 checks (the file renamed
  over by another, whose line runs); the first file followed for good (`-DDM_TEST_CMD_NO_REOPEN`) misses it and never reaches quit (2 fail)
- [x] batch 27 · 106 run diagnostics (lordran-native f5834c5 / 9567c94, MIT; the code is ours): log lines carry UTC milliseconds (`host/logstamp.h`:
  stderr becomes a stream that stamps each line however it arrives, crash dumps included; on in the iOS app, `DM_LOG_TIME=1` on the Mac;
  only lines from before `main` stay bare); `Documents/boot.running` marks a session in the foreground (`host/rundiag.h`, written on entering
  it, removed on leaving it and in `dm_host_shutdown` through `dm_exit_note`), so the next launch logs a red line and keeps that session's log
  as `boot.interrupted.txt`; the home screen quick action Export Diagnostics (`ios/Sources/diag_export.m`; also a button on the missing-data
  page) shares one file with the device, GPU, memory, thermal state, shader chain, fps line and switches and the last 8 MiB of each log
  (launched that way, it exports before the host starts); the fps / GPU line (`gfx/nvn/nvn_perfline.h`: the game's presents a second and the
  frames on screen a second over the last second, the GPU time over 32 frames, "no frame for N s" after 1.5 s) is drawn by the iOS overlay
  (Settings: Show frame rate), in the Mac window's title with `DM_PERF_LINE=1`, and logged every 300 frames (the prologue: "30 fps · GPU
  5.4-7.4 ms"). Tests: `t_rundiag` (16; not keeping the log fails 2, the start of a long log instead of its end 2), `t_logstamp` (6; a stamp
  per write fails 2), `t_perfline` (8; since the start instead of the last second fails 2). The device check (a foreground kill, then the red
  line and the export) is for the user
- [x] batch 27 · 107 the frames-stopped dump (justr4mzi's mush8 fork 979317e, GPL: the idea only): `crash.c`'s watch thread now runs in the product
  too; `DM_FREEZE_DUMP` seconds (6) without a present after the first one, in the foreground and with no system keyboard up, give one dump of
  what every thread waits on per freeze and "frames came back after N s"; the game goes on. The thread dump gathered each thread's backtrace
  while it was suspended and printed it before resuming it: a thread stopped while holding the stderr lock (in the middle of a log line) would
  have hung the dumper and itself for good; it now prints after resuming (`hle_thread.c` `thr_bt`). In the game: `DM_TEST_PRESENT_STALL=900:8`
  in the prologue gave one dump at 6.2 s (105 backtraces) and "frames came back after 8.3 s", the run went on to frame 1500. Tests: `t_freeze`
  (5; no watch without a test watchdog, as before, fails 2), `t_threaddump` (2; printing while suspended, as before, never ends: 2 fail)
- [x] batch 27 · 108 the Metal 4 iOS build compiles for `apple-a14` (was a18; lordran 78dd33d): one IPA for every Metal 4 device. Measured before
  the change: the a18 build's host code (`__TEXT,__text`, 253 distinct instructions) had no BF16 / I8MM / SME / WFxT instruction either
  (`bfi` / `bfxil` are ARMv8.0 bitfield moves); a14 refuses BF16 intrinsics outright. `tools/tests/t_ios_cpu.py` (4 with an app binary:
  project.yml, the iOS 18 variant, the free-account variant, no later instruction in the binary; the old spec fails 2)
- [x] batch 27 · 109 touch camera sensitivity (0.25-3×, applied before the turn speed saturates: 0.5× still turns at full speed with a fast swipe,
  a 6 pt/s drift stays still at 3×) and X / Y inversion (lordran 42ad4cf, MIT: the idea; `touchpad.c` `tp_look_tuning`): Settings → Touch
  Camera, or `DM_TOUCH_LOOK=<s>[,x][,y]`. In the game (the cliff save, the look test): a drag right turns the camera one way, with x inverted
  the other. `t_touchpad` (41; the sensitivity applied after saturation fails 1)
- [-] batch 27 · 110 not needed: `devicectl device copy to` already skips the files that are on the device unchanged (its help, devicectl 642.16),
  measured in its temporary domain on the iPhone 17 Pro: a 30 MB tree 6.0 s, again unchanged 0.8 s; one file changed is copied; a 1 GB copy
  stopped after 3 s left 16.7 MB of the file, the re-run (23 s) completed it, byte for byte. `ios/run.sh push-data` now says so when a copy fails
- [x] batch 27 · 111 `make -C host/tests run-ubsan`: the parsers of outside input (env.txt, the command channel, DM_KEYS, mods, code options,
  RomFS checks, controller maps, the keyboard's text, the run marker, the log stamps) built again with UBSan, fatal; all 10 clean;
  `t_ubsan_on` shows the sanitizer is on (its overflow variant must stop with a runtime error). `tools/audit_public.py` (bayonetta3-mobile's
  idea, MIT): every tracked file checked for private paths, game-container / executable / archive starts, key and token patterns and binary
  content, values never printed; the index passes (707 files); `--history` refuses only 4 PNGs from upstream d2r-mobile's own history (its
  "II MOBILE" icon and touch art, still in upstream/main; removed here in 27cd6ca). Both run in CI. `t_audit_public.py` (12; without the
  content rules 4 fail). LDC.64 with the address register among its destinations (LASRecomp a179452's cases, MIT) in `t_xlate_ops.py`: 5
  more, all right (ours copies the address first); the wrong order, emulated (`DM_TEST_LDC_BUG=1`), fails 1

Tier 3c, from the deep review of 2026-10-20 (sources in that section; after 3b, before Tier 4). Correctness, small:
- [x] batch 28 · 115 pipeline keys carry zeros for the blend state of targets whose blending is off (`nvn_pso_skey.h` `pso_key_blend_canon`, in
  `pso_key_fill` and on the player's list at warm-up, where rows that become one pipeline are folded): Tears of the Kingdom's list 729 keys →
  397, its warm-up now builds 397 render pipelines (332 folded), manosaba 23 → 23. `t_pso_canon` (5 + the lists' counts; every slot kept fails 3)
- [x] batch 28 · 116 SyncWait on a sync never fenced returns FAILED, as the SDK's NVNsyncWaitResult says (was TIMEOUT_EXPIRED); counted and logged:
  0 such waits in the prologue, the cliff save and manosaba. QueueWaitSync left as it is (the SDK gives it no failure). `t_nvn` (134; the old
  result fails 2)
- [x] batch 28 · 117 the Metal 3 buffer table grows and is indexed (`nvn_m3buf.h`, on `nvn_pool_find.h`; acnh-mobile c2af194's case, MIT): Tears of the
  Kingdom passes 513 buffers in the prologue on `dmhost_m3`, no "not in any registered buffer" line; the old 512 cap rebuilt the same way left
  binds, compute binds and indexed draws out and s1800 black but for the subtitles (99.5 % of it differs): the iOS 18 app would have shown that.
  `t_m3buf` (7: 1,537 buffers; the 512 cap fails 3)
- [x] batch 28 · 118 a write released through one range stamps every range on that host page (`nvn_dirty.c` `stamp_shared`, also in the fault handler:
  atomics only): a pool registered on a page another pool had protected started "not protected" there, and the fault stamped only the other's
  range; find_span could also answer a query about one pool with the other's range, so neither saw the write. acnh-mobile's `t_dirty_shared.c`
  (MIT) as is, the checks named: both see it now; 5,000 rounds with a writer on the page leave nothing registered (stamping one range fails 2)
- [-] batch 28 · 119 not needed: with `DM_NVN_COUNTS=1` no draw sampled a texture that shares an attachment's storage under another serial (0 in the
  prologue, the cliff save and manosaba; Tears of the Kingdom shares storage 64,000 times, never so); the count stays in the counts mode
  (`nvn_obj.h` `tex_same_storage`)
- [x] batch 28 · 120 cull FRONT_AND_BACK drops polygons only (`nvn_obj.h` `nvn_prim_has_face`: points, lines and line strips drawn; both draw paths
  dropped every primitive); neither game draws a point or a line under it (logged once if one does). `t_cull_prim` (15; every primitive fails 6)
- [x] batch 28 · 121 a pass without attachments takes the viewport's extent when no scissor is set (`nvn_attless_size`; it was 1x1: a 4x3 viewport
  gave one fragment, bayonetta3-mobile's case); with a scissor its extent as before (Tears of the Kingdom's 1x1). The samples stay 1: no
  multisample state above 1 in either game's attachmentless passes (logged if one comes), and it would change the pipeline's sample count.
  `t_attless` (7; scissor only fails 4)
- [x] batch 28 · 122 the silent audio sink starts again from now when more than 100 ms behind (`audren_pace.h` `sink_step`; acnh-mobile, nxnative
  3bd1e9e): after a 2 s freeze the next 50 ms hold 9 pulls, not 409; manosaba stopped for 3 s with SIGSTOP logs the restart. `t_audren_pace`
  (8; never restarted fails 1)
- [x] batch 28 · 123 the display-link step from the rate the link runs at (`nvn_pace.h` `nvn_link_step`: interval × link rate / present rate, rounded
  up): on the power-saving 30 Hz link interval 2 takes one tick (30 fps; was two: 15 fps). `t_pace` (31; the old step fails 3). The window
  run on the device or the Mac with the 30 fps setting is for the user
- [x] batch 28 · 124 the sticks set NpadButton StickL / StickR Left, Up, Right, Down (bits 16-23, the SDK's names; `dm_input.h` `dm_stick_dirs`): an axis
  at half its range or more (blade2-mobile 34755e9, MIT), so nothing inside the SDK's dead zone circle of AnalogStickMax / 2; whether the
  console also sets one on a diagonal outside that circle with both axes under half is not documented and not checked on a console. The touch
  controls' self-test masks them (it checks the controls' own buttons: all ok in the cliff save). `t_stickdir` (8; none fails 5), `t_hid_script`
  and `t_hid_receipt` expect them
- [x] batch 29 · 125 crash and diagnostics fixes: the crash report's backtrace reads each frame with vm_read_overwrite (`crash.c` `dm_backtrace`;
  totk-mobile does so): a corrupt x29 faulted inside the crash handler and lost the rest (`t_crash_bt`: a chain into an unmapped page prints both
  return addresses and a note; the direct read dies of SIGSEGV, 2 fail); the svc gate hands `svc_dispatch` the svc's address, not its offset in
  the module (`hle.c` `gate_dispatch`: every red SVC line and Break said "?"; `t_gate_svc`, the offset alone fails 2), and Break names the svc's
  module and its caller's apart (they were mixed); 18 `RED_ONCE` calls of 13 tags shared by unrelated messages moved to free tags, each tag one
  message (`tools/tests/t_red_once.py`; the old source fails 1); the shader capture writes `.data` then `.ctl`, each whole through a rename
  (`hle_nvn.c` `shdump_to`: a run ended between the two left a `.ctl` that marked a capture without its code as done; `t_nvn`, the old order
  fails 2)
- [-] batch 29 · 126 not needed, and wrong for Tears of the Kingdom: it keeps its own fields right after a view (7250b79: a 12-byte `view_t` overwrote
  them), so its SDK's view is not the 16 bytes Bayonetta 3's is; and every view both games hand over was made by our SetDefaults (measured once
  with a mark in bytes 10-15: 16,384+ views each in the prologue, the cliff save and manosaba, none without it; that run wrote past `view_t`, so
  the switch was not kept). No view sets a depth / stencil mode (the red line for it never fired)
- [x] batch 29 · 127 the Mac window: a press on the title bar goes to AppKit with its drag and release (it was swallowed with every press outside the
  picture: moving the window by its title bar did nothing), one on the black bars is still no touch (`host/mouse_route.h`; `t_mouse_route`, 9; the
  old routing fails 3); App Nap held off while a window runs (`beginActivityWithOptions` user-initiated, latency-critical; blade2-mobile: a hidden
  window without sound turned a 10 ms sleep into 200 ms). The window checks are for the user
- [x] batch 30 · 128 the deep review's tests (their vectors as data, the code ours): `t_writetexels` (totk-mobile's regions: a row, a cube's six
  faces, a region of face 3 with its neighbours kept, a BC4 region ending inside a block row, two 320x320 MINIMAL_LAYOUT textures back to back
  that stay inside their 204,800 bytes each; rows at the region's pitch fail 2), `t_vfetch` (dr3-mobile's UNORM8 {0, 64, 128, 255} →
  {0, 0x3e808081, 0x3f008081, 0x3f800000} and NaN payloads kept, on the GPU through our vertex formats; integer formats into float inputs keep
  their bits; without `vfmt_for_float_input` Metal refuses the pipeline, 2 fail), the svc vectors in `t_guest_mem` section 11 and `t_coremask`
  (with 143; the core table as Atmosphère, which we already followed: totk-mobile expects InvalidCoreId for ideal core 3 in mask 7 and a main
  thread mask of 7, Atmosphère's KThread gives InvalidCombination and 1 << the core), `t_time` (acnh-mobile's zones: Shanghai, UTC, Tokyo,
  Kathmandu, New York and Sydney both seasons, the repeated and the skipped hour; February 30 none with success, as the SDK documents; standard
  time only fails 4), `t_relr` (144), the pool lookup (`nvn_pool_find.h`: a range wrapping past the end of the address space was found in a pool,
  now in none; totk-mobile's nested and length-0 cases; the wrap fails 1), a NaN alpha reference in `t_alpha` (bayonetta3-mobile's: drawn by
  NOTEQUAL and ALWAYS only; taken as 0 fails 1)
- [x] batch 31 · 129 rewriter: a jump table whose add has the loaded entry first (`ldrsw x18, [xB, xI, lsl #2]; add x18, x18, xB; br x18`,
  offsets relative to the table) is found with the operands swapped (the anchor came from the first operand only: "anchor not a constant", the
  rewrite stopped); per-function decoding goes past a word Capstone cannot decode (`decode_words`; the stream stopped there, and an exclusive section
  after inline data got plain sites, which structck and equiv both pass); equiv takes an exclusive store's success path (a status `cbz` becomes a
  branch to its target; it was a nop, so 5 of the 134 sections of Tears of the Kingdom's main never left by their success exit, and a compare-and-swap
  whose store comes first looped to the step limit) and leaves out only 8-byte writes to x18's and the thread pointer's slot (it dropped every
  patched write into the 64 KiB slot area; the slot three past x18's, the review's fixture, is the gate key's, so "any key slot" would still pass
  it); `tools/tests/t_rewriter_synth.py` runs the rewriter, structck and equiv on a synthetic module in CI (smo's selftest words, bayonetta3-mobile's
  three sequences: a store-first compare-and-swap in one piece entered at its load, an earlier store next to a section, `mov sp, x18`; inline data
  before a section; `mrs cntfrq_el0` refused) and on damaged copies of its product (a site left as the original word; a success exit one
  instruction late; a stray store into the gate key's slot); CI installs lz4 and unicorn. Both games' products are byte-identical apart from the
  rewriter stamp, `verify/regress.sh --fuzz` OK for both

Tier 3c, Tears of the Kingdom:
- [x] batch 32 · 130 pre-rendered movies held at 30 fps: the game extends the user inactivity time (`nn::oe::SetUserInactivityDetectionTimeExtended`)
  for every cutscene, not only movies (checked in the 60 fps product's prologue: true from main+0x770f04, false from +0x770ad0, a real-time
  cutscene that the oe call alone held at 30 fps for 70 s); main 0x772fd8 makes the call (except for `Dm_ED_0007_PreRender`) and is called by
  0x770f00 (a real-time cutscene) and 0x2b36100 (a pre-rendered movie, FPSLocker's flag), false comes from 0x770acc and from 0x2b33914 via 0x2b36770.
  So the oe HLE hands the call and the guest's return address (`DM_GUEST_RA`) to a new game hook `dm_game_inactivity_fps`, and Tears of the
  Kingdom's (`games/totk/movie_fps.c`) holds only the call that returns to main+0x2b36104; `gfx/nvn` holds the rate through `nvn_hold_interval`
  (every pacing interval at least the one that gives the rate: 2 at 60 Hz, 3 at 90, 4 at 120), and the log names each call's caller.
  `DM_INACTIVITY_FPS` replaces the held rate (0 = no hold, for the blood moon comparison of 72); `DM_TEST_FPS_HOLD=<from>:<to>:<fps>` holds without
  a movie (60 fps product: frame time p50 33.5 ms during the hold, 8.3 outside). Tests: `t_pace` (9 hold cases; the game's interval alone fails 4),
  `t_oe` (8; the old stub fails 8), `t_movie_fps` (6; every cutscene held fails 3). `host/tests`' profile rule now depends on game.json and
  `gen_game.py` (it kept a profile from before a new field)
- [x] batch 33 · 131 the game's GPU time is measured again: Tears of the Kingdom times its frame as the difference of two TIMESTAMP reports of one
  submission (main 0xa1a50, called by the frame-rate controller's path: a report near the start, heap ring +0x10, against the last, +0x20; ns →
  µs at 0xa1b94, a share of 16,666.666 µs) and every report of a submission took its completion time, so it read 0 to 0.06 ms (p50 0.003 ms
  over 1,761 frames, probe at 0xa1b90). Now each report takes its place inside the submission (`gfx/nvn/nvn_ts.h`): within the part of the
  submission that holds it (mid-submission commits give each part its own GPU start / end, as nxnative's segments do) at the share of the
  work (draws, dispatches, copies, clears) recorded before it; between two parts a TIMESTAMP is the earlier part's end, a TIMESTAMP_TOP the
  later part's start. The game now reads p50 6.33 ms (our GPU line 5.9-7.1 ms); the last report stays at the end, so the controller's view
  is unchanged (30 fps product 33.4-33.7 ms; 60 fps product p50 8.3 ms and 7 interval changes in 75 s, both as before). `DM_NVN_GPU_TS=completion`
  restores the old timing, `DM_NVN_TS_SCALE=<x>` spreads the reports x times (x = 3: the controller changed interval 9 times instead of 1).
  Measured, not done: the dynamic resolution itself never runs here: its multiplier (main 0xd5d80, nxnative's site) is only read when the
  view code's object (heap+0x234f3630) has bit 0 of +0x1c, and main 0x8c4ed0 sets that word to 0x804 at boot and nothing writes it again
  (watchpoint, 70-80 s on the sky island, docked and handheld, also with x = 3); so the 0xA1B94 budget factor has nothing to act on yet (131a).
  Probes: `DM_PROBE_FP=1` adds s0..s7, and x4..x7 are printed. Tests: `t_ts` (17; every report at the end fails 7), `t_nvn` (a report before
  24 busy copies is 37.7 ms earlier than the one after them; 0.00 with the old timing)
- [ ] 131a what sets the dynamic resolution's enable bit (the object at heap+0x234f3630, +0x1c bit 0; T5eng saw TotK's scene size change on an
  iPhone Air), then its time budget from the frame rate (FPSLocker's factor at main 0xA1B94: target / 30000)
- [ ] 132 the wave program's wide lines (2026-10-20: not reached: no `SetLineWidth` other than 1 in the prologue, on the cliff save or in the five
  other saves of `save_play` in 75 s each, all early game; the wave program's shaders 0cd9edc0a623c237 / e04d541253557805 are not in our capture;
  totk-mobile expands them by editing those shaders' source, so it needs both: a save where the game draws them, from the user)
- [x] batch 35 · 133 a frame step in whole vsyncs decided by the host, as a switch (`DM_FRAME_STEP=vsync`; `gfx/nvn/nvn_fstep.h`, nxnative's
  design and numbers written anew, MIT): each frame stays on screen k vsyncs of 60 Hz and the game steps k (k + 1 while it pays back lost
  time, at most 15 vsyncs: the rest of a load is dropped); three late frames step up to the cost, 30 frames that fit 0.85 × (k − 1) try k − 1,
  a failed trial doubles the wait (1 s up to 8 s); item 130's movie hold is its kmin. The 60 fps product's prologue: k = 1 after boot, frame
  time p50 16.7 ms, two steps up and two down (both at boot), game time / real time 0.990 with the load's 32 vsyncs dropped; against the mean
  step with uncapped pacing (the default, unchanged): 6,900 frames in 60 s (about 115 fps on a 60 Hz panel, half never shown) for 62.2 s of
  CPU, the vsync step 3,000 frames for 40.2 s of CPU (35 % less). Measured, not needed: FPSLocker's three swapchain textures (main 0xbe14a8):
  TotK makes two, and with the vsync step 0 of 2,101 frames waited on the display path (max 0.01 ms; 3 of 6,690 with the mean step).
  Not measured: the day clock (main 0x68c15c, totk-mobile's day-rate site): its function (0x68c098) runs about 20 times a second on the cliff
  save but leaves before the increment (0x68c14c never reached), so it needs a later save (71c). FPSLocker's own step (the last frame's time,
  clamped) was not built: the vsync step is the candidate. `t_fstep` (9: 10 / 22 / 18.7 ms frames, a hitch, a stall, heavy then light, the
  movie hold, jitter; never paying back fails 3, no margin flaps 19 times in 2,000 frames and fails 1); `DM_PROBE_FP` prints s0..s15

Tier 3c, iOS:
- [x] batch 36 · 134 iOS audio: the session asks for 48 kHz and a 5 ms I/O buffer and logs what it got (RemoteIO pulled about 1,024 frames a
  callback against the 4 × 240 queued: nxnative 18c6143); each output counts underruns (started with nothing queued) and the zeros it filled,
  logged by the watchdog at most every 5 s (never from the audio thread); a watchdog (after nxnative b301431, MIT) moves a started AudioUnit that
  has not called back for 200 ms to the silent sink and retries it after 1, 2, 4, then every 8 s, and a Start or resume the device refuses does the
  same (the unit was dropped for good, or stayed "suspended" until the next resume); the media services reset makes every unit again
  (`dm_audio_reset`, from front_ios.m's observer). `t_audio` scenarios 6 (underruns: 61 with 305 ms of zeros after one 100 ms block, none before
  the start; uncounted fails) and 7 (a playing unit stopped behind the host's back: the silent sink after 252 ms with blocks still released, the
  unit again after 1,285 ms; without the watchdog fails; needs an output device). On a device (the user): the session's logged rate and buffer
- [x] batch 37 · 135 iOS: the frame rate applied is the setting held at 30 in Low Power Mode or from thermal state Serious up (`nvn_fps_policy`
  in `gfx/nvn/nvn_pace.h`, after super-mario-odyssey-ios's frame pacer, MIT), applied at start and again when the setting, the power mode or the
  thermal state changes, with the reason logged; `ENABLE_DEBUG_DYLIB: NO` in `ios/project.yml` (kept by project_m3.py). `t_pace` (7 policy cases;
  the setting alone fails 3), `t_ios_cpu.py` (2 more checks; the spec without the line fails 2). On a device (the user): Low Power Mode on and
  off while playing logs "frame rate 30 fps (Low Power Mode)" and back
- [x] batch 39 · 136 no option pages on iOS: the code options are built into the embedded image instead of written at load (iOS maps the signed
  executable's code): `ios/tools/gen_embed.py` writes the options the app is built with (DM_OPTIONS at build time, else the product's
  options.default) into a copy of each module's image, every original checked first and an option whole or the build stops (an option no module
  lists stops it too), and the loader reads them back from the image (`options.c` `dm_options_embedded`: all new bytes on, all originals off,
  anything else in red; asked for but not built in, in red), so `dm_option_on` answers as on the Mac. The choices that change at run time stay
  data: the frame rate is the host's pacing (the setting, item 130's hold, 135's policy) over fps60's minimum interval 1, the step the host page
  (realtime), the scene size the host page (scene). Making fps60's 13 sites read their minimum from the page (totk-mobile's design) would need a
  code cave each with flag-free selects (0x2db22c's flags are live): not needed with the host deciding the rate. manosaba has no options: its app
  is unchanged. `t_gen_embed.py` (7; options not written fails 3), `t_options` (1 more; embedded options ignored fails 1)

Tier 3c, larger:
- [x] batch 40 · 137 pairs noted at bind time: `pso_get` writes each vertex / pixel pair it is about to build a pipeline for to
  `<DM_SHDUMP>/pairs.tsv` (`nvn_mtl.m` `pair_note`, once per pair and process, the format ProgramSetShaders writes), so a pair BindProgram makes
  from two programs' stages, and a pair that fails to link, reaches the next shader build's padding (manosaba binds vertex and pixel in two calls:
  its capture had no pair; a 45 s title run now writes 20). Measured for the fallback library: no "not written by vertex shader" failure in any of
  the 208 run logs since chain bab8d4746968 (the last ones, 19-45 a run, are from 2026-10-18 under the old shader keys); the pairs written in the
  prologue (2,648) and the cliff save (4,038) are all in the capture's pairs.tsv already. So the union-interface library [-] here, moved to 62g.
  `t_pairs` (4: a pair that links written once, under another colour format not again, one reading a location the vertex shader does not
  write fails and is written; not noted fails 3)
- [x] batch 41 · 138 render targets of virtual pools in a heap, a switch (`DM_NVN_RT_HEAP=1`; the placement rule is totk-mobile's
  `nvn_texture_alias.inc`, Apache-2.0, written anew in `gfx/nvn/nvn_rtheap.h`): a virtual, CPU-inaccessible pool gets a placement heap of 1.25 ×
  its size and a render target of it goes at its guest offset × 1.25 when its Metal texture fits inside its guest range × 1.25, so targets
  disjoint in guest memory stay disjoint and those the game overlaps share memory; one that does not fit keeps a texture of its own. Metal 4 only
  (every encoder starts behind a queue barrier; Apple does not say whether Metal 3's tracking orders two textures of one heap) and only with the
  TBDR rewrites off (pass merges, the zero-source set and the attachment ledger go by texture). The memory line counts these targets. Tears of the
  Kingdom's 111 MiB pool on the cliff save: 320 targets, 317 MB of textures of their own → a 139 MiB heap holding 263 and 57 outside (6 MB); three
  runs each way: GPU allocated 2,111-2,132 → 1,941-1,975 MB, RSS 3,549-3,561 → 3,362-3,423 MB, submit p50 6.6-7.6 ms both ways, GPU p50 7.3-8.4
  against 6.7-8.6 ms, screenshots on against off 6-21 % apart and off against off 5-20 % (the same scene, animation timing); the prologue (182
  targets, 201 MB → 159 in the heap, 23 outside with 3 MB; GPU allocated 2,026 → 1,967 MB) draws s800 0.056 % apart from a run without it, as two
  runs without it are (0.057 %). Default off: a target the game still reads after part of its range was reused could lose other texels than on the
  console (Metal's layout is not Maxwell's), so 62h turns it on for iOS after more scenes. `t_rtheap` (10: on 20,000 random pairs none disjoint in
  guest memory overlaps in the heap, a target too large for its scaled range refused, the heap size; on the GPU two targets at one offset share
  memory and one 1 MiB on does not, cleared in turn the disjoint one keeps its colour, no command buffer error, the census; Metal 3: 7, every
  target its own texture; a target allowed past its scaled range fails 4, on Metal 3 2)
- [x] batch 38 · 139 code caves in the rewriter (`CAVES` in a game's rules.py; totk-mobile's routines appended to the patch area, written anew):
  one instruction (asserted) becomes `b <routine>`, the routine is emitted into the patch area from instruction words and forms (`('orig',)`,
  `('adrp_host', d)`, `('b' | 'bl', target)`) and branches back to the next instruction unless it leaves itself; words that cannot run elsewhere
  (PC-relative, x18, svc, system registers, register branches) are refused, as is a cave on another change; `<module>.caves.tsv` lists each
  routine's words and exits. structck ⑬ checks the original word, the branch, the routine word by word, its last word branching back and every
  exit listed; fnfuzz leaves cave functions out (an intended change, the game's tests check it). `t_rewriter_synth` (4 more: a cave that loads w0
  from the host page with x16 borrowed through the red zone, run in Unicorn: back at the next instruction with x16 and the flags kept; structck
  passes it; a PC-relative word refused; without the branch back 2 fail). Both games' products unchanged but for the stamp and an empty caves.tsv
- [x] batch 42 · 140 FIFO frame consumption for a game that steps once per frame (game.json `present_consume` `"fifo"`: manosaba;
  `DM_PRESENT_CONSUME=newest|fifo` overrides; after acnh-mobile c2af194, MIT, written anew): the display link shows the oldest queued frame instead of
  the newest (`nvn_mtl.m` `link_take`), which dropped the frames queued before it, each a step of the game never shown (manosaba on an iPhone: 14
  by tick 1,549). A frame stays at least its present period from the tick that showed it (`nvn_pace.h` `nvn_fifo_due`), as the console's swap
  interval counts vsyncs from the previous flip; acnh-mobile keeps the phase instead, which shows the frame after a late one for a single tick at
  interval 2. Uncapped pacing keeps the newest. The classic present thread (no display link: the Mac without a window) took every frame in order
  already. `t_pace` (4 more, on a simulated 60 Hz link with wandering tick times and every 10th frame late: the newest drops 59 of 599, FIFO none
  and in order; at interval 2 a frame 2 ticks at least and never held past them; after a silent link no burst; one frame a tick whatever its
  period fails 2), `t_linkfifo` (5, the production queue: the newest of three with two dropped, FIFO three in order one a tick, none dropped or
  marked done early, interval 2 every other tick, the link off; one a tick fails 1). On a device (the user): manosaba's log says "the display
  link shows every frame in order" and its "display link up to" lines count 0 "stale frames dropped by coalescing"
- [x] batch 43 · 141 IL2CPP class names in crash reports and memory snapshots (`host/il2cpp_obj.h`; armyja's nxnative-aa-port reads the same
  fields, MIT, written anew): an object starts with its Il2CppClass *, whose name and namespace pointers sit at +0x10 and +0x18; read without a
  second fault (vm_read, strings 16 bytes at a time so one ending before an unmapped page is still read) and kept only when both look like IL2CPP
  identifiers. The crash report of a Unity game (`game.c` `dm_engine_il2cpp`) names the object each register x0-x28 points to ("x19 → an IL2CPP
  UnityEngine.GameObject"); another game's registers are left alone. `tools/memsnap.py`: `class` names the object at an address, `refs` places each
  hit in the nearest object start up to 0x200 bytes before it ("near": a field holding a class pointer can look like one), and `diff` counts by
  class the objects starting in the largest new runs: manosaba from frame 600 to 2400 grew 29,776 System.String, 12,625 Naninovel.PlaybackSpot,
  8,930 Naninovel.RawValuePart, 7,924 Naninovel.UniTaskCompletionSource (the layout holds on its Unity). `t_il2cpp_obj` (7: a class with and
  without a namespace, a name ending at an unmapped page, an object on it, a small integer and a binary name give nothing, the crash report of a
  Unity game names x0 and x19 but not the binary x5 or the unmapped x7, another game's nothing; any string taken for a name fails 2),
  `t_memsnap.py` (5 more on a synthetic snapshot)

Tier 3c, smaller items listed at the user's request (2026-10-20: every finding of the three reviews as a to-do):
- [x] batch 44 · 142 the swapchain acquire against the GPU work of the frame last drawn into its image (after blade2-mobile 43e760e, MIT: UE4
  recycles per-frame memory by frame count, and without the wait it drew black frames): the present records the newest submission's fence for the
  image (`win_t` `rfence`), the acquire counts the times that work is not done yet, and the timing summary logs "swapchain acquires up to frame N:
  …, M of them before the GPU finished the frame last drawn into that image"; `DM_NVN_ACQ_GPU=wait` waits for it there. Measured, all frames: 0 of
  2,100 in the TotK prologue, 0 of 2,100 on the cliff save (also with the wait: frame p50 33.9 ms, submit 7.4, GPU 6.9 against 33.9 / 7.6 / 6.7),
  0 of 3,300 in manosaba: on the Mac the GPU is done long before, so the wait stays off; a device's log tells. `t_nvn` 6g (with completions held
  back, `mtl_test_done_hold`: counted, returns at once without the wait and only after the 150 ms hold with it; never counted or waiting,
  `DM_TEST_ACQ_GPU_NO_WAIT`, fails 2). On a device (the user): the count in TotK and manosaba logs
- [x] batch 30 · 143 svc details as Horizon (switchbrew's results; totk-mobile's vectors): SetHeapSize refuses sizes that are not 2 MiB multiples or
  above 4 GiB (InvalidSize; it took any size: a 1-byte heap then left the next growth's pages unwritable) and sizes above the heap region with
  OutOfMemory 0xD001 (was 0xD401), refuses a shrink over a MapMemory source (InvalidCurrentMemory), and gives back memory as fresh pages (a regrow
  read the old contents); MapPhysicalMemory answers an unaligned address, a zero or unaligned size and a range that wraps or leaves the alias
  region with InvalidAddress / InvalidSize / InvalidMemoryRegion (it ended the program, said 0xD401, and let a wrapping range through); GetInfo:
  PriorityMask from the NPDM's ThreadInfo (28-59 in both games; it was 0), RandomEntropy four values fixed for the process with handle 0 and
  subtypes 0-3 only, SystemResourceSize the NPDM's (`game.json` "npdm": TotK 24 MiB, manosaba 16 MiB; it was 32), Total = TotalNonSystem + it
  and Used = UsedNonSystem + it (the pairs were equal). `tools/new_game.py` reads the NPDM fields for a new game. Tears of the Kingdom's heap is
  still set at once to 2,630 MB; manosaba never calls SetHeapSize. `t_guest_mem` section 11 (the old SetHeapSize dies of SIGBUS on its 1-byte
  heap; the old MapPhysicalMemory ends the program on an unaligned address)
- [x] batch 30 · 144 the DT_RELR table checked whole before any slot is touched (`loader.c` `relr_each`): a bitmap before an address, a slot unaligned,
  outside the module or past the end of the address space, or a table outside the module refuse the module with a red line (it wrote wherever
  the words said). Both games' tables pass unchanged (TotK main 499,078 slots, manosaba main 613,227). `t_relr` (totk-mobile's table and bounds;
  without the checks 5 fail, one of them a child dying on a write far away)
- [x] batch 45 · 145 small diagnostics (bayonetta3-mobile's copy of blade2-mobile, MIT, written anew): the frame log (`DM_NVN_TSV`) stops at
  `DM_NVN_TSV_MAX_MB` (32 by default, 1-4096) with a last line saying so (it had no limit); `DM_PRESENT_LUMA=<file>` compares each present's guest
  image (the part shown) with the drawable it became, 18 rows of each copied after the present pass, a line per present and a red line once when
  the output is under 35 % of a source of 16 or more (the present path, MetalFX or the drawable's format darkening the picture; needs a window,
  the layer made readable for it; `nvn_mtl.m` `pl_*`); the HID receipt log (`DM_HID_RECEIPT_LOG`) deduplicates each (controller, style) on its own
  (a game polling two styles in turn made every poll a change). `t_nvn` 6h (past 1 MB the cap line, then nothing more; no cap fails 1), `t_pluma`
  (5, Metal 4 and Metal 3: the crop's 200 against an output of 40 dark, an output as bright not, a dark scene not flagged, a float image not
  compared, a line per present and the red line once; the whole image read fails 1), `t_hid_receipt` case 5 (two styles polled 100 times in turn:
  2 lines; one slot for all fails it)
- [x] batch 46 · 146 NCZ input in the game extraction (`tools/game_extract/Ncz.cs`; the layout as totk-mobile's `extract_romfs.py` reads it,
  Apache-2.0, written anew): an .nsz's NCZ is restored to the NCA it was made from beside the output while the extraction runs (the first 0x4000
  bytes as they are, the sections back to back from there, AES-128-CTR again for crypto types 3 and 4) and must hash to its file name, so a
  damaged NCZ stops the extraction; block compression (NCZBLOCK) is refused with a message. Zstandard from ZstdSharp.Port 0.8.8 (MIT), pinned by
  content hash like LibHac. Not run on a real .nsz here (none in the workspace). `t_game_extract_ncz.py` (`game_extract.sh --selftest-ncz`: a
  synthetic NCA of three sections back byte for byte, one named after other bytes, a block-compressed one and one with a gap refused; skipped
  without the .NET SDK; the hash check skipped fails 1)
- [x] batch 47 · 147 tests that run the game's own SDK code against the host (`host/sdktest.c`, `DM_SDK_TEST=time,font` with any run; after
  totk-mobile's `tests/test_time_sdk.c` and `test_font_sdk.c`, Apache-2.0, written anew; they need the game, not CI): at the 60th present a thread
  started through the SDK's own `nn::os::CreateThread` calls by name what the game's calls reach (`loader.c` `dm_sym_bound`: the host's take-over
  where there is one, else the SDK's code; `guest_sym` gives the SDK's definition even where the game's calls are taken over, and calling
  `nn::time::Initialize` that way reached the time service over IPC and aborted), logs "[sdk-test]" lines and ends the run (exit 0 / 1; nothing
  saved). time: Initialize, the user and network clocks within a second of the host's, ToCalendarTime of now equal to localtime_r with its UTC
  offset, the SDK libc's own time(); font: shared font 0 loaded with a TrueType header, and the SDK's font engine (nn::fontll) loads it and draws
  'A' at 30 px. Tears of the Kingdom and manosaba: 8 of 8 each. Vibration waits for 67 (the host has no vibration yet). The user clock an hour
  ahead (`DM_TEST_SDK_TIME_SKEW=1`, `hle_time.c`): 2 fail (the user and network clocks)
- [x] batch 48 · 148 an in-game settings panel and a layout editor for the on-screen controls (BotWRecomp's settings button and lordran-native's
  control editor, MIT, the ideas): `touchpad.c` moves and sizes (0.5-2×) any control and the stick's home from its default place, each kept inside
  the safe area, from a spec the front end keeps (`dm.touch_layout`), draws them at an opacity (`dm.touch_opacity`, 0.15-1), and in its editor
  (`tp_edit`) a finger drags the control under it while nothing reaches the game; a menu button between - and + asks for the panel. iOS
  (`front_ios.m`): the panel opens from that button or a three-finger tap in any game (the touches still reach the game) and writes the Settings
  page's keys, so frame rate, MetalFX, the frame rate line, the camera and the opacity apply at once (render resolution at the next launch);
  "Edit controls layout" shows every control outlined and a bar with the size of the one touched last, Reset and Done (Done saves the spec).
  The Settings page gains the opacity. `t_touchpad` (12 more: a spec moves and sizes A, pressed at its new place and not the old, B pushed off
  stays inside, the stick's home moves and shrinks, the spec read back, a bad one ignored, reset, the editor drags and sizes X with nothing
  reaching the game and X works there, the menu button presses nothing, opacity clamped; the customization ignored fails 5). On a device (the
  user): the panel from the button and from three fingers, a layout edited, saved and kept across a launch

Tier 3d, from the evening sync of 2026-10-20 (sources in that section; after 3c, before Tier 4). Correctness, small:
- [x] batch 49 · 155 the present viewport rounded before it is centred (`nvn_mtl.m` `present_vp`, now also the Metal 3 path's; Touchable 5cab3ac,
  MIT): `floor((dh - ch * s) / 2)` put the picture at -1 on 2868x1320 and 3456x2234 (the Mac's full screen), also 2796x1290 and 2556x1179.
  `t_present_vp` (4 on 11 targets × 5 pictures: whole, scaled size, centred, fills within 1 %; centred before rounding fails 2)
- [x] batch 49 · 156 dropped and slow frames judged against each frame's hold (`nvn_pace.h` `nvn_hold_ms` / `nvn_expect_ms` / `nvn_dropped` /
  `nvn_slow`, after nxnative e7a84c7, MIT): each present's hold (period × interval) goes into a ring by present number and the on-screen interval
  read 8 presents later is judged against the larger of its two presents' holds (dropped: over 1.5 × or discarded); the frame interval line counts
  frames over 1.1 × their hold (it was > 16.7 ms: 299 of 300 at 30 fps); `[slowframe]` the same. `t_pace` (3 more: 30 fps none, 60 fps with 5
  doubled frames 5, an interval change none and a discarded frame one; against 60 fps whatever the hold fails 2)
- [x] batch 49 · 157 the oe notification event set and cleared under the queue lock (`hle_oe.c`, after nxnative ee51071, MIT): cleared after
  unlocking, a push in between was signalled and then cleared, a message queued with the event clear. `t_oe` (2 more: another thread pushes between
  an empty pop's check and its clear, the event ends signalled; popped, clear; cleared after unlocking fails 1)
- [x] batch 49 · 158 the user clock context in Horizon's layout (`hle_time.c`, after nxnative a35e964, MIT): the offset in seconds and the steady
  time point it was made at with its source (it held wall minus guest nanoseconds in the first 8 bytes), kept while both clocks move together and
  made again when they part by more than 2 s, once under `DM_FIXED_TIME`. `t_time` (3 more: offset + point = user time with the point the guest
  seconds, the same bytes a second later, a new one after a 60 s jump; the old layout fails 2)
- [x] batch 49 · 159 the Metal 4 feedback queue drains its autorelease pool per work item (`nvn_mtl.m`, lordran-native 6145d1d, MIT): a
  long-lived serial queue drained it only when idle. `t_feedback_pool` (lordran's check, written anew: an object autoreleased in one work item is
  gone before the next; without the attribute fails 1; nothing on Metal 3)
- [x] batch 49 · 160 GetInfo of an unknown type answers InvalidEnumValue 0xF001 (Atmosphère's GetInfo default; module 1, description 120;
  Touchable the same; ours succeeded with 0), with a red line once per type and its caller: neither game asks one in its first 40 s. `t_guest_mem`
  section 11 (1 more; succeeding with 0 fails it)

Tier 3d, Tears of the Kingdom:
- [x] batch 50 · 161 sampler border colours Metal lacks (after MacNativeGames b6231e6 / 874a32d, #49, MIT, written anew). The census (a line per
  colour, the count in the timing summary): Tears of the Kingdom's prologue 4 border samplers, none off Metal's three; the cliff save 6, one
  (0.5, 0.5, 0.5, 0.5), the grass interaction map's, which was drawn as transparent black. Such a sampler now clamps to its edge, and a vertex or
  fragment stage that samples through it gets a variant (`border_variant`): its MSL with those `.sample(` calls made `dm_border_sample(colour, ...)`
  (2D, 2D array, 3D; depth the red; cubes have no border), compiled as `dmsh_build.py` compiles, another NvShader with the same reflection so its
  pipelines have keys of their own while the warm-up list records the plain ones; a stage is looked at again only when a bind brings such a sampler
  in or one was bound (a scan at every bind cost 7 % of the replay; now 0.6-0.9 %). The cliff save: one fragment and one vertex shader made so. The
  source is the `.metal` beside the library: an iOS app has none, so there such samples read the edge colour (a red line once; for 62 the
  sources of the shaders seen to need it could ship). `t_border` (4, Metal 4 and 3: the colour taken as one Metal lacks and black not, a plain
  sample reads the edge, the rewritten one 0.5 outside and red inside, a 2D array call rewritten compiles; the nearest of Metal's three fails 1).
  To check in a game (the user): grass that stands up away from Link and can be cut, in a grassy area of Tears of the Kingdom
- [x] batch 51 · 162 one pixel shader bound to float and to integer targets: every key in `fout.tsv` keeps its float library and gets an
  integer-output variant `<key>_i_b` (`dmsh_build.py`, the same capture translated with the integer outputs), and `pso_desc` takes the variant for
  a pipeline with an integer colour format in a slot the shader writes (`fout_fn`; the colour formats are already in the pipeline key). Before,
  `fout.tsv` made a key integer only: 0544282c162305db and e3fb0bb58a691e10 (ours) are also bound to float targets in MacNativeGames' shared list
  (MIT, keys as data), so those draws would have failed. `games/totk/fout.tsv` now lists that list's 23 keys of ours with both kinds. Chain
  ca5eb9623812 (23 integer variants for Tears of the Kingdom, 28,675 libraries; manosaba has none). `t_fout` (4, Metal 4 and 3: the float function on RGBA8 and
  the variant on RGBA16Uint, both pipelines made; no variant keeps the float function; an integer slot the shader does not write does not count;
  the float function whatever the target fails 1)
- [x] batch 52 · 163 hair and cloth at 60 / 120 fps (MacNativeGames PR #79, merged, MIT; its sites, routine words and Unicorn table as data,
  the code ours): Tears of the Kingdom's actor animation update has a second clock, for cloth and hair, that it builds at every frame as
  {seconds = factor / 30, ratio = factor}, so at 120 fps it runs four seconds per second; the real-time frame step (realtime) does not reach it.
  `games/totk/rules.py` CAVES: the two inlined copies (0x246eac, 0x345904: the fmul that makes seconds from the constant 1/30 becomes a routine
  that stores ratio = page+0 × factor over the stored one and makes seconds = page+8 × factor) and two of the three calls to the out-of-line
  update 0x2a0e40 (0x24944c, 0x28bc98: s2, s3 × page+0 before the call; the third, 0x2afac18, passes measured time already). Scratch x8 and s4
  (checked: none of the three callers sets v4, 0x2a0e40's 857 instructions never name it). The PR makes those calls `bl <veneer>` ending in
  `b 0x2a0e40`; here the routine calls it and branches back (Tears of the Kingdom has no C++ exceptions to unwind through the patch area).
  `frame_step.c` writes seconds at host page +8 too. The fix belongs to realtime, so the rewriter gained caves behind an option
  (`patch_nso.py`: a fifth item names the option; the routine is emitted, the site keeps its word and the branch to the routine is a row of that
  option in options.tsv, which the loader checks and writes as any other; caves.tsv has an option column; `structck` ⑬ checks the kept word,
  exactly one option row of the original word to `b <routine>`, and walks the routine's exits). The product: main's patch area 104 bytes longer
  (the four routines at its end), 4 more realtime rows, every other byte and table as before. `games/totk/check_cloth_clock.py <product> <main NSO>`
  (5 checks; the PR's 144 cases, 15-120 fps × factors 0, 0.25, 1, 2 × 4 sites: {factor / fps, factor × 30 / fps} with the ordinary clock, the
  factor, sp, the flags and x9 kept and execution back after the site; the original main and the product without realtime give the game's own
  4 s per s at 120 fps; the third caller untouched; +8 left at 0 fails 1 (54 cases), the product from before fails 2). `t_rewriter_synth` (+4:
  a cave behind an option keeps its word, one options row, 7 without the option and the page value with it in Unicorn, structck passes it; the
  branch written as well, DM_TEST_CAVE_GATE_WRITTEN, fails 3). `t_frame_step` (+1, page +8; not written fails 1). `verify/regress.sh --fuzz`
  OK for both games (⑬: 4 caves behind options, their exits walked). The 60 fps product: `option realtime: 13 words of main`; under lldb the
  0x246eac and 0x28bc98 routines are reached 23 s into the prologue (the other two belong to actors it does not show), 28,500 frames at 120 fps
  without a new red line. To check in a game (the user): hair and cloth in wind at 60 and 120 fps against 30
- [x] batch 52 · 164 a mod that carries one of our RomFS overlay files (MacNativeGames #73, open: a translation ships its own Common UI
  archive, whose quick-menu animations the 60 fps overlay changes; there both together stop the game). Here mods sit in front of the game's
  overlay, so the mod's copy won and the fix was lost without a word. Now at boot (`mods.c` `dm_mods_pin_overlay`) each file the overlay's
  `manifest.tsv` lists that a mod also has is decided by the SHA-256 of the mod's copy: the game's own file unchanged → the overlay's file;
  `<mods>/-merged/<that SHA-256>/<path>` exists → that merge; otherwise the mod's copy with a red line that names the command to merge them.
  The choice is a pin (`hle_fs.c` `dm_rom_pin`: that one file served from the given host file whatever the layers hold). `romfs_overlay.py
  <RomFS> <mods> --merge <the mod's file>` makes the overlay's change to the mod's copy with the same checks (capture keys hidden at 0 and shown
  at 1, only the two animations change; the game over flow must be the analysed file) and writes the merge there; a copy that changed those
  keys itself is refused and stays the mod's. Without --merge the overlay comes out byte for byte as before. `t_mods` (+4: SHA-256 against the
  standard's "abc" value; unchanged copy → overlay, merged → merge, changed without a merge → no pin; the mod's copy always fails 3), `t_fs`
  (+2: a pin wins over the layers, others as before; pins ignored fails 1), `t_romfs_overlay` (+2 with the RomFS: a copy with another member
  changed comes back with both changes, a copy with its own capture keys refused; the game's file merged instead fails 2). In the game
  (`DM_MODS`, `DM_FS=1`): a mod carrying the original archive → opened from the overlay; one with a texture byte changed and its merge →
  opened from `-merged`; the same without the merge → the mod's copy and the red line. On iOS the merge is made on a Mac and copied to
  `Documents/mods/-merged`
- [x] batch 53 · 165 decals that read their own G-buffer pass, a switch (`DM_NVN_FBX=split`; MacNativeGames PR #67, open, MIT, written anew in
  `fbx_prepare`): no copies; the pass is stored and reopened before a draw that samples an attachment holding pixels newer than memory: cleared at
  load, written by a draw that samples none of the pass's attachments (`sd_att.pw`), or written by any draw before a texture-invalidating barrier
  (as item 42's copies); a run of sampling draws (the decals) shares one split; a clear still pending before the pass opens is flushed first. The
  cliff save: 84 draws a frame sample their pass, 3 splits a frame (the PR measured about 3), GPU p50 6.0 against 5.9 ms (noise about 3 %); the
  prologue: no split at all (its self-sampling draws open their passes), s1300 and s1800 inside the console footage's ranges as the default's are.
  Default off: the ghosting it fixes shows in motion at a place with ground decals, which needs footage and a save there. `t_fbx` split set (14,
  Metal 4 and 3, API validation: one split before the reading draw, a run of two shares it, a plain draw between makes two, the first draw of a
  pass none, a pending clear flushed, a barrier between two reading draws splits; a split before every reading draw fails 2). To check (the user):
  a turning camera over paved ground or decals (Hyrule Castle courtyard in the PR) with and without the switch
- [ ] 166 a three-hour soak of the 60 fps product before 151 (MacNativeGames #57, #72, #74; upstream #90). First run (batch 53's tools:
  `tools/soak.py run --input` repeats a cycle of command channel lines around the screenshots, `--input-after` from a second on; `games/totk/
  soak_walk.cmd` walks Link round a square with jumps; `t_soak` +3, key presses taking no time fails 2): the cliff save at 120 fps stopped after
  47 min with `FATAL GPU allocated 6555 MB, above the budget of 6553 MB` (MacNativeGames #72's stop, there after 2 h 43 min); footprint +5.4 GB/h.
  The memory line names it: render targets of virtual pools, 3,185 made and all alive with textures of their own (3.6 GB at 40 min): the game
  keeps those texture objects (on the console they only alias its 111 MiB pool), each one a Metal texture here. Item 138's `DM_NVN_RT_HEAP=1`
  bounds them to a heap of 1.25 × the pool; next: the same soak with it, then its default (on iOS the TBDR rewrites turn it off)
- [x] batch 53 · 167 a teleport command (`games/totk/survey.c`, `DM_SURVEY=1`; MacNativeGames d164868 `world_survey.c`, MIT: its 1.4.3
  addresses, actor layout and bounds as data, written anew): `survey pos <tag>` / `survey move <tag> X Y Z <frames>` on the command channel (a
  new hook `dm_game_command` takes lines the host does not know, checked when read), run at the frame point (new hook `dm_game_frame_sync`: the
  game's thread right after nvnSyncWait on the last acquire sync, main 0x2db958) through the game's forceSetMatrix (main 0x45ce70, its first
  four words checked; the same in our NSO and product), the player found from the scene slot with guest reads through mach_vm_read_overwrite.
  On the cliff save: Link at (373.4, 2332.8, 1564.8) moved to (393.4, 2362.8, 1564.8) in 3 frames, then falling (2358.4 a second later), the
  shot shows him skydiving; a bad tag refused in red. `t_survey` (10 against a scene built in memory: forceSetMatrix's words, bad lines taken
  but not queued, pos, the move's matrix rows, three calls then done, busy, no player, NaN rotation; the rotation transposed fails 1),
  `t_cmdchan` (+1: a game's line checked when read and run as the game's). An overworld save is still needed for towns (the user)
- [-] batch 53 · 168 measured: no second BC4 encoder in Tears of the Kingdom's runs. A census of texture type mismatches (`ttm_note`: each
  shader, stage, slot, declared and bound type logged once) over the prologue and the cliff save: 2D against 2D array both ways (handled by
  `tex_fit`), and one kind left: pixel shaders 5a5f059e21372e70, 8cac072f6f952665 and dd129a59ab3a409b declare slot 9 as texture3d and get the
  4x4 RGBA8 texture 258, sampled for .x inside a branch (no visible effect found). The BC4 encoders (0544282c162305db and kin, texture3d) are bound
  3D here; MacNativeGames' shared list shows them on 2D textures elsewhere in the game: a 2D-to-3D copy waits for a scene that shows it

Tier 3d, iOS, Mac and tooling:
- [x] batch 53 · 169 shader functional keys as a fallback (Touchable's internal/tools/shader_keys.py, MIT, written anew): sha256 of {stage, the
  constants' start − the code's start, the end − the constants' start, a compute workgroup} and the code from its start to its end (control +0x700
  / 0x704 / 0x708 / 0x714 / 0x748), computed at SetShaders (`shader_fkey`) and kept by program serial in a side table (`mtl_prog_fkeys`: lordran
  94a6307 found a game whose NVNprogram is smaller than the header's 0xC0, so prog_t does not grow); `shmap_from_capture.py` writes it as the shader
  map's third column; a key not in the map takes the first key with its functional key (`shmap_resolve`, logged once). manosaba 2,740 shaders →
  1,299 functional keys, Tears of the Kingdom 28,652 → 25,320 (its static shaders are all captured, so nothing is rescued there today). Maps
  regenerated in the workspace (keys and stages unchanged). `t_nvn` 5c (+5: the host's value equals the map tool's for the same bytes, metadata
  does not change it, a constant byte does, compute workgroup, a stage it does not take), `t_shmap` (5: itself, the twin, none, the table, the
  side table; no fallback fails 1), `tools/tests/t_shmap_fkey.py` (6; the workgroup left out fails 1)
- [x] batch 53 · 170 the game language from the app's own language setting (Touchable's switch_language / CFBundleLocalizations, MIT, written
  anew): game.json `languages` (nn::settings codes, checked by `gen_game.py`; manosaba zh-Hans and zh-Hant, its StreamingAssets/config.json;
  Tears of the Kingdom 14 from its Mals regions, Portuguese left out: USpt's code is not in this SDK's list) → `CFBundleLocalizations`
  (`ios/tools/project_lang.py`, Apple's names), so Settings > Apps > <game> > Language offers them; at launch the first preferred language maps
  through `host/lang.h` (script and region: zh-Hant / TW / HK / MO, en-GB for GB IE AU NZ ZA, fr-CA, es-419 for Latin America) to DM_LANG when the
  game has it, else the profile's. `t_lang` (6; the language alone fails 5), `tools/tests/t_game_lang.py` (8; codes not checked fails 2). To check
  (the user): the Language row in Settings and the game's text after a change
- [x] batch 53 · 171 the touch controls below the iPadOS window controls and controllers (Touchable, MIT, written anew): `tp_layout` takes the
  window controls' bottom (iOS 26 `UIViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:`) and puts the left column (ZL, L, D-pad) below
  it, custom layouts too; a controller connected leaves the controls, a key newly pressed on it (or a stick past half way; `host/gamepad.m`
  `dm_front_controller_key`) hides them, a touch anywhere or the last controller gone shows them (`tp_controller`). `t_touchpad` (+11: Touchable's
  table of ten devices, each with every control inside its safe area, none overlapping and the left column below the window controls; the
  controller sequence; the window controls ignored fails 3, hidden on connect fails 1). To check (the user): an iPad window, a controller
- [x] batch 53 · 172 ProMotion 120 Hz for a game that steps by real time (Touchable's SS_DISPLAY_HZ=120, MIT): `nvn_promotion_hz` (Tears of
  the Kingdom with realtime: 120 while a 120 Hz screen runs at 120, back to 60 when iOS lowers it; DM_PROMOTION=0 or DM_PRESENT_HZ leave it); the
  rate hint asks 80-120 Hz and reports the rate it runs at (`nvn_display_rate_now`). manosaba checked first: with keys at fixed frames, frames 600
  and 1500 at 120 Hz match those at 60 (0.17 % and 0.00 %): it steps per frame and stays at 60. `t_pace` (+6; a game stepping per frame at 120
  fails 1). To check (the user): a ProMotion iPhone's frame rate line in Tears of the Kingdom
- [x] batch 53 · 173 install checks (Touchable's launcher install, MIT, written anew; `ios/tools/install_check.py`): `run.sh install` refuses an
  expired signature or a profile without this device (UDID from `devicectl list devices`), warns within two days of expiry, and backs the app's
  Documents up with a SHA-256 manifest first when the app is installed; a failed backup stops the install. `tools/tests/t_install_check.py` (7;
  the expiry check skipped fails 1). To check (the user): one install to the phone
- [x] batch 53 · 174 the iOS shader directory checked complete before packing (acnh-mobile 67afe52 check_invariance, MIT;
  `ios/tools/shader_check.py`): every map key's `_b.metallib` and json of this chain and stage, every fout key's `_i_b`; run.sh stops otherwise.
  Found: Tears of the Kingdom's profile names manosaba's iOS directory (2,740 libraries), so its app would have been packed with none of its
  28,652 (+23 integer variants); the check refuses it now. `tools/tests/t_shader_check.py` (6; the library check left out fails 2)
- [x] batch 53 · 175 the game data verified once, then a marker (super-mario-odyssey-ios ea19678, MIT; `dm_romfs_validate_marked`): the key
  files every launch, the tree walked once and then trusted while the marker matches the game, version, expected totals and the tree directory
  (device, inode, mtime); DM_ROMFS_RECHECK=1 walks again; a failed check removes it. iOS keeps it in Application Support and logs walked / marker
  with the time. `t_romfs_validate` (+7; a marker trusted for the key files too fails 1). To check (the user): the boot line's time on the phone
- [x] batch 53 · 176 residency sets only on an Apple6-family GPU (GirlNet d2r-mobile 3e051f1, MIT): a virtual GPU (GitHub's runners) is Mac2
  only; the Metal 3 path then relies on its useResource declarations (the whole Metal 3 gfx suite passes that way with DM_TEST_NO_RESIDENCY_SET=1),
  Metal 4 stops with a clear line. CI job `gfx-m3` (by hand: Actions → CI → Run workflow) runs the Metal 3 gfx tests on the runner's virtual GPU
  until the passing set is known. `run-m3` runs t_fbx without a set (the set made regardless fails the check of its line). Not run on GitHub yet
- [-] batch 53 · 177 measured first (LASRecomp dc1d28f: a game that waits for its newest submission right after presenting never overlaps a
  frame's CPU work with the last one's GPU work; they wait for the fence before the newest instead): a census in nvnSyncWait (waits, those on
  the newest submission still on the GPU, time; a line a frame window). Tears of the Kingdom at 120 fps: 2 waits a frame, 0.17-0.20 on the newest
  still running, 0.32-0.36 ms a frame; at 30 fps on the cliff save: none on the newest. manosaba: 4 a frame, 1.00 on the newest, 1.6-1.8 ms a frame:
  the pattern LASRecomp fixed, with room left on the M5 at 60 fps. Not changed: whether manosaba misses frames on a phone decides it (97 / 142).
  `t_nvn` 6i (2: a wait on the newest submission held on the GPU counted as such, the same after it completed not; counted done too fails 1)
- [x] batch 53 · 178 the diagnostics export redacted (lordran 40a9a72's rules, MIT, written anew in `host/rundiag.h`): line by line, header
  included: app container and bundle paths, a home folder's user, UUIDs, MAC, IPv4 and e-mail addresses; frame numbers, offsets and versions stay.
  Memory read every 60 frames (lordran 24740d4: short spikes), the line still printed on 64 MB changes; the memory line gives the Metal 4 command
  allocators (main, uploads, presents). `t_rundiag` (+5; nothing redacted fails 3), `t_cmdalloc` (2, Metal 4 and 3; left out fails 1)
- [ ] 179 tests to take: overlapping `UpdateUniformBuffer` writes (Touchable), a 512 re-upload soak (lordran), the pass-continuation cases
  (GirlNet `t_p5r_rt_continue_gpu.m`). Batch 53 took the first two: `t_nvn` 5a2 (144 bytes, 16 at 128 and 64 at 64 over them, 128 cases × 8
  rounds, every byte the last write's, run with renamed updates and with DM_NVN_UB_RENAME=0; the newest applied first fails 1) and `t_upsoak`
  (512 re-uploads of a 1920x1080 texture: one upload then 511 re-uploads, at most 3 alive, the allocation flat from the 128th, none resident after
  release; released textures left resident fails 1). The pass continuation cases are next
- [x] batch 34 · 180 screens judged by TypeSafe's Jev model drive the test runs (the d2r-mobile author's method; the user agreed to send
  screenshots, the key stays outside the repository in `~/.config/typesafe/api_key`): `tools/jevdrive.py` follows a running host's command
  channel (`DM_CMD`, `DM_NVN_SHOT_OUT=960x540`: `shot`, ask, `key`) until a goal's done condition holds on two frames in a row; goals in
  `games/<id>/jev.json` (questions in English, as the user advised; ordered rules with guards such as "never A while the HUD is up": on the
  cliff A dives off it). Tears of the Kingdom `load_save`: the cliff save's HUD at 24.5-25.5 s in three runs with four or five presses, against
  about 50 s for the fixed `A@26.5,A@31,A@35`; manosaba `first_dialogue`: the first story line at 16.9-19.2 s in three runs (the highlight
  described as pink with a Chinese caption, the padlock as no highlight: confidence 0.4-0.5 → 0.95-1.0, one press). About 2,000 input tokens
  and 350-470 ms a judged frame. Without a key it exits 2 and the fixed keys stay the fallback. `tools/tests/t_jevdrive.py` (15, no network:
  both goals' rules, a loop against a fake host, no key; each rule's first condition alone fails 4)

Tier 4, large:
- [ ] 61 NVN translation on a thread of its own (after 5, 47 and 49, to measure it; the render-queue cases and job queue of 2026-10-20 first)
  - [ ] 61a LASRecomp's render-queue cases as a test first (nested lists kept, a fence in a nested list published, two waiters, the four wait
    results, a fence whose ring slot was reused)
  - [ ] 61b deferred bindings frozen at submit; binds of more than 32 handles split, not dropped
  - [ ] 61c a per-frame critical-path trace (submit, commit, GPU, waits, acquire, present)
  - [ ] 61d the translation thread's time-constraint policy (re-declared at most every 200 ms) against none, on the iPhone
  - [ ] 61e full-screen single-draw passes as compute, measured; idle gaps between encoders from a Metal System Trace
- [ ] 62 Tears of the Kingdom on iOS: guest memory layout, VP9 on iOS, BC → ASTC, a device run (after 19, 25, 59); the cold-texture census of 56 under its memory limit; code options (57) through embedded changed pages (tomodachi-native afd88e9)
  - [ ] 62a guest memory: reserved in `main()` before UIKit and Metal, a gap log on failure, the base checked against 2^39, `guest_heap_mb` at least
    2,176 and no pool add on iOS
  - [ ] 62b VP9 on iOS: libvpx cross-built with a pinned hash, or VideoToolbox's VP9 decoder (iOS 26.2+) with libvpx as fallback
  - [ ] 62c BC → ASTC on the GPU (MacNativeGames `nvn_bc1astc.c`, `nvn_bctc.metal`)
  - [ ] 62d handheld mode by default on iOS (1280x720)
  - [ ] 62e RomFS as one image or 1 GiB parts with an index; a ready marker instead of the tree walk at every launch
  - [ ] 62f `maxBufferLength` and the reservation base logged at boot; a cooldown between A/B runs on the phone
  - [ ] 62g one varying interface as a fallback library (bayonetta3: every vertex shader exports the union of the pixel inputs) if a device's log
    shows a "not written by vertex shader" failure (item 137: none on the Mac since chain bab8d4746968; the red line names both keys)
  - [ ] 62h render targets of virtual pools in a heap on for iOS (`DM_NVN_RT_HEAP=1`, item 138: 140-190 MB less GPU memory on the cliff save)
    after screenshot comparisons over more scenes; with the TBDR rewrites on it needs them alias-aware (pass merges and content facts by heap
    range) or stays off
- [ ] 63 the 2K canvas and MetalFX (veneers in the rewriter, HeapDef overlay, larger application pool; after 52 and 95)
  - [ ] 63a MetalFX's docked guard against the scene's content size, not the allocated texture (MacNativeGames ff092e3)
- [ ] 64 for the next game: add-on content, FenceSync, ClearBuffer and the other NVN functions, LDN (the list of 2026-10-20 too)
  - [ ] 64a the 47 NVN functions bayonetta3-mobile has and we lack (deferred binds, ClearBuffer, indirect draws, getters, ZCull, subtiles)
  - [ ] 64b CopyCommands as a copy made at the call, a command-handle registry, CALL recursion bounded (dr3's 64 checks)
  - [ ] 64c depth bounds through `setDepthTestMinBound:maxBound:` (bayonetta3's 24-pixel vectors)
  - [ ] 64d rasterizer discard (vertex variants that return void), two queues, `SetScissors` / `SetDepthRanges` batches
  - [ ] 64e several texture and sampler pools (bayonetta3's descriptors, lordran's 58 checks)
  - [ ] 64f tessellation through a Metal 3 command buffer between Metal 4 submissions
  - [ ] 64g HSETP2 `.H_AND` as LASRecomp and yuzu (5 vectors); translator call arguments kept local
  - [ ] 64h symbol resolution as Horizon's rtld; a SystemEvent kept while its detached read end waits
  - [ ] 64i `getentropy` as HLE_FORCE, `nn::pl::GetSharedFont`, IL2CPP's `stat` / `lstat` by name, the crypto RNG
  - [ ] 64j AudioOut volume and counters; AVC / AAC from the bytes the game sends; FlushTexels as a CPU write
  - [ ] 64k named time-zone rules, `IsSaveDataExisting` from the directory, OpenPreselectedUser, the NPDM's system resource size,
    RegisterClientEvents keeping its pointer, ShowControllerSupport's result, `\` and `/rom:` paths, Unity shader layouts before Unity 6
  - [ ] 64l the control section length by GPU code version (0x838-0x880; acnh-mobile 0x848)
  - [ ] 64m guards against Metal aborts: a non-depth format on the depth attachment, a cube view whose slices are not a multiple of 6
  - [ ] 64n LDN as dr3 (Bonjour, an HMAC-signed control protocol, a two-process test); provisional profiles from the game files
- [ ] 112 conditional rendering on the GPU (a kernel writing indirect draw arguments)
- [ ] 113 framebuffer fetch for in-pass reads of the current pixel (after 42)
- [ ] 114 an iOS Simulator build for the touch controls, safe areas and lifecycle

Tier 5, needs the user's devices, controller or account:
- [ ] 65 increased-memory-limit with a free account (one signing attempt)
- [ ] 66 a Switch Pro controller check of 2
- [ ] 67 gyro and rumble (implemented here, checked with the controller)
  - [ ] 67a vibration handles per style, only the connected style forwarded, bool arguments masked (handles 3 / 0x10003; totk-mobile's TotK
    landing pulse, dr3's envelopes, acnh-mobile's bursts as tests)
  - [ ] 67b the Pro Controller's IMU and HD rumble over IOHID on the Mac (atone), the fallback when GameController reports zero motion
  - [ ] 67c CoreHaptics with plain players (macOS 26); phone haptics rate-limited and off at a serious thermal state
  - [ ] 67d gyro fusion with the rest bias learned (BotWRecomp's idea)
- [ ] 68 MixWithOthers on an iPhone
- [ ] 69 the game over flow with the overlay, in play
- [ ] 70 manosaba's iOS app with chain bab8d4746968 on the devices (was 1e41705de87b), with the host stack of 40 (both IPAs built with it); `ios/run.sh lifecycle` there, and the home button and the app switcher by hand (51); the log's "CoreAudio output opened in" time there (55); the on-screen controls of 58 with `DM_TOUCH=pad` (`DM_TEST_TOUCHPAD=check:30`, then by hand: the stick, the camera drag, the eye, a controller connected)
- [-] 71 not done: iOS 17 is not supported (59)
- [ ] 71b the covered-window check of 27 in a window (`DM_WINDOW=1 DM_TEST_COVER=30,10`, display on); the Metal 4 screen of 19 on a device
  (`DM_TEST_NO_METAL4=1` in Documents/env.txt); the backup exclusion of 20 in Settings → iCloud → Backups; uncapped pacing of 53 in a window (product60, `DM_WINDOW=1`)
- [ ] 71d displays above 120 Hz: macOS rounds a 60 Hz request to 72 on a 144 Hz panel (tvc97); needs such a display
- [ ] 71c in play with product60: the systems BotW had on 30 fps constants (stamina regeneration and drain, arrow speed, shield surfing, rune
  aiming, flurry rush) at 30 and at 60 fps, and cutscenes there (none listed for Tears of the Kingdom by MacNativeGames; BotWRecomp's BotW list
  as the guide)

Tier 6, optional (seen, not planned before; listed at the user's request 2026-10-20):
- [ ] 149 face buttons by position for PlayStation and Xbox controllers, as a switch (MacNativeGames 31052d6)
- [ ] 150 pchtxt patches at load and a cheat VM, refusing rewritten sites (MacNativeGames 1b33531, PR #22)
- [ ] 151 the save-loading crash workaround at 1.4.3 main+0x1bd01c4 (MacNativeGames dd0e893), if it is ever seen
- [ ] 152 frame interpolation by replaying the frame (LASRecomp `frame-interp-rt`)
- [ ] 153 frame latency 2 at 30 Hz, measured
- [ ] 154 the ultrawide aspect patch on the Mac (MacNativeGames `aspect_patch.h`)

Open:
- [ ] 72 the blood moon crash: after 1, the stack logging of 40 and 47, dump the threads right at the skip; with 130's movie hold, the skip with the
  hold, without it (`DM_INACTIVITY_FPS=0`) and with the 30 fps product (needs a blood moon in play: the user)
