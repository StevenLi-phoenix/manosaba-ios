# Development notes

Where the work stands and how to pick it up again, on this machine or a new one. The repository holds all code, tools and notes; what it
does not hold (and must not: keys, dumps, anything derived from the game) is listed below with the way to get it back.

## Restoring a working copy

```sh
git clone https://github.com/iceorange-dev/manosaba-ios.git "manosaba ios" && cd "manosaba ios"
git checkout -b manosaba-port origin/main                                  # the working branch tracks origin/main
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
2. **Trial loading peak on 8 GB iPads.** Without the increased memory limit entitlement (paid account) an iPad mini 7 lets the app use about
   3.35–3.4 GB. A trial's start adds about 1.4 GB within 0.2 s (about 116 character render targets, about 0.9 GB of Metal memory, of which the
   D32FS8 depth targets are about 0.44 GB); a fresh session keeps about 725 MB of headroom, a session of about two hours does not (the leak
   above). Render resolution does not help (the game fixes the target sizes). Memoryless depth would save most of the depth memory but the
   depth targets cross render passes that the layer splits at uniform buffer updates, so it needs pass merging first. Refusing guest memory
   does not help either: with the physical memory pool 700 MB smaller the game coped, 1000 MB smaller it broke.
3. **Unsigned IPA** (`ios/run.sh ipa`, `DM_IPA_DATA=1`): built and re-signed locally with `codesign`; installing it with an on-device signer and
   the game data read from the app bundle are being tested on an iPad Pro M4 (iOS 18.5). Not tested: a distribution certificate (no
   `get-task-allow`), the iOS 18 build on iOS 26.
4. Smaller: 13 NVN functions the game looks up that the layer does not know (returned as NULL, never called); compute dispatch never seen at
   run time; keyboard, mouse and controller input untested with this game; Metal HUD on iOS 18; generic upstream changes not ported yet
   (present pacing to the screen refresh, Mac occlusion, audio notifier QoS, `hle_fs` robustness).

Devices on 2026-10-16: iPhone 17 Pro (iOS 26.4) runs a build from before 1f54dad (the copy encoder fix), iPad mini 7 (iOS 18.6.2) a build of
68c048b, iPad Pro M4 (iOS 18.5) the unsigned IPA above.

## Community ports to draw from

Derivatives of d2r-mobile, reviewed 2026-10-16. All MIT and keep jhaizhou's LICENSE. They use the old `dm/` layout: fetch them into a scratch
repository, read only, never run their code, port by hand with a test per item, and credit the source in the commit and the README.

- [seedds/nxnative](https://github.com/seedds/nxnative) (main, tip c7e5b57c7418f80547a2a5e8017289102ff19041)
- [Johnoooob/nx-port](https://github.com/Johnoooob/nx-port) (main, single commit 3e6366b86a510361d451618ffd7c45dea5549492)
- [TommyLau/botw-mobile](https://github.com/TommyLau/botw-mobile) (branch botw-host-boot, tip 1f5bbe86f86a65fa0454da92a1e725d30ffb13db)
- [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile) (main)

Already ported: thread exit off the guest stack (014c5b3), crash report stderr trylock (1f1929e), dirty tracking of shared host pages
(e4292e1), RomFS name index (af27ace), save full-disk robustness (eb86cfc, from upstream b94670c), msl_retarget reflections (56fcd40, from
upstream 1784563).

Port when a game needs it:

- **AudioRenderer** (first-party games): nxnative `dm/host/hle_audren.c` (50a9c65b5f402b796cc7e7f0b011e11b2015f30f, the SDK renderer
  in-process); nx-port's own PCM16 / DSP-ADPCM mixer (3e6366b…); botw drives the SDK renderer from its own workers (1f5bbe86…). Ours has
  AudioOut only.
- **nn::ro .nro modules**: nx-port `dm/host/hle_ro.c` and loader changes (3e6366b…).
- **VP9 movies** with libvpx: nxnative a9e4f18ed17b17bb5cd8eef422464e94a017654b (decoder hooks), 5e84062fad1f07e95f59c1cc6a94918c463d077e
  (libvpx).
- **Shaders** from Nintendo's containers: nxnative `dm/gfx/tools/shader_split.py`; older Unity bundles: `dm/gfx/tools/unityfs.py`
  (e3cd2ff8ff0eb84a62280ef70e90c60753e546e6).
- **Tools**: nx_extract (LibHac) and nso_syms.py (aa8cc5c0cd8bd5d09bcacb019f7832df3a164452); nso_fnmap.py, which carries cuts to a new build
  of `main` (b5507f2aac7e1d1f5512de750f5810a550162b04, also for a manosaba update); build_ipa_from_nsp.py
  (0d763ec864344610cc41548a2ad01cd55def3182); DLC packing (e452f52b607bdf6f960eb843f20aeb29b803dec8); the app icon from the game's icon
  (1cd9bfb84b31fad5b413ab20baa977c16600f50a).
- **On-screen gamepad** for games without touch: nxnative `dm/ios/Sources/DmGamepad.m` (dc8a49ad7471d3f0b767032a0b755f87a98c1912,
  d41fdd6ed1643ed7f97099f18ab6fc7360786a1b, 4a0f9acbcf8e5acad01b1d6b5919bfb3e3cd0998).
- **NVN / graphics**: botw depth and stencil sampling modes (b795ac15d620f42986df3d594e846bf03f83f1ea), mip import before partial texel
  writes (549fb9d7be4362e9b82138a674a5305a722ab1a5), native sampler defaults (767a276615a6c1c1d20a7b98aec17dd170d8e3b3), declining fast-clear
  hints (0d80bff4eec2668441ff4ddd075796e3ec674099), GPU timestamps (9eebd8d746621b851189a7b4679489388911e7b7); nxnative GPU timestamps on the
  guest clock (b6b3da202abbd9ca73f45057920c7a9fdfcc0eb6), depth/stencil clear mid-pass (7058f43497c6eb26eea351ec4e45db3bf0d417ae),
  Downsample as a Metal resolve (13ff4f51a10ff6c4207a1e909e430ddd9c0644d3), per-game present interval
  (9745da2061b09fea0ea524c7d5369ad8e1357539); nx-port geometry shader point-to-quad, alpha test, newer NVN object sizes (3e6366b…).
- Pool finalize dropping texture ID views: nxnative 799df3c5edd00281c40ae8889a10c55cefe93616 (same leak class as a593388).
- botw's dirty tracking rewrite, one record per host page (1d69c8ae9cea2e83584314f8044b55c839c95c9e), if e4292e1 ever falls short.
- Small: an ad-hoc signature for LiveContainer (Kamisorara 95d6caa903bc19e602493bb5a5568650d39f52cf), gamepad face buttons by printed glyph
  (8429dc1727cbca079c60427e3b59258683f1c924).

Not worth porting: nxnative's prebuilt .NET DLLs, Android/Vulkan and MoltenVK runtimes, the upstream D2R-only parts.
