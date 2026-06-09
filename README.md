# HASHEAEngine

HASHEAEngine 是一个以现代实时渲染和引擎架构实验为目标的 C++ 图形引擎项目。当前仓库包含 Engine DLL、Editor 可执行程序和 Sandbox 验证程序，主开发平台为 Windows x64，构建系统使用 Premake5 + Visual Studio/MSBuild。

项目仍处于持续研发阶段。当前重点是打通稳定的 Engine 基础设施、Vulkan / DX12 双后端 RHI、Scene-driven 静态网格渲染、材质 V2 框架和基础 Editor 工作流，而不是完整的生产级游戏引擎。

## 当前状态

当前主干已经具备：

- Engine 分层架构：`Base`、`Graphics`、`Function` 三层，Editor / Sandbox 通过 Function 层使用引擎能力。
- Vulkan 与 DX12 双后端：运行时通过 `product/config/Engine.ini` 选择后端、垂直同步与全局验证开关，Windows Debug / Release 构建同时编入 Vulkan、DX12、DXC。
- Scene-driven 静态网格渲染：逻辑 `Scene` 通过 `ScenePresentationSubsystem` 转换为渲染线程可消费的不可变可见帧数据，并把场景级 `SceneRenderConfig` 随帧快照传入 `SceneRenderer`；opaque / masked 静态网格可走 DeferredHQ GBuffer 路径。
- Scene 编辑辅助接口：Engine 侧提供 Scene view matrix override、world bounds、screen ray、CPU AABB picking，以及 `AssetId` 驱动的 prefab/model 放置入口。
- Debug draw overlay：Engine 侧提供 frame-local `DebugDrawService`，可提交 line / box / circle / cone / axes，并由 `SceneRenderer` 在 deferred tone-map 后叠加 line-list overlay。
- 材质 V2 基础链路：支持 `Surface.StaticMesh` 的材质 shader 与 engine shader family 拼合，`.AshMat` 作为基材质，`.AshMatIns` 作为可直接赋给物体的材质实例。
- Asset 与标准 Sandbox scene：支持示例模型加载；Sandbox 默认加载 `product/assets/scenes/Sandbox.scene.json`，该场景引用 Sponza，并携带主相机、灯光、环境和 `scene_config`；标准 Sandbox scene 显式开启高可见度 Volumetric Lighting 与 Bloom。
- Editor 基础壳：具备 dockspace、Scene/Game 视口、层级、属性、控制台、资产浏览等基础面板。
- 调试与性能工具：支持日志、Vulkan validation、DX12 debug layer、GPU debug names、RHI command-buffer 错误状态、Tracy CPU profiling、frame stats overlay、Vulkan VMA 泄露定位；Vulkan（`VK_EXT_debug_utils`）与 DX12（Windows SDK `PIXBeginEvent` / command list `BeginEvent`）把当前 `PassDesc::name` 显示到 RenderDoc 事件树（名为空时用 `namelesspass`，不回退 framebuffer 对象名）。

当前仍未完成或仅处于预留阶段：

- Skeletal mesh / animation 尚未完成。
- Shadow 当前区分 sunlight 与普通 directional light：全场景最多一个 `sunlight=true` 的 directional light 使用大场景 CSM + static cache + dynamic overlay；普通 directional light 使用逐光 transient cascade shadow，每个普通方向光在自己的 deferred lighting pass 前绘制 shadow，不再受共享 directional light shadow budget 降级为 unshadowed。Sunlight 与普通方向光共用 `DirectionalShadowCascadeMath` 的 view-space Z cascade frustum / light VP 计算，稳定路径使用 view-space cascade sphere 固定投影尺寸，并按 cascade tile 分辨率对 light-space sphere center 做 shadow texel snapping，降低相机移动和旋转时的阴影游动；shadow mask 会在 cascade split 附近采样下一层并平滑混合，避免切层硬边。Sunlight 与普通方向光的 cascade shader buffer 共用 `texel_size_flags.xy = atlas texel size` 的 PCF 采样约定，sunlight static-cache copy pass 会在 RenderGraph 中显式声明 static cache SRV read。Point/spot 阴影与 VSM 仍待后续阶段。当前 deferred lighting 已接入 base/emissive、per-light directional（含 shadowed 变体）、point、spot，场景级 `scene_config` 可配置屏幕空间 AO（`Off` / `SSAO` / `HBAO` / `GTAO`）、方向光阴影、Bloom 和 Volumetric Lighting；composite（线性 HDR 中转 RT）、sky/background、体积光 HDR 合成、Bloom 与独立全屏 tone-map pass 已接入，静态网格同 mesh/material section 的 instance batching 已接入，骨骼网格 instancing 仍待后续阶段。
  Render Debug View 由 `product/config/Engine.ini` 的 `[RenderDebugView]` 进程级配置控制。
- Transparent blend mode 已进入材质静态状态和编译键，但正式透明队列尚未接入 SceneRenderer。
- PostProcess 与 UI 当前不纳入材质系统，后续应走各自的 shader/pass 与参数组织路径。
- Asset cooking pipeline、streaming、完整资源生命周期管理仍在演进中；runtime 已能直接加载部分 cooked texture payload。
- Editor 还不是完整生产工具链，材质编辑器、复杂场景编辑、导入管线等仍待补齐。
- GPU ID buffer picking 尚未接入；当前选择命中仍以 CPU AABB picking 为第一阶段能力。
- 性能优化仍在进行中，当前渲染路径以架构正确性和双后端一致性为优先。

## 架构概览

```text
HASHEAEngine/
├── premake5.lua                     # 顶层 Premake workspace
├── docs/                            # 长期维护文档与专题设计文档
├── product/                         # 运行配置、日志、缓存、可执行输出、运行期资产
├── scripts/                         # 构建、同步、验证辅助脚本
├── tools/                           # 本地工具
├── Intermediate/                    # 本地构建/调试/分析中间产物
└── project/
    ├── src/
    │   ├── engine/
    │   │   ├── Base/                # 日志、内存、断言、窗口、输入、时间、文件、序列化、线程等
    │   │   ├── Graphics/            # RHI 抽象、Vulkan / DX12 后端、shader 编译反射、资源状态
    │   │   │   ├── Vulkan/          # Vulkan 后端
    │   │   │   └── DirectX12/       # DX12 后端
    │   │   └── Function/            # Application、Renderer、RenderDevice、UIContext、Scene、AssetDatabase
    │   ├── editor/                  # Editor 可执行项目
    │   └── sandbox/                 # Engine 侧验证/示例可执行项目
    └── thirdparty/                  # GLFW、GLM、ImGui、DXC、SPIRV-Cross、Tracy、meshoptimizer 等
```

