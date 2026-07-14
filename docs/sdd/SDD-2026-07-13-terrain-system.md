# SDD-2026-07-13-terrain-system: 编辑器可用的中型地形系统

## Status

Approved

六个设计部分已于 2026-07-13 在任务讨论中逐段批准；标准 SDD 落盘版本也于同日完成用户复核。实施计划审计随后确认现有 PerfGate 没有机器可读的 GPU 时间，因此用户于同日批准下述最小双后端 GPU timing 修订。实现必须遵守本文边界；任何超出本修订的新增范围或公共 RHI 扩展都需要先修订 SDD 并重新批准。

风险级别：**S2**。原因是该功能同时改变 Scene 数据模型、Asset 格式、Render 数据流、shader 绑定约定、Editor 工作流和自动化门禁，并增加一个固定容量、异步只读、双后端等价的 GPU timing RHI contract。除该遥测 contract 外，方案不增加 texture upload、资源模型或提交语义等 RHI 能力。

## Context

HASHEAEngine 当前没有 Terrain、Landscape 或 Heightfield 实现。目标是在现有 `Base ← Graphics ← Function ← Editor/Sandbox` 分层内增加一套编辑器可用的中型地形系统，并借鉴 UE Landscape 的 Component、非破坏编辑层和工具模式，但不复制 World Partition、Runtime Virtual Texture 或 Nanite。

首期目标地形为 8 km × 8 km、约 1 m 采样间距，即 8193 × 8193 个高度点。Editor 需要支持创建/导入、分块 LOD、完整非破坏层栈、基础雕刻与材质绘制、undo/redo、增量保存和重载。性能验收目标是中高端独显上 2560 × 1440 Release Editor 视口稳定 300 FPS，对应 CPU/GPU frame time P95 均不超过 3.33 ms。

## Goals

- 支持一个 Scene Entity 通过 `TerrainComponent` 引用一个 `.AshTerrain` 资产。
- 支持 8193 × 8193 高度点、8192 × 8192 quad、32 × 32 个 256 m Component。
- 支持 Base Import 与多个可排序、隐藏、调强度的非破坏编辑层。
- 支持 Raise/Lower、Smooth、Flatten、Noise、Paint、Erase，并保证一次连续拖动对应一条可撤销命令。
- 支持全地形固定 8 个材质层，以及两张 RGBA8 最终权重图。
- 支持 PNG、RAW、EXR 高度导入/导出和平坦地形创建。
- 支持 CPU 高度、法线和精确射线查询，供 Editor 笔刷、场景拾取和角色贴地复用。
- 通过共享规则网格、Component LOD、实例化批次和权重物理 atlas 满足性能与内存门槛。
- 复用现有 readiness 信号，在一次运行中完成 smoke 与 golden capture，不增加固定帧数等待。
- Vulkan 与 DX12 使用相同的 Function 层算法、shader 数据布局和资源语义。

## Non-goals

- World Partition、世界原点重定位、跨地图流送或 One File Per Actor。
- Runtime Virtual Texture、Streaming Virtual Texture 或通用虚拟纹理页表。
- Nanite/虚拟化几何、mesh shader 专用路径或 GPU-driven occlusion culling。
- 地形洞、洞穴、可见性 mask、道路/河流 spline、侵蚀、水力侵蚀、坡道和区域复制粘贴。
- 植被、草、程序化散布、水体和地形 decal。
- 刚体 heightfield 物理碰撞；首期只提供查询接口。
- 运行时地形编辑、网络同步和多人协作锁。
- 任意 Terrain 旋转、负缩放或非 heightfield 拓扑。
- 通过降低既有渲染质量、关闭 validation 规避正确性问题，或未经确认刷新 golden/perf baseline。

## Current implementation

- Entry points:
  - Scene 组件定义和序列化位于 `project/src/engine/Function/Scene/SceneComponents.h`、`Scene.h/.cpp`。
  - Scene 到渲染的唯一桥是 `ScenePresentationSubsystem`；不可变帧数据位于 `RenderScene.h/.cpp`。
  - 帧编排位于 `SceneRenderer::render_visible_frame`，当前 GBuffer 后接 AO、阴影、延迟光照、环境、粒子和后处理。
  - AssetDatabase 当前识别 Scene/Texture/Mesh/Model/Material 等资产，没有 Terrain 类型。
  - Editor 面板通过 `UIContext`，命令通过 `CommandService` 与 `UndoRedoService`。
- Modules:
  - Scene 已有 Name/Transform/Camera/Light/Mesh/Environment/Particle 七类组件。
  - Render 已有 StorageBuffer 局部更新、ComputeProgram UAV 纹理写入、纹理数组原生资源和实例化绘制。
  - RHI 原生 `TextureCreation` 支持 array layers，但 Function 层尚无 2D texture array 创建包装。
  - 公共 `CommandBuffer` 没有 buffer-to-texture region 或 texture subresource upload；后端现有 texture upload helper 只支持整资源上传。
  - `IGpuProfilerContext` 只把 GPU zone 发送给 Tracy；非 Tracy 构建为 no-op，且 `RendererFrameStats`/PerfGate 没有 GPU frame 或 pass 时间样本。
- Data flow:
  - Scene 版本变化由 `ScenePresentationSubsystem` 增量同步到 `RenderScene`，再生成 `VisibleRenderFrame`。
  - RenderAssetManager 用 activity epoch、pending/failed 状态参与 readiness。
- Known constraints:
  - Editor/Sandbox 禁止直接依赖 Graphics 或后端细节。
  - `firstInstance` 必须为 0；实例基址只能通过独立数据或每批从 0 开始解决。
  - Vulkan sparse binding 是可选能力，不能作为 Terrain 启动或运行前提。
  - RenderGate/PerfGate 不接受固定帧成功条件；成功必须由资源与 present 完成信号证明。

