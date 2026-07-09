---
owner: huyizhou
last_reviewed: 2026-07-04
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
| `GpuProfilerRHI.h/.cpp` | GPU 计时抽象 |
| `Vulkan/` | `VulkanContext/VulkanSwapchain/Vulkan*` 全套实现 + `VulkanShaderCompiler`（HLSL→SPIR-V） |
| `DirectX12/` | `DX12Context/DX12Swapchain/DX12*` 全套实现 + `DX12ShaderCompiler`（HLSL→DXIL） |
| `Shaders/AshVertexDeclLocations.hlsli` | 顶点声明 location 约定 |

## 公共接口

- 后端选择：`RHI::load_runtime_rhi_config(configPath)` 读 Engine.ini —— `[RHI] Backend`（内部 `parse_backend_name` 接受 `vulkan/vk/directx12/dx12/d3d12`，大小写不敏感，非法值回退编译期默认并告警）、`[VulkanValidation]`（Enabled/GpuAssisted/SynchronizationValidation/BreakOnValidationError）、`[DX12Validation]`（Enabled/GpuValidation，非 Debug 构建强制关闭）。`resolve_runtime_backend` 处理未编译后端的回退；Windows 默认后端为 DX12。
- 工厂：`GraphicsContext::create(Backend)`、`Swapchain::create(Backend)` 按解析后端实例化对应后端对象。
- 设备接口（`GraphicsContext`）：`init/shutdown/destroy`、`create_shader/buffer/buffer_view/texture/texture_view/render_pass/framebuffer/graphics_render_program/compute_render_program/sampler`、`get_sampler(AshSamplerState)`、`begin_frame/end_frame`、`get_command_buffer(threadIdx)`、`wait_idle`、`get_render_memory_stats`。资源以 `std::shared_ptr` 交付。
- 命令接口（`CommandBuffer`）：barrier（`cmd_transition_resource_state`）、render pass（`cmd_begin/end_render_pass`，debug_scope_name 用于 RenderDoc/PIX 标签）、绑定/draw/dispatch/copy/update 全家族；错误经 `has_error()/get_last_error()` 暴露。
- Readback（SDD-2026-07-07-render-gate 新增，验证/调试用途，非热路径）：`CommandBuffer::cmd_copy_texture_to_buffer(source, destination, buffer_offset, row_pitch_bytes)` 把 mip0/layer0 整层拷入 CPU 可读 buffer，仅支持 4 字节颜色格式（RGBA8/BGRA8），row pitch 须 ≥ width*4 且为 256 的倍数（D3D12 约束）；另有区域版 `cmd_copy_texture_region_to_buffer`。上层 backbuffer 回读封装在 `Function/Render` 的 RenderDevice（`request_back_buffer_capture/fetch_back_buffer_capture`）。
- Indirect draw/dispatch（SDD-2026-07-09-indirect-draw-substrate 新增）：`CommandBuffer::cmd_draw_indirect(buffer, offset, drawCount, stride)`、`cmd_draw_indexed_indirect(buffer, offset, drawCount, stride)`、`cmd_dispatch_indirect(buffer, offset)`；args 结构体 `AshDrawIndirectArgs`/`AshDrawIndexedIndirectArgs`/`AshDispatchIndirectArgs` 定义在 `RHICommon.h`。Vulkan 直通 `vkCmd*Indirect`；DX12 经三种固定 argument-only command signature（设备级懒创建缓存于 `DX12Context`）+ `ExecuteIndirect`。自测链路 `--rhi-selftest-indirect`（`Function/Render/RHIIndirectSelfTest.cpp`）覆盖 compute 写 args → barrier → 三个 indirect API → readback 校验，日志标记 `[RHISelfTest] ... PASS/FAIL`。
- Swapchain：`present/resize_swapchain/get_swapchain_buffer/get_format/begin_frame/end_frame`。
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
- 首帧前销毁 RHI 资源必须置 `immediate_deletion = true`：Vulkan 的 per-frame 延迟删除队列在首个 `begin_frame` 前无效（`currentFrame` 初始为 `UINT32_MAX`，`delayed_deletion_queues[currentFrame]` 越界），预循环路径（如 RHI 自测）默认延迟删除会崩溃（RHIIndirectSelfTest 实证）。
- Present mode 语义双后端必须一致（`AshPresentMode` 为准）：FIFO=垂直同步（DX12 sync_interval=1）；MAILBOX=不等垂直同步且**绝不撕裂**（DX12 flip model sync=0 **不带** `DXGI_PRESENT_ALLOW_TEARING`，DXGI 以最新完整帧替换排队帧；Vulkan `VK_PRESENT_MODE_MAILBOX_KHR`）；IMMEDIATE=允许撕裂（DX12 sync=0 + `ALLOW_TEARING`）。MAILBOX 曾误带 tearing 标志（SDD-2026-07-07-dx12-mailbox-present-tearing 修正；该撕裂是 DX12+TAA 抖动的放大器，抖动真根因是 Function 层 jitter 重复施加，见 SDD-2026-07-07-taa-jitter-double-apply）。两后端创建 swapchain 时都必须打日志记录实际选中的 present mode。
- 工具链自包含：DXC 运行时固定取 `project/thirdparty/dxc/bin/x64/`（须支持 `-spirv` 的 dxcompiler.dll + dxil.dll），Vulkan validation layer 取 `project/thirdparty/VulkanSDK/redist/windows-x64/layers/`；构建与运行不依赖 `VULKAN_SDK` 环境变量或 PATH；仓库内 layer 不替代驱动侧 loader/ICD。
- UniformBuffer 分配 256 字节对齐；`create_uniform_buffer()` 带 initial_data 时必须先拷入同分配大小的 zero-padded 临时块再交后端，避免 `vkCmdUpdateBuffer` / DX12 upload 按分配大小读取越界。
- Root constants 约定：DX12 把 `AshRootConstants`/`RootConstants` cbuffer 作 root constants；Vulkan 编译前把该 block 重写为 `[[vk::push_constant]]` struct 并宏映射成员名（rewrite 需去掉预处理后成员的 const）。Vulkan reflection 同时为普通 uniform block（如 `AshMaterialParameters`）补齐 `ShaderParameterBlockLayout`。同一逻辑 block 双后端 byte_size 允许不同（DX12 保留尾部 padding），高层打包必须以反射 member offset/size 为准。DX12 root signature 把 CBV/SRV/UAV 合并一个 descriptor table、sampler 单独一个 table，`DX12ProgramBindingInfo::descriptorOffset` 记录 table 内偏移以规避 64 DWORD 限制。
- Debug 构建下 RHI 资源调试名必须下沉到 native GPU 对象：DX12 经 `dx12_set_debug_name()`→`ID3D12Object::SetName()`，Vulkan 经 `VulkanContext::set_resource_name()`；名字来自临时字符串时后端对象必须自持 `std::string` 并使用 owned `c_str()`，不缓存外部裸指针；两函数须容忍空 handle、空串与 validation 未启用。

