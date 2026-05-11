# AshEngine Editor 开发指南

> 适用范围：
> - `project/src/editor`
>
> 这份文档只保留当前开发时必须直接遵守的结构、边界和验证规则。

## 1. 先记住的硬边界

- Editor 主工作区只看：
  - `project/src/editor`
  - `docs`
- Editor 可以依赖：
  - `project/src/engine/Function`
  - `project/src/engine/Base` 中已稳定的基础设施
- Editor 不应直接依赖：
  - `project/src/engine/Graphics`
  - RHI 后端实现细节
  - `KEnginePub`
- 运行时 Editor UI 继续走 `UIContext`。
- 不要在活跃运行路径重新引入：
  - `ImGui::`
  - `imgui.h`
  - `imgui_internal.h`

## 2. 当前目录分工

- `project/src/editor/App`
  - 启动、关闭、主循环编排、workspace 壳层
- `project/src/editor/Core`
  - 共享上下文、panel 基类、选择/命令基础类型
- `project/src/editor/Services`
  - Scene、Selection、UndoRedo、Viewport、Asset、Settings 等领域服务
- `project/src/editor/Panels`
  - SceneHierarchy、Inspector、Viewport、AssetBrowser、Console 等面板
- `project/src/editor/Widgets`
  - 可复用 UI widget
- `project/src/editor/Shell`
  - 菜单、workspace、状态栏等壳层编排
- `project/src/editor/Shaders`
  - Editor 专属 HLSL

## 3. 当前主线结构

- `EditorApplication`
  - 负责启动、关闭、装配、主循环编排
- `Editor`
  - 构造函数只能保存轻量配置，不得调用 `HLog*` 或初始化依赖 Engine runtime 的对象。
  - `EditorApplication` 的启动必须放在 `Application::_on_startup()` 之后，此时日志、窗口、RHI、UIContext 等 Engine runtime 已完成初始化。
  - `EditorApplication` 的关闭必须通过 `Application::_on_shutdown()` 对称执行，析构函数只保留幂等兜底清理。
- `EditorPanel`
  - 负责 UI 展示、输入采集、把动作转给 service / command
- `Service`
  - 负责领域逻辑，不负责 panel 展示
- `Command`
  - 负责可撤销编辑状态变更
- `Coordinator / Controller`
  - 负责跨多个 service 的流程编排

当前已经成立的骨架：

- `SceneHierarchy`、`Inspector`、`AssetBrowser`、`Viewport` 是主工作流面板
- `SceneService`、`SelectionService`、`CommandService`、`UndoRedoService`、`EditorViewportService` 是主链路服务
- `EditorTreeWidget` 可作为共用 widget 样板

- 改了 Editor 代码后，至少保证 `Editor` 目标仍可构建。
- 影响以下行为时，必须做运行时验证：
  - startup / shutdown
  - dockspace / layout
  - scene load / save / reload
  - selection / undo / redo
  - shortcut / command routing
  - viewport 展示与尺寸同步
  - settings / persistence

## 8. 文档回写要求

- 改了架构边界、目录职责、运行时约束，更新本文档。
- 改了代码规范，更新 `docs/EditorCodeStyleGuide.md`。
- 改了协作、验证、提交流程，更新 `docs/EditorContributorGuide.md`。
- 改了主线重构目标或阶段顺序，更新 `docs/EditorArchitectureRefactorPlan.md`。
- 改了模块现状、风险、下一步，更新对应 `EditorProgress.*.md`。