## Proposal

### Architecture overview

```text
TerrainComponent
    -> TerrainAssetSnapshot
    -> ScenePresentationSubsystem
    -> RenderTerrainProxy / VisibleTerrainFrame
    -> Terrain atlas update passes
    -> Terrain shadow + GBuffer

TerrainEditorService
    -> TerrainStrokeCommand
    -> sparse edit-layer blocks
    -> background compose
    -> immutable component snapshot
    -> CPU query cache + GPU upload payload
```

一个逻辑地形对应一个 Scene Entity。32 × 32 个 Component 是资产、编辑、查询、LOD、剔除和 GPU 驻留单元，但不暴露为 Scene Entity。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Scene | 新增 `TerrainComponent`、提取描述、版本号、序列化和 schema v6 | `Function/Scene/SceneComponents.*`、`Scene.*`、scene serialization |
| SceneQuery | 新增高度、法线、预取和射线查询；复用 Terrain min/max 层级 | `Function/Scene/SceneQuery.*`、新增 `TerrainQuery.*` |
| Asset | 新增 `AssetType::Terrain`、`.AshTerrain` 容器、PNG/RAW/EXR 导入导出、不可变 snapshot | `Function/Asset/Terrain*`、`AssetDatabase.*` |
| Render asset | 新增 Terrain snapshot 的异步加载、activity epoch、失败传播和 GPU 资源缓存 | `Function/Render/RenderAssetManager.*`、新增 `TerrainRenderAsset.*` |
| Render scene | 新增 `RenderTerrainProxy`、可见 Terrain 批次、revision/dirty Component 同步 | `Function/Render/RenderScene.*`、`ScenePresentationSubsystem.*` |
| Render pass | 新增 atlas update、LOD batch、shadow/GBuffer 绘制和 debug view | 新增 `TerrainRenderPass.*`、`SceneRenderer.*`、RenderGraph 相关公共用法 |
| Render device | 新增 Function 层 `create_texture_2d_array` 包装，底层复用既有 `TextureCreation` | `Function/Render/RenderDevice.*`、`Renderer.*` |
| GPU timing | 新增独立于 Tracy 的固定容量 timestamp contract、Vulkan/DX12 等价实现和 PerfGate 采样桥 | `Graphics/GpuTimingRHI.*`、`Graphics/Vulkan/VulkanGpuTiming.*`、`Graphics/DirectX12/DX12GpuTiming.*`、`Function/Diagnostics/PerfGate.*` |
| Shader | 高度 buffer 读取、LOD morph、权重 atlas、8-slice 材质数组、GBuffer/shadow 变体 | `engine/Shaders/Terrain/*` |
| Editor | 新增 Terrain Mode、Inspector editor、编辑服务、笔刷命令和 overlay | `editor/Panels/Terrain/*`、`editor/Services/TerrainEditorService.*`、Inspector/Core |
| Tools/tests | 增加 Terrain RenderGate/PerfGate 场景、故障注入、单元和集成测试 | `project/src/tests/Terrain/*`、`scripts/Run*Gate.ps1`、`product/assets/scenes/`、`tools/render/goldens/terrain/` |
| Third party | 引入仅供 Asset 导入导出的 TinyEXR v3.2.0 | `project/thirdparty/tinyexr/`、premake files、thirdparty license inventory |
| Docs | 回写 Scene/Asset/Render/Editor/Tools 模块 spec，新增 Terrain feature spec | `docs/specs/features/terrain.md`、相关 module specs、`CODEBASE_MAP.md`、`VERIFY.md` |

### Scene contract

`SceneComponentType` 新增 `Terrain`。`TerrainComponent` 的首期公共字段为：

- `asset_path`
- `visible`
- `casts_shadow`
- `receives_shadow`
- 可选 8 个材质层覆盖引用

Transform 仅允许平移和正数缩放；不允许旋转或负缩放。序列化时对非法值报错并保持资产不变，不静默修正。Scene JSON schema 从 v5 升到 v6；旧场景不含 Terrain 时按现有默认迁移。

Terrain asset 内容变化拥有独立 revision。Scene 普通 transform 变化不重建 Terrain 数据，只更新 proxy transform 和 world bounds。

### Terrain dimensions and component ownership

- 高度采样：8193 × 8193。
- quad：8192 × 8192。
- 默认采样间距：1 m。
- Component：32 × 32，每块 256 × 256 quad。
- 渲染/查询 tile：257 × 257 高度点。

持久化层以全局 sample coordinate 为唯一真源。Component 右/上边界的重复值不作为第二份权威数据；生成 component snapshot 时从相邻 tile 构建 halo。边界修改会标记共享该边界的全部 Component dirty。

### Height and weight representation

Base height 使用 R16 归一化编码：

```text
world_height = height_offset + normalized_u16 * height_range
```

工作集、笔刷、合成和 CPU 查询使用 float32；只有持久化 Base 与 GPU 高度 buffer 使用 R16。导入器必须显式确认 `height_offset`/`height_range`，不应用 gamma、sRGB、曝光或颜色管理。

每个 Terrain 固定 8 个材质层。最终权重为两张 RGBA8；每个 sample 的 8 个整数权重之和必须精确等于 255，量化余数归入当前最大权重层。未绘制区域隐式表示为 Layer 0 = 255，不分配稀疏权重块。

### Edit-layer composition

每个 `TerrainEditLayer` 有稳定 UUID、名称、可见性、强度和高度混合模式。一个层可同时持有稀疏高度与权重 block。

