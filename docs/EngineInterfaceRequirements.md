# Engine Interface Requirements

> 面向 Editor 后续开发和重构。
>
> 只记录 Editor 仍需要 Engine 提供、稳定或继续收口的公共接口；已经具备的能力只作为现状基线，不再写成长篇待办。

## 使用规则

- Editor 默认只依赖 `project/src/engine/Function/**` 和 `project/src/engine/Base/**` 的稳定接口。
- Editor 不直接依赖 `project/src/engine/Graphics/**`、RHI 后端实现、`KEnginePub/**`。
- 如果为了 Editor 补 Engine 接口，Engine 侧改动必须包在：
  - `// editor begin 修改原因：...`
  - `// editor end`
- Engine 接口只补稳定 facade / 通用能力，不把 Editor 私有流程塞进 Engine。
- 新接口优先放 Function 层 facade；不要让 Editor 直接穿透到 renderer、registry、asset loader 内部。

## 当前已具备

| 能力 | 当前入口 | 状态 |
| --- | --- | --- |
| Scene 视口输出 | `ScenePresentationSubsystem::create_output()` / `create_view_binding()` | 已支持 Editor offscreen viewport。 |
| 显式矩阵相机 | `SceneCameraSource::Override` / `SceneViewCameraOverride` | Editor Scene 视口已使用，不再需要 preview camera entity。 |
| Scene 查询 | `Function/Scene/SceneQuery.*` | 已支持 world bounds、screen ray、CPU AABB ray cast。 |
| AssetId 实例化 facade | `instantiate_asset(Scene&, AssetDatabase&, AssetId, SceneInstantiationDesc)` | 已覆盖 `Model` / `Prefab`，未覆盖 `Mesh`。 |
| Scene 层级 facade | `Scene::create_entity()` / `destroy_entity()` / `reparent_entity()` / sibling index | Hierarchy 命令已有可用底座。 |
| 组件读写 | `Entity::read_component()` / `write_component()` / typed add/set/remove | Inspector 与命令系统已有底层边界。 |
| 组件元数据 | `get_scene_component_descriptor()` / `get_scene_enum_descriptor()` | 已有最小属性描述，可继续扩展。 |
| Scene dirty/version | `Scene::is_dirty()` / `get_change_version()` / `mark_clean()` | 已有轮询式状态，缺事件语义。 |
| Debug draw 基础能力 | `DebugDrawService::draw_line/box/circle/cone/axes()` | 已有全局 frame-local 线框队列和 SceneRenderer overlay pass。 |

## 仍需补齐

| 优先级 | 缺口 | 当前症状 | 推荐处理 |
| --- | --- | --- | --- |
| P1 | `AssetType::Mesh` 接入 `instantiate_asset(AssetId)` | Mesh 拖入场景仍要 Editor fallback 创建实体再挂 `MeshComponent`。 | 在现有 facade 内扩展 Mesh 分支。 |
| P1 | 统一 scene drop point helper | Viewport 拖拽落点规则散在 Editor：先 ray cast，再地面，再相机前方。 | Engine 提供 `find_scene_drop_point()` 或 `project_ray_to_plane()`。 |
| P1 | Scene change event 语义 | Editor 只能轮询 dirty/version，scene replace/reload/component/hierarchy 变更仍靠局部重置。 | 增加 `SceneChangeKind` 或事件快照。 |
| P2 | GPU ID buffer picking | CPU AABB picking 精度有限，复杂模型/遮挡下误差大。 | Scene viewport 提供 ID buffer 或 render picking facade。 |
| P2 | DebugDraw per-viewport / depth 语义 | 当前 DebugDraw 是全局队列，缺 viewport 作用域和 depth/xray 控制。 | 增加 viewport-scoped submit 或 overlay context。 |
| P2 | 组件元数据增强 | 现有元数据能描述基础字段，但缺 editor hint、range、asset ref 类型、只读等信息。 | 扩展 `ScenePropertyDesc` 的编辑器提示字段。 |
| P2 | 通用 Add/Remove Component facade | 目前有 typed add/remove 和 `write_component()`，但 UI 动态添加仍不够统一。 | 提供按 `SceneComponentType` 的 `add/remove/can_add/can_remove`。 |
| P3 | Viewport stats facade | Editor 状态栏/调试信息仍难稳定读取 backend、RT 尺寸、帧时间、draw calls。 | Function 层提供只读 viewport/render stats。 |

## 接口建议

