# AshEngine 文档索引与维护约定

## 长期维护主文档

- Engine：`EngineDeveloperGuide.md`
- Editor：`EditorDeveloperGuide.md`

## 当前专题文档

- Engine UI 分层：`EngineUIContext.md`
- Editor UI 分层提案：`EditorUIFacadeProposal.md`
- 历史设计问题记录：`CodeReview_DesignDefects_and_Risks.md`

## 维护规则

- Engine 相关开发完成后：
  - 至少回写 `EngineDeveloperGuide.md`
  - 若涉及专题，再同步更新对应专题文档
- Editor 相关开发完成后：
  - 至少回写 `EditorDeveloperGuide.md`
  - 若涉及 Engine / Editor 边界，再同步更新 `EngineDeveloperGuide.md`

## 使用建议

- 新加入的 Engine 开发者先看 `EngineDeveloperGuide.md`
- 新加入的 Editor 开发者先看 `EditorDeveloperGuide.md`
- 某个专题存在细节时，再继续看对应专题文档