每个 `(Layer UUID, domain, owner Component)` 最多存在一个 canonical block。Height 与 Weight 是两个独立 domain；block 使用全局 sample coordinate、exclusive maximum 和 row-major payload。canonical block 覆盖 owner 的最小非零 coverage 包围矩形，矩形内的空洞显式存为零 coverage；全部 coverage 归零时移除 block。加载、笔刷和 patch 回放都拒绝同一层、同一 domain、同一 owner 的重复 block，禁止依靠重叠 block 的顺序表达编辑结果。

- `Additive` 高度层保存有符号 delta 与 coverage mask：`H += delta * mask * opacity`。
- `Alpha` 高度层保存目标高度与 coverage mask：`H = lerp(H, target, mask * opacity)`。
- 权重层保存 8 个目标权重与 coverage mask：从下到上 alpha 合成后执行非负约束与整数归一化。

层隐藏、强度变化或重排只标记该层实际占用 block 的并集，不把全部 1024 个 Component 无条件重算。后台任务优先合成可见和正在编辑的 Component，并在完成后发布新的不可变 snapshot。

`TerrainWorkingSet` 显式维护按 `z/x` 排序去重的 `dirty_components`。一次非空笔刷、patch 回放或层栈内容修改只把 `content_generation` 增加一次；达到 `UINT64_MAX` 时原子失败。changed rect 通过共享边界 halo 规则扩张后合并进 dirty 集合。`publish_terrain_working_set` 必须收到与完整 dirty 集合精确相同、且 payload/component generation 都等于当前 generation 的 Component payload；成功时一次性替换 working-set component 指针、清空 dirty 集合并发布 immutable snapshot，失败时 working set、旧 snapshot 和输出都不变。未修改 Component 继续共享旧 const 指针。

### Brush and undo/redo contracts

首期为 7 个工具、6 个 kernel family：Raise/Lower 共用 signed sculpt kernel，另外是 Smooth、Flatten、Noise、Paint、Erase。公共 brush 参数为世界空间半径、强度、falloff、stroke spacing、当前层和确定性 seed。

原始路径点使用 terrain-local XZ；API 额外接收两个轴均为 finite positive 的 `world_meters_per_terrain_meter`。所有路径长度、spacing、半径和径向距离先用该二维度量转换到 world space，因此非均匀正缩放下的世界圆形笔刷自然映射为 terrain-local 椭圆。`apply_terrain_brush_stroke` 内部严格重采样一次，调用方不提交预采样 dab。

重采样用 double 累积距离，在精确 spacing 倍数处线性插值位置和 pressure，并始终包含首尾点；相邻距离平方不大于 `1e-12` 的重复点由后一个点替换。空路径成功返回空结果，单点路径返回该点，终点已位于最后一个 spacing 倍数时不重复添加。算法不读取 frame index、delta time 或调用次数；同一几何路径、线性 pressure、参数与 seed 在不同原始点密度和输入帧率下生成逐元素相同的 dab。

`radius` 必须 finite 且 `0 < radius <= 2048 m`，spacing 必须 finite positive，strength、falloff 和 pressure 必须 finite 且位于 `[0,1]`；世界度量、Layer UUID、tool enum 和 Paint/Erase material index 也必须有效。非法输入清空输出并保持 working set 不变。令 world-space normalized distance 为 `r`，普通 falloff 的影响为：

```text
a = pow(1 - smoothstep(falloff, 1, r), 2) * pressure * strength
```

`r >= 1` 时影响为零。`falloff == 1` 是显式 hard-edge 分支：`r < 1` 时 radial weight 为 1，边界及外部为 0，禁止调用退化的 `smoothstep(1, 1, r)`。

整个 stroke 在第一次 mutation 前冻结“Base + 选中层及其以下可见层”的高度和权重源；更高层不参与，避免把上层效果烘焙进当前层。冻结源直接从 authoritative Base/edit blocks 计算，不依赖可能 pending 或旧 generation 的 composed Component cache。连续 dab 按重采样输出顺序合并：

- Additive block 只接受 Raise、Lower、Noise。令旧 coverage 为 `c`、旧 delta 为 `d`，`p = d*c`，新 coverage 为 `c' = a + c*(1-a)`；Raise/Lower/Noise 分别把 `+a`、`-a`、`noise*a` 加到 `p`，再以 `d' = p'/c'` 保存。这样 strength/falloff/pressure 只应用一次。
- Alpha block 只接受 Smooth、Flatten。Smooth target 是冻结的 through-selected 高度四邻域平均，Terrain 外边缘对 sample coordinate clamp；Flatten target 是第一个重采样 dab 在冻结高度上的双线性值。target 与 coverage 使用 source-over：`c' = a + c*(1-a)`，`t' = (T*a + t*c*(1-a)) / c'`。
- Paint/Erase 不受高度 blend mode 限制，同样用 source-over 合并 8-lane target 与 coverage。Paint target 为选中 lane = 1 的 one-hot 权重；Erase target 把选中 lane 置零并按冻结源中其他非零 lane 的比例归一化，没有其他非零 lane 时 target 回退 Layer 0 = 1。最终 layer stack 合成后再做非负约束和精确 255 量化，禁止对 influence 重复加权。
- mode 不兼容时原子失败，禁止静默改变整个层的 height blend mode。

Noise 对 global sample `(x,z)` 与完整 64-bit seed 使用固定 unsigned 64-bit 运算：`packed = (uint64(x) << 32) | uint64(z)`，`h = seed ^ packed`，然后执行 SplitMix64 finalizer：

```text
h += 0x9E3779B97F4A7C15
h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9
h = (h ^ (h >> 27)) * 0x94D049BB133111EB
h = h ^ (h >> 31)
u = double(h >> 40) / 16777215.0
noise = 2*u - 1
```

所有溢出都使用标准 unsigned wraparound；测试锁定固定坐标/seed 的完整 patch hash。

