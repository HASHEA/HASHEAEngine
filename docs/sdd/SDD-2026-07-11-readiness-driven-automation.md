# SDD-2026-07-11-readiness-driven-automation: readiness 驱动的 smoke 与 RenderGate

## Status

Done（2026-07-12：Sandbox/Editor readiness、事务式 golden 发布、人工检查与最终门禁均完成）

## Context

现有自动化有两套互相割裂的结束语义：普通 smoke 按固定帧数或固定秒数退出；
`--dump-frame` 则等待最近场景提交且渲染资产连续 32 帧无 pending 后抓帧。
固定帧数曾直接导致错误验证：GPU 粒子候选图被手工命令 `--smoke-test=180`
提前截取，引擎已明确告警尚未 quiesce。当前超时路径仍会截取不完整画面并以 0 退出，
`-BlessGolden` 因而存在把不完整图写成基线的风险。

本变更定级 **S2**：跨 Function/Application、Graphics/RHI、Editor、Sandbox 与验证脚本，且改变自动化
退出码、命令行契约与 swapchain acquire/present 结果传播契约。

## Goals

- Sandbox 与 Editor smoke 都以“应用就绪、受跟踪资源无 pending/failed、最新资源代次已完整提交，且 present 调用无致命错误地完成”作为成功条件；物理窗口可被遮挡。
- RenderGate 的同一个 Sandbox 进程同时完成 runtime smoke、就绪帧抓取和正常退出，脚本随后做 SSIM。
- 成功路径不依赖固定帧数；超时只用 wall-clock 上限表达卡死/加载过慢，超时必须非零退出且不得产出可 bless 的图片。
- 用显式资源活动 epoch + 场景提交快照代替 32 帧稳定余量，避免帧率改变语义。
- 固定帧/固定时长运行只保留为显式 soak、性能 watchdog 或未来泄漏测试能力，不再冒充 smoke 成功判据。

## Non-goals

- 不在本次实现内增加内存泄漏/长时 soak 门禁。
- 不给 Editor 增加 golden 截图；Editor 只接入 readiness smoke。
- 不改变 RenderGate 的 SSIM 算法、阈值或 golden 人工确认规则。
- 不重构资产管线；只提供自动化所需的 O(1) readiness snapshot/epoch。
- 不重构 Vulkan/DX12 swapchain 或 RenderGraph；后端仅增加等价的三态 acquire/present 结果传播与无 image 帧的同步闭环。

## Current implementation

- Entry points：`EntryPoint.h` 把 `--smoke-test=N` 写入 `maxFrameCount`，把
  `--smoke-test-seconds=S` 写入 `maxRunSeconds`；`Application::start()` 返回 `void`，
  初始化成功后 EntryPoint 无条件返回 0。
- Application：普通 smoke 到上限即 `request_exit()`；dump 模式在
  `has_recent_scene_submission && !has_pending_render_assets` 连续成立 32 帧后抓帧，
  到帧上限则仍抓不完整画面。
- Render assets：`RenderAssetManager` 只有 `has_pending_render_assets()` 布尔查询，
  没有能证明“本次成功提交发生在最后一次流送活动之后”的 epoch。
- Scene presentation：只记录任一 scene submit 的最近成功帧；不能证明同帧所有 scene packet
  都成功，也不能绑定资源活动 epoch。
- Tools：`RunRenderGate.ps1` 传 `--smoke-test=20000`；`RunPerfGate.ps1` 借用
  `--smoke-test-seconds` 作为采样 watchdog。

## Proposal

### Chosen approach: condition + epoch handshake

引入一个可单测的 `ApplicationAutomationController` 纯状态机。该类型是 AGENTS.md
“单调用点抽象”规则的显式 SDD 例外：生产侧由 Application 持有，但同时承载两个真实协议
（readiness smoke、readiness capture），并需要脱离窗口/RHI 做失败先行的状态机测试。

每个新的 render-visible cache miss，以及受跟踪请求进入终态时，使 `RenderAssetManager` 的 activity epoch 单调递增；cache hit 不推进。
`ScenePresentationSubsystem` 在每个 Application frame 记录 scene packet 的 attempted / succeeded /
failed 数量，以及提交结束时看到的资源 epoch。一个 presented frame 仅在以下条件同时满足时 ready：

