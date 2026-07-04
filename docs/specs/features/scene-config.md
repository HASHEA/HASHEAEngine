---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: 场景渲染配置（scene_config）

## 行为

- 场景 json 顶层 `scene_config` 对象承载场景级渲染开关，加载时由 `Scene.cpp` 内 `deserialize_scene_render_config` 解析为 `SceneRenderConfig`；保存时 `serialize_scene_render_config` 回写。缺失块 / 缺失字段用 `make_default_*_config()` 默认值；枚举值非法时告警并保留默认；每块解析完过一遍 `sanitize_*_config` 数值钳制。
- 传递链（快照语义，**不可跨帧持有**）：
  `Scene::get_render_config()`（存储侧，`set_render_config` 会 sanitize）
  → `RenderScene::rebuild_render_config_from_scene()` 拷贝到渲染侧
  → `RenderScene::build_visible_render_frame()` 拷入 `VisibleRenderFrame::render_config`（逐帧值拷贝）
  → 各 pass 只从 `frame.render_config.*` 读取本帧配置，禁止缓存到成员变量跨帧使用。

## 支持的配置块

`SceneRenderConfig`（`project/src/engine/Function/Scene/SceneConfig.h`）共 5 块，块名即 json key：

| 块 | 归属 feature spec | 字段 |
| --- | --- | --- |
| `ambient_occlusion` | ambient-occlusion.md | mode、quality、radius、intensity、power、half_resolution、blur、temporal、temporal_blend、temporal_depth_threshold、temporal_normal_threshold |
| `directional_shadows` | shadows.md | enabled、default_cascade_count、default_shadow_distance、near_shadow_distance、split_lambda、near_cascade_resolution、outer_cascade_resolution、dynamic_atlas_size、static_cache_atlas_size、static_cache_budget_mb、depth_bias、normal_bias、pcf_radius |
| `bloom` | bloom.md | enabled、quality、intensity、threshold、soft_knee、size_scale、stages[]{size,tint}、debug_view |
| `volumetric_lighting` | volumetric-lighting.md | enabled、quality、froxel_resolution_scale、froxel_depth_slices、max_lights、density、scattering_intensity、extinction_scale、anisotropy、history、history_blend、screen_space_fallback、debug_view |
| `temporal_aa` | taa.md | enabled、jitter_sequence_length、history_blend、variance_gamma、luminance_weighting、debug_view |

- debug 可视化没有独立块：各块自带 `debug_view` 字段（字符串枚举）；全局中间纹理查看器 RenderDebugView 走 Engine.ini（见 render-debug-view.md），不在 scene_config 内。

## 实现

- 解析/序列化：`project/src/engine/Function/Scene/Scene.cpp`（`deserialize_scene_render_config` / `serialize_scene_render_config`）。
- 结构定义：`SceneConfig.{h,cpp}`（`SceneRenderConfig`、`make_default_scene_render_config`、`scene_render_config_equal`）。
- 参考样例：`product/assets/scenes/Sandbox.scene.json` 的 `scene_config` 段（RenderGate 默认场景）。

## 约束与已知限制

- 新增配置块必须同时改：Config 结构体 + sanitize/default、Scene.cpp 反/序列化、对应 pass 从 `frame.render_config` 读取、本表。
- 未知 json key 被静默忽略（不报错），拼错块名等于用默认值。

## 验证

- `RunRenderGate.bat`（Sandbox 场景即 scene_config 的消费者）+ `RunPerfGate.bat -Profile Standard`；解析路径有 EngineSelfTests 覆盖。
- RenderDebugView 定位：改某块后用对应 feature 的调试视图确认生效（如 `SceneAmbientOcclusion`、`SceneBloomFinal`）。

## 历史

- docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md
- docs/superpowers/plans/2026-05-25-sandbox-scene-config-implementation.md
