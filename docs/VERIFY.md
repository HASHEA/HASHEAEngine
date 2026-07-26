---
owner: huyizhou
last_reviewed: 2026-07-20
review_cycle: monthly
status: active
---

# Verification

按变更类型给出必须执行的验证。原则：**没有对应验证证据的改动不算完成**。
视觉正确性与双后端一致性由 RenderGate 自动回归（`RunRenderGate.bat`，SDD-2026-07-07-render-gate）；RenderDoc 自动取证仍是缺口，见文末"待自动化"。

执行边界：凡需真实鼠标、键盘、拖放、文件对话框或窗口交互的 UI 验证只由人类执行。Agent 不得用坐标点击、输入注入、桌面自动化或无障碍脚本代替人工验收；Agent 负责自动化命令、只读截图/日志诊断，并在交付时输出“待人工验收”清单。只有仓库内提供可重复且有明确退出码的 UI 自动化测试时，Agent 才可把它作为自动化证据运行。

## Fast path

```bat
build_editor.bat Debug        :: 或 build_sandbox.bat Debug；缺 sln 自动生成
RunTests.bat                  :: doctest 单元测试（构建 Tests.exe 并运行；含 legacy 自测桥接）
RunArchGate.bat               :: 架构边界检查（include 扫描依赖方向红线，秒级，改引擎/编辑器代码必跑）
run.bat sandbox vulkan Debug --smoke-test-seconds=120
RunRenderGate.bat             :: 渲染改动必跑：双后端 golden SSIM 回归 + 跨后端 diff
```

## Change matrix

