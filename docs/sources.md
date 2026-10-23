# Sources

The projects this port draws from, how far each was read, and what was taken or left. Fetch them into a scratch repository, read only,
never run their code; port by hand with a test per item, check our code first (several fixes are already here in another form), and credit
the source in the commit and the README. Licences decide what may be taken: MIT, ISC and Unlicense code may be rewritten here with credit;
Apache-2.0 (totk-mobile) is reimplemented or kept with its licence and NOTICE; GPL and MPL projects (mush8, yuzu, Atmosphère, Cemu's parts of
BotWRecomp, strato, IPSwitch) and projects without a licence give ideas and facts only, never code. Ryujinx, whose shader translator we build
on, is MIT.

## How far each was read

The last commit read of each (October 2026); the next sync starts there. Older markers: the history of DEVELOPMENT.md. Look for new projects in
d2r-port-radar first. The review notes with each source's commits and numbers: the merge notes of each review (not in git) and
`git show 66fa170:DEVELOPMENT.md` for those of 10-19 and 10-20.

d2r-mobile and its derivatives (MIT, keeping jhaizhou's LICENSE; the derivatives use the old `dm/` layout):

| Project | Last read | Notes |
|---|---|---|
| [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (upstream, fetch only) | main eb3fe37 (10-23 evening) | v2.0 b811b90 (Android and HarmonyOS on a Vulkan engine, Korean, Xbox layout, iPhone Duo) and PRs #119-#121, #126; open #104, #111, #127. d2r-mobile/d2r-mobile is a rewritten copy of its history up to 21cf97b |
| [seedds/nxnative](https://github.com/seedds/nxnative) | main e454ac9 (10-23) | PRs to #64, #62 open; branch `launcher` 47fc7f9. Its history was rewritten on 10-22 (b932024's tree is 5dae4db). seedds/smbw-mobile is the same repository renamed |
| [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames) | main a988b5e (10-23) | Tears of the Kingdom 1.0.0 / 1.1.2 / 1.4.3 on the Mac; PRs to #287, #222 / #257 / #286 open |
| MacNativeGames forks | 10-23 | tvc97 e8bd37d; DavidZande/MacNativeGames `performance` a60cdb3; BennettStaley/NativeTOTK-Apple-Silicon 47c261f (base e55ad1c); dnlflores/MacNativeGames c098279 (base 4fdda41); NicholasDominici/totk-ios (1.0.0 on iPad and iPhone; its aspect data is GPL-3.0); colonnellotong/MacNativePorts |
| [T5eng/nxnative_totk](https://github.com/T5eng/nxnative_totk) (MIT with a note) | 057575d | Tears of the Kingdom 1.4.3 on an iPhone Air (iOS 27.0.1) as patches on MacNativeGames |
| [atone/nxnative](https://github.com/atone/nxnative) | 185cfda | Pro Controller motion and HD rumble |
| [chayan-bit/AnySwitch](https://github.com/chayan-bit/AnySwitch) | 30f6a8a (10-23) | base nxnative 23a11da; its Tears of the Kingdom entry is nxnative's |
| LNPANDY/nxnative-romfs-archive | 5738460 (10-23) | |
| [Nishikori-Yui/lordran-native](https://github.com/Nishikori-Yui/lordran-native) | 1e6b2cf (10-22) | Dark Souls Remastered, from nxnative; PR #40 open on 10-23 |
| [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile) | `codex/p5r-runtime` c5538d3 (10-23) | Persona 5 Royal |
| [huajiaoshu520/acnh-mobile](https://github.com/huajiaoshu520/acnh-mobile) | 1c0293b (10-23), `ios18-compat` a98d127 | Animal Crossing; its base is now nxnative e72a065 |
| ha7rock/blade2-mobile | a7dff99 (10-23) | Blade II, UE4 4.17 |
| enginefuture/bayonetta3-mobile, enginefuture/dr3-mobile | e5ce116, a31c36c | dr3: Diablo III on nxnative 9579030, with LDN |
| MZhao-ouo/Touchable | 04b6c1c (10-22) | Rhythm Heaven and Silksong; touch controls |
| kuosuko/tomodachi-native | 5587d1b | Tomodachi Life, SDK 20 |
| armyja/nxnative-aa-port, armyja/nxnative-cuphead | df997fa, a393de4 | Ace Attorney (Unity, RE Engine); Cuphead |
| shh632058099/d2r-mobile | e460049 (10-23) | iOS |
| dodola, n2ray (d2r-mobile forks) | cc01ec6; main ce813ca, `fix/x18-context-cache-collisions` 8e58df8 | dodola: the newest write per 64-page block, an ordered NVN worker |
| hyc1743/super-mario-odyssey-ios | 3ef14d3, `codex/nxnative-verify-once` 103329c (10-22) | |
| q520asdf0123/nsCHaojIMaLiAo, DanielZhangyc/smbw-mobile | 10-22, 56737aa | Super Mario 3D World on upstream; Super Mario Bros. Wonder on the Mac |
| realskyrin/ns-game-apple-arm-port | 4d2986e | MIT since that commit |
| yokeyhh/switch-port-to-apple, aerolithink, bluewater0121 | 8e1d2af, 43cde9a, 30ca74e (10-22) | a porting playbook; two A12Z iPad Pro D2R ports |
| Johnoooob/nx-port, Kamisorara/d2r-mobile, 4ppp44/d2r-mobile-ios17 | 3e6366b, 1539d80, 2a7b1c6 | unchanged since 10-19; 4ppp44 is an iOS 17 build (iOS 17 is not supported here) |
| TommyLau/botw-mobile | gone (404 since 10-17) | its commits named in this file can no longer be fetched |

Independent projects:

| Project | Last read | Notes |
|---|---|---|
| [c22dev/mush8](https://github.com/c22dev/mush8) | 9f6e2b7 (10-22) | **GPL-3.0: ideas and behaviour only.** Mario Kart 8 Deluxe in Rust; its IPA is ad-hoc signed with the increased memory limit for signers that read the app's own entitlements ([ios/README.md](../ios/README.md)) |
| peiikai/LASRecomp (MIT for its own code, with a note) | main 466fee6, `beta_5` f7b986e (10-23) | Link's Awakening static recompilation; PRs to #331, `metal-overlap` 5d17070 unmerged; hjqcan's fork is a copy |
| Apolinario-coder/SkyRecomp (MIT with a note) | b3e22ed (10-22) | Skyward Sword HD; much of it is LASRecomp's |
| IshaanKalra2103/BotWRecomp | 2b84b82 | MIT; its Cemu-derived parts (MPL) and `fps.cpp`: ideas only |
| [OJZen/totk-mobile](https://github.com/OJZen/totk-mobile) | 6d4c1d3 (10-22) | Apache-2.0 (its `core/` is upstream's MIT code); Tears of the Kingdom 1.4.3 on a Mac and M1+ iPads |
| nxporting/nxport | main 0ec6d37 (10-23) | MIT, Rust, Tears of the Kingdom 1.1.2 only |
| itzzslimechisel/totkrecomp, fedebuonco/nxrt, MayBAMemory/nvn-metal | read 10-23 | MIT (Windows through dynarmic and Vulkan); Unlicense (a runtime for static recompilations); MIT (Kirby) |
| [catoncat/d2r-port-radar](https://github.com/catoncat/d2r-port-radar) | c6ea97c (10-23, 318 entries) | an index of derivative ports |
| masagrator's FPSLocker-Warehouse, alanyangcn/smbwonder-ios | | no licence: facts only |
| vecStardo/VA-11_ns2ios | | GameMaker through GLES, no NVN: little to take |

Seen, not reviewed: CODPHISH/ns2app (Silksong on iOS), WangTingYeYe/rtg-mobile, rootrd/dontstarve-harmony (NVN → Vulkan), hitsmaxft/nsp-dawrin
(GPL-3.0: a synthetic NSP that CI could use).

## Reviews and the plan items they produced

- 2026-10-17, upstream and the derivatives: ported that day (below).
- 2026-10-19, all references fetched again: ported the same day (below) but for three features, later 63 (the 2K canvas and MetalFX), 67 (gyro
  and rumble) and 62c (BC → ASTC).
- 2026-10-19, wider review (eight reviewers): items 1-72; nxnative 2b31dbd..19d7a70 the same afternoon: 73-77.
- 2026-10-20, wider review (seven reviewers): 78-114. Deep review (each project's own work against its base): 115-141, then every remaining
  finding at the user's request: 142-154. Evening sync: 155-180.
- 2026-10-22, sixth review (performance on the iPhone; four reviewers of the references, one of Touchable, one of Apple's documentation):
  181-193. Afternoon refresh: 194-215.
- 2026-10-23, seventh review (five reviewers) and a deep review of the new Tears of the Kingdom projects (five reviewers, layer by layer
  against ours): 231-259. Below the shaders none of the new projects does more than ours.
- 2026-10-23 evening, upstream v2.0: nothing to port (see Not taken).

The items: [plan-archive.md](plan-archive.md); the open ones: [DEVELOPMENT.md](../DEVELOPMENT.md#open-work).

## Not taken (and why)

- A Metal binary archive on macOS 26 without guarding `serializeToURL` (LASRecomp turned it off after crashes; PR #145 closed).
- Compiling shaders at launch and skipping draws until ready, async shader loading with skipped draws (content drawn once would stay missing).
- Present pacing to the screen refresh (games that step per frame would run fast; only Tears of the Kingdom's real-time step can use it,
  `DM_PRESENT_HZ`).
- SignalToAddress −2 (MacNativeGames dab728a, LASRecomp's `value - 2`, justr4mzi's kind 2): Atmosphère wrote −2 from 7.0.0 until 5f2d713fe4
  (2022-06-08) made it −1, as ours.
- nx-port's clamp of fragment outputs to ±65504 (breaks R32F); GirlNet's per-draw conditional render (a CPU stall).
- nxnative's prebuilt .NET DLLs, Android / Vulkan and MoltenVK runtimes (dodola, imink/hades-ios), upstream's D2R-only parts.
- Upstream v2.0 (10-23): the arena out-parameter of PR #126 is our plan item 185; the Vulkan engine's NaN-ignoring min / max / clamp is not
  needed (on the M5 `fast::min` / `fast::max` return the other operand and `fast::clamp(NaN, 0, 1)` gives 0, as Maxwell, with safe and fast math
  alike; the iPhone not measured); the adaptive present tier does not apply (Tears of the Kingdom picks its present interval itself); the main
  thread's spin before short waits, event-read hooks, pool flags, the rewriter without a red zone, `DM_SECT` and the ucontext macros are for
  Android and HarmonyOS; separate base and update packages with title keys from tickets are in `tools/game_extract`; VP9 on iOS is ours since
  batch 54. For 190: its dropped-frame rule (a run counts only its first 3 frames unless it lasts 1 s) fits; its GPU-time controller skips
  CPU-bound frames, which is how the iPhone's power split looks (230).
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

## Ported before the plan

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
  passes. The encode stage was implemented and left off that day (on by default on iOS since batch 82).
- From the early plan list: the AudioRenderer and deferred texture binds (a267ec1), VP9 movies (a8c3ff1, `host/movie_webm.c`), small guest
  stacks (40, `host/hstack.S`), the BAHS shader split (`engines/bnsh/shader_extract.py`, 70cb843, 17766ae, 36), a texture at two colour slots
  (`rt_dup_slot`, through tomodachi-native's variant), face buttons by printed glyph (2; Kamisorara 8429dc1 was the earlier, wrong variant).

## Port when a game needs it

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
