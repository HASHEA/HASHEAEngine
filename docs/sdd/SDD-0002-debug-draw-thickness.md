# Mini SDD: DebugDraw 线宽生效（thickness 参数落地为屏幕空间粗线）

级别：S1（单模块 bugfix，`Function/Render` + engine shader）

## Goal

`DebugDrawService` 的 `thickness` 参数（API 已接受并存入 `DebugDrawLine`）真正生效：
线段按屏幕空间像素宽度渲染，`thickness = N` ≈ N 像素宽，消除 spec 记载的
"转顶点时被丢弃、实际恒为 1px"缺陷。

**范围追加（验证期发现）**：目视验证暴露两个既有缺陷，一并修复：

1. **整帧闪烁（时有时无）**：render 线程 `_on_render()` 每帧末 `clear_frame()`，
   而线条由 logic 线程按 tick 提交，两线程无同步——render 帧落在"已清空、
   未重提交"窗口时 snapshot 到空列表，线条整帧消失。
2. **亚像素抖动**：DebugDraw pass 在 TAA resolve 之后渲染，却使用带 TAA jitter 的
   `frame.view_projection`，抖动无人补偿。

**验证期附带发现（非本 SDD 范围）**：DX12 后端交互态 TAA 整画面抖动，Vulkan 无此现象；
经 git stash 纯 HEAD 基线 A/B 复现确认为既有问题，与本次改动无关。已记入
`docs/specs/features/taa.md` 已知限制，另行立案排查。

## Non-goals

- 不改 editor 场景 overlay 路径（`SceneOverlayLine` 无 thickness 字段，选中高亮等行为不变）。
- 不做线帽（cap）/接头（joint）/抗锯齿；不做世界空间宽度语义。
- 不给 DebugDraw 加深度测试（另一条已知限制，保持现状）。
- 不新增 scene_config / Engine.ini 配置。

## Files

- `project/src/engine/Function/Render/SceneRenderer.cpp`：粗线顶点结构 + 输入布局 +
  program desc（TriangleList）+ `make_debug_draw_vertices` 改为 6 顶点/线 + pass 内
  root constants 补 viewport 尺寸；pass 使用去 jitter 的 view_projection
- `project/src/engine/Function/Render/SceneRenderer.h`（如成员声明在此）：新增粗线
  program / 顶点缓冲成员
- `project/src/engine/Shaders/Debug/DebugDrawOverlay.hlsl`：新增粗线 VS 入口与输入结构，
  root constants 的 padding 槽位改为 viewport 尺寸
- `project/src/engine/Function/Render/DebugDrawService.{h,cpp}`：pending/published
  双缓冲 + `commit_frame()`（修整帧闪烁）
- `project/src/engine/Function/Application.cpp`：logic tick 末尾 `commit_frame()`，
  移除 render 线程的 `clear_frame()`
- `project/src/engine/Base/EngineSelfTests.cpp`：自测试适配 commit 语义
- `docs/specs/features/debug-draw.md`：同一提交回写行为与限制

不动：场景 overlay batch 路径（编辑器选中高亮）、RenderGraph、Graphics 层。

## Approach

顶点着色器屏幕空间展开（标准做法）：

1. 每条线生成 6 个顶点（2 三角形），顶点携带 `{本端 position_ws, 对端 position_ws,
   side(±1), thickness, color}`；拓扑从 LineList 改为 TriangleList（仅 DebugDrawService
   这条 pass；场景 overlay 仍走原 LineList program）。
2. VS 中把两端投影到 NDC，算屏幕空间方向的垂线，按 `side * thickness * 0.5` 像素偏移，
   再除 viewport 尺寸折回 clip space（乘回 w 保持透视正确）。
3. `DebugDrawRootConstants` 现有 `float3 padding` 拿出 2 个 float 传 viewport 宽高
   （从 `SceneRenderViewContext` 取），布局大小不变，不影响共用该结构的 overlay 路径。
4. 新增独立 `m_debug_draw_thick_*` program 与顶点缓冲成员，`SceneDebugDrawOverlayPass`
   切换到粗线路径；`thickness=1` 也走同一路径（1px quad 与 1px 原生线视觉等价，避免双路径）。
5. 共享 shader 文件、新 VS entry（`GraphicsProgramDesc::vertex_entry` 已支持），PS 复用。
6. 闪烁修复：`DebugDrawService` 改 pending/published 双缓冲——draw_* 写 pending，
   logic tick 末尾 `commit_frame()` 原子交换发布，render 只 snapshot 已发布帧且不再
   清空；提交语义不变（每 tick 重新提交，线条存活到下一次 commit）。
7. 抖动修复：DebugDraw pass 内用 `frame.projection` 减去 `frame.taa_jitter_ndc` 重建
   无 jitter 的 view_projection（pass 在 TAA resolve 之后，不应带亚像素抖动）。

## Verification

对照 `docs/VERIFY.md`「渲染 Pass / shader / 材质」行：

1. `build_editor.bat Debug` + `build_sandbox.bat Debug`
2. `RunRenderGate.bat`：默认场景无 DebugDraw 提交方，pass 不生成，**golden 必须不变**
   （PASS 即证明门禁对无关渲染改动不误报）
3. `RunPerfGate.bat -Profile Standard`
4. 视觉验证：临时在 Sandbox 提交若干 `thickness ∈ {1, 4, 8}` 的
   line/box/circle/axes（临时代码不入库），`--dump-frame` 出图人眼/AI 确认粗细生效
   与透视正确，随后撤掉临时代码
5. 检查 `product/logs` 无 validation 报错（双后端各跑一次）

## Risk / rollback

- 风险：线段穿越近平面时屏幕空间展开可能出现拉伸伪影——调试可视化可接受，
  回写 spec 作为已知限制。
- 风险：root constants 复用 padding 若布局对不上，DX12/Vulkan 反射可能不一致——
  `attach_debug_draw_root_constants` 按反射布局裁剪拷贝，双后端 smoke 验证。
- 回滚：单一提交，`git revert` 即可；无数据/资产迁移。

## Status

Done（2026-07-05）

- 双后端构建 PASS；`RunRenderGate.bat` PASS（vulkan ssim 0.999997 / dx12 ssim 1.0 /
  跨后端 0.999842，golden 未变）；`RunPerfGate.bat -Profile Standard` PASS；日志无
  error / validation。
- 目视验证（DX12 + Vulkan，1920×1080）：thickness {1,4,10,3,6} 实测像素宽
  {1,4,10,3,6}±0px，双后端逐像素一致；线条无闪烁、无亚像素抖动（用户确认）。
- 结论已回写 `docs/specs/features/debug-draw.md`。
