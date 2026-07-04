---
owner: huyizhou
last_reviewed: 2026-07-04
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
| `*Pass.h/.cpp` | 各 feature pass 类（AO/Shadow/DeferredLighting/Environment/Sky/Volumetric/Bloom/TAA/ToneMap/DebugView） |
| `RenderAssetManager.h/.cpp`、`Material*.h/.cpp` | 渲染资产与材质 V2（见 [material-system.md](../features/material-system.md)） |

## 公共接口

- `SceneRenderer::initialize(Renderer*, DebugDrawService*)` / `shutdown()` / `handle_output_resized()`。
- `SceneRenderer::render_visible_frame(VisibleRenderFrame&, const SceneRenderViewContext&)`：一次 view 渲染入口。会写回 frame 的 `taa_enabled / taa_jitter_ndc / taa_previous_jitter_ndc` 并 jitter 投影矩阵。
- `SceneRenderer::draw_render_debug_view_ui(UIContext&)`、`complete_pending_pick_readbacks()`（editor GPU picking 回读）。
- `Renderer`：`begin_frame/end_frame/present`、资源创建转发、`begin_pass()+GraphicsPassContext::draw()`、`dispatch()`、`acquire/release_transient_render_target()`、`get_frame_stats()`。
- `RenderDevice`：同名资源创建实现、`begin_pass/end_pass`、`request_back_buffer_capture()/fetch_back_buffer_capture()`、`queue_render_target_texel_read()`。
- `ScenePresentationSubsystem`：`create_output/create_view_binding/update_presentations/submit_presentations` 等。

### Pass 序列（`SceneRenderer::render_visible_frame`，代码实际顺序）

1. `SceneGBufferPass`：DeferredHQ 5-MRT + `SceneDeferredDepth`（D32）。
2. `SceneEntityPickPass`（仅 editor pick 请求时）。
3. AO pass 族（`AmbientOcclusionPass::add_passes`，SSAO/HBAO/GTAO + blur/temporal）。
4. Sunlight CSM 深度 pass 族（`SunLightShadowPass::add_depth_passes`，配置开启时）。
5. `SceneDeferredLightingBasePass`；随后逐光源：shadow mask pass（sunlight CSM 或普通方向光路径）+ directional / point / spot lighting pass，MRT 累加 diffuse/specular。
6. `SceneDeferredEnvironmentLightingPass` → `SceneDeferredCompositePass`（写 `SceneDeferredSceneHDRLinear`）。
7. `SceneSkyBackgroundPass`。
8. 体积光 pass 族（`VolumetricLightingPass::add_passes`，froxel compute 链或屏幕空间 fallback，输出替换 HDR ref）。
9. Bloom pass 族（`BloomPass::add_passes`，输出替换 HDR ref）。
10. `SceneTemporalAAResolvePass`（compute，输出替换 HDR ref）。
11. `SceneDeferredToneMapPass`：HDR → `SceneOutput`（external）。
12. `SceneRenderDebugViewPass` + `SceneViewOverlay*Pass` + `SceneDebugDrawOverlayPass`。

AO 处于 debug 可视化模式时（`ao_outputs.debug_view` 有效），跳过第 4–9 步直接把 debug 输出接 tone-map。各 pass 的输入输出细节见 feature spec：[deferred-lighting](../features/deferred-lighting.md)、[shadows](../features/shadows.md)、[ambient-occlusion](../features/ambient-occlusion.md)、[skybox-ibl](../features/skybox-ibl.md)、[volumetric-lighting](../features/volumetric-lighting.md)、[bloom](../features/bloom.md)、[taa](../features/taa.md)、[tonemap](../features/tonemap.md)、[render-debug-view](../features/render-debug-view.md)、[debug-draw](../features/debug-draw.md)。

### SceneRenderConfig

`Function/Scene/SceneConfig.h` 的 `SceneRenderConfig`：`ambient_occlusion / directional_shadows / bloom / volumetric_lighting / temporal_aa` 五个子配置，随场景 json `scene_config` 反序列化（见 [scene-config.md](../features/scene-config.md)），经 `VisibleRenderFrame::render_config` 逐帧带入，pass 组织按它决定 add/skip。

### RenderDevice / Renderer 与 RHI 的关系

`Application` 用 `RHI::GraphicsContext` + `RHI::Swapchain` 构造 `RenderDevice`（私有构造，friend）；`Renderer` 组合 `RenderDevice` 提供 pass 级 draw 收集与帧统计。依赖方向：SceneRenderer/各 Pass → RenderGraph → Renderer → RenderDevice → RHI。Function/Render 层不 include 后端（Vulkan/DX12）头；backend 差异全部封在 `Graphics/`。

### Backbuffer capture（RenderGate，SDD-0001）

`--dump-frame=<png>` + `--smoke-test=N` 时，Application 主循环在最后一帧渲染前调 `RenderDevice::request_back_buffer_capture()`；RenderDevice 在该帧 present 前回读 backbuffer（`record_back_buffer_capture()`，同步回读，仅显式请求时有开销）；present 后 Application 经 `fetch_back_buffer_capture()` 取 RGBA8 像素写 PNG。

### TAA jitter 确定性约定

frame-dump 模式（`Application::get_frame_dump_path()` 非空）下 TAA 亚像素 jitter 强制为 `(0,0)`，其余 TAA 逻辑照常。原因：时序抖动使同参数两次 dump 有全画面边缘噪声（SSIM 底约 0.989），关掉后静态场景收敛为确定画面。改动此约定必须同步改 RenderGate 阈值预期。

## 约束与不变式

- `VisibleRenderFrame` 是快照：渲染只读场景数据（TAA 字段除外，由 SceneRenderer 写回）；不得在渲染路径回访 `Scene`（pick 回读除外，经显式 readback 队列）。
- 每 view 每帧新建 `RenderGraphBuilder`，graph 资源/ref 不跨帧缓存；跨帧资源（TAA history、shadow static cache）由 pass 类自持 `RenderTarget` 并以 external 注册。
- 输出尺寸上限 `uint16_t`（graph texture desc 限制）。
- temporal 状态按 view key（`view_id`，否则 output target 指针）隔离，多 viewport 互不污染。
- 实例 buffer 为「逻辑 slot + 3 帧物理 ring」，epoch 取渲染侧 `Application::get_frame_index()`（不是 `VisibleRenderFrame::frame_index`）；temporal history 只允许 GBuffer pass 使用。禁止改回单物理 slot：Vulkan Release 下 CPU 写 host-visible buffer 会覆盖 GPU 正在读的上一帧实例矩阵，导致 GBuffer depth/normal/motion vector 裂缝闪烁。
- 双后端等价：所有 pass 必须 Vulkan / DX12 行为一致，跨后端 diff FAIL 视同 bug。

## 验证

对齐 `docs/VERIFY.md`「渲染 Pass / shader / 材质」行：构建 + `RunRenderGate.bat`（双后端 golden SSIM + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard`；检查 `product/logs` 无 validation 报错。渲染异常用 `[RenderDebugView]` 分通道定位。

## 历史

- `docs/superpowers/specs/2026-05-14-render-graph-design.md`（graph 化迁移）
- `docs/superpowers/specs/2026-05-12-deferred-gbuffer-design.md`、`2026-05-12-deferred-lighting-design.md`
- `docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md`
- `docs/sdd/SDD-0001-render-gate.md`（backbuffer capture + 抓帧确定性）