核心边界：

- `Graphics/` 是 Engine 内部 RHI 层，不直接暴露给 Editor / Game / Client。
- `Function/` 是 Engine 对外的主要公共边界。
- Editor 只应依赖 Engine Function 层公开接口，不应直接 include 后端 RHI 头文件。
- 共享渲染路径改动默认需要同时考虑 Vulkan 和 DX12。

## 渲染与 RHI

当前渲染栈大致分为：

1. `GraphicsContext`：设备、队列、命令池、交换链、validation、后端资源创建。
2. `Swapchain`：窗口输出与 present 资源。
3. `RenderDevice`：Function 层靠近 RHI 的资源、pass、barrier、draw、dispatch 封装。
4. `Renderer`：帧级 orchestration、pass 提交、draw 收集、frame stats、UI 提交。
5. `RenderGraph`：Function/Render 帧级声明式 pass/resource 编排层。

当前已经实现或正在使用的能力：

- Vulkan / DX12 运行时后端选择。
- HLSL 主路径，通过 DXC 编译，Vulkan 消费 SPIR-V，DX12 消费 DXIL。
- shader 反射驱动 descriptor / root signature / descriptor set layout / parameter block layout。
- `SamplerCreation`：`VulkanContext` / `DX12Context` 的 `create_sampler(const SamplerCreation&)` 在 RHI 内按采样器状态字段指纹（**不含** `name`）去重，池内持有 `weak_ptr`；仍存在外部 `shared_ptr` 时命中复用，`GraphicsContext::get_sampler(AshSamplerState)` 枚举默认采样器仍为独立缓存，与该校验键路径并存。
- uniform buffer 创建会按 256 字节对齐分配，带初始数据时同步补零 padding，避免 Vulkan / DX12 后端按分配大小上传时越界读取。
- render pass 与 framebuffer 缓存。
- graphics program / pipeline variant 缓存。
- descriptor / program binding 缓存；Vulkan / DX12 in-flight frame resource ring 默认 3 slot，与默认 3 buffer swapchain 对齐；DX12 shader-visible descriptor heap 按 frame slot 分区，DX12 root signature 会把 CBV/SRV/UAV 与 sampler 分别合并成 descriptor table，descriptor table cache 的常见小表 key 走 inline CPU handle 存储，避免上一帧命令仍引用的 descriptor slot 被本帧覆写。
- pass 外资源状态转换，避免 Vulkan 在 render pass / dynamic rendering 活跃区间内提交非法 barrier；Vulkan same-layout 的只读状态扩展也会补执行依赖，保证 depth SRV 可安全进入 read-only depth attachment + SRV 组合状态。
- Vulkan resize、`OUT_OF_DATE` 和 `SUBOPTIMAL` present/acquire 路径会强制重建 swapchain，即使 surface extent 已经等于缓存尺寸；成功 acquire 后才允许读取当前 swapchain image。主窗口 resize 后，Application 会清理 transient render target / framebuffer cache，并通知 SceneRenderer 丢弃尺寸敏感的体积光 history；RenderDevice 在 `begin_frame()` 也会用 swapchain extent 变化做兜底清理，避免最大化产生的大 RT 池在恢复小窗口后继续占用显存并拖慢 frame-slot wait。
- per-frame GPU upload command path，避免资源上传创建时强制同步等待；Vulkan texture upload 的 staging slice base offset 会按 texel block size 对齐，满足 `vkCmdCopyBufferToImage` 对 `bufferOffset` 的格式对齐要求。
- transient render target pool。该池只缓存当前尺寸仍有复用价值的 RenderGraph 中间 RT，窗口尺寸变化时会被主动清空。
- Render Graph v1 已作为 Function/Render 层 orchestration 接入，支持 graph texture、raster/compute pass 声明、pass culling、transient lifetime 编译、pass-boundary barrier plan、external output / extracted texture root，以及通过现有 `Renderer / RenderDevice` 执行 graph；executor 会复用稳定拓扑的 compile result，尺寸变化不触发重新编译，缓存命中仍做拓扑等值校验避免 hash 碰撞误用。
- DeferredHQ GBuffer 静态网格路径：第一版使用 5 张 GBuffer（三张 `RGBA8_UNORM`、两张 `RGBA16_SFLOAT`）加 D32 depth；GBuffer pass 的静态网格 instance stream 会同时携带当前 / 上一帧 object-to-clip，`GBufferD` 写入 screen-space velocity.xy、上一帧 depth.z 和 temporal flag.a，供后续 TAA / AO temporal 使用；DepthOnly / shadow caster pass 不继承主相机 temporal history，实例记录中的 previous object-to-clip 回退为当前 pass 的矩阵；可选 `SceneAmbientOcclusionPass` 会在 GBuffer 与 deferred lighting 之间生成统一 AO texture（`Off` / `SSAO` / `HBAO` / `GTAO` 由 `VisibleRenderFrame::render_config` 控制），并可在 `Temporal=true` 时追加 motion-vector reprojection + depth/normal history rejection 的 temporal AO resolve；随后以 MRT 将延迟光照的 diffuse / specular 分量分别写入两张 `RGBA16_SFLOAT` transient RT（`SceneDeferredLightingDiffuse` / `SceneDeferredLightingSpecular`），composite pass 合并为线性 HDR 写入 `SceneDeferredSceneHDRLinear`（`RGBA16_SFLOAT`），sky/background 后可由 `VolumetricLightingPass` 在 Bloom 前追加 froxel 体积光 density / scattering / temporal / depth-aware full-screen integrate / composite 链或 screen-space lightshaft fallback；froxel injection 会按 view-space depth 重建世界坐标并分别计算 directional / point / spot 贡献，其中 `sunlight=true` directional 在 CSM 可用时按 cascade split 采样 `SunLightShadowPass` 的 dynamic atlas 和 `SceneDirectionalShadowCascades`，按 froxel 世界坐标得到 shadowed sunlight in-scattering，screen-space 路径只保留为 fallback/调试；temporal pass 会保存上一帧 froxel history 的 view-projection、camera position、view forward、atlas/slice 和场景 light/static revision，并在当前帧按 froxel 世界坐标重投影采样上一帧 atlas；history 状态不兼容、重投影越界或光照/static scene revision 变化时只使用当前帧，避免相机旋转时把 frustum-aligned atlas 的旧 UV 错混成漂移光束；integrate pass 读取 `SceneDeferredDepth` 按可见 view-space segment 长度积分 in-scattering，并把剩余 transmittance 写入全屏 `SceneVolumetricIntegratedLighting.a`；composite 使用 `hdr * transmittance + in_scattering` 写回线性 HDR，避免 slice 数变化时整屏 additive 抬亮或能量被稀释；`volumetric_lighting.debug_view` 可把 density / scattering / integrated / composite 等中间结果直接送入后续 tone-map 链；体积光 storage-image 中间 RT 使用 `RGBA32_SFLOAT` 匹配 typed UAV，最终 HDR composite 保持 `RGBA16_SFLOAT`，再由 `BloomPass` 追加 setup / downsample / upsample / composite 链，最后由 `PostProcessToneMapPass` 提交的独立 `SceneDeferredToneMapPass`（ACES + exposure；对非 SRGB 的 8-bit UNORM output 可选手动 sRGB 编码）写到 view output。
- Deferred lighting 第一版支持 base/emissive、directional fullscreen、point sphere volume、spot cone volume；点光/聚光 volume 使用只读 depth attachment、硬件 depth test、depth write off、双面和 additive blend。
- DebugDraw overlay 第一版使用 `RenderPrimitiveTopology::LineList`，在 `SceneDeferredToneMapPass` 后以 `RenderLoadAction::Load` 写回同一 output；当前不做 depth test、alpha blend 或宽线几何扩展。
- Render Debug View 由 `product/config/Engine.ini` 的 `[RenderDebugView]` 与 Engine overlay 选择当前帧 active RT，并在 tone-map 后、DebugDraw overlay 前把 `SceneOutput` / GBuffer / depth / lighting / HDR / AO / Volumetric Lighting 等资源直接可视化到主画面输出；`LinearHDR` 路径显示 raw pre-tonemap 线性值的 clamped preview，不再走 ACES 预览。
- draw 排序、静态网格 instance batching、单可见静态网格 direct section submit fast path、常见 vertex buffer binding inline 存储、提交前资源 transition scratch 复用与只读 barrier 合并，用于降低 Sponza 这类 section 多场景的 CPU 开销。
- `SceneRenderer` 的静态网格 instance vertex buffer 按渲染侧当前 frame epoch 映射到 3 帧物理 slot ring；同一逻辑 slot 连续渲染帧不会更新同一个 host-visible buffer，避免 Vulkan 下 CPU 写下一帧实例矩阵时覆盖 GPU 仍在读取的上一帧 draw 输入。
- `[Rendering].VSync=false` 时 swapchain 优先请求 `MAILBOX` / `IMMEDIATE`，DX12 会映射为 `Present(0, DXGI_PRESENT_ALLOW_TEARING)`（硬件/系统支持 tearing 时）；开启 VSync 时两端都请求 FIFO present。
- Runtime frame stats overlay；FPS / frame time 基于上一帧从 backend `begin_frame()` 前到 `present()` 返回后的完整 CPU wall time，包含 frame-slot wait、UI/end-frame 和 present 开销，并拆分显示 Begin / End / Present 三段 CPU 时间。
- Runtime DebugDrawService line-list overlay。

