---
owner: huyizhou
last_reviewed: 2026-07-26
status: active
---

# Module Spec: Render（帧编排层）

## 职责与边界

`project/src/engine/Function/Render/` 的帧编排层：`SceneRenderer` 消费 `ScenePresentationSubsystem` 产出的帧快照 `VisibleRenderFrame`（对逻辑场景的值拷贝，渲染期间不可变），按 `frame.render_config`（`SceneRenderConfig`）把各 feature pass 组织成一次 per-view 的 RenderGraph 并执行。本模块管 pass 顺序、graph 资源声明、实例 buffer / 时序状态等帧级编排；不管 RenderGraph 编译执行机制（见 [render-graph.md](render-graph.md)）、RHI 与双后端实现（见 [graphics.md](graphics.md)）、逻辑场景与可见性收集（见 [scene.md](scene.md)）。各 feature 的算法细节见对应 feature spec。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `SceneRenderer.h/.cpp` | 帧编排核心：`render_visible_frame()` 组 graph、TAA jitter、实例 buffer、temporal view state |
| `Renderer.h/.cpp` | `RenderDevice` 之上的提交层：`GraphicsPassContext` draw 收集、`dispatch()`、frame stats（含 submitted frame/GPU timing 结果）、transient RT 接口转发 |
| `RenderDevice.h/.cpp` | 资源创建（RT/buffer/sampler/program）、pass begin/end、barrier 提交、present、backbuffer capture 与主 command buffer GPU timing；Impl 持有 `RHI::GraphicsContext` + `RHI::Swapchain` |
| `ScenePresentationSubsystem.h/.cpp` | 输出目标 / view binding 管理；`update_presentations()` 构建帧快照，`submit_presentations()` 调 `SceneRenderer::render_visible_frame()` |
| `RenderScene.h/.cpp` | `VisibleRenderFrame` 定义与 `build_visible_render_frame()` |
| `TerrainRenderProxy.h/.cpp` | Terrain snapshot/render-asset proxy、world AABB 与 `VisibleTerrainFrame` 生成 |
| `TerrainLod.h/.cpp` | 纯 CPU Component quadtree culling、投影误差选级、邻接修复与稳定 per-LOD instance batches |
| `TerrainRenderPass.h/.cpp` | Terrain persistent atlas graph 注册、dirty raw staging、9 级共享 grid、GBuffer/方向光阴影 draw、LOD debug 与 capture readiness |
| `SceneRenderView.h` | `SceneRenderViewContext`（输出目标、clear、viewport、pick state 等 per-view 上下文） |
| `SceneDeferredGraphResources.h` | 一帧 graph 内共享的 texture ref 集合（GBuffer、depth、HDR、shadow、volumetric 等） |
| `GBufferLayout.h/.cpp` | DeferredHQ GBuffer 布局（5 个 attachment，D=motion vector，E=normal） |
| `*Pass.h/.cpp` | 各 feature pass 类（AO/Shadow/DeferredLighting/Environment/Sky/Particle/Volumetric/Bloom/TAA/ToneMap/DebugView） |
| `RenderAssetManager.h/.cpp`、`Material*.h/.cpp` | 渲染资产与材质 V2（见 [material-system.md](../features/material-system.md)） |

## 公共接口

