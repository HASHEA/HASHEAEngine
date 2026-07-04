# AshEngine 文档入口

> 用途：先决定这次任务该读哪组文档，再进入具体专题。

## 0. 所有任务的公共基线

- `AGENTS.md`（仓库根）
  - AI/协作者规则：命令、架构边界、SDD 分级、变更与验证要求
- `docs/CODEBASE_MAP.md`
  - 仓库导航：入口、目录职责、核心流程、依赖方向、常见任务
- `docs/VERIFY.md`
  - 按变更类型的验证矩阵，改完必须执行
- `docs/specs/`
  - 长期现状规格：模块 spec + feature spec；动某模块/feature 前先读对应 spec，索引见 `docs/specs/README.md`
- `docs/sdd/`
  - 变更设计文档（SDD），S1 起需要；模板见 `docs/sdd/TEMPLATE.md`；Done 后结论回写 `docs/specs/`
- `docs/AI_CODEBASE_INVENTORY.md`
  - 一次性盘点快照，仅大盘点时更新

## 1. 先选入口

- Engine 任务：
  - `docs/EngineDeveloperGuide.md`
- Editor 任务：
  - `docs/editor/README.md`
  - `docs/EditorDeveloperGuide.md`
  - `docs/EditorCodeStyleGuide.md`
- Editor 协作、提交流程、验收：
  - `docs/EditorContributorGuide.md`
  - `docs/EditorParallelCollaboration.md`

## 2. Editor 文档分类

### 2.1 主入口文档

- `docs/EditorDeveloperGuide.md`
  - 当前 Editor 架构、目录边界、运行时约束
- `docs/EditorCodeStyleGuide.md`
  - 改代码时直接执行的命名、性能、头文件、日志等规则
- `docs/EditorFileResponsibilities.md`
  - Editor 每个源码文件的职责和修改边界
- `docs/EditorContributorGuide.md`
  - 可改范围、验证流程、交付要求

### 2.2 当前活跃文档

- `docs/EditorArchitectureRefactorPlan.md`
  - 当前主线架构改造方向与冻结基线
- `docs/EditorTaskPlanning.md`
  - 当前任务桶、优先级、拆任务方式
- `docs/EditorParallelCollaboration.md`
  - 并行协作、任务卡、冲突升级规则
- `docs/EditorToEngineGapChecklist.md`
  - 需要 Engine 补的接口与能力

### 2.3 模块进度文档

- `docs/EditorProgress.WorkspaceViewport.md`
- `docs/EditorProgress.SceneInspector.md`
- `docs/EditorProgress.SceneHierarchy.md`
- `docs/EditorProgress.AssetConsole.md`
- `docs/EditorProgress.UIContextAcceptance.md`

## 3. Engine 文档入口

- `docs/EngineDeveloperGuide.md`
- `docs/EngineInterfaceRequirements.md`
- `docs/EngineRuntimeRequirements.md`
- `docs/specs/modules/render-graph.md`（RenderGraph API 契约，原 `docs/RenderGraphAPISpec.md` 已迁入）
- `docs/EngineUIContext.md`

## 4. 维护规则

- 开始任务前先读入口文档，不要直接在 `docs/` 下盲搜。
- Editor 任务默认先从 `docs/editor/README.md` 找到本次需要补读的专题文档。
- 修改公共行为、边界或验证方式时，优先更新最接近真源的那一份文档，不要在多份文档里重复写同一规则。
- 新增 Editor 文档后，必须同步更新 `docs/editor/README.md`。
- 失效文档直接删除，不继续保留历史入口。
- 保持主入口文档短、直接、可执行；避免再积累仅用于追溯的历史文档。
