# Ryujinx shader translator (vendored subset)

Three projects from Ryujinx 1.3.2: `Ryujinx.Graphics.Shader`, `Ryujinx.Common` and `Spv.Generator`.
The shader translation tool in `dm/gfx/tools/dmsh_xlate` references them to translate the original Switch shaders to SPIR-V.
License: MIT, see `LICENSE.txt`. Upstream: <https://github.com/Ryubing>.

Local changes (each marked `dm patch` in the source):
- `Translation/Optimizations/Optimizer.cs` `RemoveNode`: cascade into a source's definition only when that definition is
  really unused. Upstream also unlinks a multi-destination operation (a texture sample) when just one of its destinations
  loses its last use, so later copy propagation skips it and deletes definitions it still reads (seen in manosaba's
  TextMeshPro SDF fragment shaders with `UNITY_UI_ALPHACLIP`: the sample coordinates became undefined).

Ryujinx 1.3.2 的三个项目：`Ryujinx.Graphics.Shader`、`Ryujinx.Common`、`Spv.Generator`。
`dm/gfx/tools/dmsh_xlate` 的着色器翻译工具引用它们，把 Switch 原版着色器翻译成 SPIR-V。许可证 MIT，见 `LICENSE.txt`。

本地改动（源码里都标 `dm patch`）：
- `Translation/Optimizations/Optimizer.cs` 的 `RemoveNode`：只在来源的定义真正无用时才连带处理。上游在多目的运算（贴图采样）
  只有一个目的失去最后一个使用时也把它从来源的使用表里摘掉，之后的复制传播就跳过它，删掉它仍在读的定义
  （manosaba 的 TextMeshPro SDF 片元着色器 `UNITY_UI_ALPHACLIP` 变体：采样坐标变成未定义）。
