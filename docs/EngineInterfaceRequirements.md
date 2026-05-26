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
| Scene 查询 | `Function/Scene/SceneQuery.*` | 已支持 world bounds、screen ray、CPU AABB ray cast、`project_ray_to_plane()` / `find_scene_drop_point()`。 |
| AssetId 实例化 facade | `instantiate_asset(Scene&, AssetDatabase&, AssetId, SceneInstantiationDesc)` | 已覆盖 `Model` / `Prefab` / `Mesh`。 |
| Scene 层级 facade | `Scene::create_entity()` / `destroy_entity()` / `reparent_entity()` / sibling index | Hierarchy 命令已有可用底座。 |
| 组件读写 | `Entity::read_component()` / `write_component()` / typed add/set/remove | Inspector 与命令系统已有底层边界。 |
| 通用 Add/Remove Component | `can_add_scene_component()` / `add_scene_component()` / `can_remove_scene_component()` / `remove_scene_component()` | 按 `SceneComponentType` 的统一 facade。 |
| 组件元数据 | `get_scene_component_descriptor()` / `get_scene_enum_descriptor()` | `ScenePropertyDesc` 已含 editor hint、range、asset ref、read-only。 |
| Scene change event | `Scene::subscribe_change_events()` / `SceneChangeEvent` | 已覆盖 entity/hierarchy/component/replace/reload/dirty 事件。 |
| Scene reload / replace | `Scene::reload_from_file()` / `replace_contents()` | 保留 change event 订阅者。 |
| Scene dirty/version | `Scene::is_dirty()` / `get_change_version()` / `mark_clean()` | 轮询 + `DirtyStateChanged` 事件。 |
| GPU picking | `request_scene_entity_pick()` / `poll_scene_entity_pick_result()` | GBuffer 后 entity id pass + async readback。 |
| Scene overlay depth | `submit_scene_overlay()` / `clear_scene_overlay()` / `SceneOverlayDepthMode` | viewport-scoped、depth-aware overlay pass。 |
| Viewport stats | `get_scene_view_stats()` | output 尺寸、backend、draw calls、frame time / FPS。 |
| Debug draw 基础能力 | `DebugDrawService::draw_line/box/circle/cone/axes()` | 全局 frame-local 线框队列和 SceneRenderer overlay pass；与 per-viewport scene overlay 分工。 |

## 仍需补齐

| 优先级 | 缺口 | 当前症状 | 推荐处理 |
| --- | --- | --- | --- |
| P1 | Scene overlay phase 2 | 当前 overlay 仅 line primitive；gizmo/billboard/quad 仍缺 Engine 侧表达。 | 扩展 `SceneOverlayBatchDesc` 或专用 gizmo submit API。 |
| P2 | GPU picking 增强 | 当前 MVP 无 alpha mask、normal 来自 bounds 近似、async 而非 sync pick。 | 可选 alpha cutout pass、GBuffer normal readback、同步 facade。 |
| P3 | 组件元数据继续扩展 | 已有 hint/range/asset ref，但缺 enum 分组、条件显示、multi-edit 语义。 | 按需扩展 `ScenePropertyDesc` 或 component-level flags。 |

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

### P1. Scene Overlay / DebugDraw Depth

目标：先把 Scene 视口里“应该参与遮挡关系”的 helper 迁到 Engine scene overlay，再逐步把 Gizmo、bounds、selection overlay 从 2D 过渡绘制迁过去。

当前已有：

- `draw_line`
- `draw_box`
- `draw_circle`
- `draw_cone`
- `draw_axes`
- SceneRenderer overlay pass

当前问题：

- Editor viewport 的 reference grid、origin、camera/light helper、selection/gizmo 目前主要还是在 `UIContext` 阶段叠到 `UISurfaceHandle` 上。
- 这条路径发生在 scene color 已经提交完成之后，不参与 scene depth test，所以被 mesh 遮挡的关系一定不正确。
- Engine 现有 SceneRenderer debug overlay pass 也还是“总在最上层”的语义，不能直接承接 Editor 这批需要遮挡关系的 3D helper。

已确认的现状：

- Editor 侧 grid / origin / camera-light helper 目前在 `ViewportPanelOverlaySupport.cpp` 中通过 `UIContext::draw_window_line/rect(...)` 直接画 2D 投影线。
- selection helper / gizmo 也仍大量依赖 `SelectionOverlayRenderer.cpp`、`MoveScaleGizmoTool.cpp`、`RotateGizmoTool.cpp` 里的 `draw_window_*` 路径。
- Engine 侧 `SceneRenderer::make_debug_draw_program_desc()` 当前明确设置了：
  - `depth_test = false`
  - `depth_write = false`
