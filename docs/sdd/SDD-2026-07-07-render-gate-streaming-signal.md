# Mini SDD: RenderGate 抓帧改为流送完成信号驱动

Status: Done（2026-07-07 实现并验证通过）

## Goal

消除 RenderGate 抓帧的时序脆弱性：当前 `--smoke-test=N` 按帧数猜测资产流送何时完成，
Vulkan 高帧率（实测 ~1000fps）时 5000 帧在 Sponza 流送（~7s）前就触发抓帧，抓到空场景
（2026-07-07 bless 运行实证，坏帧一度写入 golden）。改为：抓帧由"渲染资产流送完成 +
稳定余量帧"信号驱动，帧数上限退化为超时保底；抓完即退出，同时缩短门禁耗时。

## Non-goals

- 不做通用的资产流送进度 API / 事件系统（仅两个只读查询）。
- 不改变非 dump 模式的 smoke-test 语义（`--smoke-test=N` / `--smoke-test-seconds` 照旧）。
- 不改 RenderGate 的 SSIM 阈值与 golden 流程。

## Files

- `project/src/engine/Function/Render/RenderAssetManager.h/.cpp`：新增
  `has_requested_render_assets()` / `has_pending_render_assets()` 只读查询
  （`m_mutex` 改 mutable）。pending 定义：任一 static mesh 处于
  `Uninitialized/Loading/CpuReady`（未达 `GpuReady/Failed` 终态），或
  `m_pending_texture_decodes` 非空。
- `project/src/engine/Function/Application.h/.cpp`：dump 模式抓帧时机改造（见 Approach）。
- `project/src/engine/Base/EngineSelfTests.cpp`：源码级契约测试。
- `scripts/RunRenderGate.ps1`：注释同步（默认 `-SmokeFrames 5000` 保留，语义变为超时保底）。
- 文档：`docs/VERIFY.md`（缺口表 + RenderGate 段）、`docs/specs/modules/tools.md`、
  `docs/specs/modules/application.md`。

## Approach

`Application` 直接持有 `renderAssetManager`（`Application.h:199`），无需新链路。
渲染循环内，仅当 `--dump-frame` 激活且未写盘时：

1. 每个渲染帧查询 `has_requested_render_assets() && !has_pending_render_assets()`；
   成立则 quiesce 计数 +1，否则清零（级联请求 mesh→material→texture 会重置计数）。
   `has_requested` 前置条件挡住"场景 JSON 尚在解析、还没发出任何请求"的窗口期
   （实证：该窗口在 1000fps 下可达 2000 帧，`load_state=Ready` 信号不覆盖 GPU 流送）。
2. quiesce 计数达到余量 `k_frame_dump_quiesce_frames = 32` 时触发抓帧
   （余量覆盖 RenderScene proxy 重建与最后一批 finalize 后的首帧绘制）。
3. 写盘成功后立即 `request_exit()`——门禁耗时从"固定 N 帧"变为"流送完成 + ~32 帧"。
4. 帧数上限保留为超时保底：到达上限仍未 quiesce 则按原行为抓帧并告警
   （RenderGate 会以 SSIM FAIL 的方式暴露问题，而非静默）。

空场景（无任何渲染资产请求）不会 quiesce，走超时保底，行为与现状一致。
查询走 `RenderAssetManager` 现有 mutex，每帧一次且仅 dump 模式启用，非门禁路径零开销。

## Verification

对照 `docs/VERIFY.md`（Scene/Asset/Application 生命周期 + 渲染工具链）：

- `build_sandbox.bat Debug` + `build_editor.bat Debug`（含 EngineSelfTests 新契约测试）
- `RunRenderGate.bat`：PASS，且日志确认抓帧由 quiesce 信号触发、双后端耗时显著下降
- `run.bat all Debug --smoke-test-seconds=5`：全矩阵 smoke，确认非 dump 路径行为不变
- PerfGate 不跑：新查询仅在 dump 模式执行，正常帧循环无新增开销

### 执行结果（2026-07-07）

- `build_sandbox.bat Debug` / `build_editor.bat Debug`：PASS；`--engine-self-test` PASS（含新契约测试）
- `RunRenderGate.bat`：**PASS**（vulkan ssim 0.999919、dx12 0.999916、cross 0.999442）；
  日志确认双后端均由 quiesce 信号触发抓帧——vulkan 第 4075 帧、dx12 第 1620 帧
  （均远未到 5000 帧超时上限），写盘后立即退出，无超时告警
- `run.bat all Debug --smoke-test-seconds=5`：4 组（Editor/Sandbox x DX12/Vulkan）全部
  clean_exit，Sandbox summary 全 passed，VMA 无泄漏——非 dump 路径行为不变

## Risk / rollback

- 风险：quiesce 信号早于画面真正稳定（如 material shader 首用编译晚于 GpuReady）——
  32 帧余量 + 计数遇 pending 重置缓解；若仍出现，RenderGate SSIM 会损失余量并可查
  heatmap 定位，余量帧数是单点常量可调。
- 回滚：revert 本提交即恢复纯帧数驱动抓帧。
