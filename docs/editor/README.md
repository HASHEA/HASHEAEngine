# AshEngine Editor 文档导航

> 只整理和 `project/src/editor` 开发直接相关的文档。

## 1. 阅读顺序

1. `docs/specs/modules/editor.md` —— 架构边界、单一真源不变式、改动定位与生命周期约束、冻结快捷键
2. `docs/editor/EditorCodeStyleGuide.md` —— 改代码时直接执行的规范

## 2. 相关规格

- 场景画面与视口链路：`docs/specs/modules/scene.md`（ScenePresentationSubsystem）
- UI 能力边界：`docs/specs/modules/application.md`（UIContext 节）
- 变更设计（S1+）：`docs/sdd/`

## 3. 维护规则

- 新增 Editor 文档后，同步更新本索引与 `docs/README.md`。
- 已失效文档直接删除，考古走 git 历史。
- 本索引只分流，不重复展开各文档正文。