- `SceneRenderer::initialize(Renderer*, DebugDrawService*)` / `shutdown()` / `handle_output_resized()` / `invalidate_temporal_history()`；后者清理 AO、TAA、体积光 history，不重置粒子模拟状态。
- `SceneRenderer::render_visible_frame(VisibleRenderFrame&, const SceneRenderViewContext&)`：一次 view 渲染入口。会写回 frame 的 `taa_enabled / taa_jitter_ndc / taa_previous_jitter_ndc` 并 jitter 投影矩阵。
- `SceneRenderer::draw_render_debug_view_ui(UIContext&)`、`complete_pending_pick_readbacks()`（editor GPU picking 回读）。
- `Renderer`：`begin_frame/end_frame/present`、资源创建转发、`begin_pass()+GraphicsPassContext::draw()`（direct、显式 `NonIndexed` 或 `Indexed` indirect）、`dispatch()`、transient render-target/storage-buffer pool 转发、`get_frame_stats()`。`begin_frame` 透传 swapchain acquire 三态；Retryable 时不创建/录制 command buffer，RenderDevice 只平衡 backend frame lifecycle。`RendererFrameStats` 同时携带 canonical `render_frame_id`、该帧 timing 的精确提交确认位，以及最多 3 个带各自 `frame_id` 的延迟完成 GPU sample。
- `RenderDevice`：同名资源创建实现、`begin_pass/end_pass`、`request_back_buffer_capture()/fetch_back_buffer_capture()`、`queue_render_target_texel_read()`；`get_render_frame_id()` 暴露只在成功 acquire 后递增的 canonical ID，`was_gpu_timing_frame_submitted()` 只返回本帧后端精确提交确认。`Texture2DArrayUploadDesc` 可创建一个带原生 2D-array SRV 的 sampled 资源；提供初始数据时必须覆盖每个唯一 `(array layer, mip)`，上传按 layer-major / mip-major 紧密重排且总量受 RHI 32 位上传大小上限约束。返回值是单个 `RenderTarget`，shader 的 `Texture2DArray` 参数通过 `set_texture` 绑定，不把各 layer 当成 `set_texture_array` 的多资源描述符数组。
- `TerrainRenderAsset`：消费不可变 `TerrainAssetSnapshot`，从实际 1–32 × 1–32 矩形 Component 布局推导 height bytes 与 coarse dimensions，按 Component pointer diff 生成当前 content generation 的 packed R16 高度和两路 RGBA8 权重 payload；拥有动态 height/coarse resources、staging buffer、两张固定 4144²/256-slot weight atlas、三张 8-slice material arrays 与帧边界 slot metadata。布局变化或资产 replacement 必须先验证完整非空 row-major table，再构造完整 candidate resource bundle；成功在帧边界原子发布，任一验证/分配/上传失败保留旧 published view 并产生精确阶段诊断。`RenderAssetManager` 以规范化 Terrain key 把 request/finalize、pending/failed 和 activity epoch 合入通用 readiness；全目录 refresh 对未变化容器产生不同 snapshot 指针时，仅当资产 ID、generation/residency 与双方有效物理 `TerrainContainerRevision` 全部相同时按幂等请求保留既有 pointer/readiness/activity，不放宽其他 stale snapshot 的拒绝；GPU finalize 仅允许 render thread。
- `RenderTerrainProxy` / `RenderScene`：按 Scene Terrain extraction 构建 snapshot/resources/bounds 同代的 published proxy。首次 load 在 published view 建立前保持 Pending 且不可见；同一路径异步 load 或 candidate failure 保留旧 published proxy，只有实际 asset path 改变才清除。transform-only 更新以新 proxy 集合原子替换；`build_visible_render_frame` 对 published bounds 做 frustum 裁剪并写入 `VisibleRenderFrame::terrains`，由 `TerrainRenderPass` 在 GBuffer/shadow 路径消费。
- `TerrainLod`：消费一个 immutable Terrain snapshot、world transform 与 `SceneView`，以 Component 根 min/max 构建隐式 quadtree，按投影误差选择 9 级共享网格并只向更细方向修复邻接。输出每个非空 LOD 一个 `first_instance == 0` 的稳定 batch，instance 携带坐标、较粗邻边掩码和 morph factor；渲染侧把所有 batch 打包到 3 帧 ring 的 StorageBuffer，以 root constant batch offset 索引。
- `TerrainRenderPass::prepare_graph`：对第一个 snapshot 与 render asset published generation 一致的 visible 主 Terrain 注册两张固定 4144² atlas 和一张实际 `(component_count_x × 32 + 1) × (component_count_z × 32 + 1)` coarse target；有 dirty payload 时最多排入一个 raw staging upload，并添加写三张 texture 的 `TerrainWeightAtlasUpdatePass`。dispatch 成功才更新 slot metadata 并从 pending 队列消费该项。`SceneGBufferPass` 声明同三张 texture 的 `GraphicsSRV` 读取，形成 compute→graphics barrier；首期一个 scene/view 只渲染第一个有效主 Terrain，多 Terrain 独立 program binding 留待后续。
- `TerrainRenderPass::initialize/render_gbuffer/render_shadow`：一次创建 LOD0..8 的 32-bit shared index buffers、weight/material samplers，以及 `TerrainSurface.hlsl` 的 GBuffer/depth-only/LOD-debug permutation。网格 draw 不绑定 vertex buffer，顶点坐标来自 index 值对应的 `SV_VertexID`；surface instance 使用 packed `uint4`，`TerrainSurfaceConstants` 固定 240 bytes，末尾 16 bytes 携带实际 sample/component layout。GBuffer 在既有 clear pass 内执行，shadow 通过 sunlight/普通方向光共用 caster callback 执行并尊重 `casts_shadow` / mobility filter。
- sunlight outer-cascade static cache 的 caster revision 绑定 scene runtime/content epoch，并逐项散列实际 Static/Stationary mesh shadow draw（transform、section、mesh GPU publication、已准备 DepthOnly material publication）及 `casts_shadow=true` Terrain（snapshot、一次锁内 render-publication identity、transform）。精确 draw 集合已覆盖 caster 增删，故全体 primitive/static-scene revision、全局 transform revision、Movable mesh 与 `casts_shadow=false` Terrain 都不进入该 identity。planner 不提交 cache；只有 static refresh callback 的 tile clear 与 `StaticOnly` draw 都成功录入 CPU command recording 后才提交 revision/light VP。RenderGraph 当前不暴露 pass-end、queue submit 或 GPU completion，故该提交点不宣称 GPU 完成。
- `TerrainRenderPass::is_capture_ready`：要求 visible Terrain 的 snapshot、accepted/published generation、Ready 状态和 pending upload 全部一致，并等待 atlas compute 所在 frame 之后的后续 prepare；`SceneRenderer` 与粒子 readiness 取逻辑与，不使用固定帧数。
- `ScenePresentationSubsystem`：`create_output/create_view_binding/update_presentations/submit_presentations`，以及自动化使用的当前帧 `SceneSubmissionSnapshot`（attempted/succeeded/failed/capture-ready + render asset epoch）。render asset epoch 必须在 visible frame 构建前捕获；构建中发生 publication 变化时，该帧不能在循环结束后以新 epoch 误标为 current。Terrain capture-ready 复用 `evaluate_terrain_readiness`：load、compose、height upload 与 atlas 必须属于同一 content generation，且当前 Scene packet 已成功；当前代 Failed 优先，stale 结果保持 Pending。

