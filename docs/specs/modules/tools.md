---
owner: huyizhou
last_reviewed: 2026-07-13
status: active
---

# Module Spec: Tools 与门禁

## 职责与边界

仓库工具链：性能门禁（PerfGate）、渲染回归门禁（RenderGate）、图像对比（AshImageDiff）、AI 开发诊断（AIDevDoctor）、构建/运行脚本。管"改动是否可交付"的自动验证与本地开发编排；不管引擎运行时代码。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `RunPerfGate.bat` + `scripts/RunPerfGate.ps1` / `RunPerfGateMenu.ps1` | PerfGate 入口（无参进交互菜单）与实现 |
| `tools/perf/perf_gate_baselines.json` | 可 bless 的比较基线：profiles（Standard：warmup 10s / sample 30s，target Sandbox+Editor x Vulkan+DX12）、absolute_caps、warn_thresholds、baselines |
| `tools/perf/terrain_feasibility_contract.json` | 不可 bless 的 Terrain Phase 0 硬合同：Empty 2560 × 1440，CPU/GPU frame P95 均不超过 3.33 ms |
| `product/assets/scenes/TerrainPerfEmpty.scene.json` / `.manifest.json` | 固定 Empty workload；manifest 以 canonical compact JSON + SHA-256 绑定 camera/environment/light/render config 与真实 scene |
| `RunRenderGate.bat` + `scripts/RunRenderGate.ps1` | RenderGate 入口与编排 |
| `scripts/RenderGateGoldenPublisher.ps1` / `TestRenderGateGoldenPublisher.ps1` | golden 矩阵事务：普通门禁持共享锁读取稳定快照，publisher 持同一锁的独占句柄做 stage/backup/publish；完整回滚为 NOT_BLESSED，恢复失败保留 backup 并标 ROLLBACK_FAILED，提交后清理失败标 BLESSED_CLEANUP_FAILED；拒绝覆盖崩溃遗留事务产物，并有故障注入自测 |
| `tools/render/goldens/<scene>/<backend>.png` | RenderGate 多场景 golden 基线；新增/更新只能经用户确认后的 bless 流程 |
| `tools/imagediff/AshImageDiff.cpp` | SSIM 图像对比 CLI（独立 vcxproj，产物 `AshImageDiff.exe`） |
| `scripts/AIDevDoctor.ps1` + `tools/ai-dev/`（rules/templates） | 诊断与验证计划生成（`-Mode Report` / `-Mode ValidatePlan`），规则驱动 |
| `scripts/InvokeMSBuild.ps1` | MSBuild 调用封装（build_*.bat 使用）；为 MSBuild 子进程重建大小写不敏感的环境变量表、只保留规范 `Path`，修复 `PATH`/`Path` 大小写冲突导致的 MSB6001 |
| `scripts/SyncRuntimeArtifact.ps1` | PostBuild 运行时产物（DXC/validation dll 等）同步，源缺失即失败 |
| `scripts/TestAIDevDoctor.ps1` / `TestRunPerfGate.ps1` | 两个工具的自测脚本 |
| `scripts/GetTolaria.ps1` | 按 pin 的版本+SHA256 下载 Tolaria（AGPL 独立知识库应用，vault 指向 `docs/`）安装器到 `tools/tolaria/`（gitignored）；不 vendor 源码，升级只改脚本内版本与 hash |
| `scripts/hooks/PreToolUseGuard.py` + `.claude/settings.json` | AI 护栏 hook：直改基线文件 deny、S2 路径（Graphics/RenderGraph）ask（规则见 AGENTS.md High-risk paths） |
| `RunTests.bat` + `build_tests.bat` | 单元测试入口：构建 Tests 工程（doctest，`project/src/tests/`）并运行 `Tests.exe`，透传 doctest 参数 |
| `RunArchGate.bat` + `scripts/CheckArchBoundary.ps1` | 架构边界检查：扫描各层源文件 `#include "<Layer>/..."`，按 `tools/ai-dev/rules/arch-boundary-rules.json` 判定禁止边 |
| `.github/workflows/ci.yml` | CI（GitHub Actions，windows runner）：ArchGate + 全新 sln 生成 + Editor/Sandbox Debug/Release 构建 + RunTests + Release DX12/WARP、Vulkan/lavapipe readiness smoke（含 indirect 自测）；构建/运行复用本地入口脚本 |
| `run.bat` / `run_editor.bat` / `build_editor.bat` / `build_sandbox.bat` / `generate_vs2022.bat` | 运行与构建入口 |

