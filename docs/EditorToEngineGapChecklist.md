# Editor -> Engine Gap Checklist

> 面向 Editor 开发时快速判断“是否需要补 Engine 接口”。
>
> 详细接口建议和优先级真源见 `docs/EngineInterfaceRequirements.md`。本文只保留执行时需要看的短清单。

## 使用规则

- Editor 任务默认不改 Engine。
- 只有缺少稳定 Function/Base 层接口时，才记录或补 Engine 缺口。
- 不把 Editor 私有流程塞进 Engine。
- 不让 Editor 直接依赖 `project/src/engine/Graphics/**`、RHI 后端、`KEnginePub/**`。
- 必须改 `project/src/engine/**` 时，改动块使用：
  - `// editor begin 修改原因：...`
  - `// editor end`

## 当前仍需 Engine 补

| 优先级 | 缺口 | 当前影响 |
| --- | --- | --- |
| P1 | `AssetType::Mesh` 接入 `instantiate_asset(Scene&, AssetDatabase&, AssetId, ...)` | Mesh 拖入场景仍需要 Editor fallback 创建实体并设置 `MeshComponent`。 |
| P1 | 统一 scene drop point / ray-to-plane helper | Viewport 资源投放落点规则仍散在 Editor。 |
| P1 | Scene change event / lifecycle 语义 | scene replace/reload/component/hierarchy 变化仍主要靠 Editor 局部重置和轮询。 |
| P2 | DebugDraw per-viewport / depth / xray 语义 | Gizmo、bounds、selection overlay 还不能完全迁到 Engine 3D overlay。 |
| P2 | GPU ID buffer picking | 当前 CPU AABB picking 对复杂模型、遮挡、精确命中不够。 |
| P2 | 组件元数据增强 | 缺 range、editor hint、asset ref、readonly 等 Inspector 友好字段。 |
| P2 | 通用 Add/Remove Component facade | 动态组件 UI 还缺按 `SceneComponentType` 的统一入口。 |
| P3 | Viewport/render stats facade | 状态栏和诊断 UI 缺稳定读取 backend、RT、帧时间、draw calls 的入口。 |

## 已不再作为 Engine 阻塞

| 项目 | 当前状态 |
| --- | --- |
| Scene/Game viewport 输出 | 已走 `ScenePresentationSubsystem`。 |
| Editor 显式 Scene 视口相机 | 已走 `SceneCameraSource::Override`。 |
| `F` 聚焦 bounds 查询 | 已可基于 `SceneQuery`。 |
| Viewport CPU 点击选择 | 已可基于 `SceneQuery::ray_cast_scene()`。 |
| Model / Prefab 拖拽实例化 | 已走 `instantiate_asset(AssetId)` facade。 |
| Scene 层级基础操作 | 已有 create/delete/reparent/sibling index 等 facade。 |
| Inspector 基础组件编辑 | 已有 typed component API、`read_component()` / `write_component()`、component descriptors。 |
| Scene dirty/version 轮询 | 已有 `Scene::is_dirty()` / `get_change_version()` / `mark_clean()`。 |
| DebugDraw 基础形状 | 已有 line/box/circle/cone/axes 和 SceneRenderer overlay pass。 |
| UIContext drag-drop 原语 | 已不再列为 Engine 阻塞。 |
| EditorTreeWidget 低层 UI 支撑 | 已不再列为 Engine 阻塞。 |

## 记录模板

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
