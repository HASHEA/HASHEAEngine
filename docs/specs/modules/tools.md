---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Tools 与门禁

## 职责与边界

仓库工具链：性能门禁（PerfGate）、渲染回归门禁（RenderGate）、图像对比（AshImageDiff）、AI 开发诊断（AIDevDoctor）、构建/运行脚本。管"改动是否可交付"的自动验证与本地开发编排；不管引擎运行时代码。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `RunPerfGate.bat` + `scripts/RunPerfGate.ps1` / `RunPerfGateMenu.ps1` | PerfGate 入口（无参进交互菜单）与实现 |
| `tools/perf/perf_gate_profiles.json` | 新 profile 真源；`VegetationFullPipeline` 固定 Release、Sandbox × Vulkan/DX12、2560×1440、完整场景、11 个 required GPU metric 与 95% coverage 门槛 |
| `tools/perf/perf_gate_baselines.json` | PerfGate bless 水位与 Standard legacy profile（warmup 10s / sample 30s，Sandbox+Editor × Vulkan+DX12）；受保护，只能经 bless 流程更新 |
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

- **PerfGate**：`RunPerfGate.bat [-Profile Standard] [-Configuration <cfg>] [-SkipBuild] [-DryRun] [-BlessBaseline] [-BlessBaselineFromReport <summary.json> -ExpectedReportSha256 <sha256>] [-TelemetryMode Profile|Off] [-SelfTest]`；按 profile 跑 target x backend 采样，与 `perf_gate_baselines.json` 比对并产出 PASS/WARN/FAIL；报告 `Intermediate/test-reports/perf-gate/<时间戳>/`（summary.json + summary.md），`-BlessBaseline` 回写当前 fresh run，受保护 report-import 则只导入用户已批准且字节哈希锁定的既有候选，不构建或启动程序。Standard 保持 legacy schema 与百分比阈值语义；`VegetationFullPipeline` 比较 CPU avg/p95/p99、private bytes、engine heap、draw calls，以及 11 个 required GPU metric 的 avg/p95。`GPU.Frame` 使用独立阈值，其余 metric 按 baseline avg 选择 pass tier；每个超限 statistic 产生独立 WARN。相对阈值与绝对 floor 取较大值，tiny pass tier 为 absolute-only。
- **PerfGate 可比性**：固定 profile 的 run record 与 baseline entry 都持久化 adapter、driver、OS build、source SHA，以及 workload 可读字段和 SHA-256 fingerprint。workload 规范字段为 repo 内 scene 规范路径与内容 SHA-256、extent、fixed_camera、vsync、validation、frame_cap、排序后的 required GPU metric 集合。adapter/driver/OS/fingerprint 不同或归因缺失时标 `NOT_COMPARABLE` 并 FAIL；source SHA 允许不同。整条 baseline 缺失仍保留 `MISSING` candidate；baseline 已存在后，任一 required baseline/current metric 缺失、负值或非有限值均 fail-closed。三轮稳定性 helper 只按 target+backend 分组：CPU/GPU.Frame avg spread 上限 `max(3%, 0.15 ms)`，p95 上限 `max(5%, 0.30 ms)`。
- **PerfGate 生命周期**：`-TelemetryMode Off` 用于同一固定运行时契约下的 timing-overhead A/B：仍强制 schema v2、configuration、extent、fixed camera 等运行时元数据，但不要求已关闭的 GPU timing `backend_info`/adapter/driver，且不能与 `-BlessBaseline` 同用。Editor/Sandbox 由带 `KILL_ON_JOB_CLOSE` 的 Windows Job Object 直接启动，正常退出与超时都在有界期限内终止并确认 `ActiveProcesses=0` 后才继续矩阵；该方案的受信任 launcher 前提是 `Process.Start` → `AssignProcessToJobObject` 期间不得 spawn helper/子进程，若未来不再满足则必须改为 suspended `CreateProcess` 或 `PROC_THREAD_ATTRIBUTE_JOB_LIST`。用法详见 `docs/PerfGateUsageGuide.md`。
- **RenderGate**：`RunRenderGate.bat [-Configuration Debug] [-Scenes sandbox,particles] [-Backends vulkan,dx12] [-TimeoutSeconds 120] [-ProcessTimeoutGraceSeconds 15] [-GoldenSsimThreshold 0.995] [-CrossSsimThreshold 0.99] [-BlessGolden] [-SkipCrossBackend]`；默认逐场景、逐后端用同一个 Sandbox 进程完成 readiness smoke + epoch 复核后的 capture，再与 golden 做 SSIM，并按场景做 Vulkan vs DX12 diff。子进程直接启动并异步排空 stdout/stderr；`-TimeoutSeconds` 是引擎 wall-clock 硬失败上限，脚本另加 grace 后先以有界 `taskkill /T` 请求终止树，失败时有界终止真实根进程；报告持久化 `script_timed_out/termination_failed/output_drain_failed`，超时/失败没有可 bless PNG。报告目录以毫秒时间、PID 与随机后缀唯一命名。普通回归持仓库级共享锁读取完整 golden 矩阵并拒绝未解决事务产物。**`-BlessGolden` 仅在用户确认画面正确后使用，并应显式传 `-Scenes`；所有选中 capture 与跨后端检查通过后才在同一锁的独占句柄内进入 stage/backup/publish**。完整回滚标 `NOT_BLESSED`；回滚不完整保留 backup 并标 `ROLLBACK_FAILED`；提交已完成但事务文件清理失败标 `BLESSED_CLEANUP_FAILED` 且门禁仍失败。
- **AshImageDiff**：`AshImageDiff.exe <a.png> <b.png> [--ssim-threshold=x] [--heatmap=path]`；灰度 SSIM（11x11 高斯窗口，sigma 1.5）+ 逐像素统计；stdout 输出 `key=value`：`image_a/image_b/width/height/ssim/ssim_threshold/max_abs_diff/mean_abs_diff/diff_pixel_count/diff_pixel_ratio/[heatmap]/result`；退出码 0=PASS、1=FAIL（低于阈值或尺寸不匹配）、2=用法/IO 错误。
- **AIDevDoctor**：`scripts/AIDevDoctor.ps1 -Mode Report|ValidatePlan`；基于 git dirty paths 与 `tools/ai-dev/rules/*.json` 生成诊断报告/验证计划，报告落 `Intermediate/test-reports/ai-dev/`。详见 `docs/AIDevDoctor.md`。
- **Tests**：`RunTests.bat [Config] [doctest args...]`；先经 `build_tests.bat` 构建 Tests 工程，再运行 `product/bin64/<Config>-windows-x86_64/Tests.exe`；退出码 0 = 全部通过。doctest 参数直接透传（如 `--test-case="*StringView*"`、`--list-test-cases`；经 cmd 转发时引号可能被吃掉，过滤不生效时直接调 Tests.exe）。
- **ArchGate**：`RunArchGate.bat`（可选 `-RulesPath <json>`）；纯文本扫描，秒级完成，不需要构建。三档判定：`exceptions`（长期合法例外，如 `Window.h` → `RHIBackend.h` 纯枚举头）；`legacy_violations`（既有越界，WARN 不挡、**禁止新增**）；其余禁止边命中 → FAIL 退出码 1。名单条目失配（对应文件已修复/删除）也 FAIL，强制名单只减不增。改规则或脚本跑 `scripts/TestCheckArchBoundary.ps1`。
- **构建/运行**：`generate_vs2022.bat`（premake5 生成 sln）；`build_editor.bat` / `build_sandbox.bat [Config] [Platform]`（缺 sln 自动生成，经 InvokeMSBuild.ps1）；`run.bat [editor|sandbox|all] [current|dx12|vulkan] [Config] [AppArgs...]`（临时改写 Engine.ini backend，退出后还原；`all` 为 Editor+Sandbox x DX12+Vulkan 矩阵）。

