# AshEngine Editor -> Engine 缺口清单

> 用途：
>
> - 汇总当前 `Viewport / Inspector / Hierarchy` 继续推进时，仍缺哪些引擎接口或 `UIContext` 能力
> - 供主线程统一验收后回抛给引擎同学
> - 不替代模块进度文档，只抽取跨模块、可跟进的缺口

---

## 1. 范围与口径

本清单只记录两类缺口：

1. `引擎接口缺口`
   - Editor 继续实现会被卡住，或会迫使 Editor 长期停留在占位实现
2. `UIContext 缺口`
   - 继续做 Unity 风格编辑工作流时，若不补到 `UIContext`，后续很容易诱发活跃运行路径回退到原生 `ImGui::`

不在本清单重复记录：

- 已经在 Editor 内部可自行解决的纯面板结构问题
- 各模块自己的实现细节待办
- 历史 `ImGui` 参考文件本身

优先级口径：

- `P0`
  - 不补会直接限制下一轮编辑器推进
- `P1`
  - 不补不会立刻卡死，但会让交互长期停留在保守版本
- `P2`
  - 后续增强项，先不阻塞主线

---

## 2. 总览

| 优先级 | 模块 | 缺口 | 类型 |
|---|---|---|---|
| P0 | Viewport | 按场景 / 相机 / 目标 RT 的正式渲染入口 | 引擎接口 |
| P0 | Viewport | object picking / ID buffer / 命中查询 | 引擎接口 |
| P0 | Inspector | 组件属性元数据 / 最小反射描述 | 引擎接口 |
| P0 | Inspector | 组件修改的快照 / apply / restore 边界 | 引擎接口 |
| P0 | Hierarchy | 稳定 Scene / Entity facade 与层级操作入口 | 引擎接口 |
| P0 | Hierarchy | 拖拽式 reparent 所需 drag-drop payload 能力 | UIContext |
| P1 | Viewport | gizmo / debug overlay 正式渲染接口 | 引擎接口 |
| P1 | Viewport | 快捷键 / 修饰键 / 输入捕获查询 | UIContext |
| P1 | Inspector | 资产引用槽拖放 | UIContext |
| P1 | Inspector | Add / Remove Component 的统一高层入口 | 引擎接口 |
| P1 | Hierarchy | 多选 / 范围选择 / 键盘导航支撑 | UIContext |
| P1 | Hierarchy | 场景 dirty / revision / 变更通知 | 引擎接口 |
| P1 | Cross-Module | scene lifecycle revision / reset / reload 通知语义 | 引擎接口 |
| P2 | Viewport | 视口统计 / 后端 / 帧时间查询 | 引擎接口 |
| P2 | Inspector | 更通用的属性编辑器注册所需元数据扩展 | 引擎接口 |

---

## 3. Viewport 缺口

### 3.1 P0：正式场景渲染入口

当前 `Viewport` 已有独立离屏 `RenderTarget` 与 `UIContext` 采样路径，但仍缺正式的编辑器场景渲染入口。

建议引擎补齐：

- `render_scene(scene, camera, target)`
- 或 `render_world(world, viewport_desc)`
- 或等价的 Function 层 facade

至少应明确：

- 输入：
  - scene / world
  - camera 或 view-projection 参数
  - 输出目标 `RenderTarget`
- 输出：
  - 颜色目标
  - 深度目标约束
  - 尺寸变化后的重建语义

当前价值：

- 让 Viewport 脱离 demo renderer 路径
- 让 scene / game 视图拥有稳定高层渲染语义

### 3.2 P0：拾取与命中查询

当前缺口：

- object picking
- ID buffer
- 屏幕坐标到场景实体的命中查询

建议引擎补齐：

- 基于 viewport 坐标的实体拾取接口
- 或可供 Editor 查询的 picking 结果缓冲

当前价值：

- 支撑 viewport 选中对象
- 为 gizmo、Inspector 联动打基础

### 3.3 P1：gizmo / debug overlay 渲染接口

当前缺口：

- 缺少供编辑器调用的 gizmo / debug overlay 正式渲染入口

建议引擎补齐：

- editor overlay pass
- debug lines / bounds / handles 的统一提交接口

### 3.4 P1：快捷键 / 修饰键 / 输入捕获查询

当前缺口属于 `UIContext`：

- 焦点窗口是否消费键盘
- `Ctrl / Shift / Alt` 修饰键查询
- 面板级快捷键门控

当前价值：

- 场景视口输入
- `W / E / R` 等工具快捷键
- 避免 viewport 与 text input 抢输入

### 3.5 P2：视口统计接口

建议后续补齐：

- 当前 backend
- 当前 RT 分辨率
- CPU / GPU 帧时间
- draw call / pass 统计