## Scene 与渲染主路径

当前 Scene 模块采用 facade + ECS-style 内部存储：

- 公共层保留 `Scene` / `Entity` 易用接口。
- 内部基于 entt 风格 registry 管理实体、层级和组件。
- 当前公开组件包括 `Name`、`Transform`、`Camera`、`Light`、`Mesh`。
- `MeshComponent` 支持资产路径、mesh index、section 材质覆盖、可见性、mobility、layer mask。
- `SceneQuery.h` 提供 entity/subtree world bounds、screen-to-world ray、第一阶段 CPU AABB picking，以及 `project_ray_to_plane()` / `find_scene_drop_point()` 统一投放落点规则。
- `SceneInstantiationDesc` + `instantiate_asset(Scene&, AssetDatabase&, AssetId, ...)` 提供 Editor 拖拽放置需要的 AssetId facade，可按 root world transform 实例化 prefab/model/mesh。
- `Scene::subscribe_change_events()` + `SceneChangeEvent` 提供 scene replace/reload/hierarchy/component/dirty 变更事件语义；`Scene::reload_from_file()` / `replace_contents()` 保留订阅者并发出 `SceneReloaded` / `SceneReplaced`。
- `get_scene_component_descriptor()` 的 `ScenePropertyDesc` 已扩展 editor hint、range、asset ref、read-only 元数据；`can_add_scene_component()` / `add_scene_component()` / `can_remove_scene_component()` / `remove_scene_component()` 提供按 `SceneComponentType` 的通用组件 facade。
- `submit_scene_overlay()` / `clear_scene_overlay()` + `SceneOverlayDepthMode` 提供 viewport-scoped、depth-aware 的 scene overlay 提交；overlay pass 发生在 tone-map 后、全局 DebugDraw 前，并复用当前 view 的 deferred depth 结果。
- `request_scene_entity_pick()` / `poll_scene_entity_pick_result()` 提供 viewport GPU entity ID picking facade；`SceneRenderer` 在 GBuffer 后以 depth-tested `R32G32_UINT` pass 写入 entity id，`RenderDevice` 在帧末 readback 单 texel，并结合 scene query 补齐 world position / depth / normal。
- `get_scene_view_stats()` 提供 viewport 只读 stats facade（output 尺寸、RHI backend、draw calls、CPU frame time / FPS）。

Scene 到渲染的主路径：

