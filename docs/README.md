# AshEngine 文档索引与维护约定

## 先选入口

- Engine 开发：`docs/EngineDeveloperGuide.md`
- Editor 开发：先读 `docs/editor/README.md`，再读 `docs/EditorDeveloperGuide.md`

## Editor 文档导航

编辑器相关文档目前数量较多，且包含长期主文档、阶段规划、模块进度、联调清单和历史提案。

为减少大规模迁移带来的链接冲突，当前版本不整体移动旧文件，统一通过下面入口检索：

- 编辑器文档总索引：`docs/editor/README.md`
- 编辑器长期维护主文档：`docs/EditorDeveloperGuide.md`
- 编辑器协作与提交流程：`docs/EditorContributorGuide.md`

`docs/editor/README.md` 已按以下类型整理：

- 入门必读
- 当前活跃文档
- 模块进度文档
- Engine / Editor 边界文档
- 历史参考文档

## Engine 文档入口

这次没有整理 Engine 专属文档，仍保持原入口：

- Engine 长期维护主文档：`docs/EngineDeveloperGuide.md`
- Engine Runtime 需求：`docs/EngineRuntimeRequirements.md`
- Engine UIContext 说明：`docs/EngineUIContext.md`

## 维护规则

- 开始任何 Engine / Editor 开发前：
  - 先阅读 `docs/README.md`
  - 再阅读对应方向的主入口文档
  - 若是 Editor 任务，优先通过 `docs/editor/README.md` 找到本次需要补读的专题文档
- Engine 相关开发完成后：
  - 至少回写 `docs/EngineDeveloperGuide.md`
  - 若涉及专题，再同步更新对应专题文档
- Editor 相关开发完成后：
  - 至少回写 `docs/EditorDeveloperGuide.md`
  - 若涉及协作规则、阶段清单、模块进度或边界约束，再同步更新对应 Editor 专题文档
  - 若新增了 Editor 文档类型或推荐阅读顺序，也同步更新 `docs/editor/README.md`

## 使用建议

- 新加入的 Engine 开发者先看 `docs/EngineDeveloperGuide.md`
- 新加入的 Editor 开发者先看 `docs/editor/README.md`
- 需要具体实现细节时，再进入对应专题或模块文档
