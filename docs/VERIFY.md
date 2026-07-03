---
owner: huyizhou
last_reviewed: 2026-07-03
review_cycle: monthly
status: active
---

# Verification

按变更类型给出必须执行的验证。原则：**没有对应验证证据的改动不算完成**。
当前视觉正确性仍依赖人眼确认——这是已知最大缺口，自动化方案见文末"待自动化"。

## Fast path

```bat
build_editor.bat Debug        :: 或 build_sandbox.bat Debug；缺 sln 自动生成
run.bat sandbox vulkan Debug --smoke-test-seconds=5
```

## Change matrix

| Change | Commands | 补充人工检查 |
| --- | --- | --- |
| 纯文档 / 注释 | `git diff --check` | — |
| 渲染 Pass / shader / 材质 | 构建 + `RunPerfGate.bat -Profile Standard`（覆盖 Sandbox/Editor × Vulkan/DX12） | **双后端视觉确认**（当前人眼；对照 RenderDebugView）；检查 `product/logs` 无 validation 报错 |
| RHI 接口 / 双后端实现 | 构建 + PerfGate Standard；Engine.ini 开启 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 smoke | 双后端行为一致性 |
| RenderGraph 核心（compile/barrier/lifetime） | 同 RHI 级别 | 关注 barrier/lifetime 相关 validation 输出 |
| Scene / Asset / Application 生命周期 | 构建 + `run.bat all Debug --smoke-test-seconds=5`（全矩阵 smoke） | Editor 打开默认场景操作一遍 |
| Editor 面板 / UI | 构建 + `run.bat editor` 手动过一遍改动路径 | 面板打开、交互、无报错日志 |
| `product/config/Engine.ini` | 双后端各 smoke 一次 + 查日志 | 确认开关生效 |
| 性能敏感路径 | PerfGate Standard，`FAIL` 必须修，`WARN` 需说明判断 | 对比 `summary.md` 趋势 |
| `scripts/` / `tools/` | `scripts/TestAIDevDoctor.ps1`、`scripts/TestRunPerfGate.ps1`（按所改工具） | — |
| `premake5.lua` / 构建链 | 删 sln 后全新 `generate_vs2022.bat` + 构建；确认 PostBuild artifact 同步成功 | `product/bin64` 下 DXC/validation dll 是新的 |

不确定改动属于哪类时，运行 `scripts/AIDevDoctor.ps1 -Mode ValidatePlan`，它会根据 dirty paths 生成验证计划。

## Environment

- Runtime: Windows x64，VS2022 工具链，仓库根有 `premake5.exe`
- 工作目录: 可执行程序自动重置到仓库根；脚本假定从仓库根调用
- 报告输出: `Intermediate/test-reports/`（perf-gate、ai-dev），本地生成物不提交
- 基线: `tools/perf/perf_gate_baselines.json`；确认新性能水位后 `-BlessBaseline` 更新

## Failure handling

- 构建失败：先看是否 PostBuild artifact 同步失败（stale DLL 隐患），再看编译错误
- PerfGate `FAIL`：必须修复后重跑；不允许带 FAIL 提交
- PerfGate `WARN`：允许提交，但必须在提交说明里写明判断理由
- validation / debug-layer 报错：视同 bug，定位根因，禁止靠关闭 validation 绕过
- 渲染结果异常：用 `[RenderDebugView]` 分通道定位，必要时 RenderDoc 抓帧

## 待自动化（能力缺口）

| 缺口 | 现状 | 目标 |
| --- | --- | --- |
| 视觉正确性 | 人眼确认 | headless 渲染 N 帧 → dump 截图 → golden image（SSIM）回归 |
| 双后端一致性 | 人工分别跑、肉眼对比 | 同 scene 双后端自动渲帧 + 感知 diff，不一致即 bug |
| 渲染 bug 取证 | 手动开 RenderDoc | headless 自动出 capture，供 AI 经 RenderDoc MCP 分析 |

这三项落地后，上表中所有"人工检查"列应逐步替换为对应自动命令。