- `Application` 持有 `ScenePresentationSubsystem`。
- 上层声明 `Scene + Camera + Output + View Overrides`；camera 可来自 primary camera、指定 camera entity，或显式 view/projection matrix override。
- `ScenePresentationSubsystem` 维护 per-scene `RenderScene` cache、构建 `SceneView` 和 `VisibleRenderFrame`。
- 逻辑线程负责 scene ownership 和可见帧构建。
- worker 线程执行 CPU frustum culling。
- render thread 只消费不可变 frame packet 并提交 draw。
- `RenderScene` sync 阶段会为静态网格 section 预取 CPU-only 材质代理，render submit 只在代理缺失或资源版本变化时做 GPU program / texture binding 准备。
- `Scene` 为渲染同步维护 primitive / transform / light / environment / render config 版本；`ScenePresentationSubsystem` 只有 primitive 拓扑变化或强制刷新时重建 `RenderScene`，纯 transform 变化只更新 primitive world transform 和灯光快照，纯 light / environment / render config 组件变化分别只刷新对应快照。
- Editor Scene/Game viewport 使用 engine-owned offscreen output，通过 `UISurfaceHandle` 交给 UI 展示。
- Sandbox 主窗口使用 window output + persistent binding，作为共享渲染路径验证入口。

第一阶段正式支持静态网格主链路，并已为相同 mesh/material section 使用 per-instance vertex stream 合批。默认静态网格 scene path 现在由 `RenderGraphBuilder` 表达为 `SceneGBufferPass -> SceneAmbientOcclusionPass -> SceneDeferredLightingAccumPass -> SceneDeferredEnvironmentLightingPass -> SceneDeferredCompositePass -> SceneSkyBackgroundPass -> SceneVolumetricLightingPass -> SceneBloomPass -> SceneDeferredToneMapPass`，通过 graph transient GBuffer / depth / AO / diffuse+specular lighting 分量 RT、线性 HDR 中转 RT、体积光中间 RT 和 Bloom 中间 RT 完成 deferred submit；有 active `EnvironmentComponent` 且 `SceneRenderViewContext::environment_resource` 就绪时，`EnvironmentLightingPass` 会在 composite 前追加 split-sum IBL，`SkyBackgroundPass` 会在体积光 / Bloom / tone-map 前把 radiance cubemap 写入 depth background 像素。AO、方向光阴影、Bloom 和 Volumetric Lighting 的场景级设置从 scene JSON 顶层 `scene_config` 进入 `SceneRenderConfig`，再经 `ScenePresentationSubsystem -> RenderScene -> VisibleRenderFrame -> SceneRenderer` 消费；Bloom 和 Volumetric Lighting 默认对旧场景关闭，标准 Sandbox scene 分别开启 Bloom 与高可见度 Volumetric Lighting。Volumetric Lighting 的 froxel 主路径按 view-space depth 重建 froxel 世界坐标，体积覆盖距离由 sunlight CSM 最远 split / shadow distance 或 point/spot range 推导；light injection 阶段对 sunlight directional 按 CSM split 选择 cascade 并采样 dynamic atlas/cascade buffer，使太阳体积光由遮挡关系调制；temporal scattering history 使用 world-space reprojection：当前 froxel 还原为世界坐标后投回上一帧 view-projection，再按上一帧 camera forward 算回 froxel slice，状态不兼容或像素级重投影失败时回退当前帧；in-scattering 可见度使用密度归一化标定，`density` 主要控制 extinction/雾浓度，`scattering_intensity` 主要控制光束亮度，避免为了看见 shaft 被迫把整屏雾拉高；integrate 阶段用可见 view-space segment 长度累积 in-scattering，并对 in-scattering 和 extinction 都应用世界长度尺度标定，避免 sunlight shadow distance 或 froxel slice 数把整屏雾量放大；froxel atlas 会按质量档限制运行时 froxel 数量（Low 512K、Medium 1M、High 2M、Epic 4M），所以 `froxel_resolution_scale` 是期望上限而不是无预算的显存放大器；`volumetric_lighting.debug_view` 可选择中间 RT 直接进入后续后处理链，`screen_space_fallback=true` 仅用于低成本兼容路径或调试对比。
Render Debug View 则从 `Engine.ini` 的 `[RenderDebugView]` 读取进程级运行时配置。启用 temporal AO 时，AO pass 内部会在 raw/blur AO 后追加 `SceneAmbientOcclusionTemporalPass`，并 ping-pong 维护 AO 与 depth/normal meta history。开启 `[RenderDebugView]` 且选择非 `Off` / `SceneOutput` RT 时，`SceneRenderDebugViewPass` 会在 tone-map 后把选中的 active RT 直接替换主画面输出，其中 `SceneDeferredSceneHDRLinear` 会以 raw pre-tonemap 线性值做 clamped preview，Bloom 相关的 `SceneBloomSetup` / `SceneBloomMip1..6` / `SceneBloomFinal` / `SceneBloomCompositeHDR` 和体积光相关的 `SceneVolumetricDensity` / `SceneVolumetricScattering` / `SceneVolumetricIntegratedLighting` / `SceneVolumetricCompositeHDR` / `SceneVolumetricHistoryValidity` / `SceneLightShaftOcclusionMask` / `SceneLightShaftScreenSpaceFinal` 也作为 debug target 注册；其中 froxel atlas 资源按真实 atlas 尺寸展示，`SceneVolumetricIntegratedLighting` 和 composite HDR 才是全屏输出。`SceneRenderer` 不再保留旧 `BasePass` 前向 fallback，`Surface.StaticMesh.BasePass` 仅作为材质 / shader family 能力保留。单可见静态网格帧会绕过 batch map，直接逐 section 提交并复用一个单实例 buffer；该 instance buffer slot 在同一渲染 frame 内按 view/pass submit 分配，并在跨渲染 frame 时映射到 3 帧物理 ring，避免后提交 view 或下一帧 CPU update 覆盖仍可能被 GPU 读取的 object-to-clip 数据。motion vector history 仅在 GBuffer pass 使用同 view 上一次实际渲染提交的状态，不依赖逻辑侧 prepared packet 的 `VisibleRenderFrame::frame_index`；DepthOnly / shadow caster pass 不读取主相机 history，避免 shadow map draw 的实例数据混入 camera previous matrix。skeletal mesh、occlusion culling 和动态材质实例仍是后续阶段。

## 材质系统

当前主干使用材质 V2：

