---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: Tone Mapping（HDR→LDR 输出）

## 行为

- 帧管线终点：TAA 之后，读 `scene_hdr_linear`，全屏三角形写入 `view_context.output_target`。窗口输出时该 target 即 swapchain backbuffer（`Renderer::get_back_buffer()`，由 `ScenePresentationSubsystem::submit_presentations` 绑定）；离屏输出（编辑器视口等）为独立 RT。
- **这是 RenderGate（SDD-0001）frame dump 的采样点**：`--dump-frame` 抓取的就是 tone map 之后的 backbuffer 内容（其上还会叠 RenderDebugView / overlay / debug draw pass）。
- Tone map 算子：ACES filmic 近似（Narkowicz 拟合，shader 内 `AshACESFilm`）。曝光乘数当前**固定为 1.0**（`add_pass` 调用处硬编码，无场景配置）。
- 输出格式为 `RGBA8_UNORM` / `BGRA8_UNORM`（非 sRGB 视图）时 shader 内手动做 linear→sRGB 编码，其余格式依赖硬件 sRGB 写出。

## 配置

无 scene_config / Engine.ini 配置项。

## 实现

- 类：`PostProcessToneMapPass`（`project/src/engine/Function/Render/PostProcessToneMapPass.{h,cpp}`），pass 名 `SceneDeferredToneMapPass`。
- Shader：`project/src/engine/Shaders/Deferred/DeferredToneMap.hlsl`（`VSMain`/`PSMain`，point-clamp 采样，无深度、Opaque）。

## 约束与已知限制

- 无曝光自适应 / 白点参数；改算子或曝光会全画面变化，须走 RenderGate `-BlessGolden` 流程。

## 验证

- `RunRenderGate.bat`（直接回归本 pass 输出）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView 定位：`SceneOutput`（Color）看最终 LDR；对照 `SceneBloomCompositeHDR` 等上游 LinearHDR 视图可隔离 tone map 本身的问题。

## 历史

无。
