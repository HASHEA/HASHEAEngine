---
owner: huyizhou
last_reviewed: 2026-07-03
review_cycle: monthly
status: active
---

# Verification

按变更类型给出必须执行的验证。原则：**没有对应验证证据的改动不算完成**。
视觉正确性与双后端一致性由 RenderGate 自动回归（`RunRenderGate.bat`，SDD-2026-07-07-render-gate）；RenderDoc 自动取证仍是缺口，见文末"待自动化"。

## Fast path

```bat
build_editor.bat Debug        :: 或 build_sandbox.bat Debug；缺 sln 自动生成
run.bat sandbox vulkan Debug --smoke-test-seconds=5
RunRenderGate.bat             :: 渲染改动必跑：双后端 golden SSIM 回归 + 跨后端 diff
```

## Change matrix

| Change | Commands | 补充人工检查 |
| --- | --- | --- |
| 纯文档 / 注释 | `git diff --check` | — |
| 渲染 Pass / shader / 材质 | 构建 + `RunRenderGate.bat`（双后端 golden SSIM 回归 + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard` | 检查 `product/logs` 无 validation 报错；预期内的画面变化经用户确认后 `-BlessGolden` 更新基线 |
| RHI 接口 / 双后端实现 | 构建 + `RunRenderGate.bat` + PerfGate Standard；Engine.ini 开启 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 smoke | 跨后端 diff FAIL 视同 bug |
| RenderGraph 核心（compile/barrier/lifetime） | 同 RHI 级别 | 关注 barrier/lifetime 相关 validation 输出 |
| Scene / Asset / Application 生命周期 | 构建 + `run.bat all Debug --smoke-test-seconds=5`（全矩阵 smoke） | Editor 打开默认场景操作一遍 |
| Editor 面板 / UI | 构建 + `run.bat editor Debug --smoke-test-seconds=5`（自动启动/关闭冒烟）+ `run.bat editor` 手动过一遍改动路径 | 面板打开、交互、无报错日志 |
| `product/config/Engine.ini` | 双后端各 smoke 一次 + 查日志 | 确认开关生效 |
| 性能敏感路径 | PerfGate Standard，`FAIL` 必须修，`WARN` 需说明判断 | 对比 `summary.md` 趋势 |
| `scripts/` / `tools/` | `scripts/TestAIDevDoctor.ps1`、`scripts/TestRunPerfGate.ps1`（按所改工具） | — |
| `premake5.lua` / 构建链 | 删 sln 后全新 `generate_vs2022.bat` + 构建；确认 PostBuild artifact 同步成功 | `product/bin64` 下 DXC/validation dll 是新的 |

不确定改动属于哪类时，运行 `scripts/AIDevDoctor.ps1 -Mode ValidatePlan`，它会根据 dirty paths 生成验证计划。

## Environment

- Runtime: Windows x64，VS2022 工具链，仓库根有 `premake5.exe`
- 工作目录: 可执行程序自动重置到仓库根；脚本假定从仓库根调用
- 报告输出: `Intermediate/test-reports/`（perf-gate、render-gate、ai-dev），本地生成物不提交
- 基线: 性能 `tools/perf/perf_gate_baselines.json`（`-BlessBaseline` 更新）；渲染 golden `tools/render/goldens/<scene>/<backend>.png`（`RunRenderGate.bat -BlessGolden` 更新，仅限用户确认画面正确后）

## RenderGate（渲染回归门禁）

- 机制：Sandbox `--rhi=<backend> --smoke-test=5000 --dump-frame=<png>` 抓最后一帧 → `AshImageDiff` 与 golden 做 SSIM 对比；另做 Vulkan vs DX12 跨后端 diff
- 阈值：golden 回归 0.995（实测同后端噪声底 0.999996）；跨后端 0.99（实测 0.999843）
- 确定性保证：抓帧模式固定初始相机 (0, 5, 0)、隐藏引擎 overlay、禁用 TAA 亚像素抖动；抓帧时机由资产流送 quiesce 信号驱动（连续 32 帧无 pending 渲染资产即抓帧退出），`--smoke-test=N` 仅为超时保底（此前纯帧数等待在 Vulkan 高帧率下会抓到未加载完的空场景）
- FAIL 处理：看报告目录里的 heatmap 定位差异区域；确属预期改动才允许 `-BlessGolden`

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
| RenderGate 场景覆盖 | 仅默认 Sandbox 场景 | 多场景 golden 矩阵（不同光照/后处理组合） |
| Editor UI 自动化 | 仅启动冒烟 + 纯人工走查 | headless/脚本化面板交互 smoke（打开各面板、执行代表性命令、断言无报错） |
| ~~RenderGate 流送等待~~ | ✅ 资产流送 quiesce 信号驱动抓帧，帧数上限仅超时保底（SDD-2026-07-07-render-gate-streaming-signal） | — |