- `.AshMat`：基材质资产，描述材质 shader、资源声明、参数、render state、宏等。
- `.AshMatIns`：材质实例资产，允许覆盖参数、贴图、采样器和部分实例级设置。
- 只有 `.AshMatIns` 可以直接赋给 mesh section、`MeshComponent.material_overrides` 或模型默认材质槽。
- 如果运行时解析到直接绑定 `.AshMat` 基材质，会报错并回退到 generated/default instance。
- `MaterialSystem` 负责 domain / family / pass 的验证、fallback 和 resource template 获取。
- `MaterialShaderMap` 负责不可变编译资源。
- `MaterialRenderProxy` 在 render thread submit phase 准备材质参数、贴图、sampler、graphics program 和 binding；`ScenePresentationSubsystem` 会同时覆盖 camera-visible `static_mesh_draws` 与 off-camera shadow caster `shadow_caster_static_mesh_draws`，避免仅有 shadow depth draw 时 `DepthOnly` pass 缺少 prepared proxy。
- `MaterialRenderProxy` 基于 material change version、compile hash、节流后的 shader 文件签名检查、binding snapshot version 和 texture asset change version 判断脏状态；shader 文件签名包含 engine family host、用户 shader、generated bindings 和 `Surface.StaticMesh` 共享 HLSL header，只按 proxy 周期性探测，不进入每个 section 的逐帧 filesystem 热路径，异步贴图仍在 Loading 且 fallback resource 未变化时不会每帧重复重绑。
- 材质实例的贴图 binding 可覆盖 sampler state；运行时会把该 sampler state 绑定到基材质资源声明实际生成的 shader sampler 名，避免 glTF sampler override 与生成 HLSL sampler 名不一致。
- 当前 `Surface.StaticMesh` 材质资源仍覆盖 `BasePass`、`DepthOnly` 与 `GBuffer`；`SceneRenderer` 的 opaque / masked 主提交路径使用 `GBuffer`，用户材质 shader 仍只实现材质节点接口，GBuffer MRT 编码由 Engine host shader 负责。`.AshMat` 可通过 `shading_model` 声明 `Empty`、`DefaultLitGGX`、`Unlit`、`BlinnPhong`，`DefaultLit` / `ggx` 作为 `DefaultLitGGX` 兼容别名；随仓库提供的基材质资产当前显式声明为 `ggx`。`.AshMatIns` 继承父材质 shading model，不做实例级覆盖。

材质 shader 由三部分拼合：

1. Engine shader family host，例如 `SurfaceStaticMeshBasePass.hlsl` 或 `SurfaceStaticMeshGBuffer.hlsl`。
2. 用户材质 shader，例如 `product/assets/materials/v2/M_SurfacePBR.hlsl`。
3. 由 `.AshMat` / `.AshMatIns` 生成的 bindings HLSL。

当前不把 UI 和 PostProcess 纳入材质系统。PostProcess 更适合独立 screen-pass shader/effect 管线，UI 继续由 UIContext / ImGui 相关路径负责。

目录规则：

- Engine shader family、domain hlsli 和材质拼接占位 include 放在 `project/src/engine/Shaders/MaterialV2/`。
- 材质 shader 是运行期资产，放在 `product/assets/materials/v2/`，与对应 `.AshMat/.AshMatIns` 同级。
- 默认 PBR 基材质和默认实例分别是 `materials/v2/M_SurfacePBR.AshMat` 与 `materials/v2/MI_DefaultSurface.AshMatIns`。

## Asset 与示例资源

当前资产能力包括：

- `AssetDatabase` 做资源目录、类型识别和缓存。
- `AssetDatabase` 的 Mesh / Model / Material / AshAsset 异步请求会复用同一 asset 的 in-flight `shared_future`，失败结果会缓存到下一次 `refresh()`，避免重复 worker job 和重复 decode/import。
- 支持 glTF 示例模型，包含 Sponza、DamagedHelmet、BoomBox、Avocado。
- 贴图 decode 覆盖 `png`、`jpg`、`jpeg`、`tga`、`bmp`、`hdr`，并支持 2D 非数组 `.dds` / raw `.ktx2` cooked BCn 载入；BC1/BC2/BC3/BC7 的 sRGB cooked payload 会保留为真实 GPU sRGB 格式。
- `RenderAssetManager` 负责 static mesh CPU/GPU 资源桥接、贴图请求、fallback texture、sampler cache、材质代理准备。
- 模型导入材质槽会生成或解析 `.AshMatIns`，draw-time 绑定由 `MaterialRenderProxy + MaterialSystem` 处理。
- `.AshAsset` JSON 读写已从模型导入器中拆到 `AshAssetSerializer`，cooked texture DDS/KTX2 解析已从普通贴图入口拆到 `TextureCookedDecoder`。

示例资源主要位于：

```text
product/assets/models/gltfs/
product/assets/materials/v2/
```

## Editor

Editor 当前是引擎能力验证和工具化演进的基础壳，已具备：

- Dockspace workspace。
- Scene View 与 Game View。
- Scene Hierarchy。
- Inspector。
- Console。
- Asset Browser。
- Scene load / save / reload / new scene。
- 实体编辑 undo / redo command service。
- 资产浏览过滤与搜索。
- 视口输出由 `ScenePresentationSubsystem` 管理，Editor 面板只负责 UI 展示。

Editor 启动顺序遵循 Engine `Application` 生命周期：`Editor` 构造函数只保存轻量配置，不执行日志或 runtime 依赖初始化；`EditorApplication` bootstrap 在 `_on_startup()` 中执行，此时日志、窗口、RHI 和 UIContext 已完成初始化，关闭逻辑在 `_on_shutdown()` 中对称清理。

Editor 仍在开发中，不应视为完整关卡编辑器或生产工具链。

## Sandbox

Sandbox 是 Engine 侧测试/验证可执行项目，目标是避免把引擎验证逻辑长期塞进 Editor 生命周期中。

当前 Sandbox：

- 默认加载 `product/assets/scenes/Sandbox.scene.json` 作为标准场景；该 scene 文件拥有标准模型、主相机、directional / point / spot 灯光、active sky environment 和场景级渲染设置。`Scene::save_to_file()` 会持久化 scene 内已有的 `sunlight=true` directional light；`Scene::load_from_file()` 仅在 scene 中不存在 sunlight 时，才根据 active `.ashibl` 的 `dominant_light` 自动创建 `EnvironmentSunLight`。
- 标准场景当前引用 `product/assets/models/gltfs/Sponza/glTF/Sponza.gltf`，不再通过窗口 overlay 或 `ASH_SANDBOX_MODEL` 切换启动模型。
- 保留 glTF 自带材质槽和贴图绑定；标准场景不再强制注入 debug material override。
- 走逻辑 Scene -> ScenePresentationSubsystem -> SceneRenderer 的正式链路。
- 标准 Sandbox scene 显式开启高可见度 Volumetric Lighting，用于覆盖 froxel 体积光、Bloom 和 tone-map 的共享渲染链路。
- 用于验证静态网格渲染、材质 V2、资源加载、双后端 smoke test 和性能回归。