## 验证

对齐 `docs/VERIFY.md`「RHI 接口 / 双后端实现」行：

- 双后端构建 + `RunRenderGate.bat`（双后端 golden SSIM 回归 + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard`（全矩阵）。
- Engine.ini 分别开启 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 smoke（`run.bat sandbox <backend> Debug --smoke-test-seconds=5`），检查 `product/logs` 无 validation 报错。
- 改 shader 编译/缓存时清空 `product/caches/ShaderCaches/` 后重跑冷启动验证。

## 历史

- [SDD-2026-07-09-indirect-draw-substrate GPU-driven 基建（indirect draw/dispatch）](../../sdd/SDD-2026-07-09-indirect-draw-substrate.md)：新增三个 indirect 命令接口与双后端实现（Vulkan 直通 / DX12 command signature + ExecuteIndirect），确立 args 布局权威、firstInstance==0、IndirectArgs barrier 三条跨后端契约；自测链路 `--rhi-selftest-indirect`。
- [SDD-2026-07-09-vulkan-optional-device-caps Vulkan 设备能力分级](../../sdd/SDD-2026-07-09-vulkan-optional-device-caps.md)：sparse binding 从 boot 硬断言降级为可选能力位（CI lavapipe 冒烟撞出）,确立必需/可选能力分级原则。
- [SDD-2026-07-07-render-gate 渲染验证安全网（RenderGate）](../../sdd/SDD-2026-07-07-render-gate.md)：新增 backbuffer 回读 RHI 接口与双后端实现。
- [SDD-2026-07-07-dx12-mailbox-present-tearing DX12 MAILBOX present 语义修正](../../sdd/SDD-2026-07-07-dx12-mailbox-present-tearing.md)：MAILBOX 去除误加的 `ALLOW_TEARING`。注：其"抖动根因是撕裂"的判断后被推翻，真根因见 [SDD-2026-07-07-taa-jitter-double-apply](../../sdd/SDD-2026-07-07-taa-jitter-double-apply.md)（Function 层 TAA jitter 重复施加）；present 语义修正本身仍然成立。
