---
owner: huyizhou
last_reviewed: 2026-07-15
status: active
---

# Module Spec: Graphics（RHI）

## 职责与边界

`project/src/engine/Graphics/` 是渲染硬件抽象层：定义后端无关的资源/命令接口（`RHI` 命名空间），并提供 Vulkan 与 DirectX 12 两套等价实现，以及 DXC shader 编译与磁盘缓存。它不做帧编排（SceneRenderer/RenderGraph 在 `Function/Render/`），不做 UI。上层业务（Editor/Sandbox）不直接使用本模块，统一经 `Function/` 层（RenderDevice/SceneRenderer/UIContext）访问。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `RHIBackend.h` | `Backend` 枚举（Default/Vulkan/DirectX12）+ `backend_to_string` |
| `DynamicRHI.h/.cpp` | 后端选择与运行时配置：`load_runtime_rhi_config`、`resolve_runtime_backend`、工厂实现 |
| `GraphicsContext.h` | `GraphicsContext` 设备接口 + `GraphicsContextInitConfig`、validation 配置结构 |
| `RHIResource.h/.cpp`、`RHICommon.h/.cpp` | `RHIResource/RHIView` 基类、`Ash*` 跨后端枚举（Format/Usage/Blend 等） |
| `Texture/Buffer/Sampler/Shader/RenderPass/Framebuffer/Pipeline/CommandBuffer/CommandPool/Queue/DescriptorSet*/RenderProgram/SwapChain/VertexInputLayout.h` | 资源家族的抽象接口（各自由两个后端实现） |
| `RasterizerConvention.h` | mesh winding → 各 API front-face 的唯一映射点（SPIR-V `-fvk-invert-y` 差异收口处） |
| `ShaderCompiler.h`、`DXC/DXCHelper.*`、`DXC/DXCIncludeHandler.*` | `ShaderCompiler` 接口、`AshDXCContext` DXC 封装与 include 解析 |
| `ShaderCache.h/.cpp` | shader/pipeline 缓存格式：SHA1 摘要、`ShaderCacheIndexHeader`、`PipelineCacheFileHeaderV2`，缓存目录 `product/caches/ShaderCaches/{vulkan,dx12}` |
| `GpuProfilerRHI.h/.cpp` | Tracy GPU profiler 抽象 |
| `GpuTimingTelemetryRHI.h/.cpp` | PerfGate 按需 GPU timing 契约、固定 metric/frame 状态与后端无关 sample/info 类型 |
| `Vulkan/` | `VulkanContext/VulkanSwapchain/Vulkan*` 全套实现 + `VulkanShaderCompiler`（HLSL→SPIR-V） |
| `DirectX12/` | `DX12Context/DX12Swapchain/DX12*` 全套实现 + `DX12ShaderCompiler`（HLSL→DXIL） |
| `Shaders/AshVertexDeclLocations.hlsli` | 顶点声明 location 约定 |

## 公共接口