## 公共接口

- **PerfGate**：`RunPerfGate.bat [-Profile Standard] [-Configuration <cfg>] [-Scenario Empty] [-TimingValidation] [-NoTracy] [-SkipBuild] [-DryRun] [-BlessBaseline] [-SelfTest]`；普通 profile 运行 target × backend，Empty 系列模式见下节。报告落 `Intermediate/test-reports/perf-gate/<高精度时间-随机后缀>/`（per-run telemetry + stdout/stderr + `summary.json/.md`）。`-BlessBaseline` 只更新 `perf_gate_baselines.json`，不能修改或绕过 feasibility contract。
- **RenderGate**：`RunRenderGate.bat [-Configuration Debug] [-Scenes sandbox,particles] [-Backends vulkan,dx12] [-TimeoutSeconds 120] [-ProcessTimeoutGraceSeconds 15] [-GoldenSsimThreshold 0.995] [-CrossSsimThreshold 0.99] [-BlessGolden] [-SkipCrossBackend]`；默认逐场景、逐后端用同一个 Sandbox 进程完成 readiness smoke + epoch 复核后的 capture，再与 golden 做 SSIM，并按场景做 Vulkan vs DX12 diff。子进程直接启动并异步排空 stdout/stderr；`-TimeoutSeconds` 是引擎 wall-clock 硬失败上限，脚本另加 grace 后先以有界 `taskkill /T` 请求终止树，失败时有界终止真实根进程；报告持久化 `script_timed_out/termination_failed/output_drain_failed`，超时/失败没有可 bless PNG。报告目录以毫秒时间、PID 与随机后缀唯一命名。普通回归持仓库级共享锁读取完整 golden 矩阵并拒绝未解决事务产物。**`-BlessGolden` 仅在用户确认画面正确后使用，并应显式传 `-Scenes`；所有选中 capture 与跨后端检查通过后才在同一锁的独占句柄内进入 stage/backup/publish**。完整回滚标 `NOT_BLESSED`；回滚不完整保留 backup 并标 `ROLLBACK_FAILED`；提交已完成但事务文件清理失败标 `BLESSED_CLEANUP_FAILED` 且门禁仍失败。
- **AshImageDiff**：`AshImageDiff.exe <a.png> <b.png> [--ssim-threshold=x] [--heatmap=path]`；灰度 SSIM（11x11 高斯窗口，sigma 1.5）+ 逐像素统计；stdout 输出 `key=value`：`image_a/image_b/width/height/ssim/ssim_threshold/max_abs_diff/mean_abs_diff/diff_pixel_count/diff_pixel_ratio/[heatmap]/result`；退出码 0=PASS、1=FAIL（低于阈值或尺寸不匹配）、2=用法/IO 错误。
- **AIDevDoctor**：`scripts/AIDevDoctor.ps1 -Mode Report|ValidatePlan`；基于 git dirty paths 与 `tools/ai-dev/rules/*.json` 生成诊断报告/验证计划，报告落 `Intermediate/test-reports/ai-dev/`。详见 `docs/AIDevDoctor.md`。
- **Tests**：`RunTests.bat [Config] [doctest args...]`；先经 `build_tests.bat` 构建 Tests 工程，再运行 `product/bin64/<Config>-windows-x86_64/Tests.exe`；退出码 0 = 全部通过。doctest 参数直接透传（如 `--test-case="*StringView*"`、`--list-test-cases`；经 cmd 转发时引号可能被吃掉，过滤不生效时直接调 Tests.exe）。
- **ArchGate**：`RunArchGate.bat`（可选 `-RulesPath <json>`）；纯文本扫描，秒级完成，不需要构建。三档判定：`exceptions`（长期合法例外，如 `Window.h` → `RHIBackend.h` 纯枚举头）；`legacy_violations`（既有越界，WARN 不挡、**禁止新增**）；其余禁止边命中 → FAIL 退出码 1。名单条目失配（对应文件已修复/删除）也 FAIL，强制名单只减不增。改规则或脚本跑 `scripts/TestCheckArchBoundary.ps1`。
- **构建/运行**：`generate_vs2022.bat`（premake5 生成 sln）；`build_editor.bat` / `build_sandbox.bat [Config] [Platform]`（缺 sln 自动生成，经 InvokeMSBuild.ps1）；`run.bat [editor|sandbox|all] [current|dx12|vulkan] [Config] [AppArgs...]`（临时改写 Engine.ini backend，退出后还原；`all` 为 Editor+Sandbox x DX12+Vulkan 矩阵）。