一次 mouse-down 到 mouse-up 形成一个 `TerrainStrokeCommand`。Engine 的纯逻辑 brush 在首次修改 canonical block 时懒捕获 logical before 数据，每个 `(owner, domain)` 最多输出一个最小 changed-rect patch，并按 `owner.z`、`owner.x`、Height-before-Weight 排序。patch 包含 Terrain asset ID、Layer UUID、owner、domain、changed rect、原 stroke generation，以及独立的 before/after codec 与 bytes：

- Height raw schema：每 sample 依次为 little-endian IEEE754 `float value`、`float coverage`，8 bytes。
- Weight raw schema：每 sample 依次为 8 个 little-endian IEEE754 `float target`、`float coverage`，36 bytes。
- before/after 分别比较 raw 与确定性 RLE；只有 RLE 严格更小时选择 RLE，平局保留 raw。RLE record 为 little-endian `uint32 count` + `uint8 value`，使用 maximal run，超过 `UINT32_MAX` 的 run 分段。

Engine 提供纯逻辑 batch patch Undo/Redo API。回放先解码全部 patch，验证 asset/layer/owner/domain/rect/codec/stride、当前 logical bytes 与方向要求的 source bytes 完全一致，并构造所有候选 canonical blocks；任一 patch 无效、RLE 零 count/截断/溢出/尺寸不符、allocation failure 或 generation overflow 时，working set、generation、dirty 集合与输出均保持不变。全部验证成功后才以 no-throw swap 一次提交，generation 只增加一次，合并完整 halo dirty 集合；目标 coverage 全零时收缩或移除 canonical block。Undo/Redo 不把 generation 回退到 patch 的原 stroke generation，后者只用于诊断和顺序校验。

2048 m 笔刷按 owner block 分块，所有面积和 byte-size 计算使用 checked arithmetic；只复制 touched canonical blocks 和最终 patch，不复制 8193² Base 或整个 working set，也不增加未经批准的任意内存上限。空修改产生空 patch、空新增 dirty 且 generation 不变，不进入历史。命令按 Terrain asset ID、Layer UUID 和 owner 定位，连续 stroke 不得越序发布。

### `.AshTerrain` container

`.AshTerrain` 是 little-endian、version 1 的单文件分块容器，包含：

- 固定 header 与 magic。
- Terrain dimensions、height mapping、8 个材质层和 layer stack metadata。
- Base height blocks。
- 稀疏 edit-layer height/weight blocks。
- 可重建的 composed Component cache、min/max pyramid 和 LOD error。
- 两个固定 index descriptor 槽，每个包含 generation、index offset/size 与 CRC。
- 每个 block 的 kind、Layer UUID、Component coordinate、channel、codec、offset/size 与 CRC。

v1 block codec 支持 `None` 与内建确定性 `RLE`。加载时验证两个 index descriptor，选择 generation 最大的有效索引；不需要单独 active pointer。保存时追加 dirty blocks 和新索引，flush 成功后覆盖较旧 descriptor。任何一步失败都保留上一有效 generation。

普通保存只写 dirty blocks。显式 `Optimize Terrain` 把 live blocks 压实到临时容器，校验后原子替换，不在普通保存中执行全文件重写。

### Import and export

- PNG：支持 8/16-bit grayscale；8-bit 显示精度警告。实现使用 Windows Imaging Component（WIC）原生 PNG codec；后台 worker 自行初始化 COM MTA。16-bit 导出必须请求并复核 `GUID_WICPixelFormat16bppGray`，编码器返回其他格式时直接失败，禁止静默降为 8-bit。
- RAW：支持 R16、R32F、大小端和 X/Y axis flip。
- EXR：支持用户选择的 half/float 单通道。
- 平坦地形：直接生成 Base height 与隐式 Layer 0 权重。

输入不是 8193 × 8193 时不得静默缩放。用户必须显式选择 crop 或 deterministic Catmull-Rom resample。导入在后台写临时容器，只有全部 block/index 校验通过后才发布 AssetDatabase entry。取消或失败删除临时文件。WIC 像素格式契约以 Microsoft 的 [Native pixel formats overview](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-native-pixel-formats) 为准。

导出支持最终合成高度、Base height、指定高度层和指定材质权重层。输出保持线性数值。RAW/PNG 流式编码；EXR 在分配连续缓冲前检查内存并允许取消。导入峰值内存门槛为 1 GiB。

### TinyEXR dependency

引入官方 TinyEXR `v3.2.0` tag（release commit `6f470c9`），许可证为 BSD-3-Clause。vendored 内容必须：

- 仅包含 EXR import/export 所需源码、头文件和上游许可证。
- 固定 tag/commit 与 vendored 文件 SHA-256。
- 记录 TinyEXR 及其实际启用 codec 的第三方许可证。
- 禁止 TinyEXR 类型泄漏到 Asset 公共接口。
- 禁止 Render、Scene、Editor UI 和运行时渲染依赖 TinyEXR。
- 在 premake 中作为 Asset 实现细节构建，并进行 Debug/Release 全新构建验证。

### Machine-readable GPU timing

300 FPS 和 Terrain pass 子预算必须由自动化直接读取 GPU timestamp；CPU submit 时间、Tracy 截图、PIX/RenderDoc capture 都不能代替硬门槛。新增 `GpuTimingRHI`，与面向交互分析的 `GpuProfilerRHI` 分离，避免非 Tracy 构建退化为 no-op。

公共 contract 只表达后端无关数据：

