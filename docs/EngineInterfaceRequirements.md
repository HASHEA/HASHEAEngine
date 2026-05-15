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

### 仍需补齐

| 优先级 | 缺口 | 当前症状 | 推荐处理 |
|--------|------|----------|----------|
| P0 | Scene view matrix override | Editor camera 目前需要 clone preview scene 并创建临时 camera entity | 在 `SceneCameraSelector` 中加入显式 view/projection override |
| P0 | Scene query / world bounds | F 聚焦只能用 entity position，无法按几何 bounds 调整距离 | 新增 Engine 侧 `SceneWorldBounds` 查询 |
| P1 | Screen ray + CPU picking | Viewport 点击选择、Gizmo hit test、资源拖入放置缺公共算法 | 新增 `SceneRay`、`screen_to_world_ray()`、`ray_cast_scene()` |
| P1 | Prefab placement facade | 已能按 path 实例化，但缺 `AssetId + parent + world placement` 高层入口 | 新增 `SceneInstantiationDesc` 或等价重载 |
| P2 | Debug draw overlay | 原生 3D gizmo、bounds 可视化、调试线框缺 overlay pass | 后续接 RenderGraph/SceneRenderer overlay |
| P2 | GPU ID buffer picking | CPU AABB picking 精度有限 | 后续作为 GPU picking 升级，不阻塞第一阶段 |

---

## P0-1: Scene View Matrix Override

### 当前状态

`ScenePresentationSubsystem` 目前的相机选择只有：

```cpp
enum class SceneCameraSource : uint8_t
{
    PrimaryCamera = 0,
    EntityId
};
```

`SceneViewOverrides` 已存在，但只负责 clear、pixel rect、show flags，不包含 camera/view/projection。

Editor 目前通过 `EditorViewportCameraService` 复制 active scene 到 preview scene，再创建一个临时 `EditorCamera` entity，并以 `SceneCameraSource::EntityId` 绑定。这是可运行的临时方案，但长期问题明显：

- 每次 scene change 都要 clone scene。
- Editor camera 被表达成 scene entity，污染了 scene presentation 的语义。
- 后续多 Scene viewport、多 editor camera、多 preview mode 时成本会继续放大。

### 推荐接口

把显式矩阵作为 camera selector 的一种来源，而不是额外做 `SceneViewBinding` 命令式 setter。

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

实现侧新增公共 helper：

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

缺口是：这些能力没有形成 Editor 可直接调用的 Scene 查询 API。

### 推荐接口

新增 Engine 侧 scene query API，建议放在：

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

### 推荐接口

继续放在 `SceneQuery.*`：

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

当前缺的是编辑器拖拽放置所需的高层入口：

- 以 `AssetId` 为输入。
- 可指定 parent。
- 可指定 world position / rotation / scale。
- 返回 root entity。

### 推荐接口

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

### 保留目标

```cpp
class DebugDrawService
{
public:
    void draw_line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float thickness = 1.0f);
    void draw_circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int segments = 32);
    void draw_cone(const glm::vec3& apex, const glm::vec3& direction, float length, float angle, const glm::vec4& color);
    void draw_aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void draw_sphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);
};
```

### 暂缓原因

Debug draw 会影响：

- SceneRenderer / RenderGraph pass 组织。
- Vulkan + DX12 overlay pipeline。
- 深度测试、透明混合、线宽兼容性。
- 多 view / 多 output 的提交归属。

因此它应在 P0/P1 查询接口稳定后单独推进。

---

## 实施顺序

1. 更新 `ScenePresentationSubsystem` / `SceneView`，支持 `SceneCameraSource::Override`。
2. 新增 `SceneQuery.*`，实现 world bounds、screen ray、CPU ray-AABB picking。
3. 新增 `SceneInstantiationDesc` 与 `AssetId` 版本的 `instantiate_asset()` free function。
4. 更新 `docs/EngineDeveloperGuide.md` 和根 `README.md`，记录新增 Engine-facing API。
5. 后续 Editor 任务再把 `EditorViewportCameraService` 从 preview scene 模式迁移到 matrix override。
6. 单独任务推进 DebugDrawService / GPU ID buffer picking。

---

## 验证要求

本清单涉及 Scene、Asset、ScenePresentation 和共享渲染路径。实现时至少需要：

- Engine self-test 覆盖矩阵 view override、bounds、ray、picking、prefab placement。
- Debug build 成功。
- 共享渲染路径变更完成后，按仓库验证基线运行 Sandbox / Editor 在 Vulkan 与 DX12 上的 smoke validation。
