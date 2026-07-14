---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Scene 与 ScenePresentation

## 职责与边界

逻辑场景数据模型（Entity/组件/层级/序列化/变更事件/空间查询）以及 Scene → 渲染数据的唯一桥 `ScenePresentationSubsystem`。管场景内容的增删改查与向渲染侧的增量同步；不管渲染 pass 编排（属 render 模块）、资产 IO（属 asset 模块）、编辑器交互（属 editor 模块）。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `project/src/engine/Function/Scene/Scene.h/.cpp` | `Scene`/`Entity`（pimpl 值语义句柄）、`SceneChangeEvent`、extraction desc、instantiate 系列 |
| `project/src/engine/Function/Scene/SceneComponents.h` | `SceneComponentType` 与七类组件结构、属性元数据枚举（editor hint / asset ref） |
| `project/src/engine/Function/Scene/SceneConfig.h/.cpp` | `SceneRenderConfig`（AO/Shadow/Bloom/Volumetric/TAA 子 config 聚合） |
| `project/src/engine/Function/Scene/SceneQuery.h/.cpp` | 包围盒、射线、投放点等空间查询自由函数 |
| `project/src/engine/Function/Scene/TerrainQuery.h/.cpp` | Phase 1 snapshot-local Terrain 高度、法线、精确射线与非阻塞预取查询；尚未接入 Scene 实体 |
| `project/src/engine/Function/Render/ScenePresentationSubsystem.h/.cpp` | 输出/视图绑定管理，每帧 Scene → `VisibleRenderFrame` 同步 |
| `project/src/engine/Function/Render/ScenePresentationHandles.h` | `SceneOutputHandle`/`SceneViewBindingHandle`/overlay/pick/stats 类型 |
| `project/src/engine/Function/Render/RenderScene.h/.cpp` | 渲染侧场景镜像 `RenderScene` 与不可变帧数据 `VisibleRenderFrame` |

## 公共接口

- `Scene`：`create` / `load_from_file` / `reload_from_file` / `replace_contents` / `save_to_file`；实体 CRUD；提取接口 `extract_mesh_entities` / `extract_visible_mesh_entities` / `extract_light_entities` / `extract_active_environment` / `extract_particle_entities`；`get_render_config` / `set_render_config`；`get_render_{primitive,transform,light,environment,particle,config}_version` 供渲染侧增量同步；`get_content_epoch()` 标识 reload/replace 后的新场景内容世代。
- `Entity`：`EntityId`（uint64）；七类组件 Name/Transform/Camera/Light/Mesh/Environment/Particle 的 has/get/set/add/remove；通用 `read_component` / `write_component`；层级 API；`set_transform_component_silent`（编辑器临时实体静默更新，不走变更事件）。
- 变更事件：`SceneChangeKind`（EntityAdded/EntityRemoved/HierarchyChanged/ComponentChanged/SceneReplaced/SceneReloaded/DirtyStateChanged）+ `subscribe_change_events` / `unsubscribe_change_events` / `notify_change_event`，同步回调。
- 组件反射：`get_scene_component_descriptor(s)`、`get_scene_enum_descriptor`、通用 `can_add/can_remove/add/remove_scene_component` facade（Inspector 依赖）。
- 实例化：`instantiate_model` / `instantiate_ashasset` / `instantiate_mesh` / 自由函数 `instantiate_asset(Scene&, AssetDatabase&, AssetId, SceneInstantiationDesc)`。
- `SceneQuery`：`get_entity_world_bounds` / `get_entity_subtree_world_bounds` / `screen_to_world_ray` / `ray_cast_scene` / `project_ray_to_plane` / `find_scene_drop_point`。
- `TerrainQuery`：对 `TerrainAssetSnapshot` 的 terrain-local `query_height` / `query_normal` / `ray_cast_terrain`，以及通过 `AssetDatabase` 启动整资产异步加载的 `prefetch_query_region`；状态为 Ready/Pending/Outside/Failed。射线查询遍历 Component min/max 层级并对真实三角形求交，不使用固定步进。
- `ScenePresentationSubsystem`：
  - 输出：`create_output` / `update_output` / `destroy_output`（`SceneOutputDesc`：Window/Offscreen、尺寸、`SceneOutputFormat`）；`get_ui_surface` 供 UI 显示离屏输出。
  - 视图绑定：`create_view_binding` / `update_view_binding` / `destroy_view_binding`（`SceneViewBindingDesc`：Scene 指针 + `SceneCameraSelector`（PrimaryCamera/EntityId/Override）+ 输出句柄 + `SceneViewOverrides`（clear/rect/show flags）+ sort_order）；`set_binding_enabled` / `request_refresh`。
  - 帧驱动：`update_presentations`（逻辑侧同步）+ `submit_presentations`（提交渲染）+ `get_last_scene_submission_snapshot`；快照绑定 Application frame，记录全部预期 packet 的 attempted/succeeded/failed/capture-ready 与提交结束时 asset epoch。
  - 编辑器扩展：overlay（`submit_scene_overlay` / `clear_scene_overlay`）、GPU 拾取（`request_scene_entity_pick` / `poll_scene_entity_pick_result` / `complete_gpu_pick_readbacks`）、`get_scene_view_stats`。

