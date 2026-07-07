---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Module Spec: Tools 与门禁

## 职责与边界

仓库工具链：性能门禁（PerfGate）、渲染回归门禁（RenderGate）、图像对比（AshImageDiff）、AI 开发诊断（AIDevDoctor）、构建/运行脚本。管"改动是否可交付"的自动验证与本地开发编排；不管引擎运行时代码。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `RunPerfGate.bat` + `scripts/RunPerfGate.ps1` / `RunPerfGateMenu.ps1` | PerfGate 入口（无参进交互菜单）与实现 |
| `tools/perf/perf_gate_baselines.json` | PerfGate 基线：profiles（Standard：warmup 10s / sample 30s，target Sandbox+Editor x Vulkan+DX12）、absolute_caps、warn_thresholds、baselines |
| `RunRenderGate.bat` + `scripts/RunRenderGate.ps1` | RenderGate 入口与编排 |
| `tools/render/goldens/<scene>/<backend>.png` | RenderGate golden 基线（当前仅 `sandbox/vulkan.png`、`sandbox/dx12.png`） |
| `tools/imagediff/AshImageDiff.cpp` | SSIM 图像对比 CLI（独立 vcxproj，产物 `AshImageDiff.exe`） |
| `scripts/AIDevDoctor.ps1` + `tools/ai-dev/`（rules/templates） | 诊断与验证计划生成（`-Mode Report` / `-Mode ValidatePlan`），规则驱动 |
| `scripts/InvokeMSBuild.ps1` | MSBuild 调用封装（build_*.bat 使用）；为 MSBuild 子进程重建大小写不敏感的环境变量表、只保留规范 `Path`，修复 `PATH`/`Path` 大小写冲突导致的 MSB6001 |
| `scripts/SyncRuntimeArtifact.ps1` | PostBuild 运行时产物（DXC/validation dll 等）同步，源缺失即失败 |
| `scripts/TestAIDevDoctor.ps1` / `TestRunPerfGate.ps1` | 两个工具的自测脚本 |
| `scripts/GetTolaria.ps1` | 按 pin 的版本+SHA256 下载 Tolaria（AGPL 独立知识库应用，vault 指向 `docs/`）安装器到 `tools/tolaria/`（gitignored）；不 vendor 源码，升级只改脚本内版本与 hash |
| `scripts/hooks/PreToolUseGuard.py` + `.claude/settings.json` | AI 护栏 hook：直改基线文件 deny、S2 路径（Graphics/RenderGraph）ask（规则见 AGENTS.md High-risk paths） |
| `run.bat` / `run_editor.bat` / `build_editor.bat` / `build_sandbox.bat` / `generate_vs2022.bat` | 运行与构建入口 |

## 公共接口

- **PerfGate**：`RunPerfGate.bat [-Profile Standard] [-Configuration <cfg>] [-SkipBuild] [-DryRun] [-BlessBaseline] [-SelfTest]`；按 profile 跑 target x backend 采样，与 `perf_gate_baselines.json` 比对（CPU avg/p95/p99、private bytes、engine heap、draw calls），产出 PASS/WARN/FAIL；报告 `Intermediate/test-reports/perf-gate/<时间戳>/`（summary.json + summary.md）；`-BlessBaseline` 回写基线。用法详见 `docs/PerfGateUsageGuide.md`。
- **RenderGate**：`RunRenderGate.bat [-Configuration Debug] [-Backends vulkan,dx12] [-SmokeFrames 5000] [-GoldenSsimThreshold 0.995] [-CrossSsimThreshold 0.99] [-BlessGolden] [-SkipCrossBackend]`；每后端跑 `Sandbox.exe --rhi=<backend> --smoke-test=<N> --dump-frame=<png>` 抓帧——抓帧时机由引擎侧资产流送 quiesce 信号驱动（流送完成 + 32 帧余量即抓帧退出，SDD-2026-07-07-render-gate-streaming-signal），`-SmokeFrames` 仅为超时保底——与 `tools/render/goldens/<scene>/<backend>.png` 做 SSIM 回归（阈值 0.995），再做 Vulkan vs DX12 跨后端 diff（阈值 0.99）；报告 `Intermediate/test-reports/render-gate/<时间戳>/`（抓帧 png、日志、heatmap）。**`-BlessGolden` 仅在用户确认画面正确后使用**。
- **AshImageDiff**：`AshImageDiff.exe <a.png> <b.png> [--ssim-threshold=x] [--heatmap=path]`；灰度 SSIM（11x11 高斯窗口，sigma 1.5）+ 逐像素统计；stdout 输出 `key=value`：`image_a/image_b/width/height/ssim/ssim_threshold/max_abs_diff/mean_abs_diff/diff_pixel_count/diff_pixel_ratio/[heatmap]/result`；退出码 0=PASS、1=FAIL（低于阈值或尺寸不匹配）、2=用法/IO 错误。
- **AIDevDoctor**：`scripts/AIDevDoctor.ps1 -Mode Report|ValidatePlan`；基于 git dirty paths 与 `tools/ai-dev/rules/*.json` 生成诊断报告/验证计划，报告落 `Intermediate/test-reports/ai-dev/`。详见 `docs/AIDevDoctor.md`。
- **构建/运行**：`generate_vs2022.bat`（premake5 生成 sln）；`build_editor.bat` / `build_sandbox.bat [Config] [Platform]`（缺 sln 自动生成，经 InvokeMSBuild.ps1）；`run.bat [editor|sandbox|all] [current|dx12|vulkan] [Config] [AppArgs...]`（临时改写 Engine.ini backend，退出后还原；`all` 为 Editor+Sandbox x DX12+Vulkan 矩阵）。

## 约束与不变式

- 所有脚本假定从仓库根调用；本地报告统一落 `Intermediate/test-reports/`，不提交。
- 基线文件（perf json、render golden png）提交入库；只能经 `-BlessBaseline` / `-BlessGolden` 更新，golden 更新前必须由用户确认画面正确。
- PerfGate FAIL 禁止提交；WARN 需在提交说明写明理由。RenderGate 跨后端 diff FAIL 视同 bug。
- AshImageDiff 输出格式（key=value + 退出码语义）是 RenderGate 解析契约，改动需同步 `RunRenderGate.ps1`。
- RenderGate 确定性依赖 Sandbox 抓帧约定（固定相机 (0,5,0)、隐藏 overlay、禁 TAA jitter），见 sandbox spec。

## 验证

对齐 `docs/VERIFY.md` "`scripts/` / `tools/`"行：

- 改 AIDevDoctor：`scripts/TestAIDevDoctor.ps1`
- 改 PerfGate：`scripts/TestRunPerfGate.ps1`（含 `-SelfTest` 路径）
- 改 RenderGate/AshImageDiff：完整跑一次 `RunRenderGate.bat` 确认 PASS
- 改构建链（premake/bat/InvokeMSBuild/SyncRuntimeArtifact）：删 sln 全新 `generate_vs2022.bat` + 构建，确认 PostBuild artifact 同步成功

## 历史

- `docs/sdd/SDD-2026-07-07-render-gate.md`（RenderGate + AshImageDiff）
- `docs/superpowers/specs/2026-05-18-perf-gate-design.md`（PerfGate，归档）
