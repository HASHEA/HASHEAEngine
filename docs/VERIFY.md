---
owner: huyizhou
last_reviewed: 2026-07-15
review_cycle: monthly
status: active
---

# Verification

按变更类型给出必须执行的验证。原则：**没有对应验证证据的改动不算完成**。
视觉正确性与双后端一致性由 RenderGate 自动回归（`RunRenderGate.bat`，SDD-2026-07-07-render-gate）；RenderDoc 自动取证仍是缺口，见文末"待自动化"。

## Fast path

```bat
build_editor.bat Debug        :: 或 build_sandbox.bat Debug；缺 sln 自动生成
RunTests.bat                  :: doctest 单元测试（构建 Tests.exe 并运行；含 legacy 自测桥接）
RunArchGate.bat               :: 架构边界检查（include 扫描依赖方向红线，秒级，改引擎/编辑器代码必跑）
run.bat sandbox vulkan Debug --smoke-test-seconds=120
RunRenderGate.bat             :: 渲染改动必跑：双后端 golden SSIM 回归 + 跨后端 diff
```

## Change matrix

| Change | Commands | 补充人工检查 |
| --- | --- | --- |
| 纯文档 / 注释 | `git diff --check` | — |
| Base 层纯逻辑（容器/字符串/序列化/内存等） | `RunTests.bat`（doctest 单测 + legacy 自测桥接） | 新逻辑应补对应 `project/src/tests/` 用例 |
| 渲染 Pass / shader / 材质 | 构建 + `RunRenderGate.bat`（双后端 golden SSIM 回归 + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard` | 检查 `product/logs` 无 validation 报错；预期内的画面变化经用户确认后 `-BlessGolden` 更新基线 |
| RHI 接口 / 双后端实现 | 构建 + `RunRenderGate.bat` + PerfGate Standard；Engine.ini 开启 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 smoke | 跨后端 diff FAIL 视同 bug；改 GPU timing 还须 focused tests + Debug TimingValidation + Release NoTracy |
| RenderGraph 核心（compile/barrier/lifetime） | 同 RHI 级别 | 关注 barrier/lifetime 相关 validation 输出 |
| Scene / Asset / Application 生命周期 | 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke；通常 ready 后数秒即提前退出） | Editor 打开默认场景操作一遍 |
| Terrain Asset / CPU logic | `RunTests.bat Debug` + `RunTests.bat Release` + `RunArchGate.bat`；依赖或工程生成变化时 fresh `generate_vs2022.bat` 并构建 Editor/Sandbox Debug+Release；最后 `run.bat all Debug --smoke-test-seconds=120` | 检查 `.AshTerrain` recovery、RAW/PNG/EXR、不可变 snapshot、dirty publication 与 local query focused tests；Phase 1 无 rendered frame 变化时不跑 RenderGate |
| Editor 面板 / UI | 构建 + `run.bat editor Debug --smoke-test-seconds=120`（readiness 后自动关闭）+ `run.bat editor` 手动过一遍改动路径 | 面板打开、交互、无报错日志 |
| `product/config/Engine.ini` | 双后端各 smoke 一次 + 查日志 | 确认开关生效；配置项语义/默认值变化同步 `docs/CONFIG.md` |
| 性能敏感路径 | PerfGate Standard；Terrain/1440p 相关还须 Release Empty immutable gate，`FAIL` 必须修，`WARN` 需说明判断 | 对比 `summary.md` 趋势；Empty CPU/GPU P95 任一超过 3.33 ms 即阻断 Terrain |
| `scripts/` / `tools/` | `scripts/TestAIDevDoctor.ps1`、`scripts/TestRunPerfGate.ps1`、`scripts/TestCheckArchBoundary.ps1`（按所改工具） | — |
| `premake5.lua` / 构建链 | 删 sln 后全新 `generate_vs2022.bat` + 构建；确认 PostBuild artifact 同步成功 | `product/bin64` 下 DXC/validation dll 是新的 |
| `project/src/tests/` / 测试基建 | `RunTests.bat`（退出码 0 = 全绿）；改 premake 部分按上一行执行 | 断言失败退出码非 0；可用 doctest `--test-case=` 过滤 |

不确定改动属于哪类时，运行 `scripts/AIDevDoctor.ps1 -Mode ValidatePlan`，它会根据 dirty paths 生成验证计划。

### Terrain Editor manual checklist

Phase 3 收口必须由一名具名的人类测试者在 Editor 主 Scene viewport 亲自逐项操作并签署结果。AI agent、鼠标/UI 自动化、readiness smoke、源码字符串测试和单元测试都不能执行或代签本清单；自动化只能准备 fixture、采集日志和提供辅助诊断。未取得人工签署记录时，本清单一律视为未通过，Phase 3 不得标记完成，也不得进入 Phase 4。

- Terrain Mode 创建 production-default 8193² flat Terrain，并能保存、关闭、重开。
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
- 基线: 比较性能 `tools/perf/perf_gate_baselines.json` 可用 `-BlessBaseline` 更新；Terrain feasibility `tools/perf/terrain_feasibility_contract.json` 不可 bless；渲染 golden `tools/render/goldens/<scene>/<backend>.png` 仅在用户确认画面正确后用 `RunRenderGate.bat -BlessGolden` 更新

## PerfGate（性能与 GPU timing 门禁）

常规矩阵与 Empty 专项命令：

```bat
RunPerfGate.bat -Profile Standard
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Debug -TimingValidation
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Release -NoTracy
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Release
```

- 所有模式都先等 shared readiness：application ready、render asset 无 pending/failed、render command queue 排空、当前 scene packet 全成功、asset epoch 一致且 present 完成。readiness 的 submitted frame index 是采样 watermark；窗口结束后继续渲染并 non-blocking drain expected GPU frames。固定秒数只定义 warmup/sample 统计窗口，wall-clock timeout 只作失败上限；禁止以固定帧数认定成功。
- Empty 使用 Editor 真实 Game viewport/primary scene camera，要求实际 scene output 为 2560 × 1440；报告中的 swapchain extent 独立，不能拿窗口尺寸冒充 scene output。schema v2 必须有 backend_actual、readiness、render_output、swapchain、GPU status/error/expected/received/frame percentiles、`passes` 对象与 error flags；已配置 required pass 时，其 entry 必须带 canonical name/hash/percentiles，Empty 的 required set 为空所以 `passes` 可为空。`expected == received > 0` 且无 timing error 才通过。
- `-TimingValidation` 在 Debug 下启用目标后端 validation，warning/error 均失败；只要求 readiness + 至少一份完整 expected snapshot，不应用性能阈值。`-NoTracy` 必须 fresh no-Tracy clean build、双后端运行，并在 `finally` 恢复标准 solution/clean build。
- 脚本以二进制快照事务保护 `Engine.ini`、`EditorSettings.json`、`ViewportLayout.json`、`imgui.ini`；每个后端从原始状态开始，正常或异常退出都要 SHA-256 验证逐字节恢复。
- Empty 先应用 `terrain_feasibility_contract.json` 的不可 bless CPU/GPU frame P95 3.33 ms，再做比较 baseline。missing/duplicate/unexpected frame、overflow、hash collision、required scope 缺失、`DrainTimeout`、backend/output mismatch 或 incomplete telemetry 都是 FAIL。

Terrain Phase 0 的标准 Tracy-enabled Release 证据（NVIDIA GeForce RTX 5060，报告 `Intermediate/test-reports/perf-gate/20260713-160641-8253384-74cce123/`）：

| 后端 | CPU frame P95 | GPU frame P95 | expected/received | readiness frame | scene output | 结果 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Vulkan | 1.0674 ms | 0.7860 ms | 30397 / 30397 | 128 | 2560 × 1440 | PASS |
| DX12 | 1.0274 ms | 0.8176 ms | 33444 / 33444 | 258 | 2560 × 1440 | PASS |

## RenderGate（渲染回归门禁）

- 机制：默认对 `sandbox,particles` 两个场景执行 Sandbox `--rhi=<backend> --smoke-test-seconds=120 --dump-frame=<png>`；同一进程只有在 readiness smoke 与 capture 双重成功后才以 0 退出并发布 PNG，随后每图与 golden 做 SSIM、同场景做 Vulkan vs DX12 diff。可用 `-Scenes`/`-Backends` 选择子集；wall-clock 超时非零退出且不得留下可 bless 图片。脚本级 grace 超时先以有界 `taskkill /T` 请求终止树；失败时有界终止真实根进程，并在报告记录 termination 状态。
- 阈值：golden 回归 0.995（实测同后端噪声底 0.999996）；跨后端 0.99（实测 0.999843）
- 确定性保证：抓帧模式固定初始相机、隐藏 overlay、禁 TAA jitter；提交侧深拷贝可见帧，并使用连续 render frame index 与固定 `delta_seconds=1/60`。capture 采用 asset activity epoch 前后握手、当前帧全部预期 scene packet 成功、present 调用无致命错误地完成（后台窗口可被遮挡）；ready arm 时清空被加载中画面污染的 AO/TAA/体积光 history，下一帧用最终资源直接重建，不等待固定“收敛帧”；粒子稳定窗口从 emitter 参数推导。正式门槛仍为 golden SSIM 0.995 / cross-backend 0.99；exact diff 只作专项证据。
- FAIL 处理：看报告目录里的 heatmap/进程日志定位；确属预期改动且用户目视确认后才允许 `-BlessGolden`。普通回归在共享锁内读取稳定 golden 快照，并拒绝崩溃遗留事务产物；Bless 会等所选 capture 与 cross 全过后才进入同一锁的独占 stage/backup/publish。发布 I/O 中途失败会整批回滚并标 `NOT_BLESSED`，恢复失败则保留可用 backup、标 `ROLLBACK_FAILED`，已提交但清理失败标 `BLESSED_CLEANUP_FAILED` 并让门禁失败。读写并发、混合新旧目标、回滚失败与清理失败由 `scripts/TestRenderGateGoldenPublisher.ps1` 故障注入覆盖。

## Failure handling

- 构建失败：先看是否 PostBuild artifact 同步失败（stale DLL 隐患），再看编译错误
- PerfGate `FAIL`：必须修复后重跑；不允许带 FAIL 提交
- PerfGate `WARN`：允许提交，但必须在提交说明里写明判断理由
- Empty 3.33 ms feasibility FAIL：不可 bless；停止 Terrain 后续实现并另写 performance-remediation SDD，禁止调低合同或用比较 baseline 覆盖
- GPU timing incomplete/missing/duplicate/unexpected/overflow/`DrainTimeout`，以及 TimingValidation 的 validation warning/error：均按 bug 处理
- validation / debug-layer 报错：视同 bug，定位根因，禁止靠关闭 validation 绕过
- 渲染结果异常排查顺序：1. 看 `product/logs`；2. 按后端开启 validation（`[VulkanValidation]` / `[DX12Validation]`）重跑；3. 用 `[RenderDebugView]` 分通道定位；4. RenderDoc 抓帧（pass 事件名只来自 `PassDesc::name`，空名显示 `namelesspass`，不回退 framebuffer 名）；5. Vulkan 侧看 resource tracker / barrier 日志；6. 资源泄漏看 VMA leak dump

## 待自动化（能力缺口）

| 缺口 | 现状 | 目标 |
| --- | --- | --- |
| ~~视觉正确性~~ | ✅ RenderGate golden SSIM 回归（SDD-2026-07-07-render-gate） | — |
| ~~双后端一致性~~ | ✅ RenderGate 跨后端 diff（SDD-2026-07-07-render-gate） | — |
| 渲染 bug 取证 | 手动开 RenderDoc | headless 自动出 capture，供 AI 经 RenderDoc MCP 分析 |
| RenderGate 场景覆盖 | 默认 Sandbox + GPU particles 两场景 | 继续扩充不同光照/后处理/材质组合 |
| Editor UI 自动化 | 仅启动冒烟 + 纯人工走查 | headless/脚本化面板交互 smoke（打开各面板、执行代表性命令、断言无报错） |
| ~~RenderGate 流送等待~~ | ✅ readiness + asset epoch + 当前帧全 packet + present 驱动；只有 wall-clock 失败上限，无固定帧成功/fallback（SDD-2026-07-11-readiness-driven-automation） | — |