### Pass 序列（`SceneRenderer::render_visible_frame`，代码实际顺序）

1. 可选 `TerrainWeightAtlasUpdatePass`，随后 `SceneGBufferPass`：DeferredHQ 5-MRT + `SceneDeferredDepth`（D32）；同一 clear pass 内 static mesh 后接 Terrain GBuffer。
2. `TerrainLodDebugPass`（仅 Render Debug View 选择 Terrain LOD 时；读取既有 depth）。
3. `SceneEntityPickPass`（仅 editor pick 请求时）。
4. AO pass 族（`AmbientOcclusionPass::add_passes`，SSAO/HBAO/GTAO + blur/temporal）。
5. Sunlight CSM 深度 pass 族（`SunLightShadowPass::add_depth_passes`，配置开启时）；static mesh 后接允许投影的 Terrain。
6. `SceneDeferredLightingBasePass`；随后逐光源：shadow mask pass（sunlight CSM 或普通方向光路径）+ directional / point / spot lighting pass，MRT 累加 diffuse/specular。普通方向光 shadow caster 同样包含 Terrain。
7. `SceneDeferredEnvironmentLightingPass` → `SceneDeferredCompositePass`（写 `SceneDeferredSceneHDRLinear`）。
8. `SceneSkyBackgroundPass`。
9. `ParticleSystemPass`：每 emitter 稳定 compute 压实 + GPU 写 indirect args + billboard indirect draw，写回 HDR、深度只读；SoftOff 使用 `DepthTestOnly`，SoftOn 使用 `DepthTestAndShaderResource` 并在线性 view depth 中做相交淡出。
10. 体积光 pass 族（`VolumetricLightingPass::add_passes`，froxel compute 链或屏幕空间 fallback，输出替换 HDR ref）。
11. Bloom pass 族（`BloomPass::add_passes`，输出替换 HDR ref）。
12. `SceneTemporalAAResolvePass`（compute，输出替换 HDR ref）。
13. `SceneDeferredToneMapPass`：HDR → `SceneOutput`（external）。
14. `SceneRenderDebugViewPass` + `SceneViewOverlay*Pass` + `SceneDebugDrawOverlayPass`。