1. 派生应用 readiness 为 Ready；
2. 当前没有受跟踪的 pending/failed render assets，也没有待消费 render commands；
3. 当前 Application frame 至少尝试一个有效 scene packet，且全部成功；
4. scene submission snapshot 的资源 epoch 等于 RenderAssetManager 当前 epoch；
5. 该 frame 的 present 调用已无致命错误地完成；后台运行时窗口物理可见性不是门禁条件。

普通 smoke 到此即 ready。frame dump 还要求动态内容的 capture-ready 计数覆盖全部 scene packet；
当前 GPU 粒子以 `min(capacity, ceil(spawn_rate × (lifetime + variance)))` 的累计成功模拟
spawn 数发出稳定窗口信号。它由组件语义推导，不是全局/场景固定帧数。

普通 smoke 在第一个 ready frame 后成功退出。dump 模式采用两阶段握手：第一个 ready frame
清空 AO/TAA/体积光中可能混有 fallback 资源画面的 temporal history，并 arm 当前 epoch；下一帧开始前
若 epoch 未变且仍无 pending，则请求 capture；present 后再次验证同一 ready 条件，成立才写 PNG 并
成功退出。若 capture 帧中出现新请求或提交失败，则丢弃该次 capture 并重新等待，不使用固定
“余量帧”。history invalidation 让下一帧直接以最终资源重建当前结果，替代过去偶然承担收敛作用的
32 帧等待。

### Rejected alternatives

1. **把现有 32 帧计数复用于 smoke**：改动小，但仍以帧率相关常量猜测稳定性，不能解决用户指出的根因。
2. **脚本解析日志判断 ready**：无法给 Editor/直接运行提供可靠退出码，且 `-BlessGolden` 仍可能消费错误图片。
3. **只检查 `has_pending_render_assets()==false`**：存在场景尚未提交、最后一批资源完成后尚未画入 backbuffer 的竞态。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Function/Application | 新增自动化状态机、派生 readiness hook、wall-clock timeout、成功/失败结果 | `ApplicationAutomation.*`、`Application.*`、`EntryPoint.h` |
| Graphics/RHI | acquire 与 present 均返回 `Completed / Retryable / Failed`，双后端等价传播到 Application；Vulkan 无 image 帧不等待 acquire binary semaphore、不 signal present binary semaphore，但仍推进 timeline/fence | `Swapchain.*`、`GraphicsContext.*`、`VulkanContext.*`、`DX12Context.*`、`VulkanSwapchain.*`、`DX12Swapchain.*`、`RenderDevice.*`、`Renderer.*` |
| Function/Render assets | 暴露 activity epoch + pending/failure snapshot；请求与终态变化推进 epoch | `RenderAssetManager.*` |
| Function/Scene presentation | 记录完整 scene submit snapshot、绑定资源 epoch，并在 ready capture arm 时使 AO/TAA/体积光 history 失效 | `ScenePresentationSubsystem.*`、`SceneRenderer.*`、`AmbientOcclusionPass.*` |
| Sandbox | readiness hook 绑定 startup/logic/render、scene Ready 与 presentation registration | `SandboxApplication.*` |
| Editor | readiness hook 绑定 bootstrap 成功；最终成功仍由公共 scene submit + asset 条件约束 | `Editor.*` |
| Tools | RenderGate 改用秒级失败上限并以 backup/rollback 事务发布 golden；PerfGate 改用显式 run watchdog | `RunRenderGate.bat`、`scripts/RunRenderGate.ps1`、`RenderGateGoldenPublisher.ps1`、`scripts/RunPerfGate.ps1`、`run*.bat` |
| Tests/docs | 状态机/解析契约测试；同步长期 spec、VERIFY、CODEBASE_MAP、旧 SDD superseded 标记 | `project/src/tests/`、`EngineSelfTests.cpp`、`docs/` |

### API / contract changes

