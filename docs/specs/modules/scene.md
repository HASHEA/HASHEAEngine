---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Module Spec: Scene 与 ScenePresentation

## 职责与边界

逻辑场景数据模型（Entity/组件/层级/序列化/变更事件/空间查询）以及 Scene → 渲染数据的唯一桥 `ScenePresentationSubsystem`。管场景内容的增删改查与向渲染侧的增量同步；不管渲染 pass 编排（属 render 模块）、资产 IO（属 asset 模块）、编辑器交互（属 editor 模块）。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `project/src/engine/Function/Scene/Scene.h/.cpp` | `Scene`/`Entity`（pimpl 值语义句柄）、`SceneChangeEvent`、extraction desc、instantiate 系列 |
| `project/src/engine/Function/Scene/SceneComponents.h` | `SceneComponentType` 与五类组件结构、属性元数据枚举（editor hint / asset ref） |
| `project/src/engine/Function/Scene/SceneConfig.h/.cpp` | `SceneRenderConfig`（AO/Shadow/Bloom/Volumetric/TAA 子 config 聚合） |
| `project/src/engine/Function/Scene/SceneQuery.h/.cpp` | 包围盒、射线、投放点等空间查询自由函数 |
| `project/src/engine/Function/Render/ScenePresentationSubsystem.h/.cpp` | 输出/视图绑定管理，每帧 Scene → `VisibleRenderFrame` 同步 |
| `project/src/engine/Function/Render/ScenePresentationHandles.h` | `SceneOutputHandle`/`SceneViewBindingHandle`/overlay/pick/stats 类型 |
| `project/src/engine/Function/Render/RenderScene.h/.cpp` | 渲染侧场景镜像 `RenderScene` 与不可变帧数据 `VisibleRenderFrame` |

## 公共接口

- `Scene`：`create` / `load_from_file` / `reload_from_file` / `replace_contents` / `save_to_file`；实体 CRUD（`create_entity[_with_id]`、`destroy_entity`、`reparent_entity`、`find_entity`、`get_entities*`）；提取接口 `extract_mesh_entities` / `extract_visible_mesh_entities` / `extract_light_entities` / `extract_active_environment`；`get_render_config` / `set_render_config`；脏标记与版本号（`get_change_version`、`get_render_{primitive,transform,light,environment,config}_version`，供渲染侧增量同步）。
- `Entity`：`EntityId`（uint64）；五类组件 Name/Transform/Camera/Light/Mesh/Environment 的 has/get/set/add/remove；通用 `read_component` / `write_component`；层级 API（`get_parent` / `get_children` / `set_parent` / `create_child`）；`set_transform_component_silent`（编辑器临时实体静默更新，不走变更事件）。
- 变更事件：`SceneChangeKind`（EntityAdded/EntityRemoved/HierarchyChanged/ComponentChanged/SceneReplaced/SceneReloaded/DirtyStateChanged）+ `subscribe_change_events` / `unsubscribe_change_events` / `notify_change_event`，同步回调。
- 组件反射：`get_scene_component_descriptor(s)`、`get_scene_enum_descriptor`、通用 `can_add/can_remove/add/remove_scene_component` facade（Inspector 依赖）。
- 实例化：`instantiate_model` / `instantiate_ashasset` / `instantiate_mesh` / 自由函数 `instantiate_asset(Scene&, AssetDatabase&, AssetId, SceneInstantiationDesc)`。
- `SceneQuery`：`get_entity_world_bounds` / `get_entity_subtree_world_bounds` / `screen_to_world_ray` / `ray_cast_scene` / `project_ray_to_plane` / `find_scene_drop_point`。
- `ScenePresentationSubsystem`：
  - 输出：`create_output` / `update_output` / `destroy_output`（`SceneOutputDesc`：Window/Offscreen、尺寸、`SceneOutputFormat`）；`get_ui_surface` 供 UI 显示离屏输出。
  - 视图绑定：`create_view_binding` / `update_view_binding` / `destroy_view_binding`（`SceneViewBindingDesc`：Scene 指针 + `SceneCameraSelector`（PrimaryCamera/EntityId/Override）+ 输出句柄 + `SceneViewOverrides`（clear/rect/show flags）+ sort_order）；`set_binding_enabled` / `request_refresh`。
  - 帧驱动：`update_presentations`（逻辑侧同步）+ `submit_presentations`（提交渲染）。
  - 编辑器扩展：overlay（`submit_scene_overlay` / `clear_scene_overlay`）、GPU 拾取（`request_scene_entity_pick` / `poll_scene_entity_pick_result` / `complete_gpu_pick_readbacks`）、`get_scene_view_stats`。

数据流：每帧按 Scene 的 render 版本号增量同步到 `RenderScene`（`rebuild_from_scene` / `update_transforms_from_scene` / `rebuild_lights_from_scene` / `rebuild_environment_from_scene` / `rebuild_render_config_from_scene`），再 `build_visible_render_frame` 产出该帧不可变的 `VisibleRenderFrame`（相机矩阵、可见 draw 列表、灯光、环境、TAA jitter，以及 `SceneRenderConfig` 快照），交给 `SceneRenderer` 消费。使用手册见 `docs/ScenePresentationSubsystemGuide.md`（细节以代码为准）。

## 约束与不变式

- `ScenePresentationSubsystem` 是 Scene 通往渲染的唯一入口：上层（Editor/Sandbox）不得绕过它直接驱动 `SceneRenderer` / `RenderScene`。
- 渲染侧只读 `VisibleRenderFrame`；帧数据构建后不可变，`SceneRenderConfig` 以快照形式随帧携带。
- `Scene`/`Entity` 是 shared_ptr pimpl 句柄，拷贝共享同一底层数据；`replace_contents` / `reload_from_file` 保留变更事件订阅者。
- 变更事件回调同步执行于调用线程；`RenderScene::get_static_mesh_primitives_snapshot` 是唯一声明线程安全的快照接口。
- 组件集合固定为 `SceneComponentType` 六项（Name/Transform/Camera/Light/Mesh/Environment），新增组件需同步扩展描述符表与序列化。

## 验证

对齐 `docs/VERIFY.md` "Scene / Asset / Application 生命周期"行：

- 构建 + `run.bat all Debug --smoke-test-seconds=5`（全矩阵 smoke）
- Editor 打开默认场景操作一遍（层级/Inspector/保存）
- 改动波及渲染数据流（extraction、VisibleRenderFrame、SceneRenderConfig）时加跑 `RunRenderGate.bat`

## 历史

- `docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md`（scene_config 渲染配置来源，归档）
