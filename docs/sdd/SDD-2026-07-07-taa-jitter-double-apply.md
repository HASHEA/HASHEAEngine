# Mini SDD: TAA jitter 重复施加修复（DX12 交互态整画面抖动的真根因）

级别：S1（单模块 bugfix，`Function/Render`，SceneRenderer 单点改动）

## Goal

修复 DX12 交互态 TAA 整画面抖动（Vulkan 无此现象）。RenderDoc 抓帧取证
（taa_dx12_frame6091.rdc）确认根因链条：

1. `ScenePresentationSubsystem` prepare（逻辑线程 `update_presentations`）与
   submit（渲染线程 `submit_presentations`）通过 `m_impl->prepared_packets`
   共享同一 `shared_ptr<VisibleRenderFrame>`，两阶段节奏不同步（Sandbox 逻辑
   线程带 1ms sleep 自跑，渲染线程 VSync=false 自由跑）；渲染 fps 超过逻辑
   fps 时**同一 frame 对象被重复渲染**。
2. `SceneRenderer::render_visible_frame` 施加 TAA jitter 时对共享 frame
   **原地累加**（`SceneRenderer.cpp:955-957`：`frame.projection[2][0] +=
   jitter`），重复渲染 = jitter 施加 2×。
3. 证据闭环：被重复渲染帧以 2× 振幅呈现（肉眼抖动本体）；
   `commit_temporal_view_state` 把 2× jitter 的 view_projection 存为 prev
   state，下一帧 GBufferD 运动向量实测恒为 `jitter_uv(N) − 2×jitter_uv(N−1)`
   = (0.00039, 0.00067)，与 Halton 序列 idx2→idx3 精确吻合；而
   `state.jitter_ndc` 存原始 1× 值，故 resolve 的补偿常量（Config1）数值
   正确却欠补偿。
4. DX12-only 是帧率比值表象（DX12 渲染 fps > 逻辑 tick 率，Vulkan 未超），
   非后端语义差异。此前 SDD-2026-07-07-dx12-mailbox-present-tearing 的 MAILBOX tearing 修正本身成立（present
   语义确实错了），但只是让抖动更可见的放大器，不是根因；ring 3→6 与
   k_dx12_max_frames=1 实验已排除资源竞争类假说。

## Non-goals

- 不改 prepare/submit 节奏本身：逻辑未 tick 时渲染线程重渲上一帧属设计内
  （UI/拾取仍需每帧出图）。
- 不改 TAA resolve 数学、jitter 序列、运动向量生成（取证已排除嫌疑）。
- 不动 Graphics 层 / RenderGraph / swapchain。

## Files

- `project/src/engine/Function/Render/SceneRenderer.cpp`：`render_visible_frame`
  jitter 施加段（929-958 行附近）改为幂等——进入时若上次调用已施加 jitter
  （`frame.taa_enabled` 为 true）先撤销（减去 `frame.taa_jitter_ndc` 并重建
  view_projection），再走原有重置/施加逻辑。
- `docs/specs/features/taa.md`：已知限制第 36 行（错误宣称 SDD-2026-07-07-dx12-mailbox-present-tearing 已解决
  抖动）改写为真根因结论；同步验证章节。
- `docs/specs/modules/graphics.md`：present mode 约束段中"tearing 导致抖动"
  措辞修正为"放大器而非根因"。
- `docs/sdd/SDD-2026-07-07-dx12-mailbox-present-tearing.md`：结论段补记后续取证
  推翻其根因判断，指向本 SDD。

不动：ScenePresentationSubsystem、TemporalAAPass、shader、Graphics 层。

## Approach

幂等化：`render_visible_frame` 对 frame 投影的修改必须满足"重复调用结果
不变"。进入 jitter 段时：

```cpp
if (frame.taa_enabled)
{
    // 同一 VisibleRenderFrame 可能被重复渲染（prepare/submit 节奏不同步）：
    // 先撤销上次施加的 jitter，防止投影矩阵跨调用累加。
    frame.projection[2][0] -= frame.taa_jitter_ndc.x;
    frame.projection[2][1] -= frame.taa_jitter_ndc.y;
    frame.view_projection = frame.projection * frame.view;
}
// 原有逻辑：重置 taa 字段 → (enabled 时) 计算并施加 jitter
```

- 同一 frame_index 的 jitter 值相同，撤销+重加后渲染结果与首次逐位近同
  （浮点 undo/redo 误差 ≤1 ulp 且不随次数累加，远小于 1e-4 量级的 jitter）。
- 重渲帧的 temporal 语义自洽：prev state 已被首次渲染覆盖为本帧自身 →
  prev VP = curr VP、prev jitter = curr jitter → 运动向量与 jitter 补偿均为
  0，与"屏幕内容未动"的事实一致；TAA 对相同内容继续收敛，无害。
- 兼顾 TAA 运行中被关闭的边界：重渲帧上 taa 由开转关时撤销分支仍会还原
  投影，不残留旧 jitter。

## Verification

对照 `docs/VERIFY.md`「渲染 Pass」行：

1. 临时日志实证（不入库）：撤销分支内加计数日志，DX12 交互态应高频触发、
   Vulkan 应基本为 0，坐实帧率比值解释。
2. `build_editor.bat Debug` + `build_sandbox.bat Debug`。
3. 视觉验证（关键判据）：Sandbox DX12 + TAA + VSync=false 交互态，用户确认
   抖动消失；Vulkan 同配置确认无回归。
4. `RunRenderGate.bat`（抓帧模式 jitter 强制为 0，golden 应不变，PASS 即证明
   无关路径未受影响）+ `RunPerfGate.bat -Profile Standard`。
5. `product/logs` 双后端无 validation 报错。

## Risk / rollback

- 风险：撤销分支误在"全新 frame"上触发——不可能，`build_visible_render_frame`
  每次 prepare 新建 frame，`taa_enabled` 初始为 false，仅重复渲染路径为 true。
- 回滚：单一提交 `git revert`；无数据迁移。

## Status

Done（2026-07-06）

- 临时日志实证：DX12 8 秒同帧复用 250+ 次（约 30+/秒），Vulkan 同条件 0 次——
  坐实"仅 DX12 抖"为帧率比值表象；日志已按计划撤除。
- 双后端构建 PASS；`RunRenderGate.bat` PASS（vulkan ssim 0.999996 / dx12
  0.999997 / 跨后端 0.999843，golden 未变）；`RunPerfGate.bat -Profile
  Standard` PASS；日志无 validation 报错。
- 目视验证（用户确认）：DX12 + TAA + VSync=false 交互态抖动消失，Vulkan 无回归。
- 结论已回写 `docs/specs/features/taa.md`、`docs/specs/modules/graphics.md`，
  并修正 `SDD-2026-07-07-dx12-mailbox-present-tearing` 结论段。