### P1. Mesh 资源实例化

目标：Asset Browser 拖拽 `Mesh` / `Model` / `Prefab` 走同一个 Engine facade。

```cpp
Entity instantiate_asset(
    Scene& scene,
    AssetDatabase& database,
    AssetId assetId,
    const SceneInstantiationDesc& desc = {});
```

要求：

- `Mesh`：创建单实体，设置 `MeshComponent.asset_path` / `mesh_index`，应用 `SceneInstantiationDesc`。
- `Model`：保留当前模型层级实例化。
- `Prefab`：保留当前 prefab / ashasset 实例化。
- 失败时返回 invalid `Entity`，并保留可诊断日志。

### P1. Scene Drop Point

目标：Viewport、Sandbox、测试工具复用同一套“资源投放到场景”的位置规则。

```cpp
bool project_ray_to_plane(
    const SceneRay& ray,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal,
    glm::vec3& outHitPoint);
```

或：

```cpp
bool find_scene_drop_point(
    const Scene& scene,
    AssetDatabase& database,
    const SceneRay& ray,
    glm::vec3& outWorldPosition);
```

要求：

- 优先命中 scene query。
- 未命中时回退到默认世界地面。
- 再失败时回退到相机前方固定距离。

### P1. Scene Change Event

目标：让 SceneHierarchy、Inspector、Viewport、Undo/Redo 能根据明确事件刷新，而不是猜测 scene 是否被替换。

```cpp
enum class SceneChangeKind : uint8_t
{
    EntityAdded = 0,
    EntityRemoved,
    HierarchyChanged,
    ComponentChanged,
    SceneReplaced,
    SceneReloaded,
    DirtyStateChanged
};

struct SceneChangeEvent
{
    SceneChangeKind kind = SceneChangeKind::ComponentChanged;
    EntityId entityId = 0;
    SceneComponentType componentType = SceneComponentType::Name;
    uint64_t changeVersion = 0;
    bool dirty = false;
};
```

要求：

- `changeVersion` 单调递增。
- replace/reload 必须能让 Editor 清理旧 selection、draft、viewport binding。
- component/hierarchy 变更要足够细，避免所有 panel 每帧全量刷新。

### P2. GPU Picking

目标：替代 CPU AABB picking，支撑复杂模型和遮挡正确性。

建议形式：

- Scene viewport 输出 entity ID buffer。
- 或提供 `pick_scene_entity(viewport, x, y)` facade。
- 返回 `EntityId`、depth、world position、normal。

约束：

- Editor 不直接读 RHI texture。
- Picking 入口必须位于 Function 层或 ScenePresentation facade。

### P2. DebugDraw / Overlay

目标：把 Gizmo、bounds、selection overlay 从 2D 过渡绘制逐步迁到 Engine scene overlay。

当前已有：

- `draw_line`
- `draw_box`
- `draw_circle`
- `draw_cone`
- `draw_axes`
- SceneRenderer overlay pass

仍需明确：

- per-viewport submit，而不是只有全局队列。
- per-frame 生命周期，不残留上一帧数据。
- depth test / xray 控制。
- overlay 渲染顺序：Scene 之后，UI 之前。

## 不再作为 Engine 阻塞项

| 项目 | 当前处理 |
| --- | --- |
| Editor 独立 Scene 视口相机 | 已使用 `SceneCameraSource::Override`。 |
| `F` 聚焦 bounds 查询 | 已可基于 `SceneQuery` 和 `EditorSceneBoundsUtils`。 |
| Viewport CPU 点击选择 | 已可基于 `SceneQuery::ray_cast_scene()`。 |
| Model / Prefab 拖拽实例化 | 已走 `instantiate_asset(AssetId)` facade。 |
| Inspector 基础组件编辑 | 已有 typed component API、read/write component、component descriptors。 |
| DebugDraw 基础形状 | 已有 line/box/circle/cone/axes。 |
| Scene dirty 轮询 | 已有 `is_dirty()` / `get_change_version()`，但事件语义仍需补。 |

## 建议优先级

1. 扩展 `instantiate_asset()` 覆盖 `AssetType::Mesh`。
2. 提供 `project_ray_to_plane()` 或 `find_scene_drop_point()`。
3. 补 Scene change event / lifecycle 语义。
4. 补 DebugDraw per-viewport 和 depth/xray 语义。
5. 规划 GPU ID buffer picking。
6. 扩展组件元数据和通用 Add/Remove Component facade。