AO 处于 debug 可视化模式时，跳过阴影、光照合成、天空、粒子、体积光、Bloom 与 TAA，直接把 debug 输出接 tone-map。各 pass 的输入输出细节见 feature spec：[deferred-lighting](../features/deferred-lighting.md)、[shadows](../features/shadows.md)、[ambient-occlusion](../features/ambient-occlusion.md)、[skybox-ibl](../features/skybox-ibl.md)、[particles](../features/particles.md)、[volumetric-lighting](../features/volumetric-lighting.md)、[bloom](../features/bloom.md)、[taa](../features/taa.md)、[tonemap](../features/tonemap.md)、[render-debug-view](../features/render-debug-view.md)、[debug-draw](../features/debug-draw.md)。

### SceneRenderConfig

`Function/Scene/SceneConfig.h` 的 `SceneRenderConfig`：`ambient_occlusion / directional_shadows / bloom / volumetric_lighting / temporal_aa` 五个子配置，随场景 json `scene_config` 反序列化（见 [scene-config.md](../features/scene-config.md)），经 `VisibleRenderFrame::render_config` 逐帧带入，pass 组织按它决定 add/skip。

### RenderDevice / Renderer 与 RHI 的关系

`Application` 用 `RHI::GraphicsContext` + `RHI::Swapchain` 构造 `RenderDevice`（私有构造，friend）；`Renderer` 组合 `RenderDevice` 提供 pass 级 draw 收集与帧统计。依赖方向：SceneRenderer/各 Pass → RenderGraph → Renderer → RenderDevice → RHI。Function/Render 层不 include 后端（Vulkan/DX12）头；backend 差异全部封在 `Graphics/`。

GPU timing 生命周期由 `RenderDevice` 的后端无关 coordinator 与精确 command buffer 配对。只有 swapchain acquire 为 Completed、command buffer 非空且 `begin_record` 状态验证成功后才允许 `begin_frame`；Retryable/Failed acquire 与 record 失败的路径必须保持 telemetry begin count 为 0。帧末先在同一 command buffer 仍处于 Recording 时写 end timestamp，再结束录制和提交；`GraphicsContext::end_frame` 完成后才调用 telemetry commit。只有 commit 返回 true 时 `gpu_timing_frame_submitted` 才为 true，消费者不得把失败或未确认帧计入 submitted/coverage。

完成样本是延迟、非阻塞传输：`Renderer` 在 begin/end/complete 三个安全点轮询，每次写入 `RendererFrameStats` 的固定 3-entry array，遇到 Pending/Empty 或数组满立即停止，不等待 GPU、不分配动态内存。sample 的 `frame_id` 可早于当前 `render_frame_id`，关联必须使用 sample 自带 ID，禁止按“当前帧”猜测归属。