- `SceneRenderer::add_debug_draw_overlay_pass()` 当前只向 color output 叠加 line list，没有把当前 scene depth 当成“要参与测试的同一视图深度源”来使用。

结论：

- 这个问题不是 Editor 单边能彻底修掉的问题。
- Editor 现在最多只能继续做 screen-space 投影和局部裁剪，但无法得到真正正确的 mesh 遮挡关系。
- 如果近期要把 Scene 视口做成接近 Unity/Unreal 的遮挡正确性，这项能力需要 Engine 先补接口。

仍需明确：

- per-viewport submit，而不是只有全局队列。
- per-frame 生命周期，不残留上一帧数据。
- depth test / xray 控制。
- overlay 渲染顺序：Scene 之后，UI 之前。
- depth source 必须绑定到当前 `SceneViewBindingHandle` 对应 view 的深度结果，而不是独立猜测或 UI 阶段重算。
- 需要基础 `depth bias`/`polygon offset` 语义，避免 grid、wire helper 与共面表面严重闪烁。

建议最小接口：

```cpp
enum class SceneOverlayDepthMode : uint8_t
{
    AlwaysOnTop = 0,
    DepthTest,
    DepthTestNoWrite,
    XRay
};

struct SceneOverlayLine
{
    glm::vec3 start{ 0.0f };
    glm::vec3 end{ 0.0f };
    glm::vec4 color{ 1.0f };
    float thickness = 1.0f;
    float depth_bias = 0.0f;
    SceneOverlayDepthMode depth_mode = SceneOverlayDepthMode::DepthTest;
};

struct SceneOverlayBatchDesc
{
    std::span<const SceneOverlayLine> lines{};
};

bool submit_scene_overlay(
    SceneViewBindingHandle binding,
    const SceneOverlayBatchDesc& desc);
```

或至少提供：

- `submit_scene_overlay_lines(SceneViewBindingHandle, ...)`
- `clear_scene_overlay(SceneViewBindingHandle)`
- `SceneOverlayDepthMode`
- `depth_bias`

接口要求：

- 作用域绑定到 `SceneViewBindingHandle`，不要做成全局单例队列。
- 生命周期按帧清空；Editor 每帧重提交通常 helper。
- overlay pass 必须发生在 scene geometry 完成之后、UI 合成之前。
- `DepthTest` 至少要复用当前 view 的深度结果，不能退化成 UI clip 或 CPU 可见性判断。
- `AlwaysOnTop` 和 `DepthTest` 需要并存；Editor 里会同时存在“应该被遮挡的 helper”和“故意穿透显示的强调标记”。
- `DepthTestNoWrite` 应该成为默认 editor helper 模式，避免污染场景深度。
- `XRay` 最好定义成“深度测试失败时用降 alpha / hidden-line 样式补一层”，而不是简单等同于 always-on-top。

建议分两阶段做：

第一阶段，先修当前错误的遮挡关系：

- lines / polyline 的 per-viewport submit
- `DepthTest` / `DepthTestNoWrite` / `AlwaysOnTop` / `XRay`
- `depth_bias`
- 绑定当前 `SceneViewBindingHandle` 的深度结果

这一步就足够迁移：

- scene reference grid / origin
- camera frustum / viewport helper
- light range / cone / direction helper
- 轻量 selection wire / bounds helper

第二阶段，再承接完整 Editor 3D 操作器：

- billboard / icon primitive
- quad / triangle overlay primitive
- screen-space constant size handle 语义
- 更丰富的 xray / hidden-line 风格

这一步再迁移：

- transform gizmo 的轴末端方块 / 箭头
- rotate ring marker
- plane drag handle
- scene 中的 camera / light 图标类 helper

推荐迁移到 Engine 3D overlay 的内容：

- scene reference grid / origin
- selection outline 或 selection wire helper
- camera frustum / viewport helper
- light range / cone / direction helper
- transform gizmo 的 3D 轴、环、平面把手

继续留在 Editor UI overlay 的内容：

- toolbar、文字标签、hover tooltip
- 2D icon、框选矩形、面板内状态提示
- 不依赖 scene depth 的 screen-space decoration

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
4. 补 Scene overlay per-viewport 和 depth/xray 语义。
5. 规划 GPU ID buffer picking。
6. 扩展组件元数据和通用 Add/Remove Component facade。
