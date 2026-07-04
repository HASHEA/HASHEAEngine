---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: RenderDebugView（中间纹理调试可视化）

## 行为

- 目的：把任意一帧内已注册的中间纹理（GBuffer、AO、阴影、体积光、Bloom、TAA 等）可视化后覆盖到最终输出，用于分通道定位渲染问题。
- 逐帧动态注册：`SceneRenderer::render_visible_frame` 每帧 `begin_frame()` 清空注册表，随后随各 pass 添加逐个 `register_item()`。注册项示例（name → 可视化）：`SceneOutput`(Color)、`SceneGBufferA..E`（D=MotionVector、E=Normal）、`SceneDepth`(Depth)、`SceneAmbientOcclusionRaw/SceneAmbientOcclusion/SceneAmbientOcclusionTemporal`(AO)、`SceneSunLightShadowMask`/`SceneSunLightShadowCascadeIndex`、`SceneVolumetric*`、`SceneBloom*`、`SceneTemporalAAResolved`（LinearHDR）。
- 显示 pass：tone map 之后 `RenderDebugView::add_pass` 读运行时配置，`enabled=true` 且 `selected` 命中注册项时，以对应可视化方式全屏覆盖写入输出 target；`selected` 为 `Off`/`None`/`SceneOutput`/空（大小写不敏感）时旁路（`should_bypass_debug_pass`）。
- 可视化方式 `RenderDebugVisualization` 枚举（每个注册项自带，非用户选择）：`Color` / `LinearHDR` / `Depth` / `Normal` / `MotionVector` / `AO` / `Scalar`。
- `draw_ui(UIContext&)`：引擎 overlay 里显示当前 selected 与命中状态；`set_runtime_render_debug_view_config` 可运行时切换。

## 配置

Engine.ini（`product/config/Engine.ini`）`[RenderDebugView]` 段：

```ini
[RenderDebugView]
Enabled=true      ; 总开关，默认 false
Selected=Off      ; 注册项 name 字符串；Off 表示不覆盖
```

启动时 `Application` 经 `load_runtime_render_debug_view_config` 读入全局运行时配置（`RenderDebugViewConfig{enabled, selected}`）。

## 实现

- 类：`RenderDebugView` / `RenderDebugViewFrameRegistry` / `RenderDebugViewItem`（`project/src/engine/Function/Render/RenderDebugView.{h,cpp}`、`RenderDebugViewConfig.h`）。
- Shader：`project/src/engine/Shaders/Debug/RenderDebugView.hlsl`（全屏三角形，按 visualization 分支解码）。
- 注册项携带 `name/display_name/texture(RenderGraphTextureRef)/visualization/format/width/height`，纹理引用仅本帧有效。

## 约束与已知限制

- 注册表 frame-local：`RenderGraphTextureRef` 不得跨帧持有；选中项当帧未注册（feature 关闭）则静默不覆盖。
- 覆盖发生在 tone map 之后：LinearHDR 等可视化自行做显示映射，与主 tone map 无关。
- RenderGate 抓帧场景要求 `Selected=Off`，否则 golden 对比的是调试画面。

## 验证

- `RunRenderGate.bat` + `RunPerfGate.bat -Profile Standard`。
- 自证：改 `Selected` 为任一注册项（如 `SceneGBufferD`）跑 sandbox，确认覆盖生效；EngineSelfTests 覆盖 ini 解析。

## 历史

- docs/superpowers/specs/2026-05-20-render-debug-view-design.md
- docs/superpowers/plans/2026-05-20-render-debug-view-implementation.md
