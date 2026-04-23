# AshEngine Editor 文档导航

> 这份索引只整理和 `project/src/editor` 开发直接相关的文档。
>
> Engine 专属设计、运行时、底层实现文档不在这里展开。

## 1. 建议阅读顺序

### 1.1 新加入的 Editor 开发者

1. `docs/README.md`
2. `docs/editor/README.md`
3. `docs/EditorDeveloperGuide.md`
4. 如果涉及协作、提交流程或并行开发，再读 `docs/EditorContributorGuide.md`

### 1.2 继续做日常 Editor 功能

1. `docs/EditorDeveloperGuide.md`
2. 按功能补读对应模块进度文档
3. 如果任务涉及规划、验收或引擎配合，再读对应清单

### 1.3 主线程做拆任务、联调或验收

1. `docs/EditorDeveloperGuide.md`
2. `docs/EditorTaskPlanning.md`
3. `docs/EditorParallelCollaboration.md`
4. `docs/EditorAcceptanceLedger.md`
5. `docs/EditorFinalIntegrationChecklist.md`

## 2. 长期维护主文档

这些文档应视为当前 Editor 主线的优先入口。

- `docs/EditorDeveloperGuide.md`
  - Editor 长期维护主文档，记录当前架构、可用接口、目录边界和开发约束。
- `docs/EditorContributorGuide.md`
  - 协作、提交流程、构建验证、并行开发边界说明。

## 3. 当前活跃文档

这些文档服务于当前 Editor 主线开发，仍建议持续维护。

- `docs/EditorArchitectureRefactorPlan.md`
  - Editor 架构改造主方案，定义后续分阶段重构的目标结构、边界和执行顺序。
- `docs/EditorTaskPlanning.md`
  - 阶段目标、里程碑、任务批次与任务卡规划。
- `docs/EditorParallelCollaboration.md`
  - 主线程与子线程的职责划分、交付和验收规则。
- `docs/EditorAcceptanceLedger.md`
  - 统一验收台账入口，用于汇总本轮模块结论和阻塞。
- `docs/EditorFinalIntegrationChecklist.md`
  - 最终联调与冒烟执行清单。
- `docs/EditorToEngineGapChecklist.md`
  - 继续推进 Editor 时，需要引擎同学补充的接口/能力清单。

## 4. 模块进度文档

这些文档记录各模块的当前边界、已完成能力、风险和下一步计划。

- `docs/EditorProgress.WorkspaceViewport.md`
  - Workspace、Dockspace、Viewport、多视口扩展相关。
- `docs/EditorProgress.SceneInspector.md`
  - Inspector 与场景编辑共享服务相关。
  - 不再重复记录 `SceneHierarchyPanel` 的面板交互细节。
- `docs/EditorProgress.SceneHierarchy.md`
  - SceneHierarchy 面板交互、删除确认、reparent、层级编辑相关。
- `docs/EditorProgress.AssetConsole.md`
  - Asset Browser、Console、Settings、Command 相关。
- `docs/EditorProgress.UIContextAcceptance.md`
  - UIContext 运行路径验收与补缺进度。

## 5. 专题 / 边界文档

这些文档不一定每次都要读，但涉及对应主题时应优先补读。

- `docs/Editor.UIContextGapChecklist.md`
  - 审计 Editor 当前 UIContext 使用情况与缺口。
- `docs/EditorArchitectureAndRequirements.md`
  - 从主线代码出发整理的编辑器架构与需求基线。

## 6. 历史参考文档

这些文档更适合在需要追溯方案背景、历史决策或旧问题时再看，不作为日常开发的第一入口。

- `docs/EditorUIFacadeProposal.md`
  - 早期的 Editor UI 分层提案。
  - 当前以 `docs/EditorDeveloperGuide.md` 和 `docs/Editor.UIContextGapChecklist.md` 为准。
- `docs/EditorEngineFollowupChecklist.md`
  - 之前一轮 Editor 视角下的引擎补充跟进清单。
  - 当前引擎补缺以 `docs/EditorToEngineGapChecklist.md` 为准。
- `docs/CodeReview_DesignDefects_and_Risks.md`
  - 较大范围的设计问题和风险审查记录。
  - 属于阶段性审查快照，不代表当前最新状态。
- `docs/editor/legacy-scene-runtime/README.md`
  - 早期 editor 自持 scene/runtime 代码归档，避免再把死代码留在活跃源码树里。

## 7. 与引擎协作但非 Editor 主入口的参考

这些文档和 Editor 需求有关，但更偏引擎接口讨论或旧阶段草案，不建议作为 Editor 日常开发入口。

- `docs/ImGuiLayer.InterfaceDraft.md`
  - 历史接口草案，保留用于回溯早期引擎协作背景。
- `docs/ImGuiLayer.InterfaceDraft.h`
  - 与上面草案配套的接口草图，不是当前正式约定。

## 8. 后续维护约定

- 新增 Editor 文档时，优先先判断它属于：
  - 长期维护主文档
  - 当前活跃文档
  - 模块进度文档
  - 专题 / 边界文档
  - 历史参考文档
- 新文档创建后，同步更新本索引，避免再次回到“平铺堆文件名”的状态。
- 如果某份文档已经失效，不要继续把它放在“当前活跃文档”下，改到“历史参考文档”并注明原因。
