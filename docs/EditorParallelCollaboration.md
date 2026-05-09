# AshEngine Editor 并行协作说明

> 用途：约束主线程和多个子线程并行开发 `project/src/editor` 时的分工、写入范围、验收和升级流程。

## 1. 角色

### 1.1 主线程 / 协调者

负责：

- 拆任务
- 指定写入范围
- 管理共享文件 owner
- 收口冲突
- 统一验收
- 统一测试
- 决定合并顺序

### 1.2 子线程 / 执行者

负责：

- 只在授权范围内实现
- 更新对应模块进度文档
- 提交本模块最小自测结果
- 报告阻塞、越界需求、Engine 缺口

不负责：

- 跨模块长期架构决策
- 私自接管共享入口文件
- 最终统一验收结论

## 2. 默认模块划分

### 2.1 子线程 A：Workspace / Viewport

- 允许范围：
  - `project/src/editor/App/**`
  - `project/src/editor/Editor.*`
  - `project/src/editor/Panels/ViewportPanel.*`
  - `project/src/editor/Services/EditorViewportService.*`
- 进度文档：
  - `docs/EditorProgress.WorkspaceViewport.md`

### 2.2 子线程 B：Scene / Inspector / SceneHierarchy

- 允许范围：
  - `project/src/editor/Panels/SceneHierarchyPanel.*`
  - `project/src/editor/Panels/InspectorPanel.*`
  - `project/src/editor/Services/SceneService.*`
  - `project/src/editor/Services/SelectionService.*`
  - `project/src/editor/Services/UndoRedoService.*`
  - `project/src/editor/Core/EditorSelection.h`
- 进度文档：
  - `docs/EditorProgress.SceneInspector.md`
  - `docs/EditorProgress.SceneHierarchy.md`

### 2.3 子线程 C：Asset / Console / Settings / Command

- 允许范围：
  - `project/src/editor/Panels/AssetBrowserPanel.*`
  - `project/src/editor/Panels/ConsolePanel.*`
  - `project/src/editor/Services/AssetDatabaseService.*`
  - `project/src/editor/Services/CommandService.*`
  - `project/src/editor/Services/EditorSettingsService.*`
- 进度文档：
  - `docs/EditorProgress.AssetConsole.md`

### 2.4 文档 / 审计工作

- 可单独拆给只读或文档线程：
  - `docs/**`
  - `docs/Editor.UIContextGapChecklist.md`
  - `docs/EditorProgress.UIContextAcceptance.md`

## 3. 共享文件规则

以下文件默认视为共享入口：

- `project/src/editor/App/EditorApplication.*`
- `project/src/editor/Editor.*`
- `project/src/editor/Core/EditorContext.h`
- `project/src/editor/Core/EditorPanel.*`
- `project/src/editor/premake5.lua`

规则：

- 没有任务卡显式授权时，不要改共享入口文件。
- 需要改共享入口文件时，先提交越界申请，再由主线程指定唯一 owner。
- 不允许两个子线程同时无边界修改同一共享文件。

## 4. 每轮任务卡必须包含

1. 任务编号 / 标题
2. 模块
3. 本轮目标
4. 本轮任务清单
5. 优先级
6. 是否允许并行
7. 前置依赖
8. 允许修改范围
9. 禁止修改范围
10. 预计交付物
11. 子线程自测要求
12. 主线程统一回归测试项
13. 执行人
14. 验收人
15. 预计合并顺序

没有任务卡时，子线程只做只读分析、文档整理或阻塞梳理，不默认扩实现范围。

## 5. 交付要求

子线程交付时至少写清：

- 改了哪些文件
- 本轮覆盖了什么能力
- 自测跑了什么
- 哪些验证没跑
- 当前风险 / 缺口 / 阻塞
- 是否需要 Engine 配合
- 是否影响共享语义：
  - scene lifecycle
  - shared viewport state
  - shared commands

同时更新对应 `EditorProgress.*.md`。

## 6. 验收与合并

主线程统一按下面顺序收口：

1. `code review`
2. `premake`
3. `MSBuild`
4. `smoke`

默认合并顺序：

1. 共享基础设施
2. service 变化
3. panel 变化
4. 最终编排 glue

如果 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败：

- 记为“环境 / 权限阻塞”
- 不直接记成代码失败
- 备注“建议提权后重跑”

## 7. 必须升级给主线程的情况

- 需要改共享入口文件
- 需要跨模块写代码
- 两个子线程要写同一文件
- 发现新的 Engine 接口缺口
- 发现任务会改变用户侧行为
- 发现当前模块职责和任务卡不一致

升级时至少写清：

- 问题标题
- 归属类型
  - 编辑器实现
  - Engine 接口
  - 构建问题
  - 环境 / 权限问题
  - 设计待定
- 当前影响
- 临时绕过方案
- 需要主线程决定的事项

## 8. 一句话原则

子线程负责在授权范围内实现并更新模块文档，主线程负责拆任务、管边界、做统一验收和最终收口。