## 构建与运行

### 环境

- Windows x64。
- Visual Studio 2022 或包含 MSBuild 的 Build Tools。
- 支持 Vulkan 或 DX12 的显卡与驱动。
- 仓库根目录自带 `premake5.exe`。
- 仓库已随附 Windows x64 DXC 运行时与 Debug Vulkan validation layer 运行时；开发者不需要额外安装 Vulkan SDK 或系统 DXC 才能编译和运行项目的默认 Vulkan/DX12 shader 编译路径。显卡驱动提供的 Vulkan loader / ICD 仍然是运行 Vulkan 后端的前提。

### 生成解决方案

```bat
generate_vs2022.bat
```

### 构建 Editor

```bat
build_editor.bat Debug x64
build_editor.bat Release x64
```

### 构建 Sandbox

```bat
build_sandbox.bat Debug x64
build_sandbox.bat Release x64
```

`build_editor.bat` 与 `build_sandbox.bat` 会通过 `scripts/InvokeMSBuild.ps1` 启动 MSBuild。该启动器会在子进程环境中合并大小写冲突的 `PATH` / `Path` 变量，只保留规范的 `Path`，避免 MSBuild/.NET 因重复环境变量报 `MSB6001`。

### 运行 Editor

```bat
run_editor.bat Debug
run_editor.bat Release
```

### 运行 Sandbox

```bat
product\bin64\Debug-windows-x86_64\Sandbox.exe
product\bin64\Release-windows-x86_64\Sandbox.exe
```

构建输出与运行目录：

```text
_BUILD/<Config>-windows-x86_64/
product/bin64/<Config>-windows-x86_64/
```

Engine 构建后会把 `project/thirdparty/dxc/bin/x64/dxcompiler.dll` 和 `dxil.dll` 同步到运行目录，避免运行时从开发者机器的 `PATH` 上加载不带 SPIR-V codegen 的系统 DXC。Debug 构建还会把 `project/thirdparty/VulkanSDK/redist/windows-x64/layers/` 下的 Khronos validation layer 同步到 `product/bin64/Debug-windows-x86_64/vulkan_layers/`。

## 后端配置

运行时后端由 `product/config/Engine.ini` 控制：

```ini
[RHI]
Backend=Vulkan

[Rendering]
VSync=false

[VulkanValidation]
Enabled=false
GpuAssisted=true
SynchronizationValidation=true
BreakOnValidationError=true

[DX12Validation]
Enabled=false
GpuValidation=true

[EnvironmentLighting]
RuntimeBakeCache=true
DefaultRadianceSize=1024
DefaultIrradianceSize=64
DefaultPrefilterSize=256
DefaultPrefilterMipCount=8
DefaultBRDFLUTSize=256
DefaultSampleCount=1024

[RenderDebugView]
Enabled=true
Selected=SceneDeferredSceneHDRLinear
```

可选后端：

- `Vulkan`
- `DX12`

Validation 开关只在 Debug 配置下生效。Release 构建中即使配置文件打开 Vulkan 或 DX12 validation，引擎也会强制关闭 validation / debug layer。
Debug Vulkan 启用 validation 时，后端会在枚举 layer 前把运行目录下的 `vulkan_layers` 子目录追加到进程级 `VK_ADD_LAYER_PATH`，优先使用仓库随附的 `VK_LAYER_KHRONOS_validation` manifest 和 DLL。

Engine 侧通过 `RenderFeatureConfig` 管理全局渲染开关，开关描述表会从同一个 `Engine.ini` 读取并发布到运行时原子开关表；当前注册项包括 `Rendering.VSync`，默认 `false` 保持低延迟/benchmark 友好的非垂直同步 present，设为 `true` 时使用 FIFO present。

Sandbox 标准场景从 `product/assets/scenes/Sandbox.scene.json` 启动；scene JSON 顶层 `scene_config` 拥有场景级渲染设置：`ambient_occlusion`、`directional_shadows`、`bloom` 和 `volumetric_lighting`。Bloom 和 Volumetric Lighting 默认对旧场景关闭，标准 Sandbox scene 开启 Bloom 与高可见度 Volumetric Lighting，用于验证 HDR bloom、froxel 体积光、sunlight CSM lightshaft、world-space temporal reprojection 和 tone-map 主链路；该场景的 `froxel_resolution_scale` 设为 `0.25`、`scattering_intensity` 设为 `2.0`，实际 froxel 数量仍会被质量档 froxel budget 约束。`product/config/Engine.ini` 作为进程级配置源，继续负责 RHI backend、validation、VSync、`[EnvironmentLighting]` 烘焙/cache 策略，以及 `[RenderDebugView]` 诊断视图开关和目标 RT 选择；不要把 `[AmbientOcclusion]`、`[DirectionalShadows]`、`[Bloom]` 或 `[VolumetricLighting]` 放回 `Engine.ini`。

`[EnvironmentLighting]` 控制 `.ashibl` 烘焙默认分辨率、prefilter mip 数、sample count 与 runtime source cache 读取；`RenderAssetManager` 不会在首帧同步烘焙 HDR。`.ashibl` 的 irradiance payload 存储半球积分后的 irradiance（常量 radiance 会烘焙为 `pi * radiance`），deferred diffuse IBL 在 shader 中再应用 Lambert BRDF 的 `base_color / pi`；BRDF LUT 使用 IBL 版 Smith geometry term，避免 split-sum specular 在掠射角产生额外能量放大。运行时优先加载 `EnvironmentComponent.ibl_asset_path`，失败后在 `RuntimeBakeCache=true` 时按 source 内容 hash 查找 `product/caches/EnvironmentCaches/<hash>.ashibl`，仍缺失则立即使用 fallback environment。`EnvironmentComponent.intensity` 是总倍率，`lighting_intensity` 只影响 IBL 光照，`background_intensity` 只影响 skybox 背景，方便保留 HDR 背景亮度但降低无阴影环境填光。`.ashibl` metadata 可记录 `dominant_light`，baker 会从 radiance cubemap 的高亮峰值簇提取 skybox-local 太阳方向；`Scene::load_from_file()` 仅在 scene 中还没有 `sunlight=true` directional light 时，才会根据 active environment metadata 自动创建 `EnvironmentSunLight`；已有 sunlight 会原样保留并由 `Scene::save_to_file()` 写回 scene 文件。`read_ashibl_metadata_file()` 可只读取 metadata，不加载 IBL payload。使用 `--bake-ashibl` 离线生成目标 `.ashibl` 或预填 source cache。

