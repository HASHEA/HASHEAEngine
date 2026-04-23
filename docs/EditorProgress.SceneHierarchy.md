# Editor 模块进度：SceneHierarchy

> 说明：
>
> - 本文档只记录 `SceneHierarchyPanel` 的面板交互与层级编辑状态。
> - Inspector 与共享场景编辑服务的状态请查看 `docs/EditorProgress.SceneInspector.md`。

## 1. 模块范围

- `project/src/editor/Panels/SceneHierarchyPanel.*`

## 2. 当前任务卡

- `EDT-R3-C SceneHierarchy 删除确认与独立进度文档`

## 3. 本轮实际写入

- `project/src/editor/Panels/SceneHierarchyPanel.h`
- `project/src/editor/Panels/SceneHierarchyPanel.cpp`
- `docs/EditorProgress.SceneHierarchy.md`

## 4. 已完成能力

- Scene tree 展示与单选
- `Add Root`
- `Add Child`
- `Rename`
- `Reparent`
- `Delete Selected`
- `Delete Selected` 已增加最小可用确认交互：
  - 通过 `UIContext` modal 二次确认
  - 确认框会显示当前待删除实体名称
- 上述高频操作当前都通过命令边界触发，不再直接由按钮回调写 scene
- `Rename Entity` 已与 Inspector 统一复用共享命令定义：
  - `project/src/editor/Core/EntityCommands.*`

## 5. 当前限制

- `Delete Restore` 当前仍然是最小子树快照恢复：
  - 覆盖 `Name / Transform / Camera / Light / Mesh / children`
  - 不是通用序列化快照层
  - 恢复后实体 ID 不保证和删除前一致
- `Reparent` 当前只有弹窗版：
  - 通过 `UIContext` modal 选择父节点
  - 还没有拖拽式 reparent
- 目前仍未实现：
  - 多选
  - 批量删除
  - 层级排序调整
  - 撤销历史可视化
- `Create / Delete / Reparent` 仍只在 Hierarchy 内部定义，尚未继续抽到共享命令层

## 5.1 Scene Lifecycle Reset / Delete Restore 口径

- `new scene`
- `startup scene load`
- `reload active scene`

上述路径现在统一先清空 selection / undo-redo，再重新选默认实体。

当前影响：

- 层级树数据直接跟随 active scene 刷新
- 待 rename / 待 reparent / 待 delete 的临时 UI 状态仍以面板本地生命周期为准
- 删除恢复仍维持“最小子树快照恢复”口径，不承诺恢复原始 entity id

## 6. 本轮自测记录

- 已做：
  - 静态检查删除入口已改为“先确认，再执行删除命令”
  - 静态检查删除确认框显示待删除实体名
  - 静态检查运行时路径仍然只走 `UIContext`
- 未做：
  - 未执行 `premake`
  - 未编译
  - 未运行 Editor 做冒烟

## 7. 待主线程验收点

- 验收 `Delete Selected` 现在是否必须经过确认框
- 验收确认框显示的实体名是否正确
- 验收删除确认后的行为是否仍走原有命令链和 `Undo/Redo`
- 验收 `new/load/reload scene` 后层级树、selection、Inspector 是否同步刷新
- 验收 `Delete Restore` 作为“最小子树快照恢复”是否可接受进入下一轮
- 验收 `Reparent` 目前只有弹窗版是否符合本轮范围
- 统一执行：
  - `premake`
  - `Editor Debug` 编译
  - 运行时冒烟

## 8. 最近更新时间

- 2026-04-18
