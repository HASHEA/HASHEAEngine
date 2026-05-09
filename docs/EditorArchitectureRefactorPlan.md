# AshEngine Editor 架构改造方案

> 状态：
> - 当前主线改造方案
>
> 适用范围：
> - `project/src/editor`
>
> 不直接覆盖：
> - Engine 底层渲染架构
> - RHI / Graphics / Renderer 内部实现

---

## 1. 目标

当前 Editor 已经有可运行骨架，但中期风险很明确：

- `EditorApplication` 过重
- `EditorContext` 过宽
- `SceneService` 边界过大
- 快捷键、事件、命令模型还不够统一
- 大 panel 文件继续膨胀
- `ViewportService` 混合了 UI、持久化、运行态绑定

本方案的目标不是推倒重来，而是把主线逐步改造成：

- 边界更清楚
- 职责更单一
- 更适合并行开发
- 更容易回归验证
- 不破坏当前可用工作流

---

## 2. 当前已成立的骨架

当前主线已经成立的部分：

- `EditorApplication` 负责启动、更新、主循环编排
- `EditorPanel` 作为统一 panel 基类
- `SceneService` / `SelectionService` / `CommandService` / `UndoRedoService` 提供基础编辑能力
- `SceneHierarchy` / `Inspector` / `AssetBrowser` / `Viewport` 已形成基础工作流
- `EditorTreeWidget` 已可作为共享 widget 样板

因此方向不是重写，而是收口和拆分。

---

## 3. 改造总原则

- 保持功能连续可用。
- 小步提交，每一步都尽量可编译、可运行、可回归。
- 先拆编排层，再拆领域层。
- UI 与领域状态分离。
- 缩窄依赖面，不继续扩大 `EditorContext`。
- 可撤销编辑必须继续走 command 主链路。
- service 按领域拆，不按 panel 拆。
- coordinator 只做编排，不吞领域逻辑。
- 默认保持行为等价，除非任务明确允许改产品行为。
- 与 Engine 既有边界保持一致，尤其继续遵守 scene-driven viewport 主线。

---

## 4. 重构期间的冻结基线

除非任务明确允许调整，否则以下用户侧行为默认不改：

### 4.1 Workspace / Layout

- Editor 能启动进入主工作区
- 主菜单、Dockspace、各 panel 正常显示
- `Window -> Reset Layout` 正常
- Scene / Game viewport 的 panel open state 保持当前语义

### 4.2 Scene 工作流

- 新建场景后默认场景内容保持当前语义
- 重载场景后 SceneHierarchy / Inspector / Selection 正常刷新
- 保存场景仍沿用当前保存路径语义
- 切场景后 Undo/Redo 清空，Selection 重置为默认实体

### 4.3 SceneHierarchy

- 选择、Rename、Reparent、Delete、拖拽 `Before / After / Into` 行为不变
- root append drop slot 行为不变

### 4.4 Inspector

- Identity / Transform / Camera / Light / Mesh 仍可编辑
- Apply / Revert 语义不变
- 编辑仍进入 Undo/Redo

### 4.5 AssetBrowser

- 目录树、List / Icons、Search / Filter、双击激活、右键菜单语义不变

### 4.6 Viewport

- Scene / Game viewport surface 正常显示
- 尺寸同步、toolbar / overlays / stats、primary 逻辑不变

### 4.7 持久化与配置

- 不修改 Scene 文件格式
- 不修改已有 `EditorSettings` 字段语义
- 不修改 `ViewportLayout.json` 现有字段语义
- 不随意修改依赖持久化的 panel title / id

### 4.8 快捷键

以下快捷键默认冻结：

- `Ctrl+N`
- `Ctrl+R`
- `Ctrl+S`
- `Ctrl+Shift+R`
- `Ctrl+Shift+A`
- `Ctrl+Alt+A`
- `F2`
- `Ctrl+Shift+P`
- `Delete`
- `Ctrl+Z`
- `Ctrl+Y`
- `Ctrl+Shift+Z`
- `F5`
- AssetBrowser 内容区 `Enter` / `Backspace`

---

## 5. 重构核心不变量

- 当前活动场景只有一个真源对象
- Selection 只由 `SelectionService` 维护
- Undo/Redo 栈只由 `UndoRedoService` 维护
- viewport presentation 状态只由 `EditorViewportService` 维护
- 主 UI 继续走 `UIContext`
- 不允许为了重构方便绕开 command 链直接改 scene
- action shortcut 文案与真实触发规则最终必须来自同一真源
- 关键 id 保持稳定：
  - action id
  - panel id
  - viewport id
  - drag payload type
- service / panel 生命周期顺序必须可追踪

---

## 6. 当前主要问题

### 6.1 `EditorApplication` 过重

当前它同时承担：

- 服务初始化
- 上下文注入
- panel 创建与销毁
- 菜单绘制
- action 注册
- 场景切换与默认选择
- layout 持久化
- runtime scene presentation 同步

它已经是最明显的集中风险点。

### 6.2 `SceneService` 边界过宽

当前混合了：

- 场景基础编辑
- 加载 / 保存
- snapshot 捕获与恢复
- 组件 JSON 编解码
- 默认场景模板

后续必须拆领域责任。

### 6.3 `EditorContext` 是宽权限入口

它提升了开发速度，但带来：

- 面板隐藏耦合
- 依赖不透明
- 测试困难
- 后续多 session 更难拆