Reverse-Z 不是 `Engine.ini` 开关，而是 `CameraComponent.reverse_z` 的逐相机属性。开启后该相机的 SceneView 使用 near=1 / far=0 的 reverse-Z 深度映射，默认 depth clear value 为 `0.0`，渲染管线会为该视图使用反向 depth compare 变体；deferred point / spot light volume draw 也必须携带当前 view 的 reverse-Z 标志。

## 验证与调试

常用验证入口：

```bat
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=5
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
product\bin64\Debug-windows-x86_64\Sandbox.exe --bake-ashibl product\assets\textures\skybox\citrus_orchard_puresky_4k.hdr product\assets\textures\skybox\citrus_orchard_puresky_4k.ashibl --radiance-size=1024 --irradiance-size=32 --prefilter-size=128 --prefilter-mips=8 --brdf-lut-size=128 --sample-count=256
```

开发侧 AI 上下文报告可通过 AIDevDoctor 生成。该工具只读仓库状态、日志和最近测试报告，把 AI review contract、规则化风险、dirty change groups、change signals、validation evidence index、Sandbox / Editor × Vulkan / DX12 覆盖矩阵、证据新鲜度、验证缺口、诊断提示、验证计划和 AI prompt 写入 `Intermediate/test-reports/ai-dev/<timestamp>/`；验证计划只列出建议命令和人工检查项，并会引用覆盖矩阵中缺失或过期的目标 / 后端证据，不会自动执行构建、运行程序或改写 `Engine.ini`：

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\AIDevDoctor.ps1 -Mode Report -IncludeLogs -IncludePerfGate
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\AIDevDoctor.ps1 -Mode ValidatePlan
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\TestAIDevDoctor.ps1
```

标准性能门禁入口：

```bat
RunPerfGate.bat
```

无参数运行会打开控制台交互菜单；带参数运行时会直接透传到 `scripts/RunPerfGate.ps1`，例如：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard
```