## 约束与不变式

- 所有脚本假定从仓库根调用；本地报告统一落 `Intermediate/test-reports/`，不提交。
- PerfGate 每个 matrix run 的 `engine_logs` 必须是该子进程启动前/退出后日志路径集合的精确差集；不得用 LastWriteTime 窗口吸收上一进程的延迟 flush。`run.bat` 的 single 与 `all` 路径必须逐个保留任意数量的 application arguments，矩阵四格使用同一参数序列。
- PerfGate baseline bless 只能使用终态 PASS/WARN、已确认 Job Object 清理且 comparison identity 完整的 run；`TelemetryMode Off`、FAIL、DRY_RUN、不可比数据均不得 bless。baseline source SHA 只记录采样来源，不参与相等性判定。
- PerfGate 受保护 report-import 只能读取当前仓库 `Intermediate/test-reports/perf-gate/` 下、SHA-256 与显式批准值逐字节匹配、未 bless 且整体/逐 run 均为 PASS 的 schema v2 报告；profile/configuration/baseline path、精确 target×backend 矩阵、当前 source SHA、workload fingerprint、extent/runtime flags、required metric 集合、coverage 与进程树/Job cleanup 必须全部匹配。任何校验失败都不得改变 baseline；成功 entry 记录 repo-relative `source_report` 与 `source_report_sha256`，且导入路径在任何 build/report/process side effect 前退出。
- PerfGate 的 JSON writer 固定输出 UTF-8 without BOM、LF 行尾和单个末尾 LF；baseline bless 与 summary 使用同一 writer，重复写入同一对象必须字节一致且不得产生尾随空白。
- 基线文件（perf json、render golden png）提交入库；只能经 `-BlessBaseline` / `-BlessGolden` 更新，golden 更新前必须由用户确认画面正确。
- PerfGate FAIL 禁止提交；WARN 需在提交说明写明理由。RenderGate 跨后端 diff FAIL 视同 bug。
- AshImageDiff 输出格式（key=value + 退出码语义）是 RenderGate 解析契约，改动需同步 `RunRenderGate.ps1`。
- RenderGate 确定性依赖 Sandbox 抓帧约定（asset epoch + 当前帧全部 scene packet + 动态 capture-ready 信号、ready 时清空 AO/TAA/体积光 history、固定相机、隐藏 overlay、禁 TAA jitter、连续渲染 frame index 与 1/60 delta），见 sandbox/application spec。

