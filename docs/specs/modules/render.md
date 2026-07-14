---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Render（帧编排层）

## 职责与边界

`project/src/engine/Function/Render/` 的帧编排层：`SceneRenderer` 消费 `ScenePresentationSubsystem` 产出的帧快照 `VisibleRenderFrame`（对逻辑场景的值拷贝，渲染期间不可变），按 `frame.render_config`（`SceneRenderConfig`）把各 feature pass 组织成一次 per-view 的 RenderGraph 并执行。本模块管 pass 顺序、graph 资源声明、实例 buffer / 时序状态等帧级编排；不管 RenderGraph 编译执行机制（见 [render-graph.md](render-graph.md)）、RHI 与双后端实现（见 [graphics.md](graphics.md)）、逻辑场景与可见性收集（见 [scene.md](scene.md)）。各 feature 的算法细节见对应 feature spec。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `SceneRenderer.h/.cpp` | 帧编排核心：`render_visible_frame()` 组 graph、TAA jitter、实例 buffer、temporal view state |
| `Renderer.h/.cpp` | `RenderDevice` 之上的提交层：`GraphicsPassContext` draw 收集、`dispatch()`、frame stats、transient RT 接口转发 |
| `RenderDevice.h/.cpp` | 资源创建（RT/buffer/sampler/program）、pass begin/end、barrier 提交、present、backbuffer capture；Impl 持有 `RHI::GraphicsContext` + `RHI::Swapchain` |
| `ScenePresentationSubsystem.h/.cpp` | 输出目标 / view binding 管理；`update_presentations()` 构建帧快照，`submit_presentations()` 调 `SceneRenderer::render_visible_frame()` |
| `RenderScene.h/.cpp` | `VisibleRenderFrame` 定义与 `build_visible_render_frame()` |
| `SceneRenderView.h` | `SceneRenderViewContext`（输出目标、clear、viewport、pick state 等 per-view 上下文） |
| `SceneDeferredGraphResources.h` | 一帧 graph 内共享的 texture ref 集合（GBuffer、depth、HDR、shadow、volumetric 等） |
| `GBufferLayout.h/.cpp` | DeferredHQ GBuffer 布局（5 个 attachment，D=motion vector，E=normal） |
| `*Pass.h/.cpp` | 各 feature pass 类（AO/Shadow/DeferredLighting/Environment/Sky/Particle/Volumetric/Bloom/TAA/ToneMap/DebugView） |
| `RenderAssetManager.h/.cpp`、`Material*.h/.cpp` | 渲染资产与材质 V2（见 [material-system.md](../features/material-system.md)） |

## 公共接口

- `SceneRenderer::initialize(Renderer*, DebugDrawService*)` / `shutdown()` / `handle_output_resized()` / `invalidate_temporal_history()`；后者清理 AO、TAA、体积光 history，不重置粒子模拟状态。
- `SceneRenderer::render_visible_frame(VisibleRenderFrame&, const SceneRenderViewContext&)`：一次 view 渲染入口。会写回 frame 的 `taa_enabled / taa_jitter_ndc / taa_previous_jitter_ndc` 并 jitter 投影矩阵。
- `SceneRenderer::draw_render_debug_view_ui(UIContext&)`、`complete_pending_pick_readbacks()`（editor GPU picking 回读）。
- `Renderer`：`begin_frame/end_frame/present`、资源创建转发、`begin_pass()+GraphicsPassContext::draw()`（支持 direct 或单条 non-indexed indirect draw）、`dispatch()`、`acquire/release_transient_render_target()`、`get_frame_stats()`。`begin_frame` 透传 swapchain acquire 三态；Retryable 时不创建/录制 command buffer，RenderDevice 只平衡 backend frame lifecycle。`RendererFrameStats` 同时携带 canonical `render_frame_id`、该帧 timing 的精确提交确认位，以及最多 3 个带各自 `frame_id` 的延迟完成 GPU sample。
- `RenderDevice`：同名资源创建实现、`begin_pass/end_pass`、`request_back_buffer_capture()/fetch_back_buffer_capture()`、`queue_render_target_texel_read()`；`get_render_frame_id()` 暴露只在成功 acquire 后递增的 canonical ID，`was_gpu_timing_frame_submitted()` 只返回本帧后端精确提交确认。
- `ScenePresentationSubsystem`：`create_output/create_view_binding/update_presentations/submit_presentations`，以及自动化使用的当前帧 `SceneSubmissionSnapshot`（attempted/succeeded/failed/capture-ready + render asset epoch）。

