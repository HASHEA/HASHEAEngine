# AshEngine Editor 文档导航

> 只整理和 `project/src/editor` 开发直接相关的文档。

## 1. 建议阅读顺序

### 1.1 日常 Editor 开发

1. `docs/EditorDeveloperGuide.md`
2. `docs/EditorCodeStyleGuide.md`
3. 按模块补读对应进度文档

### 1.2 新加入 Editor 或切换到陌生模块

1. `docs/README.md`
2. `docs/editor/README.md`
3. `docs/EditorDeveloperGuide.md`
4. `docs/EditorCodeStyleGuide.md`
5. 若涉及协作、提交或验收，再读 `docs/EditorContributorGuide.md`

### 1.3 主线程做拆任务、联调或验收

1. `docs/EditorDeveloperGuide.md`
2. `docs/EditorContributorGuide.md`
3. `docs/EditorTaskPlanning.md`
4. `docs/EditorParallelCollaboration.md`

## 2. 主入口文档

- `docs/EditorDeveloperGuide.md`
  - 当前 Editor 架构、层次边界、运行时约束
- `docs/EditorCodeStyleGuide.md`
  - 改代码时可直接执行的代码规范
- `docs/EditorFileResponsibilities.md`
  - Editor 每个源码文件的职责和修改边界
- `docs/EditorContributorGuide.md`
  - 修改范围、验证流程、交付要求

## 3. 当前活跃文档

- `docs/EditorArchitectureRefactorPlan.md`
  - 当前主线重构目标、阶段顺序、冻结基线
- `docs/EditorTaskPlanning.md`
  - 当前任务桶、优先级、拆任务方式
- `docs/EditorParallelCollaboration.md`
  - 主线程 / 子线程分工、任务卡、冲突升级规则
- `docs/EditorToEngineGapChecklist.md`
  - Engine 侧缺口记录

## 4. 模块进度文档

- `docs/EditorProgress.WorkspaceViewport.md`
  - Workspace、Dockspace、Viewport、多视口相关
- `docs/EditorProgress.SceneInspector.md`
  - Scene / Inspector 共享编辑链路
- `docs/EditorProgress.SceneHierarchy.md`
  - SceneHierarchy 面板交互与命令化进度
- `docs/EditorProgress.AssetConsole.md`
  - Asset Browser、Console、Settings、Command 相关
- `docs/EditorProgress.UIContextAcceptance.md`
  - UIContext 路径审计与验收进度

## 5. 专题 / 边界文档

- `docs/Editor.UIContextGapChecklist.md`
  - 当前 UIContext 缺口与审计结论
- `docs/EditorArchitectureAndRequirements.md`
  - Editor 架构与需求基线
- `docs/ScenePresentationSubsystemGuide.md`
  - scene-driven viewport 主路径约束

## 6. 维护规则

- 新增 Editor 文档时，先判断它属于：
  - 主入口文档
  - 当前活跃文档
  - 模块进度文档
  - 专题 / 边界文档
- 新文档创建后，同步更新本索引。
- 已失效文档直接删除，不继续保留“历史入口”。
- 保持本索引可检索、可分流，不在这里重复展开各文档正文。
