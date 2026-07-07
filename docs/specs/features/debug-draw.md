---
owner: huyizhou
last_reviewed: 2026-07-05
status: active
---

# Feature Spec: DebugDrawService（调试几何绘制）

## 行为

- 提供 frame-local 的世界空间调试线段收集：`draw_line` / `draw_box`（AABB 12 边）/ `draw_circle` / `draw_cone` / `draw_axes`（RGB 三轴），全部退化为 `DebugDrawLine{start,end,color,thickness}` 列表；线程安全（内部 mutex）。
- `thickness` 为屏幕空间像素宽度语义（`thickness = N` ≈ N 像素宽），下限 clamp 到 1。
- 生命周期（pending/published 双缓冲）：任意系统经 `Application::get_debug_draw_service()` 提交，`draw_*` 写入 pending 列表；logic tick 末尾 `Application` 调 `commit_frame()` 原子交换发布；渲染端 `snapshot_lines()` 只拷走已发布帧，render 线程不清空。跨帧持续显示需每 tick 重新提交（线条存活到下一次 commit）。此设计消除 logic/render 双线程下"已清空、未重提交"窗口被采样成空帧导致的整帧闪烁。
- 渲染：`SceneRenderer::add_debug_draw_overlay_pass` 在 tone map、RenderDebugView、scene view overlay 之后追加 `SceneDebugDrawOverlayPass`——每线 6 顶点（2 三角形）TriangleList，VS 内屏幕空间垂线展开成 `thickness` 像素宽 quad（乘回 w 保持透视正确）、无深度测试/写入、Opaque，直接叠加在 LDR 输出上；不参与光照与 tone map，颜色原样输出。无线段时不添加 pass。
- pass 位于 TAA resolve 之后，使用去 jitter 的 view_projection（`frame.projection` 减 `frame.taa_jitter_ndc` 重建），避免线条亚像素抖动。

## 配置

无 scene_config / Engine.ini 配置项。

## 实现

- 服务：`project/src/engine/Function/Render/DebugDrawService.{h,cpp}`（双缓冲 + `commit_frame`）；commit 调用点在 `Application.cpp`（logic 线程循环及单线程 `_tick_frame`）。
- 绘制：`SceneRenderer.cpp`（`DebugDrawThickVertex{本端位置, 对端位置, side±1+thickness, color}`、按需增长的 `SceneDebugDrawLineVB` 顶点缓冲、根常量传 `view_projection` + viewport 尺寸）；shader `project/src/engine/Shaders/Debug/DebugDrawOverlay.hlsl` 的 `VSThickMain`（editor 场景 overlay 仍走 `VSMain` LineList 路径，无 thickness）。

## 约束与已知限制

- 无深度测试：线段永远画在场景之上，可能穿透几何造成误读。
- 无线帽（cap）/接头（joint）/抗锯齿；无世界空间宽度语义。
- 线段端点穿越/贴近近平面时屏幕空间展开可能出现拉伸伪影（VS 内 w clamp 到 1e-4 兜底，不裁剪）。

## 验证

- `RunRenderGate.bat` + `RunPerfGate.bat -Profile Standard`（默认无提交方，不影响 golden）。
- RenderDebugView 定位：`SceneOutput`（Color）即包含叠加结果；EngineSelfTests 覆盖线段生成 API 与 commit/snapshot 语义。

## 历史

- docs/sdd/SDD-2026-07-07-debug-draw-thickness.md（thickness 落地 + 双缓冲防闪烁 + 去 jitter）。