数据流：每帧按 Scene 的 render 版本号增量同步到 `RenderScene`（含 primitives/transforms/lights/environment/particles/config），`build_visible_render_frame` 产出相机、draw、灯光、环境、particle emitters 与配置快照；`ScenePresentationSubsystem` 再补入 scene runtime/content 标识和 render-submit delta，交给 `SceneRenderer`。

## 约束与不变式

- `ScenePresentationSubsystem` 是 Scene 通往渲染的唯一入口：上层（Editor/Sandbox）不得绕过它直接驱动 `SceneRenderer` / `RenderScene`。
- 自动化不得用“任一 view 成功”代表整帧成功：每个 enabled 且绑定非空 Scene 的预期 packet 都必须计 attempted；output/extent、RenderScene/view/visible-frame、material 或最终 render 任一失败均计 failed。Application 只消费 frame index 与本次 present 对齐的快照。
- 渲染侧只读 `VisibleRenderFrame`；帧数据构建后不可变，`SceneRenderConfig` 以快照形式随帧携带。
- `Scene`/`Entity` 是 shared_ptr pimpl 句柄，拷贝共享同一底层数据；`replace_contents` / `reload_from_file` 保留变更事件订阅者。
- 变更事件回调同步执行于调用线程；`RenderScene::get_static_mesh_primitives_snapshot` 是唯一声明线程安全的快照接口。
- 组件集合固定为 `SceneComponentType` 七项（Name/Transform/Camera/Light/Mesh/Environment/Particle）。当前 scene JSON schema 为 version 5；Particle 的 `blend_mode` 写为 `Additive` / `AlphaBlend` 字符串，读取兼容旧整数。
- Phase 1 Terrain 查询只消费 asset snapshot，不应用 Entity Transform，也不参与 `ray_cast_scene`、Scene pick 或角色贴地。`TerrainComponent`、世界空间 Scene adapter、Terrain extraction/render version 与 scene JSON schema v6 均尚未实现（not implemented）。
- `get_content_epoch()` 只在 load/reload/replace 内容时变化，普通组件编辑不得推进；它用于重置跨帧渲染状态，不替代细粒度 render version。
- 粒子 GPU 状态以进程内 `scene_runtime_id + entity_id` 隔离；场景解绑会显式释放该 runtime 的状态。
- 边界：custom pass、compute、后处理实验、调试渲染继续走 Renderer 直驱，不强行塞进 scene presentation；上层需求统一表达为 Scene + Camera + Output + Overrides，不把 `RenderScene`/`SceneView`/`VisibleRenderFrame`/`SceneRenderer` 暴露回上层；UI 显示离屏输出用 `UISurfaceHandle` + `draw_surface_fill_available`。
- 当前限制：view 的 viewport/scissor 只约束光栅化区域；clear 作用于整个 attachment 而非 rect（多 binding 共享 output 的保留语义后续用 preserve/load 解决）；Window output 不返回有效 `UISurfaceHandle`；`show_flags` 为预留字段。

## 验证

对齐 `docs/VERIFY.md` "Scene / Asset / Application 生命周期"行：

- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke）
- Editor 打开默认场景操作一遍（层级/Inspector/保存）
- 改动波及渲染数据流（extraction、VisibleRenderFrame、SceneRenderConfig）时加跑 `RunRenderGate.bat`
- 只改 snapshot-local Terrain 查询时按 `docs/VERIFY.md` "Terrain Asset / CPU logic" 行执行；它不产生 rendered frame 变化。

## 历史

- `docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md`（scene_config 渲染配置来源，归档）
- [SDD-2026-07-10-gpu-particles](../../sdd/SDD-2026-07-10-gpu-particles.md)（ParticleComponent、schema v5 与提取链）
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)（当前帧 scene packet readiness 快照）
- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)（Terrain 总体设计；当前仅 snapshot-local Phase 1 查询已实现）
