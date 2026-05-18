# AshEngine Editor 任务规划

> 用途：把 Editor 工作拆成可执行、可验证、可并行的任务。

## 1. 规划目标

当前规划仍按三层推进：

- `M0`：收口骨架
  - 稳定主工作区、主面板、主服务、验证流程
- `M1`：补齐 P0 可用性
  - 让 Editor 从“能展示”进入“能持续编辑”
- `M2`：补齐最小内容闭环
  - 让 Scene、Inspector、Asset 工作流形成更稳定的闭环

## 2. 当前主要任务桶

### 2.1 架构收口

- 缩小 `EditorApplication`
- 缩小 `EditorContext`
- 抽离过大的 service 职责
- 收口共享 id、helper、shortcut 真源

### 2.2 工作流稳定性

- scene load / save / reload
- selection / undo / redo
- command / shortcut routing
- layout / session persistence

### 2.3 面板收口

- 拆大 panel 文件
- 把领域逻辑从 panel 下沉到 service / command
- 保持 panel 本地状态只服务 UI

### 2.4 性能与工程卫生

- 减少不必要拷贝
- 减少热路径临时分配
- 缩小头文件传播范围
- 清理重复 helper、重复常量、调试残留

### 2.5 UIContext 路径治理

- 审计活跃运行路径
- 记录原生 ImGui 遗留点
- 明确缺失原语应补到 `UIContext` 还是仅保留历史参考

## 3. 当前推荐优先级

### 3.1 P0

- `EditorApplication` / `EditorContext` 收口
- Scene / Inspector / SceneHierarchy 的命令边界
- shortcut / action / event 真源统一
- viewport 状态分层与主路径稳定

### 3.2 P1

- AssetBrowser / Console / Settings 工作流升级
- UIContext 缺口补齐与活跃路径审计
- 重复 helper / 常量 / 小型基础设施收口
- viewport 工具语义补齐：
  - `Move / Scale Gizmo`
  - scene 视口选中反馈降噪
  - scene/game 视图语义继续分离
  - 视口调试信息默认收敛，减少工作流噪音

### 3.3 P2

- Selection / Undo 模型升级
- Inspector 可扩展注册机制
- 多 viewport / 辅助工具的进一步扩展

## 4. 当前推荐并行拆分

- `A. Workspace / Viewport`
  - `App`、`Editor.*`、`ViewportPanel`、`EditorViewportService`
- `B. Scene / Inspector / SceneHierarchy`
  - `SceneHierarchyPanel`、`InspectorPanel`、`SceneService`、`SelectionService`、`UndoRedoService`
- `C. Asset / Console / Settings / Command`
  - `AssetBrowserPanel`、`ConsolePanel`、`AssetDatabaseService`、`CommandService`、`EditorSettingsService`
- `D. 文档 / 审计`
  - `docs`、UIContext 路径审计、验收清单整理

共享入口文件不要默认并行写：

- `project/src/editor/App/EditorApplication.*`
- `project/src/editor/Editor.*`
- `project/src/editor/Core/EditorContext.h`
- `project/src/editor/Core/EditorPanel.*`
- `project/src/editor/premake5.lua`

## 5. 任务拆分规则

- 一个任务只保留一个主目标。
- 结构整理和行为调整尽量拆开。
- 大规模目录搬迁、公共 API 改签名、用户侧行为变化不要混在一个任务里。
- 优先按文件所有权和模块边界拆任务，再按功能点拆。
- 如果一个任务已经同时碰到多个无关模块，先拆再做。

## 6. 完成定义

一个任务完成，至少满足：

- 代码或文档已落地
- `code review` 已做，或明确说明未做原因
- 必要验证已完成，或明确说明未验证项
- 如果行为、边界、规则变化了，对应文档已回写

建议统一记录：

```md
Smoke Record
- Scope:
- Build:
- Result: pass / fail / blocked
- Covered:
- Not Covered:
- Blockers:
```

## 7. 规划时不要忘记

- 当前主线优先保证可回归，不追求一次性重写
- Engine 缺口先记录，不默认直接侵入 `project/src/engine`
- 需要并行开发时，具体任务卡和冲突规则以 `docs/EditorParallelCollaboration.md` 为准

## 8. 当前活跃开发批次（2026-05-15）

- `EDT-P1-VP-Tooling`
  - 目标：
    - 完成 scene viewport 的基础变换工具可用性
    - 优先补 `Scale Gizmo`
    - 收敛过强的选中包围盒视觉权重
    - 继续把 scene 视口从“调试视图”推进到“日常编辑视图”
- 推荐落地顺序：
  1. `Scale Gizmo`
  2. 选中反馈降噪
  3. viewport 顶部工具栏与 overlay 信息收敛
    - 已完成首轮减法整理：
      - 默认关闭 stats / overlay
      - 高频操作留在 toolbar
      - 低频选项收进 `Options`
      - 操作提示改为聚焦/悬浮时显示的紧凑单行提示
  4. 旋转 gizmo 与更完整的 transform tool 体系
    - 进行中：
      - editor 侧补齐 `Rotate Gizmo`
      - 继续复用现有 command transaction / undo / viewport ray 主链