- 后端选择：`RHI::load_runtime_rhi_config(configPath)` 读 Engine.ini —— `[RHI] Backend`（内部 `parse_backend_name` 接受 `vulkan/vk/directx12/dx12/d3d12`，大小写不敏感，非法值回退编译期默认并告警）、`[VulkanValidation]`（Enabled/GpuAssisted/SynchronizationValidation/BreakOnValidationError）、`[DX12Validation]`（Enabled/GpuValidation，非 Debug 构建强制关闭）。`resolve_runtime_backend` 处理未编译后端的回退；Windows 默认后端为 DX12。
- 工厂：`GraphicsContext::create(Backend)`、`Swapchain::create(Backend)` 按解析后端实例化对应后端对象。
- GPU timing 初始化：`GraphicsContextInitConfig.enableGpuTimingTelemetry` 默认 false；Application 仅在 PerfGate 已启用且显式请求 `gpu-timing=on` 时置 true，普通运行和仅给 timing override 而未启用 PerfGate 的运行都不得创建或启用这套 telemetry。validation 的 Vulkan/DX12 两份 resolved config 与该开关一起在 `GraphicsContext::init` 前注入；resolved validation 元数据必须反映后端实际编译能力，Release 构建即使收到 `validation=on` 请求也记录 false。
- GPU timing 提交确认：`IGpuTimingTelemetry::commit_frame(frame_id)` 返回精确提交确认，只有后端已经观察到该 timing frame 的同一 native command buffer/list 进入实际 graphics batch、native submit 成功、既有 completion primitive 已绑定，且 common frame state 已转为可轮询 Pending 时才返回 true。false 不得计入任何 submitted/coverage 统计。确定命令未执行时允许安全 abort 并复用原 slot；命令可能或已经执行但提交结果/完成绑定不可证明时必须 fail-closed，保留 native timing 资源并 quarantine 到 shutdown。`submission_bound=true` 后即使上层在确认前异常调用 `abort_frame`，也不得立即开放 slot，必须进入同样的 quarantine。
- Vulkan GPU timing：启用时由 `VulkanContext` 独占一个 66-query timestamp pool（3 个后端 frame slot × 每帧 22 queries），持久记录主 graphics queue family 的 `timestampValidBits`、设备 `timestampPeriod` 与 adapter/driver 元数据。timing frame 只接受包含其精确 `VkCommandBuffer` 的 graphics submit；`vkQueueSubmit*` 成功且既有 timeline semaphore / frame fence completion 已随同绑定后才可 commit。精确 command buffer 未进入 batch 是可复用的安全 abort；submit 失败或缺少 completion binding 时进入直到 shutdown 才复位的 quarantine。旧 slot 只能在既有 timeline/fence completion 明确成功后读取并复用；读取固定使用 `64_BIT | WITH_AVAILABILITY`，禁止 `WAIT_BIT`、额外 fence wait 或 device idle。零 query、缺失/倒序的 `GPU.Frame` pair 均产出 invalid sample，禁止伪造有效 0 ms。CPU completed queue 为固定容量；若消费者持续滞后至队列满，已确认完成的最旧 sample 作为 unresolved 丢弃并立即释放其物理 query slot，coverage 会暴露该背压，禁止连锁覆盖 pending slot。关闭 telemetry 时不得创建该 query pool。
- DX12 GPU timing：启用时由 `DX12Context` 独占一个 66-entry timestamp query heap，以及按 3 个后端 frame slot × 每帧 22 个 `uint64_t` timestamp 分段的持久映射 READBACK resource；初始化只查询一次 direct graphics queue 的 `GetTimestampFrequency()`，并记录 adapter/driver 元数据。`ResolveQueryData` 只解析本帧实际使用的连续 query 区间，目标 byte offset 必须落在同一物理 slot 的 readback 分段内。timing slot 只有在其精确 native command list 确实进入 `ExecuteCommandLists`，且既有 frame fence 的 `Signal` 返回成功并给出非零 target 后才可绑定/commit：精确 list 未执行时是可复用的安全 abort；精确 list 已执行但 `Signal` 失败或 target 非法时，timing telemetry 必须进入直到 shutdown 才复位的 quarantine，保留 native query/readback resource 且不得再开放或复用任何 timing slot。相同失败若发生在任一实际执行的 graphics batch，`DX12Context` 的 completion policy 必须单调进入 Lost：后续不得重置 allocator/descriptor/staging、取得 command buffer、排队 upload、提交或报告 frame completion；零实际执行的 batch 信号失败不得毒化 Context。该生产 policy 同时缓存 queue teardown readiness：初始及完整 shutdown 后为 Drained，真实 `ExecuteCommandLists`、成功的 `Queue::Signal`（包括零 command-list batch）或其他实际 queue work 均须转为 Unknown；失败的空 batch `Signal` 没有入队，不得擦除已证明的 readiness。fresh queue-tail target 确认完成后记录 Drained，`GetDeviceRemovedReason()!=S_OK` 时记录 DeviceRemoved。policy 的 state/readiness 会被 render、worker 与 lifecycle 路径跨线程读取，存储必须使用 atomic release、读取必须使用 atomic acquire，禁止普通字段数据竞争。后续 `wait_idle`/shutdown 必须直接复用已证明的 Drained/DeviceRemoved，未出现新 work 时不得用第二次失败的 `Signal` 覆盖；Lost 状态不得随 readiness 被证明而恢复。`DX12Fence::wait` 必须返回 Completed/Timeout/DeviceRemoved/EventRegistrationFailed/WaitFailed/InvalidFence 的结构化结果及 completed/target/HRESULT/DWORD 诊断字段；`SetEventOnCompletion` 失败后禁止继续调用 `WaitForSingleObject`，所有必需等待使用有限上限并结合 completed value 与 `GetDeviceRemovedReason` fail-closed，bounded capture wait 的纯 Timeout 只返回 false、不得毒化 Context。首次 Lost 返回调用方前必须完成 queue-tail 证明；readiness 仍为 Unknown 属于未证明完成，必须 fail-stop，禁止让上层 immediate resource 析构继续。Lost 后证明 Drained 时，telemetry 必须以 SubmissionFailed 终止所有旧 committed tracking；确认 DeviceRemoved 时则以 DeviceRemoved 终止。两种通知均须同时清空 common frame state 与 backend slot id/target/occupied metadata、按 completed queue 容量输出 invalid sample、可重复调用且最终 poll 收敛 Empty；`resolve_recycled_slot` 观察到 `UINT64_MAX` 必须复用同一 DeviceRemoved 通知路径。确认 DeviceRemoved 的 shutdown 必须在 reset telemetry、卸载/析构 Tracy profiler 或释放任何 GPU-owned resource 之前 fail-stop，避免 Tracy 对不再推进的 UINT64_MAX fence 永久 busy-wait。native query/readback resource 在 quarantine 中仍保留到正常 shutdown。poll/readback 正常路径不得新增 `WaitForSingleObject`、fence wait 或 device idle；固定 CPU completed queue 满时沿用 Vulkan 的 unresolved/drop 背压语义。关闭 telemetry 时不得创建新增 query heap 或 readback resource。
- DX12 fence stale-wake：有限 `DX12Fence::wait` 超时后，旧 target 的 `SetEventOnCompletion` 注册仍可能留在同一个 auto-reset event 上。后续等待更高 target 时，`WAIT_OBJECT_0` 但 completed value 仍低于当前 target 只表示旧注册唤醒，不是 WaitFailed；必须在同一次 `GetTickCount64` monotonic 总 deadline 的剩余预算内继续等待，禁止重新注册 event 或重置预算。target 已完成、`UINT64_MAX`、真正总超时及 `WAIT_FAILED` 均不得继续循环。
- DX12 required tail signal：required queue-tail `Signal` 成功时即使 concurrent proof 刚发布 Drained，也必须先按新的 queue work 把 readiness 失效为 Unknown；required `Signal` 失败没有入队，已证明的 Drained 保持不变，只有原缓存为 Unknown 时才将 completion state poison 为 Lost。
- 设备接口（`GraphicsContext`）：`init/shutdown/destroy`、`create_shader/buffer/buffer_view/texture/texture_view/render_pass/framebuffer/graphics_render_program/compute_render_program/sampler`、`get_sampler(AshSamplerState)`、`begin_frame/end_frame`、`get_command_buffer(threadIdx)`、`wait_idle`、带纳秒上限的 `wait_for_frame_completion`、`get_render_memory_stats`。资源以 `std::shared_ptr` 交付。
- 命令接口（`CommandBuffer`）：barrier（`cmd_transition_resource_state`）、render pass（`cmd_begin/end_render_pass`，debug_scope_name 用于 RenderDoc/PIX 标签）、绑定/draw/dispatch/copy/update 全家族；错误经 `has_error()/get_last_error()` 暴露。
- Readback（SDD-2026-07-07-render-gate 新增，验证/调试用途，非热路径）：`CommandBuffer::cmd_copy_texture_to_buffer(source, destination, buffer_offset, row_pitch_bytes)` 把 mip0/layer0 整层拷入 CPU 可读 buffer，仅支持 4 字节颜色格式（RGBA8/BGRA8），row pitch 须 ≥ width*4 且为 256 的倍数（D3D12 约束）；另有区域版 `cmd_copy_texture_region_to_buffer`。上层 backbuffer 回读封装在 `Function/Render` 的 RenderDevice（`request_back_buffer_capture/fetch_back_buffer_capture`）；fetch 必须传有限 timeout，并通过 `wait_for_frame_completion` 只等待产生该 readback 的当前 graphics frame，禁止用无上限的 device `wait_idle`。
- Indirect draw/dispatch（SDD-2026-07-09-indirect-draw-substrate 新增）：`CommandBuffer::cmd_draw_indirect(buffer, offset, drawCount, stride)`、`cmd_draw_indexed_indirect(buffer, offset, drawCount, stride)`、`cmd_dispatch_indirect(buffer, offset)`；args 结构体 `AshDrawIndirectArgs`/`AshDrawIndexedIndirectArgs`/`AshDispatchIndirectArgs` 定义在 `RHICommon.h`。Vulkan 直通 `vkCmd*Indirect`；DX12 经三种固定 argument-only command signature（设备级懒创建缓存于 `DX12Context`）+ `ExecuteIndirect`。`--rhi-selftest-indirect` 先由 `RHIIndirectSelfTest` 覆盖 compute 写 args → barrier → 三个 raw indirect API → readback，再由 Function `RenderGraphIndirectSelfTest` 覆盖 graph-managed `StorageBuffer` UAV→IndirectArgs/SRV 与 indexed draw；日志分别标记 `[RHISelfTest]`、`[RenderGraphSelfTest]`。Phase 1 完全复用上述接口与既有 `StorageBuffer` state，没有新增或修改 Graphics virtual API。
- Swapchain：`present/resize_swapchain/get_swapchain_buffer/get_format/begin_frame/end_frame`。`begin_frame()` 与 `present()` 均返回 `SwapchainPresentResult`：`Completed` 表示 acquire/present 无致命错误地完成，`Retryable` 表示当前无法处理但可继续（Vulkan no-image/out-of-date），`Failed` 表示致命后端错误。Vulkan acquire Retryable 帧不录制命令、不 present；`GraphicsContext::end_frame(false)` 仍推进 timeline/fence，但不等待未 signal 的 acquire binary semaphore，也不 signal 无人消费的 present binary semaphore。Vulkan `SUBOPTIMAL` 视为 Completed；DX12 acquire 为 Completed，所有 `SUCCEEDED(hr)` present（含 `DXGI_STATUS_OCCLUDED`）也为 Completed。DX12 fence 的 `GetCompletedValue()==UINT64_MAX` 表示 device removed，必须作为 Failed，禁止误判 capture 完成。只有完成 acquire 后才录制，只有完成 present 才可满足 readiness。
- Shader 编译：`ShaderCompiler::check_and_compile_shader` 由 `VulkanShaderCompiler`/`DX12ShaderCompiler` 实现，均走 DXC（`AshDXCContext`），产物按 SHA1（编译器输入 hash + 内容 hash）落盘到各自缓存目录，命中则跳过编译。注意 `ShaderCache.h` 中 `class ShaderCache` 为空壳，缓存读写逻辑在两个后端 compiler 内。
- Shader 预处理（rewrite）阶段的坑（SDD-2026-07-07-taa-uav-image-format 实证）：`AshDXCContext::preprocess_shader_file_to_full_text` 用 `IDxcRewriter2::RewriteWithOptions`（AST 重发射，非 `-P`），**`[[...]]` 属性会被丢弃**——这是 `[[vk::push_constant]]` 需在 rewrite 后由 `rewrite_root_constant_blocks_for_vulkan` 注入的原因。且 rewrite 阶段完整执行预处理，彼时仅 item.macroDefine 生效、`ASH_VULKAN` **未定义**（compile 阶段才 `-D` 注入），shader 源码里 `#if ASH_VULKAN` 的分支在 Vulkan 运行时路径永远不会存活。需要影响 SPIR-V 元数据时只能靠能活过 rewrite 的语言构造（如元素类型，storage image 格式经 `min16float4` 推导 `Rgba16f`）或 rewrite 后文本注入。