```cpp
namespace RHI
{
    constexpr uint32_t kMaxGpuTimingScopes = 128;

    enum class GpuTimingResult : uint8_t
    {
        Success,
        Pending,
        Unsupported,
        CapacityExceeded,
        InvalidState,
        StaleHandle,
        RecordFailed,
        ResolveFailed,
        DeviceLost,
        QueueFrequencyInvalid
    };

    struct GpuTimingScopeHandle
    {
        uint32_t frame_slot = 0;
        uint32_t scope_slot = 0;
        uint64_t generation = 0;
    };

    struct GpuTimingScopeSample
    {
        uint64_t stable_name_hash = 0;
        double elapsed_ms = 0.0;
    };

    struct GpuTimingFrameSnapshot
    {
        uint64_t submitted_frame_index = 0;
        double frame_elapsed_ms = 0.0;
        uint32_t scope_count = 0;
        bool overflowed = false;
        std::array<GpuTimingScopeSample, kMaxGpuTimingScopes> scopes{};
    };

    class IGpuTimingContext
    {
    public:
        virtual ~IGpuTimingContext() = default;
        virtual auto begin_frame(CommandBuffer* cmd, uint64_t submitted_frame_index) -> GpuTimingResult = 0;
        virtual auto begin_scope(CommandBuffer* cmd, uint64_t stable_name_hash,
                                 GpuTimingScopeHandle& out_handle) -> GpuTimingResult = 0;
        virtual auto end_scope(CommandBuffer* cmd, const GpuTimingScopeHandle& handle) -> GpuTimingResult = 0;
        virtual auto end_frame(CommandBuffer* cmd) -> GpuTimingResult = 0;
        virtual auto try_collect(GpuTimingFrameSnapshot& out_snapshot) -> GpuTimingResult = 0;
    };

    auto gpu_timing_install(IGpuTimingContext* context) -> void;
    auto gpu_timing_get() -> IGpuTimingContext*;
}
```

实现约束：

- 整帧范围是 present 对应的主 graphics command buffer；Terrain pass 使用稳定哈希 `Terrain.GBuffer` 与 `Terrain.Shadow`。Graphics 不包含 Terrain 枚举或字符串。
- 每个 in-flight frame 拥有独立 query/readback 槽。Vulkan 使用 timestamp query pool 与 `timestampPeriod`；DX12 使用 timestamp query heap、resolve/readback 和 graphics queue frequency。
- 后端私有提交 hook 在真实 queue submit 成功后把 recording slot 绑定到该次 fence/timeline completion value；不增加公共 queue/submit API。query/readback slot 状态机固定为 `Idle -> Recording -> Submitted -> Completed -> Materialized/Failed -> Idle`，完整 materialize 或失败前不得 reset/reuse。
- 若主 command buffer 录制失败或跳过真实 submit，后端在 frame close 时取消对应 recording slot 并回到 `Idle`，不得生成 snapshot；Application 也不得把该 frame index 加入 PerfGate expected set。timing 自身的录制失败仍按其错误码立即使 PerfGate FAIL。
- GPU 完成后，后端先把结果 materialize 到有界 CPU FIFO，再释放 query/readback slot；FIFO item 独立经历 `Queued -> Published`。FIFO 满是 `CapacityExceeded`，不能覆盖旧结果。每个 `submitted_frame_index` 恰好入队并发布一次。
- `try_collect` 按提交顺序只发布关联 fence/timeline 已完成的 frame，按 `submitted_frame_index` 关联样本；未完成返回 `Pending`，不得调用 `wait_idle`、阻塞主线程、跳过较旧样本或读取未完成 query。
- contract 在非 Tracy 构建中同样工作；Tracy zone 继续由现有 `GpuProfilerRHI` 独立处理。
- `GpuTimingResult` 是跨后端稳定的机器错误码。`Pending` 只允许由 `try_collect` 返回且不失败；`begin_frame`、`begin_scope`、`end_scope`、`end_frame` 只能返回 `Success` 或立即失败码，任何非 `Success` 都立即使当前 frame 与 PerfGate FAIL。固定容量耗尽、query 错误、后端不支持或采样帧缺失都使 GPU timing snapshot 无效。PerfGate 在 JSON 中记录错误码；采样结束后直到 hard deadline 仍为 `Pending` 记录为 `DrainTimeout`。禁止静默丢 scope 或回退 CPU 时间。
- Vulkan 初始化读取 graphics queue family 的 `timestampValidBits`：0 映射为 `Unsupported`；小于 64 时对 raw timestamp 做有效位 mask 和模差，再乘 `timestampPeriod`。timestamp 写入统一使用 synchronization2 `ALL_COMMANDS` stage，fallback 使用语义等价的 `BOTTOM_OF_PIPE` 结束点；GPU completion 后仍验证 query availability，异常映射 `ResolveFailed`。
- PerfGate 只统计 readiness 与 warmup 完成后登记的 frame index。采样窗口结束后继续正常泵帧并异步排空已登记样本，直到全部完成或 wall-clock hard deadline；不以固定帧数判断成功。
- PerfGate 维护 expected frame-index set，拒绝 duplicate 或 unexpected snapshot。一个 frame 内相同 stable hash 的所有 scope 先求和，再对每帧总和计算 P95；每个 Terrain sample frame 都必须出现 `Terrain.GBuffer` 与 `Terrain.Shadow`，缺失即 FAIL。Function 维护 hash 到 canonical name 的字典并检测碰撞，JSON 同时输出名字与 hash。
- Premake 增加 `--no-tracy` 生成选项，使 Engine 不定义 `TRACY_ENABLE`/`TRACY_ON_DEMAND`。Phase 0 必须 fresh-generate 该变体并分别运行 Vulkan/DX12 空 Editor PerfGate，随后恢复标准 solution；普通 Debug/Release 运行不能替代此证据。
- Phase 0 在任何 Terrain 渲染实现前交付这一 contract、双后端测试与空 Editor GPU 基线。该基础能力可供后续非 Terrain gate 复用，但本 SDD 不扩展为通用 GPU profiler UI。

### GPU data and residency

高度使用 tile-major StorageBuffer，而不是大型可变 texture：

