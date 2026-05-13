# 编辑器所需引擎接口清单

本文档列出编辑器功能所需但引擎目前尚未提供的接口。按优先级从高到低排列。

---

## P0 — 编辑器相机与聚焦

### 1. SceneViewOverride — 自定义 View/Projection 矩阵注入

**用途：** 编辑器 Scene 视口需要独立于场景实体的相机（Editor Camera），用于自由视角浏览。当前 `ScenePresentationSubsystem` 的 `SceneCameraSource` 仅支持 `PrimaryCamera` 和 `EntityId` 两种模式，无法注入外部 view/projection matrix。

**建议接口：**
```cpp
// ScenePresentationSubsystem.h
enum class SceneCameraSource : uint8_t
{
    PrimaryCamera = 0,
    EntityId,
    Override  // 新增
};

struct SceneViewOverride
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};

// 在 SceneViewBinding 中新增：
void set_camera_override(const SceneViewOverride& override);
```

**影响范围：** `ScenePresentationSubsystem`、`SceneViewBinding`。

---

### 2. get_entity_world_bounds — 实体世界空间包围盒

**用途：** F 聚焦选中实体时，编辑器需要计算实体的世界空间 AABB 来确定相机位置和距离。也可辅助鼠标拾取。

**建议接口：**
```cpp
// Scene.h 或独立的 SceneQuery API
struct WorldBounds
{
    glm::vec3 min;
    glm::vec3 max;
    bool valid = false;  // false 表示无几何体
};

WorldBounds get_entity_world_bounds(EntityId id) const;
```

**说明：** 需要结合 `TransformComponent` 的世界变换和 `MeshComponent` 的本地包围盒（`PrimitiveBounds`）计算。`PrimitiveBounds` 已存在于渲染层但未对外暴露。

---

## P1 — Gizmo 交互与资源放置

### 3. screen_to_world_ray — 屏幕坐标转世界射线

**用途：**
- Gizmo 交互（判断鼠标在哪个轴上、拖拽时的世界空间偏移）
- Asset 拖入 Viewport 时确定放置的世界坐标
- 鼠标拾取实体

**建议接口：**
```cpp
// 数学工具函数，可放在 MathUtils 或 Camera 相关头文件
struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;  // 归一化
};

Ray screen_to_world_ray(
    float screenX,      // 视口内像素坐标 (0,0 = 左上)
    float screenY,
    float viewportWidth,
    float viewportHeight,
    const glm::mat4& viewMatrix,
    const glm::mat4& projectionMatrix);
```

**说明：** 纯数学函数，不依赖渲染管线。编辑器也可自行实现，但放在引擎层可复用。

---

### 4. ray_cast_scene — 场景射线检测

**用途：** 鼠标点选实体（Picking）。在视口中点击时，将屏幕坐标转为射线，检测命中哪个实体。

**建议接口（二选一）：**

**方案 A：CPU 端 ray-AABB 检测**
```cpp
struct RayHitResult
{
    EntityId entityId;
    float distance;
    glm::vec3 hitPoint;
};

// 返回按距离排序的命中结果
std::vector<RayHitResult> ray_cast_scene(
    const Scene& scene,
    const Ray& ray,
    float maxDistance = FLT_MAX);
```

**方案 B：GPU ID Buffer**
- 渲染时额外输出一张 EntityId 纹理（每像素存 entity ID）
- CPU 回读指定像素即可得到 entity ID
- 精度更高但需要 render pass 改动

**推荐：** P1 阶段先用方案 A（CPU ray-AABB），P2 阶段可升级到 GPU ID Buffer。

---

## P2 — 原生 3D Gizmo 与 Prefab

### 5. DebugDrawService — 3D 即时绘制 API

**用途：** 在场景上叠加渲染 3D 线框几何体，用于：
- 原生 3D 变换 Gizmo（替代 ImGuizmo 的 2D 叠加方案）
- 碰撞体/包围盒可视化
- 选中实体的轮廓高亮

**建议接口：**
```cpp
class DebugDrawService
{
public:
    // 所有绘制在当前帧结束后自动清除
    void draw_line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float thickness = 1.0f);
    void draw_circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int segments = 32);
    void draw_cone(const glm::vec3& apex, const glm::vec3& direction, float length, float angle, const glm::vec4& color);
    void draw_aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void draw_sphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);
};
```

**说明：** 需要引擎新增一个 overlay render pass，在场景渲染之后、UI 之前绘制，且不参与深度测试（或可选深度测试）。

**优先级说明：** 当前编辑器可先使用 ImGuizmo（纯 ImGui 2D 叠加，零引擎依赖）。待 DebugDrawService 就绪后可切换为原生 3D Gizmo。

---

### 6. instantiate_prefab — Prefab 实例化

**用途：** 从 Asset Browser 拖拽 Prefab 资源到 Viewport 时，创建该 Prefab 的实体实例。

**建议接口：**
```cpp
// Scene.h
EntityId instantiate_prefab(
    AssetId prefabAssetId,
    EntityId parentEntity = 0,          // 0 = 根级
    const glm::vec3& position = {},     // 世界坐标
    const glm::quat& rotation = glm::identity<glm::quat>());
```

**说明：** 当前编辑器已能为简单 Mesh 资源手动创建实体 + 设置 MeshComponent。但 Prefab 可能包含多个实体和组件层级，需要引擎支持。

---

## 已有可用接口（无需新增）

| 接口 | 位置 | 编辑器用途 |
|------|------|-----------|
| `get_entity_world_transform()` | Scene.h | 获取实体世界变换矩阵，Gizmo 显示/操作 |
| `TransformComponent` (position, rotation, scale) | SceneComponents.h | 编辑器修改实体变换 |
| `CameraComponent` (fov, near, far, projection) | SceneComponents.h | 编辑器读取/修改相机参数 |
| `InputState` (keyboard, mouse, scroll) | Input.h | 编辑器读取用户输入 |
| `ScenePresentationSubsystem` (create_output, create_view_binding) | ScenePresentationSubsystem.h | 视口渲染输出管理 |

---

## 实施建议

1. **最优先：** `SceneViewOverride`（P0-1）— 没有它编辑器无法实现独立相机，所有视口操作都被阻塞。
2. **次优先：** `get_entity_world_bounds`（P0-2）— F 聚焦是高频操作。
3. **中期：** `screen_to_world_ray` + `ray_cast_scene`（P1）— 鼠标拾取和 Gizmo 交互。
4. **后期：** `DebugDrawService` + `instantiate_prefab`（P2）— 原生 Gizmo 和 Prefab 工作流。