- `--smoke-test-seconds=S`：语义改为“等待 readiness，S 仅为失败超时”；ready 后可提前成功退出。
- `--dump-frame=<png>`：隐式走同一 readiness smoke；仅 ready capture 能写盘。
- `--run-for-seconds=S` / `--run-for-frames=N`：显式固定运行 watchdog/soak，永不代表 readiness 成功。
- 旧 `--smoke-test=N` 暂保留为 `--run-for-frames=N` 的 deprecated alias，并记录告警；仓库管线与文档不再使用。
- `Application::start()` 返回运行成功/失败；EntryPoint 对 readiness timeout、应用失败、capture 失败返回非零。
- swapchain acquire/present 复用同一三态：`Completed` 才进入对应的录制或 readiness completion；尚无可呈现 image / 交换链过期为 `Retryable` 并继续等待；设备/后端致命错误为 `Failed` 并立即非零退出。acquire Retryable 帧不录制、不 present，也不消费 capture 请求；DXGI 成功状态（包括 OCCLUDED）属于 Completed，使后台 golden 不依赖窗口遮挡关系。
- frame-dump 的 wall-clock 硬上限覆盖进程初始化、GPU readback、PNG 编码、最终发布与 teardown；GPU readback 用双后端 current-frame completion 的有限等待消费剩余 deadline，禁止调用无上限的 device idle。EntryPoint 的 readiness-only 进程 watchdog 覆盖 driver hang 与无界 graceful teardown：deadline 到达后以失败码直接终止，不继续做不安全的 GPU 析构；跨过 deadline 的临时/最终文件不能成为 bless 输入。
- RenderGate 参数 `-SmokeFrames` 改为 `-TimeoutSeconds`；报告记录 wall-clock timeout 与 runtime smoke 结果。
- `-BlessGolden` 只有在进程以 readiness success 退出且 PNG 存在时才能复制基线。
- 普通回归持仓库级共享锁读取稳定 golden 矩阵并拒绝未解决事务产物；所选 bless 矩阵全部通过后，在同一锁的独占句柄内 stage、备份并整批发布。完整回滚标 `NOT_BLESSED`，恢复失败保留可用 backup 并标 `ROLLBACK_FAILED`，提交后清理失败标 `BLESSED_CLEANUP_FAILED` 且门禁失败。故障注入覆盖混合新旧目标、读写并发锁、崩溃遗留、回滚失败与清理失败。

### Backend impact

`Swapchain::begin_frame/present` 改为三态结果。Vulkan acquire 的 `VK_ERROR_OUT_OF_DATE_KHR` 为 Retryable，
`VK_SUCCESS` / `VK_SUBOPTIMAL_KHR` 为 Completed，其余错误为 Failed；无 acquired image 时 frame lifecycle 仍完成 timeline/fence，但不提交 swapchain binary wait/signal。DX12 acquire 为 Completed，present 的全部 `SUCCEEDED(hr)` 状态为 Completed（物理遮挡不影响 backbuffer capture），失败 HRESULT 为 Failed。最终必须分别验证 Sandbox/Editor readiness smoke，
并跑 RenderGate 跨后端 diff与双后端 validation。

### Performance

正常交互运行除资源 epoch 的 O(1) 更新外，ScenePresentation 每帧会读取一次 O(1) readiness snapshot 并写入提交 epoch。
smoke 每帧约两次 snapshot（scene submission + post-present），frame dump 的 capture 帧最多三次（pre-frame、submission、post-present）；snapshot 是一次短互斥下的 O(1) 计数快照。
PerfGate 不进入 readiness smoke，改用显式 `--run-for-seconds` watchdog，因此采样窗口不会提前退出；性能结果仍按现有绝对上限/基线状态独立判断。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| RED/GREEN 单测 | ready、epoch 失效、提交失败、capture 重试、timeout | `RunTests.bat Debug`（先见预期 FAIL，再实现至 PASS） |
| 构建 | Engine + 两个派生应用 | `build_editor.bat Debug`、`build_sandbox.bat Debug` |
| 架构 | Function/Editor/Sandbox 边界 | `RunArchGate.bat` |
| 正向 smoke | Editor/Sandbox × Vulkan/DX12 ready 后 0 退出 | `run.bat all Debug --smoke-test-seconds=120` |
| 负向 smoke | 极短 timeout 非零；无 dump PNG | 直接运行 Sandbox `--smoke-test-seconds=0.01 --dump-frame=<temp>` |
| RenderGate | 一次进程同时证明 smoke + ready capture + SSIM | `RunRenderGate.bat -TimeoutSeconds 120` |
| PerfGate | watchdog 迁移且无提前退出/水位回归 | `RunPerfGate.bat -Profile Standard` |
| validation | 粒子场景双后端无 validation/debug-layer 错误 | 两后端各跑 readiness smoke（Engine.ini 临时开 validation） |
| 工具契约 | 脚本参数/报告字段 | `scripts/TestRunPerfGate.ps1` + RenderGate 实跑 |