- 1024 × 257 × 257 个 R16 sample 全部常驻，约 129 MiB。
- 每个 `uint32` 打包两个 R16 sample；每个 Component 占独立、4-byte 对齐的连续 slot，末尾奇数 sample 用 padding 补齐。该布局不依赖可选的 16-bit storage feature。
- dirty block 通过现有 StorageBuffer range update 上传；顶点 shader 使用 Component slot 和 local sample coordinate 从 `uint32` 显式解包高度。
- 法线由邻接高度中心差分生成，边界读取 halo。

高精度权重使用两个 RGBA8 物理 atlas：

- 16 × 16，共 256 个 Component slot。
- 每个 slot 含过滤 gutter，总预算不超过 140 MiB。
- CPU 权重 tile 先写 StorageBuffer，再由 compute pass 以 `RWTexture2D<unorm float4>` 写入固定 `RGBA8_UNORM` atlas。
- 正在编辑的 Component 被 pin，atlas slot 只在帧边界切换。
- 远处或未驻留 Component 使用常驻 1025 × 1025 全局低分辨率权重图；dirty 区域由同一 compute 链增量更新。
- 首期 brush 半径上限为 2048 m，保证一次 stroke 的活动 Component 可被 pin。

8 个材质层被 cook 为三个 1024 × 1024、8-slice、带 mip 的 `Texture2DArray`：Base Color、Normal、ORM。Function 层增加 `create_texture_2d_array`，直接构造已有 RHI `TextureCreation` 和 array view，不修改 Graphics 公共接口。Terrain shader 选择权重最大的 4 层；零权重层不采样，最坏为 12 次材质纹理采样。

### LOD, culling, and draw submission

每个 Component 有 9 级共享规则网格：256²、128²、64²、32²、16²、8²、4²、2²、1² quad。每级预计算几何误差和 min/max bounds。

运行时流程：

1. CPU quadtree 层级视锥剔除。
2. 根据投影屏幕误差选 LOD。
3. 迭代修正，使相邻 Component LOD 差不超过 1。
4. 生成每个 LOD 的 instance buffer。
5. 每个 LOD 一次 instanced indexed draw。

geomorph 平滑 LOD 变化；边缘顶点向相邻低一级网格对齐，不使用永久 skirt。每批 instance buffer 从 0 开始，遵守 `firstInstance == 0`。首期不实现 GPU culling 或第二套 indirect 可见性逻辑。

Terrain 进入方向光 shadow 和 deferred GBuffer，写入 depth、material、normal 和 motion-vector 所需数据。Terrain 内容 revision 发布时使受影响 view 的 TAA history 失效；不得让旧高度历史与新几何混用。

### RenderGraph ordering

```text
Dirty StorageBuffer upload completion
    -> weight atlas compute update (only when dirty)
    -> terrain directional shadow
    -> terrain GBuffer
    -> existing deferred lighting and post process
```

Compute atlas pass 只在 dirty payload 存在时加入。RenderGraph 声明 atlas UAV 写入与后续 SRV 读取关系，validation barrier 错误视为阻断缺陷。

### Terrain queries

公共查询状态为 `Ready`、`Pending`、`Outside`、`Failed`。API 覆盖：

- `query_height`
- `query_normal`
- `ray_cast_terrain`
- `prefetch_query_region`

查询输入输出均为 world space；内部转换到 axis-aligned terrain-local sample coordinates。射线先遍历 Terrain quadtree 与 Component AABB，再通过 min/max 高度金字塔逐级下降，最后对命中 cell 的两个三角形做精确求交，不使用固定 ray-march step。Editor 未拿到 `Ready` 前不得开始 stroke。

### Editor workflow

Inspector 为 `TerrainComponent` 提供资产、可见性、阴影和正缩放属性。独立 Terrain Mode 包含：

- Manage：创建、导入、导出、保存、Optimize 和状态。
- Sculpt：高度层、笔刷和 Raise/Lower、Smooth、Flatten、Noise。
- Paint：8 个材质层、Paint、Erase。
- Layers：创建、删除、复制、重命名、排序、隐藏和强度。

`TerrainEditorService` 是可变编辑会话的唯一所有者。ViewportPanel 只提交意图；后台任务只产生新 immutable snapshot。笔刷贴地预览通过 `ScenePresentationSubsystem` overlay 提交世界空间折线，Editor 不创建 GPU buffer、不访问 Graphics。

Terrain Mode 根据当前高度层的 blend mode 只启用兼容工具：Additive 启用 Raise/Lower/Noise，Alpha 启用 Smooth/Flatten；Paint/Erase 始终按材质层选择工作。任何来自快捷键、旧 UI 状态或脚本的 mode 不兼容意图都由 Engine brush API 再次原子拒绝，UI 禁用不能替代契约校验。

### Save, reload, and failure semantics

Editor 分别跟踪 Scene dirty 和每个 Terrain asset 的 dirty generation。`Ctrl+S` 先保存被当前 Scene 引用的 dirty Terrain，再保存 Scene。保存开始后发生的新编辑属于下一 generation，不被错误清除。

外部文件变化：

- 本地无 dirty：后台加载并在帧边界替换 snapshot。
- 本地有 dirty：保留内存数据，要求 reload/discard、keep local 或 save as。
- Reload 成功后清除引用该 Terrain asset 的 undo/redo 记录。

最新 index 损坏时回退上一有效 generation 并警告。两个 index 都无效或关键 block 无法恢复时，资产进入 `Failed`，Renderer 显示诊断占位，Editor 进入只读恢复状态，readiness 失败。保存失败不得覆盖旧 generation、清 dirty 或删除 undo 历史。

### Readiness contract

Terrain capture-ready 同时要求：

- import/load 没有 pending 或 failed。
- 当前 content generation 的可见 Component 已合成。
- 所需高度 buffer 和权重 atlas 更新已由 GPU 完成信号确认。
- 当前 Scene packet 成功提交并完成非致命 present。