## 约束与不变式

- 设备能力分级（SDD-2026-07-09-vulkan-optional-device-caps）：**必需集**（缺失则报错优雅退出）= 物理设备存在、graphics+compute 队列族、`VK_KHR_swapchain`、`VK_KHR_shader_draw_parameters`；**可选集** = `DeviceExtensionAndFeaturesFlags` 全部条目（DynamicRendering/TimelineSemaphore/Sync2/MeshShaders/Multiview/FSR/RayTracing/RayQuery/Bindless/HostCoherentCached/SparseBinding），查不到只记 false + 警告日志,引擎照常启动。**新可选能力必须 flag 门控,禁止 boot 断言**；依赖可选能力的代码在使用点检查 flag（软件驱动如 lavapipe/WARP 是 CI 冒烟基线,不满足可选集是常态）。
- 双后端等价：任何 RHI 接口改动必须同时提供 Vulkan 与 DX12 等价实现，行为差异视同 bug（RenderGate 跨后端 diff FAIL 即 bug）。
- 依赖方向：Graphics 只依赖 Base；Editor/Sandbox 等上层禁止直接 include/依赖 Graphics，必须经 `Function/` 层。
- 跨后端 API 差异（Y 翻转/winding、资源状态模型）必须收口在单点（如 `RasterizerConvention.h`、各后端 ResourceTracker），不得散落到上层。
- 后端对象经 `Ash_New` 分配；validation/debug-layer 报错视同 bug，禁止靠关闭 validation 绕过。
- readback API 仅限验证/调试路径调用，不得进入常规帧热路径。
- Indirect 跨后端契约（SDD-2026-07-09-indirect-draw-substrate）：① args 结构体布局以 engine 侧 `Ash*IndirectArgs` 为唯一权威（VK/D3D12 原生布局本就相同），compute shader 按此布局写入；② **indirect draw 的 `firstInstance` 恒为 0**——DX12 下 `SV_InstanceID` 拿不到 StartInstanceLocation（SM6.8 以下），实例基址/偏移一律走 constant buffer 或独立 storage buffer，禁止依赖 firstInstance 进 shader；③ args buffer 在 indirect 消费前必须转到 `AshResourceState::IndirectArgs`；④ DX12 command signature 的 ByteStride 固定为结构体大小，multi-draw（drawCount>1）时 stride 必须等于 `sizeof(Ash*IndirectArgs)`（DX12 侧有断言）。
- `AshResourceState::ConstBuffer` 表示当前 `GraphicsProgram` / `ComputeProgram` 可读取的 uniform buffer。Vulkan 映射精确使用 vertex + fragment + compute shader stage 与 `VK_ACCESS_UNIFORM_READ_BIT`；不使用 `ALL_GRAPHICS` / `ALL_COMMANDS`，DX12 保持 `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER`。当前 Vulkan command buffer 均来自 graphics-capable queue family；未来引入 dedicated compute-only command buffer 时，stage policy 必须按 queue capability 收窄。验证入口 `--rhi-selftest-constant-buffer` 在首帧前覆盖 compute/fragment CB 上传、消费与精确读回。
- Vulkan 原生资源在首帧前销毁时不得直接索引 `currentFrame == UINT32_MAX`。current-frame deletion queue accessor 对 sentinel 返回 queue 0：首个 `begin_frame()` 选择 frame 0 后 flush。Vulkan 与 DX12 的 shutdown 都必须在首次 deletion-queue drain 前释放从未提交的 pending buffer/texture upload ownership，使资源在 allocator、descriptor heap 与 device 仍有效时进入既有回收队列；不得在 teardown 录制或提交这些已失去消费者的 upload。有效 frame 的 upload/deletion 行为不变；首帧前路径可按资源自身同步条件选择 `immediate_deletion`，不再以其规避无效 queue。constant-buffer self-test 的故障注入必须证明首帧前失败由正常 teardown 返回非零，且不发生 access violation、VMA/D3D12MA live-allocation assertion 或 watchdog hang。
- DX12 shader-visible descriptor table cache 的内容身份由 heap type、descriptor count、数组顺序以及每项 `{CPU descriptor address, allocation serial}` 共同组成。CPU descriptor heap 在既有 allocation mutex 内为每次 allocation 分配非零、单调递增且禁止回绕的 serial；同一活跃 allocation 保持 cache hit，slot 释放后即使地址被复用，新 allocation 也必须 cache miss 并重新复制 descriptor。free 路径不得扫描、反向索引或清空整帧 cache；未来若允许活跃 handle 原地重写 descriptor，必须同时推进内容身份并补回归测试。
- Present mode 语义双后端必须一致（`AshPresentMode` 为准）：FIFO=垂直同步（DX12 sync_interval=1）；MAILBOX=不等垂直同步且**绝不撕裂**（DX12 flip model sync=0 **不带** `DXGI_PRESENT_ALLOW_TEARING`，DXGI 以最新完整帧替换排队帧；Vulkan `VK_PRESENT_MODE_MAILBOX_KHR`）；IMMEDIATE=允许撕裂（DX12 sync=0 + `ALLOW_TEARING`）。MAILBOX 曾误带 tearing 标志（SDD-2026-07-07-dx12-mailbox-present-tearing 修正；该撕裂是 DX12+TAA 抖动的放大器，抖动真根因是 Function 层 jitter 重复施加，见 SDD-2026-07-07-taa-jitter-double-apply）。两后端创建 swapchain 时都必须打日志记录实际选中的 present mode。
- 工具链自包含：DXC 运行时固定取 `project/thirdparty/dxc/bin/x64/`（须支持 `-spirv` 的 dxcompiler.dll + dxil.dll），Vulkan validation layer 取 `project/thirdparty/VulkanSDK/redist/windows-x64/layers/`；构建与运行不依赖 `VULKAN_SDK` 环境变量或 PATH；仓库内 layer 不替代驱动侧 loader/ICD。
- UniformBuffer 分配 256 字节对齐；`create_uniform_buffer()` 带 initial_data 时必须先拷入同分配大小的 zero-padded 临时块再交后端，避免 `vkCmdUpdateBuffer` / DX12 upload 按分配大小读取越界。
- Root constants 约定：DX12 把 `AshRootConstants`/`RootConstants` cbuffer 作 root constants；Vulkan 编译前把该 block 重写为 `[[vk::push_constant]]` struct 并宏映射成员名（rewrite 需去掉预处理后成员的 const）。Vulkan reflection 同时为普通 uniform block（如 `AshMaterialParameters`）补齐 `ShaderParameterBlockLayout`。同一逻辑 block 双后端 byte_size 允许不同（DX12 保留尾部 padding），高层打包必须以反射 member offset/size 为准。DX12 root signature 把 CBV/SRV/UAV 合并一个 descriptor table、sampler 单独一个 table，`DX12ProgramBindingInfo::descriptorOffset` 记录 table 内偏移以规避 64 DWORD 限制。
- Debug 构建下 RHI 资源调试名必须下沉到 native GPU 对象：DX12 经 `dx12_set_debug_name()`→`ID3D12Object::SetName()`，Vulkan 经 `VulkanContext::set_resource_name()`；名字来自临时字符串时后端对象必须自持 `std::string` 并使用 owned `c_str()`，不缓存外部裸指针；两函数须容忍空 handle、空串与 validation 未启用。

