# 编辑器所需引擎接口清单

本文档记录 Editor 继续推进 Viewport、Gizmo、Picking、Prefab 放置等工作时，需要 Engine 侧提供或收口的公共接口。

当前状态不是“完全没有接口”：`ScenePresentationSubsystem`、`Scene / Entity`、`AssetDatabase`、`AshAsset` 已经提供了部分底座。下面按“当前状态 -> 缺口 -> 推荐方案 -> 实施顺序”重新整理。

---

## 当前结论

### 已经具备

| 能力 | 当前入口 | 说明 |
|------|----------|------|
| Scene 视口输出 | `ScenePresentationSubsystem::create_output()` / `create_view_binding()` | 已支持 window/offscreen output、persistent binding、UI surface 展示 |
| Camera entity 渲染 | `SceneCameraSource::PrimaryCamera` / `EntityId` | 可绑定场景内相机实体 |
| 世界矩阵查询 | `Scene::get_entity_world_transform()` | Gizmo 显示和基础聚焦可用 |
| Mesh 本地 bounds | `Scene::try_get_mesh_local_bounds()` | 需要 `AssetDatabase`，当前只按 `MeshComponent` 查本地 bounds |
| Prefab-style 资源实例化 | `Scene::instantiate_ashasset()` / `instantiate_asset()` | `.ashasset` 已可实例化到 Scene |
| 组件基础反射 | `get_scene_component_descriptor()` | Inspector 基础字段展示已可复用 |
| Debug draw overlay | `Application::get_debug_draw_service()` / `DebugDrawService` | Engine 侧 frame-local 线框提交与 SceneRenderer overlay pass 已接入 |

### 仍需补齐

| 优先级 | 缺口 | 当前症状 | 推荐处理 |
|--------|------|----------|----------|
| P2 | GPU ID buffer picking | CPU AABB picking 精度有限 | 后续作为 GPU picking 升级，不阻塞第一阶段 |

---

## P0-1: Scene View Matrix Override

### 当前状态

`ScenePresentationSubsystem` 的相机选择已支持：

```cpp
enum class SceneCameraSource : uint8_t
{
    PrimaryCamera = 0,
    EntityId,
    Override
};
```

`SceneCameraSelector::override_view` 已承载显式 view/projection/camera position；`SceneViewOverrides` 继续只负责 clear、pixel rect、show flags。

Editor 侧如果仍在通过 `EditorViewportCameraService` 复制 active scene 并创建临时 `EditorCamera` entity，那已经属于 Editor 接入迁移任务，不再是 Engine 接口缺口。长期建议仍然是让 Editor camera 直接走 `SceneCameraSource::Override`，避免：

- 每次 scene change 都要 clone scene。
- Editor camera 被表达成 scene entity，污染了 scene presentation 的语义。
- 后续多 Scene viewport、多 editor camera、多 preview mode 时成本会继续放大。

### 已落地接口

显式矩阵作为 camera selector 的一种来源，而不是额外做 `SceneViewBinding` 命令式 setter。

```cpp
enum class SceneCameraSource : uint8_t
{
    PrimaryCamera = 0,
    EntityId,
    Override
};

struct SceneViewCameraOverride
{
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::vec3 camera_position{ 0.0f };
    bool enabled = false;
};

struct SceneCameraSelector
{
    SceneCameraSource source = SceneCameraSource::PrimaryCamera;
    EntityId entity_id = 0;
    SceneViewCameraOverride override_view{};
};
```

实现侧公共 helper：

```cpp
bool build_scene_view_from_matrices(
    const SceneViewDesc& desc,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& camera_position,
    SceneView& out_view);
```

### 设计取舍

- 不把矩阵放进 `SceneViewOverrides`，避免把 camera source 与 clear/rect/show flag 混在一起。
- 不新增 `set_camera_override()`，保持 `SceneViewBindingDesc` 的声明式更新模式。
- `Override` 仍要求绑定有效 `Scene*`，因为 ScenePresentation 还需要从 scene 构建 render scene 和 visible frame。