| Change | Commands | 人工检查（仅人类执行） |
| --- | --- | --- |
| 纯文档 / 注释 | `git diff --check` | — |
| Base 层纯逻辑（容器/字符串/序列化/内存等） | `RunTests.bat`（doctest 单测 + legacy 自测桥接） | 新逻辑应补对应 `project/src/tests/` 用例 |
| 渲染 Pass / shader / 材质 | 构建 + `RunRenderGate.bat`（双后端 golden SSIM 回归 + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard` | 检查 `product/logs` 无 validation 报错；预期内的画面变化经用户确认后 `-BlessGolden` 更新基线 |
| RHI 接口 / 双后端实现 | 构建 + `RunRenderGate.bat` + PerfGate Standard；Engine.ini 开启 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 smoke | 跨后端 diff FAIL 视同 bug |
| RenderGraph 核心（compile/barrier/lifetime） | 同 RHI 级别 + 双后端 `run.bat sandbox <backend> Debug --rhi-selftest-indirect --run-for-frames=1` + `RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release` | raw RHI 与 Function RenderGraph 两段都须 PASS；关注 buffer barrier/lifetime/binding validation，profile 必须 COMPARED 且 coverage 合同满足 |
| Scene / Asset / Application 生命周期 | 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke；通常 ready 后数秒即提前退出） | 人类在 Editor 打开默认场景并操作一遍；Agent 只移交清单 |
| Terrain Asset / CPU logic | `RunTests.bat Debug` + `RunTests.bat Release` + `RunArchGate.bat`；依赖或工程生成变化时 fresh `generate_vs2022.bat` 并构建 Editor/Sandbox Debug+Release；最后 `run.bat all Debug --smoke-test-seconds=120` | 检查 `.AshTerrain` recovery、RAW/PNG/EXR、256×8192 / 2048×4096 / 2048×2048 / 8192×8192 target matrix、显式 8193² full-pressure fixture、不可变 snapshot、candidate rollback、dirty publication 与 local query focused tests；涉及 rendered frame 时同时按「渲染 Pass / shader / 材质」行执行 |
| Editor 面板 / UI | 构建 + `run.bat editor Debug --smoke-test-seconds=120`（readiness 后自动关闭）+ 相关自动化测试 | 人类运行 `run.bat editor`，检查面板打开、交互和日志；Agent 禁止直接驱动 UI，只移交清单 |
| `product/config/Engine.ini` | 双后端各 smoke 一次 + 查日志 | 确认开关生效；配置项语义/默认值变化同步 `docs/CONFIG.md` |
| 性能敏感路径 | PerfGate Standard，`FAIL` 必须修，`WARN` 需说明判断 | 对比 `summary.md` 趋势 |
| GPU timing telemetry / 固定性能 profile | `RunTests.bat Debug` + `scripts/TestRunPerfGate.ps1` + Editor Debug、Sandbox Debug/Release 构建 + 双后端 Debug validation 短 profile smoke + `RunRenderGate.bat` + PerfGate Standard + `RunPerfGate.bat -Profile VegetationFullPipeline` + 同 profile `-TelemetryMode Off -SkipBuild` A/B | 审计 adapter/driver、actual extent、fixed camera、总/逐 metric coverage、invalid/unresolved、每 run 精确日志；正式 candidate validation off，不 bless |
| `scripts/` / `tools/` | `scripts/TestAIDevDoctor.ps1`、`scripts/TestRunPerfGate.ps1`、`scripts/TestCheckArchBoundary.ps1`（按所改工具） | — |
| `premake5.lua` / 构建链 | 删 sln 后全新 `generate_vs2022.bat` + 构建；确认 PostBuild artifact 同步成功 | `product/bin64` 下 DXC/validation dll 是新的 |
| `project/src/tests/` / 测试基建 | `RunTests.bat`（退出码 0 = 全绿）；改 premake 部分按上一行执行 | 断言失败退出码非 0；可用 doctest `--test-case=` 过滤 |

不确定改动属于哪类时，运行 `scripts/AIDevDoctor.ps1 -Mode ValidatePlan`，它会根据 dirty paths 生成验证计划。

### Terrain Editor manual checklist

Phase 3 收口必须由一名具名的人类测试者在 Editor 主 Scene viewport 亲自逐项操作并签署结果。AI agent、鼠标/UI 自动化、readiness smoke、源码字符串测试和单元测试都不能执行或代签本清单；自动化只能准备 fixture、采集日志和提供辅助诊断。未取得人工签署记录时，本清单一律视为未通过，Phase 3 不得标记完成，也不得进入 Phase 4。

- Terrain Mode 新打开时 Target Size 显示 2048 × 2048 m；分别输入 `300`、`384`、`3500`，确认 Enter 与失焦时规范化为 `256`、`512`、`4096`（中点向上），Samples / Components / 1 m/sample 标签同步更新。
- 输入非法文本，确认原文本保持可见且 Create/Import 都 fail closed；设置不同 X/Z，确认 Create Flat 与 Import 共用同一 target，而 Import source width/height 保持独立。
- 创建并保存矩形 Terrain，关闭重开后在 Vulkan/DX12 画面正确；同路径 reload/reimport 到不同布局时，失败必须保留旧画面并显示路径/阶段/layout/原因/retention 诊断，成功才原子切换最终画面。
- 分别从 PNG、RAW R16、RAW R32F、EXR 导入高度图；导出四种格式并重新导入，核对尺寸、范围和方向。
- Manage / Sculpt / Paint / Layers 全路径可用；Raise、Lower、Smooth、Flatten、Noise、Paint、Erase 各执行一次，并逐项 Undo/Redo。
- 图层新增、复制、删除、重命名、排序、隐藏、强度与锁定保持稳定 ID；锁定层拒绝笔刷。
- Scene viewport 拾取、笔刷 world-space overlay、非均匀正缩放下圆形半径与角色贴地查询正确；相机/gizmo/selection 输入仲裁无串扰。
- Save、Save Copy As、Optimize、外部改盘 Conflict（Reload / Keep Local / Save Copy As）和损坏 descriptor 的 RecoveredReadOnly / Repair 全部走通；任何失败都保留本地 dirty bytes 与历史。
- New/Open/Reload Scene 在 Terrain dirty、load、composition、save 或 conflict 未解决时 fail closed；成功切换后 Terrain session、Selection、UndoRedo 按规定顺序清理。
- Vulkan/DX12 日志无 validation/debug-layer/error，并在两个后端人工确认 Terrain 场景可见性与编辑反馈；Phase 3 还须保持默认 sandbox/particles RenderGate golden 与 cross-backend 回归通过且不得 bless。Terrain 专用非 bless capture、cross-backend 目视确认与 golden 授权属于 Phase 4，默认 RenderGate PASS 不能冒充该证据。

人工签署必须使用 [`docs/templates/TerrainEditorManualSignoff.md`](templates/TerrainEditorManualSignoff.md)。测试者填写后，将记录保存为 `docs/verification/terrain/<YYYY-MM-DD>-<short-sha>-manual-signoff.md` 并与阶段收口一同提交。记录必须包含测试者、日期与时区、精确 commit、构建配置、GPU/驱动、Vulkan/DX12 的逐项结果、证据路径、失败项及最终 PASS/FAIL；缺失任一必填字段均不构成有效签署。

## Environment

- Runtime: Windows x64，VS2022 工具链，仓库根有 `premake5.exe`
- 工作目录: 可执行程序自动重置到仓库根；脚本假定从仓库根调用
- 报告输出: `Intermediate/test-reports/`（perf-gate、render-gate、ai-dev），本地生成物不提交
- 基线: 性能 `tools/perf/perf_gate_baselines.json`（`-BlessBaseline` 更新）；渲染 golden `tools/render/goldens/<scene>/<backend>.png`（`RunRenderGate.bat -BlessGolden` 更新，仅限用户确认画面正确后）

## 日志证据

- 每次 `LogService::init` 在 `product/logs` 创建一对唯一 Engine/Application 日志；文件名共享 `YYYYMMDD_HHMMSS_ffffff_p<PID>_s<SEQ>` session 后缀。不同进程或同进程快速重启不得覆盖、追加或交织到旧会话文件。
- smoke/validation 前记录现有 `*.logfile` 路径集合，结束后审计本次新增的精确文件对；矩阵运行应逐会话保留 readiness、clean-exit 与 validation 证据。禁止只取“最新一个日志”代表整个矩阵，也不得再按分钟文件名推测增量范围。

## RenderGraph indirect 全链诊断

- 使用 `run.bat sandbox <vulkan|dx12> Debug --rhi-selftest-indirect --run-for-frames=1`，并设置外层 120 秒 watchdog；禁止依赖人工关闭窗口。
- 同一进程先要求 `[RHISelfTest] indirect draw substrate PASS`，再要求 `[RenderGraphSelfTest] compute-to-indexed-indirect PASS`，最后要求 Sandbox `clean_exit=yes`。后一段覆盖 external candidate、transient visible/args、compute UAV、indexed indirect、Graphics SRV args 校验和 backbuffer capture。
- Debug Vulkan 必须确认 validation layer 实际加载，Debug DX12 必须确认 debug layer 与 GPU-based validation 实际启用；新增日志不得含 generic error/critical、validation error、device lost、access violation、fatal 或 assert。
- Release 双后端仍运行相同 bounded self-test，但当前编译策略只允许 Debug validation：Vulkan validation 受 `VULKAN_DEBUG_REPORT` 限制，DX12 非 `ASH_DEBUG` 强制关闭。Release PASS 只能作为功能证据，不得写成 validation 已启用。

## RenderGate（渲染回归门禁）

- 机制：默认对 `sandbox,particles` 两个场景执行 Sandbox `--rhi=<backend> --smoke-test-seconds=120 --dump-frame=<png>`；同一进程只有在 readiness smoke 与 capture 双重成功后才以 0 退出并发布 PNG，随后每图与 golden 做 SSIM、同场景做 Vulkan vs DX12 diff。可用 `-Scenes`/`-Backends` 选择子集；wall-clock 超时非零退出且不得留下可 bless 图片。脚本级 grace 超时先以有界 `taskkill /T` 请求终止树；失败时有界终止真实根进程，并在报告记录 termination 状态。
- 阈值：golden 回归 0.995（实测同后端噪声底 0.999996）；跨后端 0.99（实测 0.999843）
- 确定性保证：抓帧模式固定初始相机、隐藏 overlay、禁 TAA jitter；提交侧深拷贝可见帧，并使用连续 render frame index 与固定 `delta_seconds=1/60`。capture 采用 asset activity epoch 前后握手、当前帧全部预期 scene packet 成功、present 调用无致命错误地完成（后台窗口可被遮挡）；ready arm 时清空被加载中画面污染的 AO/TAA/体积光 history，下一帧用最终资源直接重建，不等待固定“收敛帧”；粒子稳定窗口从 emitter 参数推导。正式门槛仍为 golden SSIM 0.995 / cross-backend 0.99；exact diff 只作专项证据。
- FAIL 处理：看报告目录里的 heatmap/进程日志定位；确属预期改动且用户目视确认后才允许 `-BlessGolden`。普通回归在共享锁内读取稳定 golden 快照，并拒绝崩溃遗留事务产物；Bless 会等所选 capture 与 cross 全过后才进入同一锁的独占 stage/backup/publish。发布 I/O 中途失败会整批回滚并标 `NOT_BLESSED`，恢复失败则保留可用 backup、标 `ROLLBACK_FAILED`，已提交但清理失败标 `BLESSED_CLEANUP_FAILED` 并让门禁失败。读写并发、混合新旧目标、回滚失败与清理失败由 `scripts/TestRenderGateGoldenPublisher.ps1` 故障注入覆盖。

## Failure handling

- 构建失败：先看是否 PostBuild artifact 同步失败（stale DLL 隐患），再看编译错误
- PerfGate `FAIL`：必须修复后重跑；不允许带 FAIL 提交
- PerfGate `WARN`：允许提交，但必须在提交说明里写明判断理由
- validation / debug-layer 报错：视同 bug，定位根因，禁止靠关闭 validation 绕过
- 渲染结果异常排查顺序：1. 看 `product/logs`；2. 按后端开启 validation（`[VulkanValidation]` / `[DX12Validation]`）重跑；3. 用 `[RenderDebugView]` 分通道定位；4. RenderDoc 抓帧（pass 事件名只来自 `PassDesc::name`，空名显示 `namelesspass`，不回退 framebuffer 名）；5. Vulkan 侧看 resource tracker / barrier 日志；6. 资源泄漏看 VMA leak dump

## 待自动化（能力缺口）

| 缺口 | 现状 | 目标 |
| --- | --- | --- |
| ~~视觉正确性~~ | ✅ RenderGate golden SSIM 回归（SDD-2026-07-07-render-gate） | — |
| ~~双后端一致性~~ | ✅ RenderGate 跨后端 diff（SDD-2026-07-07-render-gate） | — |
| 渲染 bug 取证 | 手动开 RenderDoc | headless 自动出 capture，供 AI 经 RenderDoc MCP 分析 |
| RenderGate 场景覆盖 | 默认 Sandbox + GPU particles 两场景 | 继续扩充不同光照/后处理/材质组合 |
| Editor UI 自动化 | Agent 仅运行 readiness smoke；交互走查由人类完成 | 仓库内提供 headless/脚本化面板测试命令（明确断言与退出码），避免 Agent 直接驱动桌面 UI |
| ~~RenderGate 流送等待~~ | ✅ readiness + asset epoch + 当前帧全 packet + present 驱动；只有 wall-clock 失败上限，无固定帧成功/fallback（SDD-2026-07-11-readiness-driven-automation） | — |