## 验证

对齐 `docs/VERIFY.md`「RHI 接口 / 双后端实现」行：

- 双后端构建 + `RunRenderGate.bat`（双后端 golden SSIM 回归 + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard`（全矩阵）。
- Engine.ini 分别开启 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 readiness smoke（`run.bat sandbox <backend> Debug --smoke-test-seconds=120`），检查 `product/logs` 无 validation 报错。
- graph buffer / Function indirect 改动额外在双后端 Debug validation 下运行 `run.bat sandbox <backend> Debug --rhi-selftest-indirect --run-for-frames=1`，要求 raw 与 graph 两段 PASS、clean exit。Release 运行相同功能自测，但现有编译策略只允许 Debug validation，不得把 Release PASS 写成 validation 证据。
- 改 shader 编译/缓存时清空 `product/caches/ShaderCaches/` 后重跑冷启动验证。

## 历史

- [SDD-2026-07-09-indirect-draw-substrate GPU-driven 基建（indirect draw/dispatch）](../../sdd/SDD-2026-07-09-indirect-draw-substrate.md)：新增三个 indirect 命令接口与双后端实现（Vulkan 直通 / DX12 command signature + ExecuteIndirect），确立 args 布局权威、firstInstance==0、IndirectArgs barrier 三条跨后端契约；自测链路 `--rhi-selftest-indirect`。
- [SDD-2026-07-13-gpu-driven-foundation](../../sdd/SDD-2026-07-13-gpu-driven-foundation.md)：Function/RenderGraph 复用既有 RHI StorageBuffer 与 indirect API，未扩张 Graphics virtual surface。
- [SDD-2026-07-09-vulkan-optional-device-caps Vulkan 设备能力分级](../../sdd/SDD-2026-07-09-vulkan-optional-device-caps.md)：sparse binding 从 boot 硬断言降级为可选能力位（CI lavapipe 冒烟撞出）,确立必需/可选能力分级原则。
- [SDD-2026-07-07-render-gate 渲染验证安全网（RenderGate）](../../sdd/SDD-2026-07-07-render-gate.md)：新增 backbuffer 回读 RHI 接口与双后端实现。
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：新增双后端 acquire/present 三态结果、Vulkan no-image 同步闭环并传播到 readiness。
- [SDD-2026-07-13-gpu-performance-observability](../../sdd/SDD-2026-07-13-gpu-performance-observability.md)：新增默认关闭、仅由 PerfGate 启动配置 opt-in 的后端无关 GPU timing telemetry 契约。
- [SDD-2026-07-07-dx12-mailbox-present-tearing DX12 MAILBOX present 语义修正](../../sdd/SDD-2026-07-07-dx12-mailbox-present-tearing.md)：MAILBOX 去除误加的 `ALLOW_TEARING`。注：其"抖动根因是撕裂"的判断后被推翻，真根因见 [SDD-2026-07-07-taa-jitter-double-apply](../../sdd/SDD-2026-07-07-taa-jitter-double-apply.md)（Function 层 TAA jitter 重复施加）；present 语义修正本身仍然成立。
