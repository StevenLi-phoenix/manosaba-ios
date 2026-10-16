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
| Game dump (NSP, ExeFS, RomFS) | `dump/`, `extracted/2.0.4b/` | [PORTING.md § 1](PORTING.md#1-dump-the-game-from-your-console). `work/data/switch/manosaba-2.0.4b/RomFS` was a link to `extracted/2.0.4b/RomFS` |
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

- [seedds/nxnative](https://github.com/seedds/nxnative) (main, reviewed through 20cba12c4299d77f05b6bbfd3dfa42d6c6035da4): NSP → IPA; by
  its README Mario Wonder playable on iPhone, Super Mario 3D World + Bowser's Fury and Breath of the Wild 1.9.0 in bring-up on the Mac.
- [Johnoooob/nx-port](https://github.com/Johnoooob/nx-port) (main, single commit 3e6366b86a510361d451618ffd7c45dea5549492, unchanged).
- [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile) (main: only upstream merges since 95d6caa; branch
  face-buttons-by-printed-glyph).
- [4ppp44/d2r-mobile-ios17](https://github.com/4ppp44/d2r-mobile-ios17) (branch codex/ios17-a16-v1.0.17): an experimental iOS 17 / A16
  Metal 3 build (406028335f5f0fa65c5a421867abcde0987d0a9e, b638a1886b16f9a9d5c10eaa118231fb8544dfb9); its author reports one short manual
  test on an iPhone 14 Pro Max with iOS 17.0. For running on iOS 17 devices.
- TommyLau/botw-mobile: gone from GitHub (404 on 2026-10-17). Its commits listed below can no longer be fetched from there; nxnative now
  carries Breath of the Wild.

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
- **Tools**: nx_extract (LibHac) and nso_syms.py (aa8cc5c0cd8bd5d09bcacb019f7832df3a164452); nso_fnmap.py, which carries cuts to a new build
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
