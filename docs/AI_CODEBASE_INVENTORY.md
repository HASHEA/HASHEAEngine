---
owner: huyizhou
last_reviewed: 2026-07-03
review_cycle: quarterly
status: active
---

# AI Codebase Inventory

一次性盘点文档：记录仓库的技术栈、可用命令、结构、边界与验证要求的当前事实。
后续维护以 `docs/CODEBASE_MAP.md` 与 `docs/VERIFY.md` 为准，本文仅在大盘点时更新。

## Stack

- Language: C++17（Engine / Editor / Sandbox），HLSL（shader，经 DXC 编译）
- Build system: Premake5 生成 VS2022 solution，MSBuild 构建
- Platform: Windows x64（Debug / Release / Dist 三种 configuration）
- Graphics: Vulkan 与 DX12 双后端同时编入，运行时经 `product/config/Engine.ini` 选择
- Test framework: 无通用单测框架；Sandbox 内置 `SandboxTestRegistry`（`project/src/sandbox/Tests/`）；验证以 PerfGate + smoke run 为主
- Scripting/tooling: PowerShell（`scripts/`）、批处理入口（仓库根目录 `*.bat`）

## Commands

| Purpose | Command | Works | Notes |
| --- | --- | --- | --- |
| 生成 sln | `generate_vs2022.bat` | 文档确认 | 调用仓库根目录 `premake5.exe vs2022` |
| 构建 Editor | `build_editor.bat [Debug\|Release] [x64]` | 文档确认 | 缺 sln 时自动先生成 |
| 构建 Sandbox | `build_sandbox.bat [Debug\|Release] [x64]` | 文档确认 | 同上 |
| 运行（推荐） | `run.bat editor\|sandbox [vulkan\|dx12] [Debug] [--smoke-test-seconds=N]` | 文档确认 | 会临时改写并恢复 Engine.ini 以切换后端；`run.bat all` 走矩阵 |
| 直接运行 | `product/bin64/<Config>-windows-x86_64/{Editor,Sandbox}.exe` | 文档确认 | EntryPoint 启动时把工作目录重置到仓库根 |
| 性能门禁 | `RunPerfGate.bat -Profile Standard [-SkipBuild] [-DryRun] [-BlessBaseline]` | 已验证（DryRun，2026-07-03） | Sandbox/Editor × Vulkan/DX12 矩阵，输出到 `Intermediate/test-reports/perf-gate/<ts>/` |
| AI 工作区诊断 | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode Report`（或 `-Mode ValidatePlan`） | 已验证（2026-07-03） | 只读；输出到 `Intermediate/test-reports/ai-dev/<ts>/` |
| 工具自测 | `scripts/TestAIDevDoctor.ps1`、`scripts/TestRunPerfGate.ps1` | 文档确认 | 验证两套工具自身的输出完整性 |

"文档确认" = 命令语义取自脚本源码与 `docs/EngineDeveloperGuide.md` / `docs/PerfGateUsageGuide.md`，本次盘点未逐一实跑。

## Structure

| Path | Responsibility | Risk |
| --- | --- | --- |
| `project/src/engine/Base/` | 日志、内存、断言、窗口、输入、序列化、时间、数据结构 | 中 |
| `project/src/engine/Graphics/` | RHI 抽象（`RHICommon.h`、`RHIResource.h`、`DynamicRHI.h`）、`Vulkan/`、`DirectX12/` 后端、`DXC/` shader 编译 | 高 |
| `project/src/engine/Function/` | Application、`Render/`（RenderGraph、SceneRenderer、各 Pass、材质）、`Scene/`、`Asset/`、UIContext、Gui、Diagnostics | 高（Render/RHI 相关） |
| `project/src/engine/Shaders/` | Engine HLSL shader 源码 | 高 |
| `project/src/editor/` | Editor：App/Core/Shell/Panels/Services/Widgets/ImGui | 中 |
| `project/src/sandbox/` | Sandbox：App/Demos/Tests | 低 |
| `product/` | 运行配置（`config/Engine.ini`）、运行期资产、日志、可执行输出（`bin64/`） | 配置高风险 |
| `scripts/` | 构建、同步、PerfGate、AIDevDoctor 脚本 | 中 |
| `tools/` | `ai-dev/`（规则 json）、`perf/`（PerfGate 基线） | 中 |
| `docs/` | 长期文档 + `superpowers/`（历史 plan/spec）| 低 |
| `_BUILD/`、`Intermediate/` | 构建中间产物、本地报告，不提交 | — |
| `KEnginePub/`、`RenderControl/` | 参考/外部库（CMake 体系），不属于当前引擎实现 | 不动 |

## Boundaries

| Module | May depend on | Must not depend on |
| --- | --- | --- |
| `engine/Base` | （无上层依赖） | Graphics、Function、渲染后端语义 |
| `engine/Graphics` | Base | Function、Editor |
| `engine/Function` | Base、Graphics | Editor、Sandbox |
| `editor/` | Function 层公共接口 | **Graphics 层（显式禁止）**、Vulkan/DX12 细节 |
| `sandbox/` | Function 层公共接口 | Graphics 层内部 |

## High-risk areas

| Path | Risk | Required review |
| --- | --- | --- |
| `engine/Graphics/RHICommon.h`、`RHIResource.h`、`DynamicRHI.h` | RHI 接口改动波及双后端 | 双后端构建 + PerfGate 全矩阵 |
| `engine/Function/Render/RenderGraph*` | 编译/barrier/生命周期错误阻断整个渲染 | PerfGate + validation 开启的运行 |
| `engine/Graphics/DXC/`、`engine/Shaders/` | shader 编译与绑定约定，双后端共用 | 双后端运行 + 视觉确认 |
| `product/config/Engine.ini` | 直接改变运行行为（后端/validation/DebugView） | 双后端 smoke run + 日志 |
| `premake5.lua` PostBuild 链（SyncRuntimeArtifact） | 失败会导致 stale DLL/DXC 隐患 | 全新构建验证 |
| `engine/Function/Application.*` | 启停生命周期影响所有目标 | Editor + Sandbox smoke run |

## Existing specs

| Document | Area | Status | Accurate |
| --- | --- | --- | --- |
| `docs/EngineDeveloperGuide.md` | Engine 分层/构建/RHI | active | 是（本次盘点核对） |
| `docs/specs/`（22 份模块/feature spec） | 长期现状规格基线 | active | 2026-07-04 建立；`RenderGraphAPISpec.md` 已迁入 `specs/modules/render-graph.md` 并删除 |
| `docs/EditorDeveloperGuide.md` 等 Editor 系列 | Editor 架构与规则 | active | 未逐一复核 |
| `docs/superpowers/specs/`（13 份，2026-04~06） | 材质/Deferred/RenderGraph/Shadow/Bloom/体积光等设计文档 | 历史设计文档 | **描述的是设计时点，不保证与当前实现一致**；Spec 基线阶段逐份甄别 |
| `docs/superpowers/plans/`（17 份） | 历史实现计划 | 已完成/过期 | 仅作历史参考，不应作为现状依据 |
| `docs/EditorProgress.*`、`*Plan*.md`、`*Checklist*.md` | 进度快照/计划 | 天然腐烂 | 不应作为现状依据 |

## Required validation

| Change type | Commands |
| --- | --- |
| 详见 `docs/VERIFY.md` 的变更矩阵 | — |