### PerfGate Empty 与 GPU timing 模式

- 普通 `-Scenario Empty -Configuration Release` 只跑 Editor，顺序固定为 Vulkan → DX12；加载 canonical Empty scene，关闭 VSync，以真实 Game viewport/primary scene camera 分配 2560 × 1440 scene output。shared readiness 完成后才进入 Standard 的 10 秒 warmup + 30 秒 sample；scene output 与 swapchain 分开校验。每端先应用不可 bless 的 CPU/GPU P95 3.33 ms 硬合同，再做可 bless 的比较基线。
- `-Scenario Empty -Configuration Debug -TimingValidation` 开启目标后端的 validation/debug layer。Vulkan 使用 core + synchronization validation（GPU-assisted 关闭，避免与 core checks 的 validation-mode 冲突），DX12 使用 debug layer + GPU validation；任何 warning/error 均失败。成功条件是 readiness 完成且至少一份 expected GPU snapshot 完整到达，不跑性能/feasibility/比较阈值，也不用固定帧数。
- `-Scenario Empty -Configuration Release -NoTracy` 证明机器 timing 不依赖 Tracy：先 `premake5.exe --no-tracy vs2022`，Release clean + Editor build，再跑双后端；`finally` 中无条件恢复标准 premake、clean 与 Editor build。该模式拒绝 `-SkipBuild/-DryRun/-BlessBaseline/-TimingValidation`。

所有 PerfGate run 都由 shared readiness 启动采样，并在窗口结束后继续渲染直至 GPU expected set drain 完成；固定秒数只定义性能统计窗口，wall-clock timeout 只作失败上限。脚本对 `Engine.ini`、`EditorSettings.json`、`ViewportLayout.json`、`imgui.ini` 做二进制快照，两个后端各自从原始快照开始，结束或异常时都按 SHA-256 验证逐字节恢复。

### PerfGate schema v2

Empty/no-Tracy/TimingValidation 要求 runtime telemetry `schema_version=2`。除 CPU、memory、render stats 外，必须包含：解析后的 `backend_actual`；`readiness.status/submitted_frame_index`；实际与期望 `render_output`；独立 `swapchain` extent；`gpu_timing.status/error/expected_frames/received_frames/frame_time_ms(p50/p95/p99)/passes`；以及 abnormal exit、backend/output mismatch、timeout、incomplete、GPU timing 等 error flags。已配置的 required pass entry 同时报告 canonical name、stable hash 与 percentile；Empty 的 required set 为空，因此 `passes` 可为空对象。通过条件包含 `expected_frames == received_frames > 0`、GPU status complete/error Success、实际输出 2560 × 1440；missing/duplicate/unexpected frame、overflow、hash collision、required scope 缺失或 drain timeout 都失败。

### Terrain Phase 0 Empty 实测基线（2026-07-13）

