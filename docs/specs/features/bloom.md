---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: Bloom

## 行为

- 帧管线位置：体积光之后、TAA 之前。输入 `scene_hdr_linear`，输出 composite HDR 并替换下游 `scene_hdr_linear`。`enabled=false` 时直通。
- 四个阶段（全部 raster 全屏三角形，RGBA16_SFLOAT）：
  1. Setup：亮度阈值提取。`threshold` + `soft_knee` 二次软膝过渡；`threshold<0` 时跳过提取直接透传颜色。
  2. Downsample：从 setup 逐级半分辨率降采样，级数由 quality 决定：Low 3 / Medium 4 / High 5 / Epic 6（受 `stages` 数组上限 6 钳制）。
  3. Upsample：从最低级逐级上采样合并，每级应用 `BloomStageConfig` 的 `size`（×`size_scale`）与 `tint`。
  4. Composite：`final_bloom × intensity` 叠加到场景 HDR。

## 配置

`scene_config.bloom`（`sanitize_bloom_config` 兜底）：
`enabled`(false)、`quality`(High)、`intensity`(0.6)、`threshold`(1.0)、`soft_knee`(0.5)、`size_scale`(1.0)、`stages`（最多 6 项，每项 `size`/`tint`）、`debug_view`(Off，可选 Setup/Mip1..Mip6/Final/CompositeHDR)。

## 实现

- 类：`BloomPass` / `BloomConfig`（`project/src/engine/Function/Render/BloomPass.{h,cpp}`、`BloomConfig.{h,cpp}`）。
- Shader（`project/src/engine/Shaders/Deferred/`）：`BloomSetup.hlsl`、`BloomDownsample.hlsl`、`BloomUpsample.hlsl`、`BloomComposite.hlsl`，公共 `BloomCommon.hlsli`。
- 输出结构 `BloomPassOutputs`：`setup` / `mips[6]` / `final_bloom` / `composite_hdr`；全部为 RenderGraph 逐帧纹理，无跨帧状态。

## 约束与已知限制

- mip 链最小尺寸钳制到 1×1；`stages` 超出 active mip 数的项被忽略。
- 无自动曝光联动，阈值针对当前固定曝光（见 tonemap spec，exposure=1.0）。

## 验证

- `RunRenderGate.bat` + `RunPerfGate.bat -Profile Standard`。
- RenderDebugView 定位：`SceneBloomSetup`、`SceneBloomMip1..6`、`SceneBloomFinal`、`SceneBloomCompositeHDR`（LinearHDR 可视化）。

## 历史

- docs/superpowers/specs/2026-06-05-bloom-pass-design.md
- docs/superpowers/plans/2026-06-05-bloom-pass-implementation.md
