# engines/bnsh — games that ship NVN BNSH shader files

Nintendo's own engines keep their compiled shaders in BNSH files (inside BFSHA archives, SARC packs and effect sets) instead of building them
at run time. Found while porting Tears of the Kingdom 1.4.3; which files to read and the BNSH layout follow
[MacNativeGames](https://github.com/ly05010419/MacNativeGames)' `prepare_shader_seed.py` (MIT), the code is ours.

## Shader extraction

```sh
python3.14 engines/bnsh/shader_extract.py <RomFS> $DM_ROOT/<workspace.shader_capture>
```

Python 3.14 or later (`compression.zstd`), no other packages. It reads `Pack/Bootup.Nin_NX_NVN.pack.zs`, `Lib/**`, `Shader/**` and the effect
sets `Effect/**.esetb.byml.zs` (zstd frames take their dictionary from `Pack/ZsDic.pack.zs` by dictionary ID) and writes the capture format of the
host's `DM_SHDUMP` (`<key>.ctl` + `<key>.data`), `bnsh_index.tsv` (key, stage, source) and `pairs.tsv` (the vertex / pixel pair of every BNSH
program, which `dmsh_build.py` pads vertex shaders for). Pointed at an existing capture directory it adds only what is missing: shaders already
there must be byte-identical, and only new pairs are appended.

On Tears of the Kingdom 1.4.3: 24371 shaders (14154 vertex, 10036 pixel, 181 compute), 19669 pairs, in a few seconds. 3451 of the 3793
shaders a run of the title and the prologue captured are among them, byte-identical; the other 342 come from agl / gsys / Terrain shader
archives (`Lib/**.sharcb`) and from `main`, which this tool does not parse (keep them from run-time captures). Next steps:
[PORTING.md](../../PORTING.md) (stage map, `dmsh_build.py`, `c1_guard.py`).