### Graphics indirect contract

`GraphicsDrawDesc::indirect_kind` 必须显式为 `None / NonIndexed / Indexed`。indirect 路径同时提供带 `indirect_args` usage 的 `StorageBuffer`、对齐 offset、非零 draw count，以及 0（使用原生结构大小）或合法结构 stride；范围计算使用 checked arithmetic。`Indexed` 必须绑定 index buffer并先完成 index bind，`NonIndexed` 禁止携带 index buffer。两类 indirect 都与 direct 的 count/first/offset 字段互斥，冲突输入 fail-closed，不静默忽略。提交前 args 必须转换到 `AshResourceState::IndirectArgs`；RHI 仍复用既有 `cmd_draw_indirect` / `cmd_draw_indexed_indirect`，没有新增 Graphics virtual API。Particle 已显式迁移为 `NonIndexed`，其渲染行为不变。

### GPUDriven experimental foundation

`Function/Render/GPUDriven/` 提供后续 grass/tree 与普通 static-mesh GPU path 可共用的最小底座：非零 `GpuDrivenPrototypeId`、`slot+generation` page handle、按 canonical completed frame 延迟回收的 page allocator、版本化 instance page desc、`CompressedTRS` 32-byte / `Affine3x4F32` 48-byte encoding、view/draw-group 数据，以及验证后创建 `StorageBuffer` 的 ownership helper。generation 回绕会永久 seal slot，避免 ABA；payload 字节数、capacity/count 与 stride 都先做 checked validation。

该目录当前是 experimental foundation，不是生产植被系统：尚未实现 prototype 资产入口、SpeedTree、分块流送、GPU culling/HZB、HLOD/远景替代、GPU grass/tree shader family 或 Editor 植被笔刷。后续功能必须另写 S2 设计并复用这里的通用 page/buffer 契约，禁止向底座泄漏 vegetation-specific 字段。

全链诊断 `--rhi-selftest-indirect --run-for-frames=1` 在 raw RHI 自测后执行一次 Function lifecycle：external candidate → transient visible/args → compute UAV → indexed indirect raster → args `GraphicsSRV` validation → bounded capture。begin 成功后 scope guard 保证 `end_frame` exactly-once；回调异常转受控失败。oracle 用非恒等 index/firstIndex=1，误发 non-indexed native command 会退化而不能伪 PASS。

### GPU timing / PerfGate bridge

`RenderDevice` 在 canonical graphics command buffer 上拥有固定 `GPU.Frame` 生命周期；只有后端精确确认提交的 frame 才进入 PerfGate expected set。RenderGraph pass 只携带固定 `GpuTimingMetric` 或 `Invalid`：executor 把相邻同类 pass 合并为一个非重叠 group scope，metric 变化时先关闭前一组再打开后一组，失败/中止路径由 lifecycle coordinator fail-closed 收口。完成样本通过 `RendererFrameStats.completed_gpu_timing_samples` 延迟、非阻塞地交给 PerfGate，关联键始终是 sample 自带的 `frame_id`。

`SceneGBufferPass` 的固定组为 `GPU.GBuffer`，方向光阴影 pass 的固定组为 `GPU.Shadows`；Terrain draw 作为这些既有 pass 的 callback 工作被包含在组总时长中，但这不等价于独立的 `Terrain.GBuffer` / `Terrain.Shadow` 指标。Terrain atlas update、LOD debug 与测试 contract 当前使用 `Invalid`，不得把固定组聚合结果冒充 Terrain 专属 required scope；若需要专属可比较指标，必须另行完成 S2 设计与双后端验证。

### Backbuffer capture（RenderGate，SDD-2026-07-07-render-gate）