---

## 4. Inspector 缺口

### 4.1 P0：组件属性元数据 / 最小反射描述

当前 `Inspector` 已做出 `Name / Transform / Camera / Light / Mesh` 的分区，但仍缺统一元数据来源。

建议引擎补齐：

- 组件类型名
- 字段名
- 字段类型
- getter / setter 或等价访问边界
- 基础数值、bool、enum、`vec2/vec3/vec4`

当前价值：

- 让 Inspector 从“写死面板”走向“可扩展属性编辑”

### 4.2 P0：组件修改的快照 / apply / restore 边界

当前缺口：

- Camera / Light / Mesh 仍缺可复用的命令化承载层
- 缺少统一组件前后值快照与恢复边界

建议引擎补齐：

- 组件级 snapshot
- apply / restore
- 或统一的属性修改事务接口

当前价值：

- 支撑 Undo / Redo
- 避免 Inspector 长期保留组件级整块直写

### 4.3 P1：Add / Remove Component 的统一高层入口

当前缺口：

- 组件增删仍缺统一 facade，后续容易在 Editor 侧分散直写

建议引擎补齐：

- `add_component(entity, type)`
- `remove_component(entity, type)`
- 必要时附带能力查询和失败原因

### 4.4 P1：资产引用槽拖放

当前缺口属于 `UIContext`：

- drag-drop payload
- 目标槽接收与校验

当前价值：

- Mesh / Material / Texture 引用槽
- AssetBrowser -> Inspector 拖放赋值

### 4.5 P2：通用属性编辑器扩展元数据

后续若要把 `Inspector` section 注册机制外放，建议进一步补：

- 分类标签
- 可见性条件
- 只读条件
- 属性分组与排序

---

## 5. Hierarchy 缺口

### 5.1 P0：稳定 Scene / Entity facade 与层级操作入口

当前 `Hierarchy` 已具备编辑器侧的 create / rename / reparent / delete 命令边界，但从长期看仍需要更稳定的引擎 facade。

建议引擎补齐：

- 稳定 entity id
- 父子层级读写接口
- create / delete / rename / reparent 高层入口
- 最小场景读写 facade

当前价值：

- 减少 Editor 自己长期承载场景骨架
- 为真实 scene runtime 对接留出统一边界

### 5.2 P0：拖拽式 reparent 所需 drag-drop payload

当前 `Hierarchy` 的 reparent 已有弹窗版，但若要升级为更自然的层级拖拽，仍缺 `UIContext` 的 drag-drop 能力。

建议补齐：

- 拖拽源 payload
- 目标节点接收
- 合法性反馈
- drop 结果提交

### 5.3 P1：场景 dirty / revision / 变更通知

当前缺口：

- SceneHierarchy / Inspector / UndoRedo 之间还缺更稳定的变更通知语义

建议引擎补齐：

- 场景 dirty 标记
- revision / version
- 变更通知或查询接口

当前价值：

- 刷新面板
- 保存提示
- 减少“修改后靠局部猜测刷新”的风险

### 5.5 P1：scene lifecycle revision / reset / reload 通知语义

当前 Editor 已经在自身层面把：

- `startup scene load`
- `new scene`
- `reload active scene`

统一收口到 scene-change helper，但长期仍希望引擎补齐更稳定的生命周期通知。

建议引擎补齐：

- scene revision / version
- reset / reload / replaced 语义区分
- 面向 Editor 的场景变更通知或查询接口

当前价值：

- 让 Inspector / Hierarchy / Viewport 不必只靠 selection 重建来推导 scene 是否被替换
- 为未来更细粒度的面板刷新与缓存失效策略打基础

### 5.4 P1：多选 / 范围选择 / 键盘导航

当前缺口属于 `UIContext`：

- tree / list 多选辅助
- 范围选择
- 键盘导航与焦点移动

当前价值：

- 层级批处理
- 更接近 Unity 风格工作流

---

## 6. 建议先补顺序

如果只排“最影响编辑器继续推进”的顺序，建议是：

1. Viewport 的正式场景渲染入口
2. Viewport 的 picking / ID buffer
3. Inspector 的组件属性元数据 / 最小反射描述
4. Inspector 的组件 snapshot / apply / restore
5. Hierarchy 的稳定 Scene / Entity facade
6. `UIContext` 的 drag-drop payload
7. `UIContext` 的快捷键 / 修饰键 / 输入捕获查询

---

## 7. 统一回抛给引擎同学的记录模板

```md
Engine Gap Record
- Module:
- Priority:
- Gap Type: engine interface / UIContext
- Current Editor Symptom:
- Proposed Engine Support:
- Dependent Editor Task:
- Owner:
- Status:
```