设备为 NVIDIA GeForce RTX 5060；命令为标准 Tracy-enabled Release `RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Release`，报告 `Intermediate/test-reports/perf-gate/20260713-160641-8253384-74cce123/`。这是 Phase 0 结论来源，不使用 DryRun、Debug TimingValidation 或 no-Tracy 数字。

| 后端 | CPU frame P95 | GPU frame P95 | GPU expected/received | readiness frame | scene output | swapchain | 3.33 ms 合同 |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| Vulkan | 1.0674 ms | 0.7860 ms | 30397 / 30397 | 128 | 2560 × 1440 | 1920 × 1080 | PASS |
| DX12 | 1.0274 ms | 0.8176 ms | 33444 / 33444 | 258 | 2560 × 1440 | 1920 × 1080 | PASS |

## 约束与不变式

- 所有脚本假定从仓库根调用；本地报告统一落 `Intermediate/test-reports/`，不提交。
- 比较基线 `perf_gate_baselines.json` 与 render golden 提交入库，只能经 `-BlessBaseline` / `-BlessGolden` 更新，golden 更新前必须由用户确认画面正确。`terrain_feasibility_contract.json` 标记 `immutable=true/blessable=false`，禁止通过 bless 降低或绕过 3.33 ms 门槛。
- PerfGate FAIL 禁止提交；WARN 需在提交说明写明理由。RenderGate 跨后端 diff FAIL 视同 bug。
- PerfGate 配置事务恢复失败与 workload manifest/scene/hash 漂移均为 fail-closed；禁止因测试失败留下用户 Editor 布局或 Engine.ini 改写。
- AshImageDiff 输出格式（key=value + 退出码语义）是 RenderGate 解析契约，改动需同步 `RunRenderGate.ps1`。
- RenderGate 确定性依赖 Sandbox 抓帧约定（asset epoch + 当前帧全部 scene packet + 动态 capture-ready 信号、ready 时清空 AO/TAA/体积光 history、固定相机、隐藏 overlay、禁 TAA jitter、连续渲染 frame index 与 1/60 delta），见 sandbox/application spec。

## 验证

对齐 `docs/VERIFY.md` "`scripts/` / `tools/`"行：

- 改 AIDevDoctor：`scripts/TestAIDevDoctor.ps1`
- 改 ArchGate（脚本或 arch-boundary-rules.json）：`scripts/TestCheckArchBoundary.ps1`
- 改 PerfGate：`scripts/TestRunPerfGate.ps1`（含 `-SelfTest` 路径）
- 改 GPU timing/Empty 编排：额外跑 Debug `-TimingValidation`、Release `-NoTracy`、标准 Release Empty 3.33 ms 门禁，并检查双后端 schema v2、validation 日志与配置 hash 恢复。
- 改 RenderGate/AshImageDiff：完整跑一次 `RunRenderGate.bat` 确认 PASS
- 改 golden 发布事务：`scripts/TestRenderGateGoldenPublisher.ps1` 故障注入必须 PASS
- 改构建链（premake/bat/InvokeMSBuild/SyncRuntimeArtifact）：删 sln 全新 `generate_vs2022.bat` + 构建，确认 PostBuild artifact 同步成功

## 历史

- `docs/sdd/SDD-2026-07-07-render-gate.md`（RenderGate + AshImageDiff）
- `docs/sdd/SDD-2026-07-11-readiness-driven-automation.md`（readiness smoke/capture、wall-clock timeout、批量 bless）
- `docs/sdd/SDD-2026-07-13-terrain-system.md`（Phase 0 GPU timing、固定 Empty workload 与不可 bless 性能合同）
- `docs/sdd/SDD-2026-07-08-doctest-unit-test-layer.md`（doctest 单测工程 + RunTests.bat）
- `docs/sdd/SDD-2026-07-08-arch-boundary-check.md`（ArchGate 架构边界检查）
- `docs/superpowers/specs/2026-05-18-perf-gate-design.md`（PerfGate，归档）