## Task breakdown

- [x] **T1 RED：状态机契约**——新增纯逻辑测试，覆盖 ready smoke、两阶段 capture、epoch 改变后重试、失败与 timeout；确认因类型/API 不存在而失败。
- [x] **T2 GREEN：自动化状态机**——新增 `ApplicationAutomation.*` 最小实现，使 T1 通过，不接主循环。
- [x] **T3 RED/GREEN：资源与提交 snapshot**——先加 epoch/scene-submit 契约测试，再实现 RenderAssetManager 与 ScenePresentationSubsystem snapshot。
- [x] **T4 RED/GREEN：Application 与退出码**——先扩源码/解析契约测试，再接主循环、capture 接受/丢弃、timeout 非零及固定运行 watchdog。
- [x] **T5 派生应用**——Sandbox/Editor 实现 readiness hook；构建并跑 Vulkan 正向 smoke，完整四组合列入最终门禁。
- [x] **T6 工具迁移**——RenderGate 改 `-TimeoutSeconds`，PerfGate 改 `--run-for-seconds`；负向 timeout 已确认非零且删除旧 PNG。
- [x] **T7 粒子自动化收尾**——合并本次契约到 particles/application/render/tools spec，完成粒子测试、构建、AlphaBlend 双后端覆盖、双后端 validation、PerfGate 与候选图。
- [x] **T8 人工检查与最终门禁**——Editor 手工完成加组件→调参→保存→重载并切 AlphaBlend 观察深度遮挡；用户确认粒子候选画面后仅 bless `particles`，默认 RenderGate 全矩阵 PASS。

## Verification evidence（2026-07-12）

