# 编辑器所需引擎接口清单

本文档记录 Editor 继续推进 Scene 视口、拖拽放置、Gizmo、场景生命周期管理时，仍然需要 Engine 提供或继续收口的公共接口。

> 当前状态说明：
> - `SceneCameraSource::Override` 已具备，Editor Scene 视口已经可以直接绑定显式 view/projection，不再需要 clone preview scene + 临时 camera entity。
> - `SceneQuery` 已具备，Editor 已开始用它接 `F` 聚焦、点击选择，以及后续 Gizmo 命中的共享射线路径。
> - `AssetId` 版本的 `instantiate_asset()` 已具备，Editor 已开始用它处理 `Model` / `Prefab` 资源拖拽放置。
> - 如果为补这些接口必须修改 `project/src/engine/**`，所有引擎侧改动都必须包在：
>   - `// editor begin 修改原因：...`
>   - `// editor end`

---

## 当前已具备

| 能力 | 当前入口 | 说明 |
|------|----------|------|
| Scene 视口输出 | `ScenePresentationSubsystem::create_output()` / `create_view_binding()` | 已支持 offscreen output、persistent binding、UI surface 展示 |
| Editor 显式矩阵相机 | `SceneCameraSource::Override` + `SceneViewCameraOverride` | Editor Scene 视口已可直接绑定 view/projection |
| Scene query | `project/src/engine/Function/Scene/SceneQuery.*` | 已提供 world bounds、screen ray、CPU ray cast |
| 资源实例化 facade | `instantiate_asset(Scene&, AssetDatabase&, AssetId, SceneInstantiationDesc)` | 当前已支持 `Model` / `Prefab` |
| 场景基础查询 | `Scene::get_entity_world_transform()` / `Scene::try_get_mesh_local_bounds()` | 继续作为底层支撑 |
| Debug draw overlay | `Application::get_debug_draw_service()` / `DebugDrawService` | Engine 侧 frame-local 线框提交与 SceneRenderer overlay pass 已接入 |

### 仍需补齐

| 优先级 | 缺口 | 当前症状 | 推荐处理 |
|--------|------|----------|----------|
| P2 | GPU ID buffer picking | CPU AABB picking 精度有限 | 后续作为 GPU picking 升级，不阻塞第一阶段 |

---

## 仍需补齐

### P1. Mesh 资源也要接入 `AssetId` 实例化 facade

**当前问题：**
- 现在 `instantiate_asset()` 只分发 `AssetType::Model` / `AssetType::Prefab`
- `AssetType::Mesh` 仍然需要 Editor 自己走“创建空实体 + 设置 `MeshComponent`”的 fallback
- 这导致 Asset Browser 拖拽放置路径没有完全统一

**Editor 当前表现：**
- `Model` / `Prefab` 已走新的 `AssetId instantiate_asset()` facade
- 原始 `Mesh` 资源仍走 Editor 本地命令 fallback

**建议接口方向：**
```cpp
Entity instantiate_asset(
    Scene& scene,
    AssetDatabase& database,
    AssetId assetId,
    const SceneInstantiationDesc& desc = {});
```

在现有实现里继续扩展：
- `AssetType::Mesh`
- `AssetType::Model`
- `AssetType::Prefab`

**建议行为：**
- `Mesh`：创建单实体并自动挂 `MeshComponent`
- `Model`：保留现有模型层级实例化逻辑
- `Prefab`：保留现有 `AshAsset` / prefab 实例化逻辑

---

### P1. 统一的 Scene 资源投放落点 helper

**当前问题：**
- Editor 现在已经能拖资源进 Scene 视口并实例化
- 但“放到哪里”这件事还是 Editor 本地拼出来的：
  - 优先 scene ray cast
  - 再回退到地面平面
  - 再回退到相机前方固定距离
- 这条规则本质上是 Engine / Editor / Sandbox 都可能复用的通用放置语义

**建议接口：**
```cpp
bool project_ray_to_plane(
    const SceneRay& ray,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal,
    glm::vec3& outHitPoint);
```

或更高层：
```cpp
bool find_scene_drop_point(
    const Scene& scene,
    AssetDatabase& database,
    const SceneRay& ray,
    glm::vec3& outWorldPosition);
```

**建议行为：**
- 优先命中场景已有对象
- 未命中时回退到默认世界地面
- 再回退到相机前方固定距离

---

### P2. Debug draw / overlay pass

**当前状态：**
- Engine 侧第一版 `DebugDrawService` 与 SceneRenderer overlay pass 已接入。
- 当前可作为 Gizmo / bounds 的过渡底座，但还不是完整 3D 编辑器 overlay 方案。

**建议继续明确的语义：**
- overlay 必须能按 `SceneView` / `Scene viewport` 独立提交，而不是只有全局单例队列。
- overlay 数据必须是 per-frame 的；同一视口下一帧不应残留上一帧 gizmo 线段。
- `draw_line` 之外，最好补 `draw_polyline` 或 `draw_circle` 作为正式公共入口，方便 Rotate gizmo 画圆环。
- 最好支持 `depth_test = true/false` 或等价的 `xray` 模式；否则 gizmo 会频繁被场景遮住。
- 如果能补 `draw_cone` / `draw_billboard` / `draw_box`，Move / Scale gizmo 的箭头和端点会更容易做得完整。
- 最好约定 overlay 的渲染顺序为 `Scene` 之后、`UI` 之前，避免与编辑器 2D 前景层互相覆盖。

**Editor 当前可以先做什么：**
- 先在 Editor 侧用 `SceneQuery + EditorCamera` 做 gizmo 命中与拖拽数学。
- 先用 2D UI overlay 画一个过渡版 gizmo。
- 最终版本再把 bounds / gizmo 视觉迁到 Engine overlay。

---

### P2. Scene revision / dirty / replace 通知语义

**当前问题：**
- Editor 已经能靠本地 service 做部分同步。
- 但随着 Scene 视口、Hierarchy、Inspector、Asset 放置都在变重，缺一个更稳定的引擎级生命周期语义。
- 尤其是 scene replace、scene dirty、revision bump、component / hierarchy 变更类别。

**建议接口：**
```cpp
struct SceneRevisionInfo
{
    uint64_t revision = 0;
    bool dirty = false;
};

SceneRevisionInfo Scene::get_revision_info() const;
```

或事件语义：
```cpp
enum class SceneChangeKind : uint8_t
{
    EntityAdded = 0,
    EntityRemoved,
    HierarchyChanged,
    ComponentChanged,
    SceneReplaced
};
```

---

## 这次迁移后确认不再属于阻塞项

| 能力 | 当前状态 |
|------|----------|
| Editor 独立 Scene 视口相机 | 已切到 `SceneCameraSource::Override` |
| `F` 聚焦按几何 bounds 调整 | 已可基于 `SceneQuery` 接入 |
| Scene 视口点击选择 | 已可基于 `SceneQuery` 接入 |
| `Model` / `Prefab` 资源拖拽实例化 | 已可走 `AssetId instantiate_asset()` facade |

---

## 建议优先级

1. 扩展 `instantiate_asset()` 覆盖 `AssetType::Mesh`
2. 提供统一的 drop point / ray-to-plane helper
3. 补齐 DebugDrawService 的 per-viewport overlay 语义
4. 补 Scene revision / dirty / replace 通知