### 6.4 快捷键 / action / 事件体系割裂

当前存在：

- shortcut 文案与真实触发规则分离
- global shortcut 与 panel-scoped shortcut 分散
- 事件传播仍混合直接调用、局部 observer、轮询刷新

### 6.5 Viewport 状态未分层

当前 `EditorViewportService` 同时持有：

- 视口实例状态
- 展示配置
- 渲染同步状态
- 输出句柄
- 绑定句柄
- 持久化信息

后续扩展会继续膨胀。

---

## 7. 目标方向

建议把 Editor 主体逐步收口到下面几层：

- `EditorApplication`
  - 最外层生命周期入口
- `EditorBootstrap`
  - 一次性启动/关闭装配
- `EditorShell`
  - 主菜单、workspace host、dockspace、panel 管理
- `EditorSession`
  - 当前编辑会话总协调
- Domain Services
  - scene / asset / selection / viewport 等领域能力
- Panels
  - 只负责 UI 与交互

关键目标：

- `EditorApplication` 降级为入口
- Shell 负责“编辑器壳子”
- Session 负责“当前编辑会话”
- service 负责领域逻辑
- panel 负责显示与输入

---

## 8. 分阶段改造顺序

### 阶段 1：拆 `EditorApplication`

目标：

- 把菜单、layout、panel 生命周期、scene workflow 从 `EditorApplication` 中迁出

建议顺序：

1. `PanelManager`
2. `DockLayoutController`
3. `MainMenuController`
4. `SceneWorkflowCoordinator`

完成标志：

- `EditorApplication` 明显瘦身
- 壳层职责清楚
- 用户侧行为不变

### 阶段 2：拆 Scene 领域服务

目标：

- 缩窄 `SceneService`

优先顺序：

1. `SceneTemplateService`
2. `SceneSnapshotService`
3. `SceneHierarchyService`

完成标志：

- `SceneService` 文件体量下降
- Undo/Redo 仍稳定
- panel 不再复制领域规则

### 阶段 3：升级 Action / Shortcut / Event 基础设施

目标：

- action 元数据、shortcut 触发、event 通知收口

最小范围：

- `can_execute`
- shortcut 真源统一
- global / panel shortcut owner 清楚
- 最小可用 `EditorEventBus`

完成标志：

- 新增快捷键不再需要改多处分散逻辑
- 菜单显示与真实触发规则不再长期分叉

### 阶段 4：缩窄 panel 依赖

目标：

- 从宽 `EditorContext` 迁到窄 deps / frame context

建议迁移顺序：

1. `SceneHierarchyPanel`
2. `AssetBrowserPanel`
3. `InspectorPanel`
4. `ViewportPanel`
5. `ConsolePanel`

### 阶段 5：升级 Selection / Undo 模型

目标：

- 为多选、资产编辑器、图编辑器扩展打基础

### 阶段 6：重构 Viewport 分层

目标：

- 把 UI 状态、持久化状态、runtime render binding 分层

---

## 9. 每阶段的闸门

进入下一阶段前，至少满足：

### 9.1 结构闸门

- 新增类职责清楚
- 没有新增长期重复实现
- 过渡层若存在，明确是 forwarding / compatibility path

### 9.2 编译闸门

- 工程能重新生成
- Editor 目标能编译
- 新文件已接入构建

### 9.3 行为闸门

- 第 4 节冻结基线未被破坏
- 本阶段相关模块已回归
- 未引入明显连带回归

### 9.4 文档闸门

- 本文档已同步阶段状态
- 若存在过渡层，已记录后续移除条件

---

## 10. 最小回归清单

每完成一个阶段，至少手工检查：

1. 启动 Editor
2. 打开 `Scene Hierarchy`
3. 打开 `Inspector`
4. 打开 `Asset Browser`
5. 确认 `Scene / Game` viewport 有内容
6. 新建 root entity
7. 拖拽 entity 到 before / after / into
8. Rename entity
9. Delete 后 Undo / Redo
10. Inspector 改 Transform 后 Undo / Redo
11. Refresh Assets
12. Reset Layout
13. 验证 `Ctrl+S`、`Ctrl+Z`、`Ctrl+Y` / `Ctrl+Shift+Z`
14. 验证 AssetBrowser `Enter` / `Backspace`

---

## 11. 当前推荐优先级

按收益 / 风险比，推荐顺序：

1. 拆 `EditorApplication`
2. 抽 `SceneSnapshotService`
3. 抽 `SceneHierarchyService`
4. 升级 action / shortcut / event 基础设施
5. 缩窄 `EditorContext`
6. 升级 Selection / Undo
7. 重构 Viewport 分层

原因：

- `EditorApplication` 是当前最明显的集中风险点
- `SceneSnapshotService` 收益高且相对可控
- `SceneHierarchyService` 与当前主线工作高度相关

---

## 12. 非目标

本轮不追求：

- 一次性支持多文档
- 一次性做完整快捷键系统
- 一次性重构所有 UI 组件抽象
- 一次性做插件化框架
- 一次性推倒现有 Editor 主线

---

## 13. 当前结论

当前 Editor **可以继续演进，不需要推倒重来**。

真正要做的是：

- 控制集中化
- 明确层次边界
- 让 command、session、panel、domain service 各司其职

后续重构默认按本方案推进，但每一步都要以“行为不回退、验证可跟上”为前提。
