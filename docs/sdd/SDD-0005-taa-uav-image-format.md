# Mini SDD: TAA resolve UAV 的 SPIR-V 格式修正（消除 Vulkan StorageImage FormatMismatch warning）

级别：S1（单 shader bugfix，`Shaders/Deferred/TemporalAAResolve.hlsl`）

## Goal

消除 Vulkan validation warning `Undefined-Value-StorageImage-FormatMismatch-ImageView`：
`TemporalAAResolve.hlsl` 的 `SceneTaaResolveOutput`(u0) / `SceneTaaHistoryWrite`(u1)
声明为 `RWTexture2D<float4>`，DXC 编 SPIR-V 时默认按元素类型推导 `Format=Rgba32f`，
而实际绑定的 view 是 `VK_FORMAT_R16G16B16A16_SFLOAT`——按 Vulkan spec 这属于
undefined value 风险（validation 明确警告 load/store 结果未定义），且项目规则
validation 报错视同 bug。

排查确认仅 TAA 两个 UAV 中招；Volumetric 系列 `RWTexture2D<float4>` 的 view 为
RGBA32F，与默认推导一致，无警告。

## Non-goals

- 不用全局 `-fspv-use-unknown-image-format` 编译选项（影响所有 storage image、
  依赖设备 capability，超出必要范围）。
- 不改 TAA RT 格式（RGBA16F 是既定精度/带宽权衡）。
- 不改 Vulkan shader 编译管线（rewrite 阶段属性丢失是既有行为，本 SDD 只绕过）。

## 取证：`[[vk::image_format]]` 方案不可行

原计划两个 UAV 声明前加 `[[vk::image_format("rgba16f")]]`，CLI 直接编译验证有效
（仓库 DXC 1.8.0.5123，SPIR-V `OpTypeImage Format=Rgba16f`），但**运行时无效**：

- 引擎 Vulkan 路径编译前经 `AshDXCContext::preprocess_shader_file_to_full_text`
  （`IDxcRewriter2::RewriteWithOptions`，非 `-P` 预处理）；该 legacy rewriter 走
  AST 重发射，`[[...]]` 属性被丢弃——这也正是引擎要在 rewrite 之后手动注入
  `[[vk::push_constant]]` 的原因（`VulkanShaderCompiler.cpp`
  `rewrite_root_constant_blocks_for_vulkan`）。
- 宏方案同样不可行：探针实验（shader 内 `#ifdef ASH_VULKAN` + `#error`，清缓存
  冷启动）证明 rewrite 阶段**完整执行预处理**且此时 `ASH_VULKAN` 未定义
  （`-D ASH_VULKAN=1` 在 compile 阶段才注入）——条件块在 rewrite 时即被剔除，
  宏展开出的属性字面量也会被 AST 重发射丢弃。

## 实际方案

UAV 元素类型改为 `min16float4`：**类型能活过 rewriter**，DXC 对 16-bit float
向量的 storage image 格式推导即为 `Rgba16f`（CLI 实证
`OpTypeImage %float 2D 2 0 0 2 Rgba16f`）。写入侧显式 `min16float4(...)` 转换
消除 `-Wconversion`。语义等价：目标 RT 本就是 RGBA16F，写入值原本就会被舍入到
fp16，显式转换不改变最终纹素值。

## Files

- `project/src/engine/Shaders/Deferred/TemporalAAResolve.hlsl`：两个 UAV 声明改
  `RWTexture2D<min16float4>`，两处写入加显式转换。DXIL 路径 `min16float` 合法，
  DX12 UAV 同为 RGBA16F，行为不变。
- `docs/specs/features/taa.md`：约束节记录 UAV 必须保持 `min16float4` 声明。
- `docs/specs/modules/graphics.md`：shader 编译要点补记 rewrite 阶段丢
  `[[...]]` 属性、预处理在 rewrite 期求值（`ASH_VULKAN` 彼时未定义）的约束。

## Verification

1. 清 `product/caches/ShaderCaches/`（shader 改动冷启动验证）。
2. Engine.ini 开 `[VulkanValidation]`，vulkan smoke：FormatMismatch warning 消失、无新增报错。
3. DX12 smoke（开 `[DX12Validation]`）：无报错、正常出图。
4. `RunRenderGate.bat`：golden 必须不变（格式标注不改变数值行为）。

## Risk / rollback

- 风险：`min16float` 的 RelaxedPrecision 语义理论上允许驱动对写入链降精度——
  实际写入目标本就是 fp16 RT，RenderGate golden 不变即证明无可见影响。
- 回滚：类型声明与两处转换改回 `float4` 即回。

## Status

Done（2026-07-06）

- 清 ShaderCaches 冷启动 Vulkan smoke（validation 开）：FormatMismatch warning
  由每帧多条降为 **0**，无新增报错。
- DX12 smoke（`[DX12Validation]` 开）：无报错、正常出图。
- `RunRenderGate.bat` PASS：vulkan ssim 0.999998 / dx12 0.999669 / 跨后端
  0.999517，golden 未变——证明 `min16float4` 不改变可见输出。
- Engine.ini 调试用 validation 开关已还原基线。
- 结论回写 `docs/specs/features/taa.md`（UAV 类型约束）、
  `docs/specs/modules/graphics.md`（rewrite 阶段丢属性/预处理时机约束）。