---

## P0-2: Entity / Subtree World Bounds

### 当前状态

Engine 已有：

- `Scene::get_entity_world_transform(EntityId)`
- `Scene::try_get_mesh_local_bounds(AssetDatabase&, const MeshComponent&, SceneMeshBounds&)`
- Render 侧 `PrimitiveBounds` 的 world AABB 计算逻辑

这些能力已形成 Editor 可直接调用的 Scene 查询 API。

### 已落地接口

Engine 侧 scene query API 位于：

```text
project/src/engine/Function/Scene/SceneQuery.h
project/src/engine/Function/Scene/SceneQuery.cpp
```

接口：

```cpp
struct SceneWorldBounds
{
    bool is_valid = false;
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 0.0f };
    glm::vec3 center{ 0.0f };
    glm::vec3 extents{ 0.0f };
};

bool get_entity_world_bounds(
    const Scene& scene,
    AssetDatabase& database,
    EntityId entity_id,
    SceneWorldBounds& out_bounds);

bool get_entity_subtree_world_bounds(
    const Scene& scene,
    AssetDatabase& database,
    EntityId root_entity_id,
    SceneWorldBounds& out_bounds);
```

### 设计取舍

- 使用 free function，不直接塞进 `Scene`，避免 `Scene` facade 继续膨胀。
- 显式传入 `AssetDatabase&`，因为 mesh bounds 来自 asset/model 数据，不应隐藏资源依赖。
- `get_entity_world_bounds()` 只查当前 entity 自身 mesh；`get_entity_subtree_world_bounds()` 聚合子树，供 F 聚焦和 bounds 可视化使用。

---

## P1-1: Screen Ray 与 CPU Picking

### 已落地接口

接口位于 `SceneQuery.*`：

```cpp
struct SceneRay
{
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
};

struct SceneRayHit
{
    EntityId entity_id = 0;
    float distance = 0.0f;
    glm::vec3 position{ 0.0f };
    SceneWorldBounds bounds{};
};

SceneRay screen_to_world_ray(
    float screen_x,
    float screen_y,
    float viewport_width,
    float viewport_height,
    const glm::mat4& view,
    const glm::mat4& projection);

std::vector<SceneRayHit> ray_cast_scene(
    const Scene& scene,
    AssetDatabase& database,
    const SceneRay& ray,
    float max_distance = FLT_MAX);
```

### 第一阶段行为

- `screen_x/screen_y` 使用 viewport 内像素坐标，左上为 `(0, 0)`。
- 使用 `projection * view` 的逆矩阵从 near/far NDC 点还原 world ray。
- `ray_cast_scene()` 第一版只做 CPU ray vs world AABB。
- 命中结果按距离升序排序。
- 不命中无几何 bounds 的 entity。

### 后续升级

GPU ID buffer picking 可作为 P2/P3 升级项：

- Scene pass 输出 entity id attachment。
- UI 点击时回读目标像素。
- 精度高于 CPU AABB，但会引入 render pass / readback / synchronization 设计，不应阻塞第一阶段。

---

## P1-2: Prefab 放置 Facade

### 当前状态

`.ashasset` 已是 prefab-style JSON 资源，`AssetDatabase` 可按 id/path 加载，`Scene` 可实例化 `AshAsset`。

编辑器拖拽放置所需的高层入口已接入：

- 以 `AssetId` 为输入。
- 可指定 parent。
- 可指定 world position / rotation / scale。
- 返回 root entity。

### 已落地接口

```cpp
struct SceneInstantiationDesc
{
    Entity parent{};
    glm::vec3 world_position{ 0.0f };
    glm::vec3 world_rotation_euler_degrees{ 0.0f };
    glm::vec3 world_scale{ 1.0f };
    bool use_world_transform = false;
    std::string root_name_override{};
};

Entity instantiate_asset(
    Scene& scene,
    AssetDatabase& database,
    AssetId asset_id,
    const SceneInstantiationDesc& desc = {});
```