- `RunTests.bat Debug`：36/36 cases、425/425 assertions PASS；包含 capture arm/history invalidation、epoch 失效、硬 deadline、CLI 负数（含前导空格负帧数）/初始化前拒绝、present completion 门槛、粒子语义 warmup 与完整 64-bit entity seed。
- 提交前只读终审补出 Vulkan acquire P1：连续 out-of-date/no-image 原先会在 present-copy 前误判 fatal，并让 Vulkan submit 等待未 signal 的 acquire semaphore。修复后 VkResult 纯逻辑测试覆盖 SUCCESS/SUBOPTIMAL→Completed、OUT_OF_DATE→Retryable、DEVICE_LOST→Failed；源码全链契约覆盖 acquire→RenderDevice→Renderer→Application 与无 image binary semaphore 禁用。RED 为缺少分类/API 的预期编译失败，GREEN 后完整测试通过。
- 同轮终审补出 frame-dump readback/teardown P1：`fetch_back_buffer_capture` 与后续析构原先可进入无上限 `wait_idle`，绕过 wall-clock deadline。新增 `GraphicsContext::wait_for_frame_completion(timeout_nanoseconds)`，Vulkan 以 timeline semaphore/current frame fence、DX12 以 current frame fence 等价实现；Application 只传剩余 deadline，DX12 device-removed 的 `UINT64_MAX` 明确失败。EntryPoint 的进程 watchdog 覆盖 initialization→run→destroy 整段，deadline 后以失败码快速终止。源码契约先后以 6 条和 5 条缺失断言 RED，最终 targeted GREEN 为 1/1 case、17/17 assertions。
- `build_editor.bat Debug`、`build_sandbox.bat Debug`：PASS；`RunArchGate.bat` PASS（35 条既有 legacy warning，未新增越界）。
- `run.bat all Debug --smoke-test-seconds=120`：Editor/Sandbox × Vulkan/DX12 四组合 readiness PASS，总耗时 22.8 秒；报告 `Intermediate/test-reports/readiness/20260711-211508-023/`。
- 极短 timeout：Sandbox 非零退出且目标 PNG 不存在；自带退出码的负向报告 `Intermediate/test-reports/readiness-negative/20260711-212147-564/`。RenderGate 1ms 脚本超时 probe 的持久化摘要 `Intermediate/test-reports/render-gate-process-probe/20260711-214956-086/summary.json` 记录 child exit=1、耗时 0.744 秒、`script_timed_out=true`、`termination_failed=false`、`output_drain_failed=false`、无 capture PNG，且 probe 前后没有新增 Sandbox PID；对应门禁报告为 `Intermediate/test-reports/render-gate/20260711-214956-387-50292-814329d9/`。`scripts/TestRunPerfGate.ps1` 与 `scripts/TestRenderGateGoldenPublisher.ps1` PASS；后者覆盖混合新旧目标成功发布/整批回滚、共享读锁与独占发布互斥、崩溃遗留拒绝、恢复失败保留 backup 及提交后清理失败状态。AIDevDoctor 仅成功生成计划，不能替代其中列出的 fresh validation evidence。
- Sandbox temporal A/B：直接 ready capture 相对旧 golden 为 0.959；ready 时语义化清空 AO/TAA/体积光 history 后恢复为 Vulkan 0.996278、DX12 0.996177，cross 0.999747；两次 Vulkan candidate exact（max diff 0）。最新报告：`Intermediate/test-reports/render-gate/20260711-215137-133-8996-d429fa3a/`。
- 粒子 candidate：同后端重复抓帧 Vulkan/DX12 各自 exact；跨后端 SSIM 1.0、max diff 1、1 pixel。用户确认后，`RunRenderGate.bat -Scenes particles -BlessGolden` 事务发布 PASS（报告 `Intermediate/test-reports/render-gate/20260712-155108-228-48200-fb1691a9/`）。
- Editor 人工链路：隔离场景完成 Inspector 添加 Particle、设置 `spawn_rate=200`/`AlphaBlend`、保存、关闭和重载；组件和值均持久化。发射器置于头盔后方时模型覆盖区域正确遮挡粒子，Console 为 Warn 0 / Error 0；证据位于 `Intermediate/test-temp/editor-particle-validation/`。
- 最终门禁：默认 `RunRenderGate.bat` 的 Sandbox/Particles × Vulkan/DX12 golden 与两组 cross-backend diff 全部 PASS；粒子两后端 golden 和 cross SSIM 均为 1.0（报告 `Intermediate/test-reports/render-gate/20260712-155130-523-57884-6a47628a/`）。
- Vulkan GPU-assisted/synchronization validation 与 DX12 GPU validation：粒子 stable capture 均正常退出，无 API/lifetime/leak 错误；报告 `Intermediate/test-reports/validation/20260711-211129-009/`。AlphaBlend 另以已删除的临时场景做过双后端专项，`Intermediate/test-reports/particles-alpha-{vulkan,dx12}.png` 的 SHA-256 相同且 AshImageDiff 复核 exact；不把 validation 目录误作该专项证据。
- `RunPerfGate.bat -Profile Standard`：Sandbox/Editor × Vulkan/DX12 全部 PASS，无 failure/warning；四项 `baseline_status=MISSING`，因此本次只证明运行成功和绝对上限，未证明相对基线无回归。fresh 报告 `Intermediate/test-reports/perf-gate/20260712-155843/`。

## Risks

| Risk | Mitigation |
| --- | --- |
| 资源完成与 capture 之间出现级联请求 | activity epoch + capture 前后双重 ready 验证；变化即丢弃并重试 |
| 某 scene view 失败但另一 view 成功导致误 PASS | snapshot 要求同帧所有有效 scene packet 全成功 |
| Editor 无有效 scene/viewport 时永远 Pending | wall-clock timeout 非零退出并记录缺少的 readiness 条件 |
| 受跟踪的 mesh/texture/runtime resource Failed 被当作“无 pending” | readiness snapshot 区分 Pending / Failed；受跟踪失败立即失败。材质/IBL 原资产失败但 fallback 成功属于合法降级，不误报 Failed |
| swapchain 暂时不可处理与致命错误混淆 | acquire/present 三态：Retryable 等下一帧，Failed 立即终止，只有 Completed 才录制或满足 readiness；Vulkan 无 image 帧仅推进 timeline/fence，DXGI OCCLUDED 作为非致命完成以支持后台验证 |
| PerfGate 被新 smoke 语义提前终止 | PerfGate 只用 `--run-for-seconds` watchdog，采样控制器仍负责正常退出 |
| 旧脚本依赖 `--smoke-test=N` | 保留 deprecated alias 与告警；仓库内调用一次性迁移 |
| 用户关闭窗口被误判 smoke 成功 | 自动化未到 Ready 时的外部退出返回失败；非自动化运行保持正常关闭语义 |

## Open questions

- 无；Sandbox + Editor 覆盖、无固定帧成功判据、同进程 smoke + golden 已由用户明确批准。
