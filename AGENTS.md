# Agent Instructions

## Project

HASHEAEngine（AshEngine）：C++17 图形引擎，Vulkan/DX12 双后端，Premake5 + MSBuild，Windows x64。
Engine（DLL）分 Base / Graphics / Function 三层，Editor 与 Sandbox 是其上的可执行程序。

## Read first

- 仓库导航：`docs/CODEBASE_MAP.md`（入口、目录、流程、依赖方向、常见任务）
- 验证要求：`docs/VERIFY.md`（按变更类型的验证矩阵，改完必须执行）
- 现状规格：`docs/specs/`（模块 spec + feature spec，动某模块/feature 前读对应 spec）
- 文档路由：`docs/README.md`；Engine 细节 `docs/EngineDeveloperGuide.md`；Editor 细节 `docs/EditorDeveloperGuide.md`

## Commands

```bat
generate_vs2022.bat                                   :: premake 生成 sln
build_editor.bat Debug / build_sandbox.bat Debug      :: 构建
run.bat editor|sandbox|all [vulkan|dx12] [Debug] [--smoke-test-seconds=N]
RunPerfGate.bat -Profile Standard                     :: 性能门禁（全矩阵）
RunRenderGate.bat                                     :: 渲染门禁：双后端 golden SSIM 回归 + 跨后端 diff（渲染改动必跑）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

## Architecture rules

- 依赖方向：`Base ← Graphics ← Function ← Editor/Sandbox`，禁止反向
- Editor / Sandbox 禁止依赖 Graphics 层或任何 Vulkan/DX12 细节；UI 交互走 `UIContext`
- RHI（`DynamicRHI` / `RHIResource`）改动必须双后端等价实现，禁止单后端特化泄漏到 Function 层
- 跨模块只依赖公共接口，禁止 import 其他模块 internal
- 禁止为单一调用点造抽象；新抽象需至少两个真实调用点或 SDD 明确要求

## SDD rules

变更按风险分级，级别决定仪式量：

| 级别 | 场景 | 要求 |
| --- | --- | --- |
| S0 | 文档、注释、局部小修 | commit 说明即可 |
| S1 | 单模块 feature / bugfix（如单个 Pass 的修复） | Mini SDD（`docs/sdd/`，一页以内） |
| S2 | 跨模块、RenderGraph API、RHI 接口、材质/shader 绑定约定、场景数据模型 | 标准 SDD，**经用户批准后才能动代码** |
| S3 | 架构调整（新后端、线程模型、资产管线重构） | 标准 SDD + ADR + 分阶段计划 |

模板：`docs/sdd/TEMPLATE.md`。SDD 完成后其结论回写对应长期 spec（`docs/specs/`），SDD 本身归档。
**禁止从一句聊天需求直接跳到大范围实现。**

## Change rules

- 动代码前读 `docs/CODEBASE_MAP.md` 的对应条目和相邻实现
- diff 保持小而聚焦；refactor 与行为变更禁止混在同一笔提交
- bugfix 先定位根因，提交说明必须写根因与影响范围
- 渲染改动禁止只在单一后端验证
- 不新增第三方依赖，除非 SDD 批准
- validation / debug-layer 报错视同 bug，禁止靠关闭 validation 绕过

## Validation

按 `docs/VERIFY.md` 的变更矩阵执行；交付时说明跑过哪些验证、结果如何。
PerfGate `FAIL` 禁止提交；`WARN` 需写明判断理由。渲染改动必须跑 `RunRenderGate.bat`；FAIL 时先看 heatmap 定位，确属预期画面变化且经用户确认后才允许 `-BlessGolden` 刷新基线。

## High-risk paths

| Path | Rule |
| --- | --- |
| `project/src/engine/Graphics/`（RHI + 双后端 + DXC） | S2 起步；双后端构建 + PerfGate 全矩阵 + validation 开启 |
| `project/src/engine/Function/Render/RenderGraph*` | S2 起步；关注 barrier / lifetime validation |
| `product/config/Engine.ini` | 改动需双后端 smoke + 日志确认 |
| `premake5.lua` 及 PostBuild 链 | 全新构建验证 artifact 同步 |
| `tools/perf/perf_gate_baselines.json` | 仅在用户确认新水位后经 `-BlessBaseline` 更新 |
| `tools/render/goldens/` | 仅在用户确认画面正确后经 `RunRenderGate.bat -BlessGolden` 更新 |
| `docs/` 长期文档 | 代码行为变化必须同步更新对应文档，过期文档标记 superseded |