### 设计取舍

- 不新增只叫 `instantiate_prefab()` 的窄接口，因为 Asset Browser 拖入也可能是 model asset。
- 统一走 `AssetType::Prefab` / `AssetType::Model` 分发。
- `use_world_transform = false` 时保持当前资产本地 transform。
- `use_world_transform = true` 时只设置返回 root entity 的 transform，子层级保持 prefab/model 内部相对关系。

---

## P2: DebugDrawService

### 当前状态

Engine 侧第一版已接入：

```cpp
class DebugDrawService
{
public:
    void draw_line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float thickness = 1.0f);
    void draw_box(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec4& color, float thickness = 1.0f);
    void draw_circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, uint32_t segments = 32, float thickness = 1.0f);
    void draw_cone(const glm::vec3& apex, const glm::vec3& direction, float length, float angle_degrees, const glm::vec4& color, uint32_t segments = 32, float thickness = 1.0f);
    void draw_axes(const glm::mat4& transform, float length = 1.0f, float thickness = 1.0f);
};
```

使用入口：

- Runtime 可通过 `Application::get_debug_draw_service()` 获取服务。
- 服务内部按 frame-local line list 记录提交，`Application::_on_render()` 在 `renderer->end_frame()` 后清空。
- `SceneRenderer` 在 `SceneDeferredCompositePass` 之后按需追加 `SceneDebugDrawOverlayPass`，用 `RenderLoadAction::Load` 保留 scene color 后叠加线框。
- RenderGraph pass 会显式声明对 output 的 load/read 依赖，避免 overlay 成为 external output 最终 producer 时把 composite pass 裁掉。

第一版边界：

- 当前渲染路径使用 `RenderPrimitiveTopology::LineList`，以 1px 线框直接覆盖到 output。
- `thickness` 会保留在提交数据中并 clamp 到至少 `1.0f`，但当前 Vulkan/DX12 overlay pipeline 暂不扩展成宽线几何。
- `color.a` 会保留在提交数据中，但当前 overlay 使用 opaque blend，透明混合后续再补。
- 当前 overlay 不绑定 scene depth，调试线默认绘制在最终画面上层；需要遮挡关系时再接 depth-aware debug pass。

### 后续升级

- GPU ID buffer picking 仍是剩余 P2 项。
- Editor 侧 gizmo / bounds 的具体调用与工具交互由 Editor 同学接入。
- Debug draw 可继续扩展 `draw_sphere()`、depth-aware 模式、alpha blend、screen-space fixed-size line 等能力。

---

## 实施顺序

1. 已完成：更新 `ScenePresentationSubsystem` / `SceneView`，支持 `SceneCameraSource::Override`。
2. 已完成：新增 `SceneQuery.*`，实现 world bounds、screen ray、CPU ray-AABB picking。
3. 已完成：新增 `SceneInstantiationDesc` 与 `AssetId` 版本的 `instantiate_asset()` free function。
4. 已完成：新增 Engine 侧 `DebugDrawService` 与 SceneRenderer overlay pass。
5. Editor 任务：把 `EditorViewportCameraService` 从 preview scene 模式迁移到 matrix override，并接入 DebugDrawService 绘制 gizmo / bounds。
6. 后续 Engine 任务：GPU ID buffer picking。

---

## 验证要求

本清单涉及 Scene、Asset、ScenePresentation 和共享渲染路径。实现时至少需要：

- Engine self-test 覆盖矩阵 view override、bounds、ray、picking、prefab placement。
- Debug build 成功。
- 共享渲染路径变更完成后，按仓库验证基线运行 Sandbox / Editor 在 Vulkan 与 DX12 上的 smoke validation。
