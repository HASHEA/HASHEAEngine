---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: DebugDrawService（调试几何绘制）

## 行为

- 提供 frame-local 的世界空间调试线段收集：`draw_line` / `draw_box`（AABB 12 边）/ `draw_circle` / `draw_cone` / `draw_axes`（RGB 三轴），全部退化为 `DebugDrawLine{start,end,color,thickness}` 列表；线程安全（内部 mutex）。
- 生命周期：任意系统经 `Application::get_debug_draw_service()` 在一帧内提交；渲染端 `snapshot_lines()` 拷走；`Application` 主循环每帧 `clear_frame()` 清空。跨帧持续显示需每帧重新提交。
- 渲染：`SceneRenderer::add_debug_draw_overlay_pass` 在 tone map、RenderDebugView、scene view overlay 之后追加 `SceneDebugDrawOverlayPass`——LineList、无深度测试/写入、Opaque，直接叠加在 LDR 输出上；不参与光照与 tone map，颜色原样输出。无线段时不添加 pass。

## 配置

无 scene_config / Engine.ini 配置项。

## 实现

- 服务：`project/src/engine/Function/Render/DebugDrawService.{h,cpp}`。
- 绘制：`SceneRenderer.cpp`（顶点转换、按需增长的 `SceneDebugDrawLineVB` 顶点缓冲、根常量传 `view_projection`）；shader `project/src/engine/Shaders/Debug/DebugDrawOverlay.hlsl`。

## 约束与已知限制

- `thickness` 参数被 API 接受并存入 `DebugDrawLine`，但转顶点时被丢弃（`make_debug_draw_vertices` 只取 start/end/color），实际恒为 1px 线。
- 无深度测试：线段永远画在场景之上，可能穿透几何造成误读。

## 验证

- `RunRenderGate.bat` + `RunPerfGate.bat -Profile Standard`（默认无提交方，不影响 golden）。
- RenderDebugView 定位：`SceneOutput`（Color）即包含叠加结果；EngineSelfTests 覆盖线段生成 API。

## 历史

无。