### Pass 序列（`SceneRenderer::render_visible_frame`，代码实际顺序）

1. `SceneGBufferPass`：DeferredHQ 5-MRT + `SceneDeferredDepth`（D32）。
2. `SceneEntityPickPass`（仅 editor pick 请求时）。
3. AO pass 族（`AmbientOcclusionPass::add_passes`，SSAO/HBAO/GTAO + blur/temporal）。
4. Sunlight CSM 深度 pass 族（`SunLightShadowPass::add_depth_passes`，配置开启时）。
5. `SceneDeferredLightingBasePass`；随后逐光源：shadow mask pass（sunlight CSM 或普通方向光路径）+ directional / point / spot lighting pass，MRT 累加 diffuse/specular。
6. `SceneDeferredEnvironmentLightingPass` → `SceneDeferredCompositePass`（写 `SceneDeferredSceneHDRLinear`）。
7. `SceneSkyBackgroundPass`。
8. `ParticleSystemPass`：每 emitter 稳定 compute 压实 + GPU 写 indirect args + billboard indirect draw，写回 HDR、深度只读；SoftOff 使用 `DepthTestOnly`，SoftOn 使用 `DepthTestAndShaderResource` 并在线性 view depth 中做相交淡出。
9. 体积光 pass 族（`VolumetricLightingPass::add_passes`，froxel compute 链或屏幕空间 fallback，输出替换 HDR ref）。
10. Bloom pass 族（`BloomPass::add_passes`，输出替换 HDR ref）。
11. `SceneTemporalAAResolvePass`（compute，输出替换 HDR ref）。
12. `SceneDeferredToneMapPass`：HDR → `SceneOutput`（external）。
13. `SceneRenderDebugViewPass` + `SceneViewOverlay*Pass` + `SceneDebugDrawOverlayPass`。

AO 处于 debug 可视化模式时，跳过阴影、光照合成、天空、粒子、体积光、Bloom 与 TAA，直接把 debug 输出接 tone-map。各 pass 的输入输出细节见 feature spec：[deferred-lighting](../features/deferred-lighting.md)、[shadows](../features/shadows.md)、[ambient-occlusion](../features/ambient-occlusion.md)、[skybox-ibl](../features/skybox-ibl.md)、[particles](../features/particles.md)、[volumetric-lighting](../features/volumetric-lighting.md)、[bloom](../features/bloom.md)、[taa](../features/taa.md)、[tonemap](../features/tonemap.md)、[render-debug-view](../features/render-debug-view.md)、[debug-draw](../features/debug-draw.md)。

### SceneRenderConfig

`Function/Scene/SceneConfig.h` 的 `SceneRenderConfig`：`ambient_occlusion / directional_shadows / bloom / volumetric_lighting / temporal_aa` 五个子配置，随场景 json `scene_config` 反序列化（见 [scene-config.md](../features/scene-config.md)），经 `VisibleRenderFrame::render_config` 逐帧带入，pass 组织按它决定 add/skip。

### RenderDevice / Renderer 与 RHI 的关系

`Application` 用 `RHI::GraphicsContext` + `RHI::Swapchain` 构造 `RenderDevice`（私有构造，friend）；`Renderer` 组合 `RenderDevice` 提供 pass 级 draw 收集与帧统计。依赖方向：SceneRenderer/各 Pass → RenderGraph → Renderer → RenderDevice → RHI。Function/Render 层不 include 后端（Vulkan/DX12）头；backend 差异全部封在 `Graphics/`。