Terrain readiness 合入现有 asset activity epoch 和 scene submission snapshot。成功路径没有固定 frame count、固定 sleep 或 fallback capture；wall-clock 只作为失败上限。

### API / contract changes

- Scene schema v6 与 `TerrainComponent`。
- `AssetType::Terrain` 与 `.AshTerrain` version 1。
- `TerrainAssetSnapshot`、Terrain revision/readiness、dirty Component payload。
- `TerrainWorkingSet::dirty_components`、canonical edit blocks、原子 `publish_terrain_working_set` generation 闭环。
- world-metric Terrain brush、确定性 stroke resampling、独立 before/after codec 的 `TerrainEditPatch` 与 Engine batch Undo/Redo API。
- `TerrainQueryStatus` 与四个公共查询入口。
- `VisibleTerrainFrame` / `RenderTerrainProxy`，只通过 ScenePresentationSubsystem 传递。
- Function 层 `create_texture_2d_array`；不修改 RHI `GraphicsContext`/`CommandBuffer`。
- `GpuTimingRHI` 固定容量异步 snapshot contract；不修改资源、barrier、upload 或提交接口。
- Terrain shader 固定绑定：height StorageBuffer、instance buffer、两张 weight atlas、coarse weights、三张 8-slice material arrays、samplers/root constants。
- PerfGate 新增 `-Scenario Terrain`，RenderGate 新增 `terrain` scene。

### Backend impact

Vulkan 与 DX12 共享：

- CPU layer compose、LOD/culling、instance list 和 upload payload。
- HLSL Terrain shader 源码及绑定名称。
- StorageBuffer 高度布局、RGBA8 atlas 格式和 2D array slice 语义。
- `firstInstance == 0` 批次契约。

Terrain 渲染仍只使用现有 buffer staging upload、texture array、UAV texture、instanced draw 和 RenderGraph barrier。唯一批准的新后端能力是上一节定义的只读 GPU timestamp contract；它不得改变资源、barrier、upload、queue 或提交语义。若实现中发现必须增加公共 texture-region upload 或其他 RHI API，当前 SDD 即失效，必须先修订 SDD 并重新批准，不得顺手扩 Graphics。

### Performance

固定验收配置：

- Release Editor，validation off，VSync off。
- RTX 4070 / RX 7800 XT 级别独显。
- 2560 × 1440。
- TerrainPerf：8 km × 8 km、8 个材质层已加载、典型区 1–2 层混合、压力区 4 层混合、单方向光与阴影、无植被/水体/其他 mesh。
- readiness 完成后预热，随后采样 30 秒。
- GPU 样本按提交 frame index 与 CPU 采样窗口关联；缺失、overflow 或未在 hard deadline 前排空的样本令 gate 失败。

硬门槛：

- CPU frame P95 ≤ 3.33 ms。
- GPU frame P95 ≤ 3.33 ms。
- Terrain CPU cull/LOD/submit P95 ≤ 0.25 ms。
- Terrain GBuffer P95 ≤ 0.8 ms。
- Terrain shadow P95 ≤ 0.6 ms。
- Terrain GPU resident memory ≤ 512 MiB。
- 30 分钟运行无持续内存增长。
- 64 m brush 固定路径编辑期间 frame P95 ≤ 16.67 ms。
- stroke end 到 compose/upload 完成信号 P95 ≤ 100 ms；信号后立即重新适用 3.33 ms 门槛。

Phase 0 先测同配置空 Editor 基线。若空场景已经超过 3.33 ms，不降低 Terrain 目标，也不把全局优化混入本 SDD；先建立独立性能整改 SDD，完成后再继续 Terrain 性能验收。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| Unit tests | tile ownership、canonical block、层合成、权重归一化、world-metric 重采样、7 工具/6 kernel family、brush→patch→Undo→Redo、generation/dirty 原子性、RLE malformed、LOD、query、容器恢复 | `RunTests.bat Debug`、`RunTests.bat Release` |
| Architecture | Editor/Scene/Render/Graphics 依赖方向 | `RunArchGate.bat` |
| Fresh build | TinyEXR、premake、Editor/Sandbox、artifact 同步 | `generate_vs2022.bat`，`build_editor.bat Debug/Release`，`build_sandbox.bat Debug/Release` |
| Scene lifecycle | Scene v6、Terrain create/load/reload/save、readiness | `run.bat all Debug --smoke-test-seconds=120`，双后端 |
| GPU timing contract | 非 Tracy 可读、frame/scope 关联、overflow/missing fail、无同步等待 | 单元测试 + Vulkan/DX12 空 Editor PerfGate |
| RHI validation | timestamp query、StorageBuffer upload、atlas UAV/SRV barrier、texture array | Vulkan/DX12 validation 各跑 Terrain readiness capture |
| Render regression | Terrain golden 与 cross-backend SSIM | `RunRenderGate.bat`（新增 terrain scene） |
| Standard performance | 既有四组合无回归 | `RunPerfGate.bat -Profile Standard` |
| Terrain performance | 1440p/300 FPS 与子预算 | `RunPerfGate.bat -Profile Standard -Scenario Terrain` |
| Tool tests | gate 参数、故障注入、超时与报告 | 对应 `scripts/Test*.ps1` |
| AI plan audit | 变更矩阵与高风险路径 | `scripts/AIDevDoctor.ps1 -Mode ValidatePlan` |
| Manual Editor | Terrain Mode、全部工具、layer 管理、冲突重载、错误提示 | 按 `docs/VERIFY.md` Terrain checklist |

Render golden 只能在用户目视确认预期画面后通过 `RunRenderGate.bat -BlessGolden` 更新。Terrain perf baseline 只能在用户确认参考硬件水位后通过正式 bless 流程更新；绝不直接编辑基线文件。

