# AshEngine Agent Core v0 设计

## 1. 背景

AshEngine 后续希望具备更强的 AI / Agent 结合能力：用户可以用自然语言指导引擎生成可运行工程、场景、材质、灯光、交互和验证任务，并能通过后续自然语言持续调整细节。

本设计不把目标限定为某个具体产品展示、关卡或游戏类型，而是先建立可复用的 Agent 底座。具体 demo、CLI、Editor 面板和真实模型接入都应建立在该底座之上。

## 2. 设计目标

Agent Core v0 的目标是建立一套稳定的中间层，让自然语言驱动的生成过程可以被表达、检查、计划、执行和回滚。

核心目标：

- 在 `project/src/engine/Agent/` 下建立独立 Engine layer。
- `Agent/` 与 `Base/`、`Function/`、`Graphics/` 在目录上平级。
- 依赖方向保持为 `Agent -> Function -> Graphics`，禁止 Agent 直接依赖 Graphics 内部类型。
- Core v0 只定义语义模型、能力模型、计划模型、工具注册抽象、事务记录和观察摘要。
- Core v0 不接真实 LLM，不实现 CLI，不实现 Editor UI，不直接修改 `Scene` / `Asset` / `Material`。
- 后续所有自然语言生成能力都先落到结构化 `ExperienceGraph` / `GenerationPlan`，再通过受控工具执行。

## 3. 非目标

Core v0 明确不做以下事项：

- 不让 AI 直接生成或修改任意 C++ 代码。
- 不让 Agent 直接 include 或操作 `Graphics/`、Vulkan、DX12、descriptor、pipeline、command buffer、barrier 等内部对象。
- 不在第一期接入远程 LLM、本地 LLM、ONNX、DirectML 或其他推理后端。
- 不实现命令行产品形态；CLI 后续只作为 `Agent/` 的薄入口。
- 不实现 Editor 面板；Editor 后续只通过 Agent-facing API 调用该层能力。
- 不在运行时主循环中加入每帧 Agent 服务。
- 不把 Product Showcase、ArchViz、Puzzle Room、Arena 等样例写死成 Core 能力。

## 4. 层级与依赖

推荐目录：

```text
project/src/engine/
├── Base/
├── Function/
├── Graphics/
└── Agent/
    ├── Core/
    ├── Tools/
    ├── Planning/
    ├── Observation/
    ├── Providers/
    └── Recipes/
```

依赖规则：

```text
Agent
  -> Base
  -> Function
      -> Graphics
```

说明：

- `Agent/Core/` 应尽量保持纯数据结构与计划逻辑，只依赖 `Base` 或 C++ 标准库。
- `Agent/Tools/` 是后续唯一允许通过 `Function` 修改引擎状态的模块。
- `Agent/Observation/` 可以通过 `Function` facade 获取 scene、asset、render、validation 的摘要。
- `Agent/Providers/` 只做模型来源适配，不应影响 Core schema。
- `Agent/Recipes/` 只表达样例或类型配方，不应成为 Core 的硬依赖。

## 5. Core v0 模块

### 5.1 ExperienceGraph

`ExperienceGraph` 是自然语言生成目标的稳定中间表示。

它描述：

- 生成目标：prototype、visual demo、showcase、game slice、tooling task。
- 场景结构：scene、zone、landmark、object group、spawn point。
- 对象语义：hero object、environment object、interactable、camera target。
- 视觉风格：mood、palette、lighting intent、material intent、composition intent。
- 交互语义：hotspot、trigger、state change、objective、feedback。
- 技术约束：target backend、frame budget、asset budget、validation scope。
- 验证要求：build、run、screenshot、log scan、scene sanity、backend matrix。

第一期只需要提供可序列化的数据结构和基础 builder，不需要完整自然语言解析。

### 5.2 CapabilityGraph

`CapabilityGraph` 描述 Agent 可组合的生成能力。

能力示例：

- `camera.first_person`
- `camera.third_person_orbit`
- `camera.product_turntable`
- `layout.room_graph`
- `layout.showroom`
- `lighting.visual_director`
- `material.variant_set`
- `interaction.hotspot`
- `gameplay.lock_and_key`
- `validation.backend_smoke`
- `observation.screenshot`

Genre / Recipe 不应是最底层单位。它们只是 capabilities 的组合，例如：

```text
ProductShowcase = layout.showroom + camera.product_turntable + lighting.visual_director + material.variant_set
PuzzleRoom      = layout.room_graph + interaction.trigger + gameplay.lock_and_key + validation.objective_reachable
TopDownArena    = camera.top_down + gameplay.wave_spawn + interaction.pickup + validation.win_condition
```

### 5.3 GenerationPlan

`GenerationPlan` 是可检查的有序执行计划。

它包含：

- plan id、名称、来源 prompt 摘要。
- 目标 `ExperienceGraph` 版本。
- 有序 step 列表。
- 每个 step 对应一个注册工具调用。
- step 的风险等级、依赖、预期输出、dry-run 描述。
- 执行前置条件和验证步骤。