`--dump-frame=<png>` 走 readiness 两阶段握手：前一 ready frame 先清空 AO/TAA/体积光中被加载中画面污染的 history，再 arm `RenderAssetManager` activity epoch；下一帧开始前仍相同才请求 capture；present 后要求当前 frame 的全部预期 scene packet 成功、提交 epoch 等于最新 asset epoch，且动态内容 capture-ready。失效 capture 只读回丢弃并重试；wall-clock 超时非零退出且不写 PNG。该语义化 history invalidation 取代固定“收敛余量帧”。

`RenderAssetManager::query_readiness()` 在一次锁内 O(1) 返回 `activity_epoch/pending/failed`；新 render-visible cache miss 与异步终态推进 epoch，cache hit 不推进。Static mesh 在同步 CPU load 前登记 pending，render finalizer 对 Loading asset 使用非阻塞锁，禁止让 render 线程等待 logic 线程磁盘 IO。Texture 显式 Failed 即使持有 fallback 也使自动化失败；材质/IBL 成功 fallback 仍是合法降级。

可见帧资产准备属于 submit/render thread：`finalize_pending_assets()` 仍在 submit 开头执行，随后 material proxy prepare 成功后为每个 particle emitter 请求 sRGB sprite（空路径为 White fallback），并把 `TextureAsset` shared handle 写入该次 `VisibleRenderFrame`。logic-side `RenderScene::rebuild_particles_from_scene()` 只复制 `ParticleComponent`/path，不请求 GPU texture；准备失败计入对应 scene packet failure，且不进入 `SceneRenderer`。

### TAA jitter 确定性约定

frame-dump 模式下 TAA jitter 强制为 `(0,0)`；提交给渲染侧的 frame index 每帧稳定递增，`delta_seconds` 固定为 `1/60`，使跨帧模拟不受 logic/render 调度速度影响。改动此约定必须同步改 RenderGate 阈值预期。

动态 capture-ready 不是固定预热帧：无动态粒子的帧立即 ready；粒子 emitter 以 `min(max_particles, ceil(spawn_rate × (lifetime + lifetime_variance)))` 的累计成功模拟 spawn 数作为稳定窗口信号。普通 smoke 不等待该视觉稳定窗口，frame dump 等待。

普通模式的 `delta_seconds` 取相邻实际进入 scene render 的新 Application frame 之间的 steady-clock 间隔；空 packet、未进入 render 的输出/材质准备失败、以及重复同帧 submit 不推进时钟。一旦进入 render，即使调用随后失败也消费该帧时间，避免非事务式渲染失败后重复积分；同一 Application frame 内的额外 view 只重复绘制，不重复模拟。

## 约束与不变式