## 验证

对齐 `docs/VERIFY.md` "`scripts/` / `tools/`"行：

- 改 AIDevDoctor：`scripts/TestAIDevDoctor.ps1`
- 改 ArchGate（脚本或 arch-boundary-rules.json）：`scripts/TestCheckArchBoundary.ps1`
- 改 PerfGate：`scripts/TestRunPerfGate.ps1`（含 `-SelfTest` 路径）
- 改 RenderGate/AshImageDiff：完整跑一次 `RunRenderGate.bat` 确认 PASS
- 改 golden 发布事务：`scripts/TestRenderGateGoldenPublisher.ps1` 故障注入必须 PASS
- 改构建链（premake/bat/InvokeMSBuild/SyncRuntimeArtifact）：删 sln 全新 `generate_vs2022.bat` + 构建，确认 PostBuild artifact 同步成功

## 历史

- `docs/sdd/SDD-2026-07-07-render-gate.md`（RenderGate + AshImageDiff）
- `docs/sdd/SDD-2026-07-11-readiness-driven-automation.md`（readiness smoke/capture、wall-clock timeout、批量 bless）
- `docs/sdd/SDD-2026-07-08-doctest-unit-test-layer.md`（doctest 单测工程 + RunTests.bat）
- `docs/sdd/SDD-2026-07-08-arch-boundary-check.md`（ArchGate 架构边界检查）
- `docs/superpowers/specs/2026-05-18-perf-gate-design.md`（PerfGate，归档）