第一期计划模型只需要支持顺序执行；后续再扩展依赖图、并行步骤和条件分支。

### 5.4 ToolRegistry

`ToolRegistry` 只声明工具，不直接实现复杂业务。

工具描述包括：

- tool name。
- tool category。
- 参数 schema。
- 输出 schema。
- 是否支持 dry-run。
- 是否支持 apply。
- 是否需要用户确认。
- 风险等级。
- 所需 capability。

第一期示例工具可以只注册声明：

- `scene.create`
- `scene.add_entity`
- `scene.set_transform`
- `asset.reference`
- `material.create_instance`
- `render.set_debug_view`
- `validation.run_profile`

真实实现后续放在 `Agent/Tools/`。

### 5.5 AgentTransaction

`AgentTransaction` 记录一次 plan 执行产生的结构化变更。

它应支持：

- dry-run 结果记录。
- apply 结果记录。
- 每个 step 的状态：pending、skipped、applied、failed、rolled_back。
- 文件变更摘要。
- 引擎状态变更摘要。
- rollback 所需数据。
- 错误列表和诊断信息。

Core v0 只需要定义 transaction 数据结构和状态机，不要求实现完整文件级回滚。

### 5.6 ObservationSummary

`ObservationSummary` 是运行观察结果的统一输入格式，用于后续 patch planning。

它描述：

- build / run / validation outcome。
- 日志摘要。
- shader compile error 摘要。
- validation / debug layer 摘要。
- frame stats。
- scene stats。
- screenshot / capture 路径。
- Agent 识别出的 issue 列表。

Core v0 只定义结构，不负责真正采集。采集后续由 `Agent/Observation/` 通过 Function facade 和脚本入口完成。

## 6. Tool 执行原则

后续 `Agent/Tools/` 必须遵守以下规则：

- 每个工具都应支持 dry-run，至少能说明将修改什么。
- 修改引擎状态必须走 `Function` 层 facade。
- 工具不允许直接访问 `Graphics/` 内部对象。
- 涉及文件修改时必须记录文件路径、变更类型和 rollback 信息。
- 高风险工具必须要求显式确认。
- 工具执行结果必须能写入 `AgentTransaction`。

## 7. Graphics 边界

Agent 不直接操控 Graphics。

如果 Agent 需要渲染信息，应由 Function 层提供窄 facade，例如：

- render backend 名称。
- frame stats。
- RenderGraph pass 摘要。
- debug view 切换。
- screenshot / capture request。
- shader compile error 摘要。
- GPU timing 摘要。
- validation / debug-layer 日志摘要。

这些 facade 可以后续设计在：

```text
project/src/engine/Function/Render/RenderDiagnostics.*
project/src/engine/Function/Render/RenderDebugControl.*
project/src/engine/Function/Render/RenderCaptureRequest.*
```

Agent 不应直接访问：

- Vulkan / DX12 native handle。
- descriptor set / root signature。
- command buffer。
- pipeline object。
- barrier plan。
- framebuffer / render pass cache。
- backend resource lifetime。

## 8. 第一阶段落地建议

第一阶段实现顺序建议：

1. 新增 `project/src/engine/Agent/Core/`。
2. 定义 `ExperienceGraph`、`CapabilityGraph`、`GenerationPlan`、`ToolRegistry`、`AgentTransaction`、`ObservationSummary`。
3. 提供基础 JSON 序列化 / 反序列化。
4. 提供轻量自测：构造一个 ProductShowcase-style graph，生成计划，验证 schema round-trip。
5. 不接真实工具执行。
6. 不接 CLI。
7. 不更新 Editor。

该阶段完成后，下一阶段可以添加：

- `Agent/Tools/SceneTools` 最小实现。
- `ash-agent` CLI 薄入口。
- 规则驱动 recipe：ProductShowcase、VisualSceneDemo、PuzzleRoom。
- Sandbox 验证入口。

## 9. 验证策略

Core v0 主要是数据结构和计划模型，验证重点不是图形后端运行，而是 schema 稳定性。

建议验证：

- Engine Base/self-test 中加入 Agent Core round-trip 测试，或新增独立 Core 单元测试入口。
- 构造最小 `ExperienceGraph`。
- 注册一组 mock tools。
- 生成 `GenerationPlan`。
- 生成 dry-run `AgentTransaction`。
- 序列化再反序列化后保持关键字段一致。

当后续 `Agent/Tools/` 开始真实修改 Scene、Asset、Material 或 Render config 时，再按共享 Engine 路径要求执行 Sandbox / Editor、Vulkan / DX12 验证矩阵。

## 10. 文档维护

当该设计进入实现后，需要同步更新：

- `README.md`：当前状态、架构概览和文档入口。
- `docs/EngineDeveloperGuide.md`：新增 Agent layer 的职责、依赖边界、运行入口和验证要求。
- 后续若接入 Editor 面板，再更新 `docs/EditorDeveloperGuide.md`。

Core v0 实现前，本设计只作为专题设计文档，不代表主干已经具备 Agent runtime 能力。
