# Feature Spec: Shadows（Sunlight CSM + 普通方向光阴影）

## 行为

方向光阴影有两条路径，产物都是屏幕空间 shadow mask（RGBA8，1=受光），
由 `DeferredLightingPass::add_directional_light_pass` 的 Shadowed 变体采样：

1. **SunLightShadowPass**（sunlight CSM）：只处理 `sunlight && casts_shadow` 的方向光，且最多 1 盏
   （出现多盏 sunlight 候选时整个 plan 置空，不产生阴影）。级联 0 覆盖 `[0.01, near_shadow_distance]`，
   每帧全量重绘（NearEveryFrame）；外层级联按 `mix(linear, log, split_lambda)` 切分到 shadow_distance，
   静态部分缓存：静态场景 revision 与光源 VP 未变时为 StaticCached（从 static cache atlas 经
   DepthCopy 拷入动态 atlas，仅补绘 Movable 物体），否则 StaticRefresh（StaticOnly 重绘缓存 tile 后拷贝，
   再绘全部 caster）。静态缓存 atlas 常驻、按 `(light_entity, cascade)` 记录 entry、超预算 LRU 逐出。
2. **DirectionalLightShadowPass**（普通方向光）：逐盏非 sunlight 方向光每帧构建 transient 级联
   atlas（`DirectionalLightShadowTransientAtlas`）与 mask，无静态缓存（cache_mode=Uncached），
   级联切分/mask shader 与 sunlight 路径共用。

shadow mask pass 全屏执行：重建世界坐标（GBufferE 法线做 normal_bias 偏移），按 view depth 选级联，
级联末端 8% 过渡带混合下一级联，(2r+1)² box PCF（r=pcf_radius），depth_bias 比较偏移。
动态 atlas tile 分配失败时外层级联逐级降分辨率（degraded 计数），仍失败则跳过该光源。

## 配置

`DirectionalShadowConfig`（`DirectionalShadowConfig.h`），scene json `scene_config.directional_shadows`，
经 `sanitize_directional_shadow_config` 清洗；两条路径共用：

| 字段 | 默认 | 含义 |
| --- | --- | --- |
| enabled | true | 总开关（关闭则两条路径都不跑） |
| default_cascade_count | 4 | 级联数（光源可用 shadow_cascade_count 覆盖） |
| default_shadow_distance | 160 | 阴影距离（光源可覆盖） |
| near_shadow_distance | 16 | 级联 0 范围 |
| split_lambda | 0.65 | 线性/对数切分插值 |
| near_cascade_resolution / outer_cascade_resolution | 2048 / 1024 | 级联 tile 分辨率 |
| dynamic_atlas_size / static_cache_atlas_size | 4096 / 4096 | 动态 / 静态缓存 atlas 尺寸 |
| static_cache_budget_mb | 64 | 静态缓存预算（超出 LRU 逐出） |
| depth_bias / normal_bias | 0.0015 / 0.05 | 深度 / 法线偏移 |
| pcf_radius | 1 | PCF 半径（0 = 单次采样） |

## 实现

- `SunLightShadowPass.h/.cpp`：`add_depth_passes`（plan 构建 + StaticCacheRefresh/DynamicCascade pass 族）、
  `add_shadow_mask_pass`、`add_cascade_debug_pass`；级联数据经 StorageBuffer 上传。
- `DirectionalLightShadowPass.h/.cpp`：`add_shadow_passes` + `add_shadow_mask_pass`。
- 级联矩阵：`DirectionalShadowCascadeMath.h/.cpp`。
- shader（`project/src/engine/Shaders/Shadow/`）：`DirectionalShadowCommon.hlsli`、
  `DirectionalShadowDepthTileClear.hlsl`（tile 深度清除）、`DirectionalShadowDepthCopy.hlsl`（静态缓存拷贝）、
  `DirectionalShadowMask.hlsl`（PCF mask）、`DirectionalShadowCascadeDebug.hlsl`（级联可视化）。
- plan 构建暴露 `build_sunlight_shadow_frame_plan_for_tests` 等测试钩子。

## 约束与已知限制

- sunlight 路径同帧最多 1 盏；多盏 sunlight = 无阴影（计入 skipped 统计并限流打日志）。
- 阴影 caster 仅静态网格（`render_shadow_static_meshes_to_pass`），按 SceneMobility
  Static/Stationary vs Movable 区分静态/动态 caster。
- 普通方向光路径逐光源独立 atlas，无跨帧缓存，光源多时开销线性增长。

## 验证

- `RunRenderGate.bat`（golden SSIM 0.995 / 跨后端 0.99）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView：`SunLight Shadow Dynamic Atlas`、`SunLight Shadow Static Cache`、
  `SunLight Shadow Mask`、`SunLight Shadow Cascade Index`、
  `Directional Light Shadow Transient Atlas` / `Transient Mask`。

## 历史

- `docs/superpowers/specs/2026-05-25-directional-csm-shadow-design.md`
- `docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md`