报告输出到 `Intermediate/test-reports/perf-gate/<timestamp>/`。崩溃、超时、backend 错配、validation/debug-layer 错误、Engine heap 或 Vulkan VMA shutdown live bytes 会失败；CPU frame time、FPS、draw/pass/dispatch 和内存峰值会和 `tools/perf/perf_gate_baselines.json` 中已 bless 的同 profile/config/target/backend 基线比较，超过 `warn_thresholds` 时标记为 WARN。首次建立或接受新基线时运行：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline
```

常用调试输出：

- 运行日志：`product/logs/`
- Shader cache：`product/caches/ShaderCaches/`
- Pipeline cache：`product/caches/PipelineCaches/`
- 本地构建、cdb、测试、shader debug dump、性能分析报告：`Intermediate/`，其中临时测试工作文件统一放在 `Intermediate/test-temp/`

调试与性能工具：

- spdlog 日志。
- Vulkan validation layer。
- DX12 debug layer / GPU validation。
- RenderDoc 抓帧。
- Tracy profiling。
- 新增或修改 render pass、RenderGraph pass、compute dispatch 路径和明确性能热点时，需要同步补 Tracy scope / count / name 打点，避免热点只靠事后猜测定位。
- RHI native GPU object debug name。
- Vulkan VMA leak tracking。
- Runtime frame stats overlay。

共享渲染、RHI、Scene、Asset、Application 生命周期或配置相关改动，默认至少需要覆盖：

- Sandbox + Vulkan。
- Sandbox + DX12。
- Editor + Vulkan。
- Editor + DX12。

文档维护约定：

- 任何代码、资源、配置、构建、验证、架构或工作流变更，都需要在同一轮同步更新根目录 `README.md`，保证仓库入口描述与当前状态一致。
- 重要项目状态、路径规则或工作流变化不应只记录在 `docs/` 文档或对话历史中。

## 开发进度

| 模块 | 当前进度 |
| --- | --- |
| Engine 基础设施 | 日志、断言、窗口输入、文件、时间、服务、线程等基础能力已具备，仍在规范化错误处理和生命周期细节。 |
| RHI | Vulkan / DX12 双后端可运行，shader 编译反射、资源状态、pipeline、descriptor、debug name、validation、Vulkan acquire 到首个 swapchain layout/copy/clear barrier 的 semaphore wait 与 barrier source scope（含 ImGui secondary viewport swapchain）、DX12 mailbox present 映射、shader-visible descriptor heap 分帧分区、command-buffer 错误状态、BCn/sRGB 格式映射正在持续完善。 |
| Renderer | 已有 frame、pass、draw、dispatch、transient RT、frame stats、UI submit 等高层封装；`RenderGraph` 第一版已接入 Function/Render，支持 texture graph、raster/compute pass 声明、pass culling、lifetime 编译、稳定拓扑 compile cache、pass-boundary barrier plan，并完成 scene deferred 主路径迁移；`RenderFormatUtils` 统一维护高层格式到 RHI 的映射，Vulkan upload queue 实现已从 context 主文件拆分，DeferredHQ GBuffer、第一版 deferred lighting（含 RGBA16F HDR 中转、Volumetric Lighting froxel / lightshaft fallback、Bloom 后处理链与独立 tone-map pass）、静态网格 draw 排序、instance batching、单可见静态网格 fast path、vertex binding inline storage、barrier 去重和 pass/framebuffer cache 已接入。 |
| Scene | ECS-style 内部存储和 Scene facade 已具备，静态网格 scene-driven 渲染链路已打通，渲染同步已拆分 primitive / transform / light / environment / render config 版本以避免局部变更触发整场景重建，scene JSON 顶层 `scene_config` 可随场景保存 AO、方向光阴影、Bloom 和 Volumetric Lighting 设置，并提供 SceneQuery、matrix camera override、AssetId prefab/model 放置 helper 和 Engine-side DebugDrawService。 |
| Material | V2 `Surface.StaticMesh` BasePass、DepthOnly 与 GBuffer pass 已接入，`.AshMat` / `.AshMatIns` 资产格式已建立，shader map 通过 shader reflection artifact 生成资源布局，不再为模板资源创建临时 program；builtin fallback 材质创建已从 JSON 解析实现中拆分，透明、骨骼、decal 等仍待后续阶段。 |
| Asset | glTF 示例模型、普通贴图 decode、DDS/KTX2 cooked BCn 与 sRGB 压缩格式载入、async in-flight 去重、失败缓存、static mesh render asset 桥接已具备，`.AshAsset` 序列化已从模型导入器中拆分，完整 asset cooking / streaming 尚未完成。 |
| Editor | 基础 workspace 和常用面板已具备，当前更偏向引擎验证与工具雏形，完整编辑器能力仍在开发。 |
| Sandbox | 已作为标准 Engine 验证程序，默认加载 `product/assets/scenes/Sandbox.scene.json`，由 scene 文件拥有 Sponza、主相机、directional / point / spot 灯光、active environment、Bloom 和高可见度 Volumetric Lighting 场景级 render config；scene 文件内保存的 sunlight 会在 load/save 往返中保留，只有缺失 sunlight 时才会从 active `.ashibl` 的 `dominant_light` 自动补齐 `EnvironmentSunLight`，并走正式 ScenePresentation 渲染链。 |
| Profiling / Debug | Tracy、validation、debug name、日志、frame stats、DebugDrawService、VMA leak tracking 已接入；新增/修改 pass 与性能热点时应同步补 Tracy 打点，粒度和自动化验收仍在扩展。 |

## 文档入口

- Engine 开发指南：[`docs/EngineDeveloperGuide.md`](docs/EngineDeveloperGuide.md)
- Editor 开发指南：[`docs/EditorDeveloperGuide.md`](docs/EditorDeveloperGuide.md)
- Scene Presentation 指南：[`docs/ScenePresentationSubsystemGuide.md`](docs/ScenePresentationSubsystemGuide.md)
- RenderGraph 使用教程与 API Spec：[`docs/RenderGraphAPISpec.md`](docs/RenderGraphAPISpec.md)
- Engine UIContext：[`docs/EngineUIContext.md`](docs/EngineUIContext.md)
- Editor UI 分层提案：[`docs/EditorUIFacadeProposal.md`](docs/EditorUIFacadeProposal.md)
- PerfGate 性能门禁使用说明：[`docs/PerfGateUsageGuide.md`](docs/PerfGateUsageGuide.md)
- AIDevDoctor 开发侧 AI 上下文工具：[`docs/AIDevDoctor.md`](docs/AIDevDoctor.md)
- Deferred GBuffer 设计草案：[`docs/superpowers/specs/2026-05-12-deferred-gbuffer-design.md`](docs/superpowers/specs/2026-05-12-deferred-gbuffer-design.md)
- Deferred Lighting 设计草案：[`docs/superpowers/specs/2026-05-12-deferred-lighting-design.md`](docs/superpowers/specs/2026-05-12-deferred-lighting-design.md)
- Sandbox 场景化与 SceneConfig 设计：[`docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md`](docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md)
- Skybox / IBL 设计：[`docs/superpowers/specs/2026-05-25-skybox-ibl-design.md`](docs/superpowers/specs/2026-05-25-skybox-ibl-design.md)
- Skybox / IBL 实现计划：[`docs/superpowers/plans/2026-05-25-skybox-ibl-implementation.md`](docs/superpowers/plans/2026-05-25-skybox-ibl-implementation.md)
- Directional CSM Shadow 设计草案：[`docs/superpowers/specs/2026-05-25-directional-csm-shadow-design.md`](docs/superpowers/specs/2026-05-25-directional-csm-shadow-design.md)
- Directional CSM Shadow 实现计划：[`docs/superpowers/plans/2026-05-25-directional-csm-shadow-implementation.md`](docs/superpowers/plans/2026-05-25-directional-csm-shadow-implementation.md)
- SunLight / DirectionalLight Shadow Pass 拆分设计：[`docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md`](docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md)
- SunLight / DirectionalLight Shadow Pass 拆分实现计划：[`docs/superpowers/plans/2026-05-26-sunlight-directional-shadow-pass-split-implementation.md`](docs/superpowers/plans/2026-05-26-sunlight-directional-shadow-pass-split-implementation.md)
- Volumetric Lighting 子系统设计：[`docs/superpowers/specs/2026-06-05-volumetric-lighting-design.md`](docs/superpowers/specs/2026-06-05-volumetric-lighting-design.md)
- Agent Core v0 设计草案：[`docs/superpowers/specs/2026-06-03-agent-core-v0-design.md`](docs/superpowers/specs/2026-06-03-agent-core-v0-design.md)
- 环境光遮蔽（AO）设计草案：[`docs/superpowers/specs/2026-05-20-ambient-occlusion-design.md`](docs/superpowers/specs/2026-05-20-ambient-occlusion-design.md)
- 环境光遮蔽（AO）实现计划：[`docs/superpowers/plans/2026-05-20-ambient-occlusion-implementation.md`](docs/superpowers/plans/2026-05-20-ambient-occlusion-implementation.md)
- Render Graph 设计草案：[`docs/superpowers/specs/2026-05-14-render-graph-design.md`](docs/superpowers/specs/2026-05-14-render-graph-design.md)
- Render Graph 实现计划：[`docs/superpowers/plans/2026-05-14-render-graph-implementation.md`](docs/superpowers/plans/2026-05-14-render-graph-implementation.md)
- 性能门禁设计草案：[`docs/superpowers/specs/2026-05-18-perf-gate-design.md`](docs/superpowers/specs/2026-05-18-perf-gate-design.md)
- 性能门禁实现计划：[`docs/superpowers/plans/2026-05-18-perf-gate-implementation.md`](docs/superpowers/plans/2026-05-18-perf-gate-implementation.md)
- 静态代码审查与风险记录：[`docs/EngineStaticCodeReview_2026-05-06.md`](docs/EngineStaticCodeReview_2026-05-06.md)

如果 README 与详细文档存在冲突，以 `docs/EngineDeveloperGuide.md` 和 `docs/EditorDeveloperGuide.md` 中的最新约定为准。