## Task breakdown

1. **Phase 0：可行性与空 Editor 基线**
   - 先实现固定容量 `GpuTimingRHI`、Vulkan/DX12 timestamp/readback、PerfGate frame/pass 汇总和故障语义。
   - 建立 1440p TerrainPerf harness，并用机器可读 GPU timing 测量整帧与稳定命名 pass。
   - 记录空 Editor CPU/GPU P95；不满足 3.33 ms 时停止 Terrain 实现并转独立性能 SDD。
   - 用现有 StorageBuffer update + compute UAV atlas 做双后端最小原型，validation 零错误后才继续。
2. **Phase 1：资产与纯逻辑**
   - 引入 TinyEXR v3.2.0 与许可证/哈希。
   - 实现 Terrain 数据结构、`.AshTerrain` 双索引容器、增量保存、Optimize、PNG/RAW/EXR 导入导出。
   - 实现 layer compose、canonical block、world-metric 笔刷核、patch Undo/Redo、generation/dirty 发布闭环、query、min/max/LOD error 与故障注入测试。
   - 验收：纯逻辑单测全绿，保存失败不损坏上一 generation。
3. **Phase 2：Scene 与渲染闭环**
   - 增加 Scene v6 `TerrainComponent`、RenderTerrainProxy、可见帧数据和 readiness。
   - 实现 height buffer、weight atlas compute update、texture arrays、LOD batches、shadow/GBuffer/debug view。
   - 验收：双后端 validation、readiness smoke、RenderGate 临时 capture 通过；不 bless 未确认画面。
   - 2026-07-14 exit gate：Vulkan core/synchronization validation 与 DX12 GPU validation 的 Terrain readiness capture 均零 validation/error/warning；既有 RenderGate 与 Standard PerfGate 四组合 PASS，未 bless。Standard 历史 baseline 缺失，故该 PerfGate 只证明本次运行健康，300 FPS 与历史回归仍属于 Phase 4。
4. **Phase 3：Editor authoring**
   - 实现 Terrain Mode、Inspector、TerrainEditorService、overlay 和 TerrainStrokeCommand。
   - 完成 7 工具/6 kernel family、layer 管理、undo/redo、save/reload/conflict UI。
   - 验收：脚本化/人工 checklist 通过，Editor 无 Graphics 依赖。
5. **Phase 4：性能与交付**
   - 优化 cull/LOD、instance upload、atlas residency、shader top-4 blend 和异步合成。
   - 跑全量验证矩阵、30 分钟稳定性和 TerrainPerf 硬门槛。
   - 用户目视确认 Terrain golden 后才 bless；结论回写长期 specs，SDD 标记 Done。

每个 Phase 独立提交、独立验证。任一硬门槛失败时不进入下一 Phase。

## Risks

| Risk | Mitigation |
| --- | --- |
| 300 FPS 高于当前 Editor 基线能力 | Phase 0 先做空场景测量；基线失败转独立性能 SDD，不降低目标或污染 Terrain diff |
| GPU timing 查询造成 stall 或样本错配 | 每个 in-flight frame 独立 query/readback；只读取已完成 frame；按提交 frame index 关联；overflow/missing 直接令 gate 失败；双后端 validation 与非 Tracy 测试 |
| 8 层材质的最坏像素成本过高 | 固定 texture arrays、top-4 选择、零权重不采样；用 Terrain GBuffer GPU 计时守门 |
| 256-slot atlas 在大笔刷或高空视图下压力过大 | 活动区 pin、2048 m brush 上限、远景 coarse weights、帧边界 LRU；内存与 residency telemetry |
| Component 边缘产生裂缝或查询不一致 | 全局 sample 单一权威、halo 派生、邻接 dirty、LOD 差≤1、geomorph 与边界专项 golden |
| 非破坏层重排触发大范围重算 | 稀疏 occupancy 索引、只合成受影响 block 并优先可见块；不可变 snapshot 发布 |
| 增量容器断电/崩溃损坏 | 双 index descriptor、generation/CRC、append-before-commit、上一代回退、故障注入 |
| Undo 内存失控 | 只保存改变矩形、raw/RLE 自适应、history telemetry 和可配置总预算；不得复制整地形 |
| 2048 m 笔刷产生超大 patch 或半提交 | 按 owner 分块、checked arithmetic、只复制 touched blocks、全 batch 候选成功后 no-throw swap；allocation failure 保持零 mutation |
| generation 更新但 Component 仍为旧内容 | working set 显式维护完整 halo dirty 集合；publish 要求 payload 集合与 generation 精确匹配，成功后原子替换并清 dirty |
| EXR 解码扩大供应链或内存风险 | TinyEXR 固定 tag/hash/license、隔离在 Asset、malformed corpus、1 GiB import 上限 |
| WIC PNG encoder 静默选择较低精度格式 | `SetPixelFormat` 后检查返回 GUID 必须仍为 `GUID_WICPixelFormat16bppGray`；worker 对称初始化/释放 COM；16-bit round-trip 测试 |
| Function texture-array 包装暴露后端差异 | 只映射已有 RHI array 语义；双后端自测与 validation；发现需扩 RHI 时停止并修订 SDD |
| 异步编辑产生乱序或半帧状态 | stroke sequence、generation、immutable snapshot、帧边界发布和 readiness 完成信号 |
| Save/Reload 与现有 Editor dirty/undo 语义冲突 | Terrain asset 独立 dirty generation；Ctrl+S 顺序明确；reload 清理相关命令；失败不清状态 |

## Open questions

无。任何新增范围（物理碰撞、spline、植被、World Partition、RVT、Nanite、RHI texture-region API，或超出本 SDD timing snapshot 的 RHI 改动）都需要新的或修订后的 SDD，并重新取得批准。