GPU timing 生命周期由 `RenderDevice` 的后端无关 coordinator 与精确 command buffer 配对。只有 swapchain acquire 为 Completed、command buffer 非空且 `begin_record` 状态验证成功后才允许 `begin_frame`；Retryable/Failed acquire 与 record 失败的路径必须保持 telemetry begin count 为 0。帧末先在同一 command buffer 仍处于 Recording 时写 end timestamp，再结束录制和提交；`GraphicsContext::end_frame` 完成后才调用 telemetry commit。只有 commit 返回 true 时 `gpu_timing_frame_submitted` 才为 true，消费者不得把失败或未确认帧计入 submitted/coverage。

完成样本是延迟、非阻塞传输：`Renderer` 在 begin/end/complete 三个安全点轮询，每次写入 `RendererFrameStats` 的固定 3-entry array，遇到 Pending/Empty 或数组满立即停止，不等待 GPU、不分配动态内存。sample 的 `frame_id` 可早于当前 `render_frame_id`，关联必须使用 sample 自带 ID，禁止按“当前帧”猜测归属。

`StorageBufferDesc::indirect_args` 申请可被 GPU 写入并被 draw 间接消费的 buffer；`GraphicsDrawDesc::indirect_args_buffer/offset` 选择 indirect 路径。提交前必须转换到 `AshResourceState::IndirectArgs`，且 indirect 与 direct 参数互斥。

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
- 每 view 每帧新建 `RenderGraphBuilder`，graph 资源/ref 不跨帧缓存；跨帧资源（TAA history、shadow static cache）由 pass 类自持 `RenderTarget` 并以 external 注册。
- 输出尺寸上限 `uint16_t`（graph texture desc 限制）。
- temporal 状态按 view key（`view_id`，否则 output target 指针）隔离，多 viewport 互不污染。
- 粒子状态按 `scene_runtime_id + entity_id` 隔离；capacity、scene content epoch 或模拟参数 fingerprint 改变时仅重置对应 emitter。删除/解绑场景必须释放相关状态并清空 program 的 buffer 引用。
- 实例 buffer 为「逻辑 slot + 3 帧物理 ring」，epoch 取渲染侧 `Application::get_frame_index()`（不是 `VisibleRenderFrame::frame_index`）；temporal history 只允许 GBuffer pass 使用。禁止改回单物理 slot：Vulkan Release 下 CPU 写 host-visible buffer 会覆盖 GPU 正在读的上一帧实例矩阵，导致 GBuffer depth/normal/motion vector 裂缝闪烁。
- 双后端等价：所有 pass 必须 Vulkan / DX12 行为一致，跨后端 diff FAIL 视同 bug。
- 粒子 sprite/radial/soft-depth 仅扩展 Function pass、frame asset prepare 与 HLSL program variants；pass 顺序保持 Sky 后、Volumetric 前，未修改 RHI 或 RenderGraph core 接口。

## 验证

对齐 `docs/VERIFY.md`「渲染 Pass / shader / 材质」行：构建 + `RunRenderGate.bat`（双后端 golden SSIM + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard`；检查 `product/logs` 无 validation 报错。渲染异常用 `[RenderDebugView]` 分通道定位。

## 历史

- `docs/superpowers/specs/2026-05-14-render-graph-design.md`（graph 化迁移）
- `docs/superpowers/specs/2026-05-12-deferred-gbuffer-design.md`、`2026-05-12-deferred-lighting-design.md`
- `docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md`
- `docs/sdd/SDD-2026-07-07-render-gate.md`（backbuffer capture + 抓帧确定性）
- [SDD-2026-07-10-gpu-particles](../../sdd/SDD-2026-07-10-gpu-particles.md)（GPU 粒子 pass 与稳定 capture-ready）
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)（资源 epoch、提交快照与 temporal history invalidation）
