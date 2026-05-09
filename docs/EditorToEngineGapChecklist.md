# AshEngine Editor -> Engine 缺口清单

## 1. 当前范围

这份清单现在只保留**仍然需要引擎层支持**的缺口。

已经在这轮由编辑器侧直接补掉、不再作为引擎阻塞项的内容包括：

- `UIContext` drag-drop payload/source/target API
- engine-owned key / modifier / chord 抽象
- `EditorTreeWidget` 所需的低层 item rect / open state / 定制绘制支撑

因此下面不再重复把这些列成“等待引擎同学”的待办。

---

## 2. 仍然需要引擎补的核心缺口

| 优先级 | 模块 | 缺口 | 类型 |
|---|---|---|---|
| P0 | Viewport | 正式场景渲染入口（按 scene/camera/target） | 引擎接口 |
| P0 | Viewport | object picking / ID buffer / 命中查询 | 引擎接口 |
| P0 | Inspector | 组件属性元数据 / 最小反射描述 | 引擎接口 |
| P0 | Inspector | 组件修改的 snapshot / apply / restore 边界 | 引擎接口 |
| P0 | Hierarchy | 稳定 Scene / Entity facade 与层级操作入口 | 引擎接口 |
| P1 | Viewport | gizmo / debug overlay 正式渲染接口 | 引擎接口 |
| P1 | Inspector | Add / Remove Component 统一高层入口 | 引擎接口 |
| P1 | Inspector | 资源引用解析与赋值高层入口 | 引擎接口 |
| P1 | Hierarchy | 多选 / 范围选择 / 键盘导航所需的场景级语义 | 引擎接口 |
| P1 | Cross-Module | scene dirty / revision / replace / reload 通知语义 | 引擎接口 |
| P2 | Viewport | 视口统计 / backend / 帧时间查询 | 引擎接口 |
| P2 | Inspector | 更通用的属性编辑器注册元数据 | 引擎接口 |

---

## 3. Viewport 缺口

### 3.1 P0：正式场景渲染入口

当前编辑器已经完成：

- Scene/Game 视口 surface 展示
- editor 侧 viewport presentation 管理
- 通过 `UIContext` 显示离屏结果

但仍缺引擎正式提供的高层渲染入口，例如：

- `render_scene(scene, camera, target)`
- `render_world(world, viewport_desc)`
- 或等价的 Function 层 facade

至少要明确：

- 输入 scene/world
- 输入 camera 或 view-projection
- 输出 `RenderTarget`
- resize 时的资源重建语义

### 3.2 P0：拾取与命中查询

仍缺：

- object picking
- ID buffer
- 屏幕坐标到实体命中查询

这会直接限制：

- viewport 点击选中
- gizmo 绑定
- Inspector 联动

### 3.3 P1：gizmo / debug overlay 渲染入口

后面如果要做 Unity 风格的：

- 移动/旋转/缩放 gizmo
- bounds
- debug lines

仍需要引擎提供 editor overlay pass 或统一 debug submit facade。

### 3.4 P2：视口统计接口

建议后续补：

- backend 名称
- 当前 RT 分辨率
- CPU/GPU 帧时间
- draw call / pass 统计

---

## 4. Inspector 缺口

### 4.1 P0：组件属性元数据 / 最小反射描述

当前 `Inspector` 已经有：

- Name
- Transform
- Camera
- Light
- Mesh

这些 section，但仍偏手工拼装。

建议引擎补最小元数据：

- 组件类型名
- 字段名
- 字段类型
- getter / setter 或等价访问边界
- 基础数值 / bool / enum / `vec2/vec3/vec4`

### 4.2 P0：组件修改 snapshot / apply / restore

当前 Editor 侧已经在统一修改边界，但组件级别仍缺正式承载层。

建议引擎补：

- snapshot
- apply
- restore
- 或统一属性事务接口

这样 Undo/Redo、Prefab、属性记录才有真正一致的底层语义。

### 4.3 P1：Add / Remove Component 高层入口

建议引擎补：

- `add_component(entity, type)`
- `remove_component(entity, type)`
- `can_add / can_remove`

### 4.4 P1：资源引用解析与赋值入口

现在 `UIContext` drag-drop 已经有了，Inspector 后面真正缺的是“拖过来以后怎么落到组件上”的高层引擎接口，例如：

- `assign_mesh(entity, asset_handle)`
- `assign_material(entity, slot, asset_handle)`
- `assign_texture(entity, property, asset_handle)`

---

## 5. Hierarchy 与 Scene Workflow 缺口

### 5.1 P0：稳定 Scene / Entity facade

当前 `Hierarchy` 已经有编辑器侧命令边界：

- create
- rename
- reparent
- delete

但长期仍应该落到更稳定的引擎 facade 上。

建议引擎补：

- 稳定 entity id
- 父子层级读写接口
- create / delete / rename / reparent 高层入口
- 最小场景读写 facade

### 5.2 P1：多选 / 范围选择 / 键盘导航的场景语义

UI 侧原语这轮已经补了，剩下真正缺的是：

- 多实体选择的场景语义
- 批量操作边界
- selection order / anchor 语义

### 5.3 P1：scene dirty / revision / replace / reload 语义

这轮 Editor 内部已经把：

- new scene
- load scene
- reload active scene

收到了统一的 scene workflow 协调器里。

但长期仍希望引擎补齐更稳定的生命周期语义：

- dirty
- revision
- replaced
- reloaded

这样各面板不必靠局部重置去猜 scene 是否被替换。

---

## 6. 建议补齐顺序

如果按“最影响编辑器继续推进”的顺序排，建议是：

1. Viewport 正式场景渲染入口
2. Viewport picking / ID buffer
3. Inspector 组件属性元数据
4. Inspector snapshot / apply / restore
5. 稳定 Scene / Entity facade
6. Add / Remove Component 高层入口
7. 资源引用解析与赋值入口
8. scene dirty / revision / reload / replace 语义

---

## 7. 给引擎同学的记录模板

```md
Engine Gap Record
- Module:
- Priority:
- Gap Type: engine interface / runtime facade / scene lifecycle
- Current Editor Symptom:
- Proposed Engine Support:
- Dependent Editor Task:
- Owner:
- Status:
```
