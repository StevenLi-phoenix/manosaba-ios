# Switch games on Apple silicon, natively

This repository runs Nintendo Switch games on iPhone, iPad and Mac without emulating the CPU. A Switch and an Apple device use the same class of
processor, so the game's own aarch64 code executes directly. Only the few instructions that cannot run under Apple's kernel are rewritten
offline; the console's system services and its NVN graphics API are reimplemented by a small host on Apple's frameworks and Metal; the game's
shaders are translated to Metal ahead of time.

The supported game is **魔法少女ノ魔女裁判** (manosaba), Switch version 2.0.4b. The code is organized so that another game is a new
directory under `games/` plus whatever services and graphics paths that game needs: see [PORTING.md](PORTING.md).

It is a fork of [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), which ported Diablo II: Resurrected the same way; the D2R-specific code
has been removed here.

**No game content is in this repository.** You dump your own copy from your own console; everything derived from it (rewritten modules,
translated shaders, the app icon) is generated on your machine and stays out of git.

## Status (manosaba 2.0.4b)

Verified on a Mac (Apple silicon, macOS 26) and on an iPhone 17 Pro (iOS 26), including the reorganized iOS app:

- boot, title screen, new game, story scenes with sprites, CG, text and UI, the opening movie, a trial including presenting evidence;
- native touch input (one rewriter rule turns on Unity's touchscreen device): scripted touches on the Mac, by hand on the iPhone;
- saving and loading, with transactional commits; iCloud sync of the save files (needs a paid developer account's entitlements; not tested);
- memory over a long session: finalized render targets used to stay alive in the texture ID view cache (about 800 MB within three minutes of a
  trial on the Mac; an iPhone session ended without a crash report after about 40 minutes near the memory limit). Fixed: a 16-minute iPhone
  session through scene changes and a trip to the background stayed between 1.2 and 1.5 GB resident, with no upward trend;
- graphics settings in the Settings app (frame rate 60 / 30 fps, render resolution, MetalFX upscaling): 720p with MetalFX used on the iPhone;
  30 fps not yet tried there. On the Mac the render resolution barely changes this game's GPU load; 30 fps is the setting that lowers it;
- rewrite equivalence: `verify/regress.sh --fuzz` passes on all three modules.

Not verified or known open: compute shader dispatch at run time (the game's compute shaders are Unity's built-in skinning, blend shape, virtual
texturing and probe debug kernels; no dispatch happens in the story or in a trial); 13 NVN functions the game looks up that the layer does not
know (returned as NULL: this build stores the pointers but never calls them); an intermittent texture crash seen early on the iPhone, likely
fixed by an NVN struct size correction (not seen since, unconfirmed); keyboard, mouse and game controller input not tested with this game.

## How it works

| Part | Directory | What it does |
|---|---|---|
| Rewriter | `tools/patch_nso.py` | Rewrites x18, thread pointer, `svc` and timer instructions into jumps to small patch blocks; the rest of the code is untouched |
| Verification | `verify/` | Proves the rewritten modules equivalent to the originals (structure, per site, per function, gates) |
| Host | `host/` | Loader (rtld), kernel calls, `nn::` services (file system, input, audio, time, movies, fonts, …) |
| Graphics | `gfx/` | NVN on Metal, record and replay; offline shader translation (Ryujinx translator → SPIR-V → MSL → metallib) |
| Apps | `ios/`, `tools/mac_app.sh` | iOS app; Mac command-line host and `.app` |
| Per game | `games/<id>/`, `engines/<engine>/` | Game profile, rewriter rules, hooks; engine-specific tools (Unity shader extraction) |

## Getting started

1. Requirements and workspace: [PORTING.md § 0](PORTING.md#0-machine-and-workspace).
2. Dump the game (ExeFS, RomFS, system fonts) from your console into `$DM_ROOT/data/switch/` ([§ 1](PORTING.md#1-dump-the-game-from-your-console));
   for manosaba the expected paths are in [games/manosaba/game.json](games/manosaba/game.json).
3. Build and check the product, the shaders and the host:

   ```sh
   export DM_ROOT=~/dm-work
   tools/build_product.sh && verify/regress.sh
   # shaders: PORTING.md § 5
   make -C host
   DM_SAVE=$DM_ROOT/run/save DM_TEMP=$DM_ROOT/run/temp DM_WINDOW=1 DM_AUDIO=1 host/dmhost
   ```

4. iPhone / iPad: [ios/README.md](ios/README.md). Mac app: `tools/mac_app.sh`.

## Documentation

| Document | Covers |
|---|---|
| [PORTING.md](PORTING.md) | The workflow for a new game, from the dump to the device |
| [games/README.md](games/README.md) | Game directory: `game.json`, `rules.py`, C hooks |
| [host/README.md](host/README.md) | Loader, services, files, environment variables |
| [gfx/README.md](gfx/README.md), [gfx/nvn_metal_mapping.md](gfx/nvn_metal_mapping.md), [gfx/tex/README.md](gfx/tex/README.md) | Graphics layer, shader chain, NVN → Metal mapping, textures |
| [ios/README.md](ios/README.md) | iOS app: setup, signing, data on the device |
| [verify/README.md](verify/README.md) | Rewrite equivalence checks |
| [tools/README.md](tools/README.md), [engines/unity/README.md](engines/unity/README.md) | Tools; Unity-specific notes |
| [third_party/README.md](third_party/README.md) | Bundled third-party code and licenses |

## Legal

Use this only with games you own, dumped from your own console. This is an independent, non-commercial project, not affiliated with or
endorsed by Nintendo or any game publisher. Nintendo Switch is a trademark of Nintendo.

## Acknowledgements

- **d2r-mobile** (jhaizhou, MIT): the architecture and most of the host, graphics layer and verification tools.
- **Ryujinx** (MIT): the shader translator in `third_party/ryujinx`.
- **deko3d** (devkitPro, zlib): the texture layout size algorithm in `gfx/nvn/nvn_tex.c`.
- **nvn.h** (h1k421, MIT): NVN declarations in `gfx/nvn/include/nvn.h`.
- **bcdec** (iOrange, MIT or Unlicense): BC texture decoding.
- Tools: SPIRV-Cross, XcodeGen, .NET, Capstone, Unicorn, lz4, UnityPy, hactool, nxdumptool, libimobiledevice.

## License

MIT, see [LICENSE](LICENSE). Third-party code keeps its own license in its own directory.