- `VisibleRenderFrame` 是快照：渲染只读场景数据（TAA 字段除外，由 SceneRenderer 写回）；不得在渲染路径回访 `Scene`（pick 回读除外，经显式 readback 队列）。
- Terrain topology 使用独立 scene revision；同帧 topology 与 transform 均变化时先重建 proxy 集合，再更新 transform，避免访问已删除实体。已发布 `VisibleTerrainFrame` 持有自己的 const snapshot 与 render asset shared ownership，后续 rebuild 不得改写它。
- 每 view 每帧新建 `RenderGraphBuilder`，graph 资源/ref 不跨帧缓存；跨帧资源（TAA history、shadow static cache）由 pass 类自持 `RenderTarget` 并以 external 注册。
- 输出尺寸上限 `uint16_t`（graph texture desc 限制）。
- temporal 状态按 view key（`view_id`，否则 output target 指针）隔离，多 viewport 互不污染。
- 粒子状态按 `scene_runtime_id + entity_id` 隔离；capacity、scene content epoch 或模拟参数 fingerprint 改变时仅重置对应 emitter。删除/解绑场景必须释放相关状态并清空 program 的 buffer 引用。
- Terrain weight staging 是单个 raw buffer；一个 graph 最多上传并 dispatch 一个 Component。禁止为了批处理而在同一 graph 里反复覆盖该 staging 后再提交多个 dispatch，除非先引入可证明独立生命周期的 ring/offset 方案。
- Terrain shared grid 固定使用 9 个 LOD、`uint32_t` index 和零 vertex stream；GBuffer/depth permutation 必须复用同一 packed-height/morph helper。材质权重 tie 只能以较小 layer index 获胜，全部为零必须回退 Layer 0，禁止依赖 texture/filter 遍历顺序。
- Terrain GBuffer 必须复用既有 `SceneGBufferPass` 的 attachments 与一次 clear；atlas update 必须先写 `ComputeUAV`，GBuffer 再读 `GraphicsSRV`。方向光 shadow callback 组合 static mesh 与 Terrain，Terrain 不进入 `DynamicOnly` cache 更新。Terrain generation 变化只失效对应 temporal view 的 TAA history。
- 屏幕空间 scene-depth coverage 必须通过 `Shaders/Scene/SceneDepthCommon.hlsli` 的统一 helper 判定：reverse-Z 仅 `depth <= 0.0`、normal-Z 仅 `depth >= 1.0` 视为 depth target 背景。禁止用固定 epsilon 代替 clear 端点，因为有限远平面下的合法远端 reverse-Z 深度可任意接近零。
- 实例 buffer 为「逻辑 slot + 3 帧物理 ring」，epoch 取渲染侧 `Application::get_frame_index()`（不是 `VisibleRenderFrame::frame_index`）；temporal history 只允许 GBuffer pass 使用。禁止改回单物理 slot：Vulkan Release 下 CPU 写 host-visible buffer 会覆盖 GPU 正在读的上一帧实例矩阵，导致 GBuffer depth/normal/motion vector 裂缝闪烁。
- GPU timing 身份来自固定 `GpuTimingMetric` 枚举而非 pass 名 hash；RenderGraph 同一时刻最多有一个 group scope。duplicate/overlap/incomplete、invalid/unresolved frame 与 coverage 不足均由主线 PerfGate schema v2 fail-closed，禁止静默补 CPU 值、复用别帧结果或把聚合组改名成 Terrain 专属指标。
- 双后端等价：所有 pass 必须 Vulkan / DX12 行为一致，跨后端 diff FAIL 视同 bug。
- 粒子 sprite/radial/soft-depth 仅扩展 Function pass、frame asset prepare 与 HLSL program variants；pass 顺序保持 Sky 后、Volumetric 前，未修改 RHI 或 RenderGraph core 接口。

## 验证

对齐 `docs/VERIFY.md`「渲染 Pass / shader / 材质」与「RenderGraph 核心」行：构建 + `RunRenderGate.bat` + PerfGate Standard；改 graph buffer/indirect/GPUDriven 底座时还必须运行双后端 bounded indirect self-test 与 `VegetationFullPipeline` non-bless compare。检查每个 session 的日志无 validation/debug-layer 报错。渲染异常用 `[RenderDebugView]` 分通道定位。

## 历史

- `docs/superpowers/specs/2026-05-14-render-graph-design.md`（graph 化迁移）
- `docs/superpowers/specs/2026-05-12-deferred-gbuffer-design.md`、`2026-05-12-deferred-lighting-design.md`
- `docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md`
- `docs/sdd/SDD-2026-07-07-render-gate.md`（backbuffer capture + 抓帧确定性）
- [SDD-2026-07-10-gpu-particles](../../sdd/SDD-2026-07-10-gpu-particles.md)（GPU 粒子 pass 与稳定 capture-ready）
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)（资源 epoch、提交快照与 temporal history invalidation）
- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)（Terrain render asset、LOD、GBuffer/方向光阴影与 capture readiness）
- [SDD-2026-07-13-gpu-driven-foundation](../../sdd/SDD-2026-07-13-gpu-driven-foundation.md)（显式 indexed/non-indexed Function contract、GPUDriven experimental foundation 与全链自测）
