# Ryujinx shader translator (vendored subset)

Three projects from Ryujinx 1.3.2: `Ryujinx.Graphics.Shader`, `Ryujinx.Common` and `Spv.Generator`.
The shader translation tool in `gfx/tools/dmsh_xlate` references them to translate the original Switch shaders to SPIR-V.
License: MIT, see `LICENSE.txt`. Upstream: <https://github.com/Ryubing>.

Local changes (each marked `dm patch` in the source):
- `Translation/Optimizations/Optimizer.cs` `RemoveNode`: cascade into a source's definition only when that definition is
  really unused. Upstream also unlinks a multi-destination operation (a texture sample) when just one of its destinations
  loses its last use, so later copy propagation skips it and deletes definitions it still reads (seen in manosaba's
  TextMeshPro SDF fragment shaders with `UNITY_UI_ALPHACLIP`: the sample coordinates became undefined).
