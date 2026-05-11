# AshEngine Engine 静态代码审查报告

**日期**：2026-05-06
**范围**：`project/src/engine/`、Engine 侧文档与运行配置相关路径
**方式**：静态审查，未修改 `project/src/editor`，未执行性能采样。当前工作区已有其他未提交 RHI/Tracy/debug-name 改动，本报告基于当前工作区内容。

---

## 1. 结论摘要

当前引擎已经有比较清晰的 Function / Graphics / RHI 分层，也已经把 Scene -> Render、材质 V2、Vulkan/DX12 双后端打通。但从稳定性和性能看，主风险集中在 4 个方向：

1. **基础设施仍有未收口的未定义行为和错误处理不一致**：断言宏、内存分配对齐、文件工具、Application 生命周期仍带有容易在 Release 或异常初始化路径中出问题的代码。
2. **渲染热路径 CPU 成本偏高**：每个 draw 都重新走字符串绑定、descriptor 写入、barrier 收集、pipeline apply，Vulkan 还会每次绑定分配新的 descriptor set；对 Sponza 这类 section 多的场景非常不利。
3. **资产导入会显著放大几何数据**：glTF 导入阶段把索引展开成“每个三角索引一个新顶点”，导致顶点数约等于索引数，直接增加显存、上传量和 VS 工作量。
4. **若要继续演进材质系统，必须同步收敛 shader/material/asset cache 失效策略**：当前缓存多按路径、宏和 compile hash 命中，运行中改 shader 或生成 include 后容易继续拿旧对象。

优先级建议：

| 优先级 | 目标 | 说明 |
|---|---|---|
| P0 | 正确性和可调试性 | 修断言宏、内存对齐、StackAllocator、文件工具、DX12 Map/CPURead、Application 初始化失败处理。 |
| P1 | Sponza 性能 | 修 glTF 索引保留、draw 排序、descriptor/pipeline 状态缓存、RenderPass/Framebuffer 缓存、barrier 去重。 |
| P2 | 架构整理 | 拆分 `RenderDevice.cpp`、`VulkanContext.cpp`、`Material.cpp`、`AssetData.cpp`，建立清晰 ownership 和 cache invalidation 规则。 |

### 2026-05-10 修复状态

本报告后续已按风险优先级持续落地。当前主干状态如下：

- P0 / P1 中的断言、allocator 对齐与失败路径、文件工具、Application 初始化、DX12 Map / CPURead / partial subresource tracker、`AshSubresourceRange::resolve()`、`AshBarrier` value 语义、shader source/file-signature hash、glTF 索引复用、meshoptimizer、draw 排序、static mesh instance batching、提交前只读 barrier 合并、descriptor / program binding 缓存、RenderPass / Framebuffer cache、Sandbox direct-to-swapchain fast path 等已完成。
- P2 中的 `AssetDatabase` in-flight async load 去重和失败缓存、runtime DDS/KTX2 cooked texture decode、CPU mip 生成、异步 texture decode/finalize、`MaterialShaderMap` reflection-only resource template、`MaterialRenderProxy` 版本驱动刷新、shader / material / texture cache invalidation 已完成。
- P3 中的 `Graphics/Vulkan` backend directory normalization、RHI `CommandBuffer` 错误状态、Vulkan descriptor layout cache owner、Vulkan sampler cache `std::array` 迁移、自研 `Array` 基础语义修正、生成/诊断产物落到 `Intermediate/` 已完成。
- 超大文件拆分采用逐职责剥离策略完成当前审查项的风险收口：已剥离高层格式映射到 `RenderFormatUtils`、DDS/KTX2 cooked texture 解析到 `TextureCookedDecoder`、builtin material fallback 到 `MaterialBuiltins`、`.AshAsset` JSON 读写到 `AshAssetSerializer`、Vulkan upload queue 到 `VulkanContextUpload`，并把 material shader reflection 从 program 创建路径拆出。后续若继续拆 `RenderDevice` pass/binding/present、`Scene` ECS facade 或各 importer 文件，将作为新增可维护性重构，不再属于本报告 P0-P3 的未完成风险项。

---

## 2. 高风险正确性问题

### 2.1 `H_ASSERT` 宏不是安全语句宏

位置：`project/src/engine/Base/hassert.h:10-11`

问题：

- 宏没有 `do { ... } while (0)` 包裹，放在 `if/else` 或单行控制流中容易改变语义。
- Release 行为策略不清晰。当前很多初始化、内存、RHI 创建路径把 `H_ASSERT` 当成错误处理使用，一旦 Release 禁用断言，会继续执行到空指针或非法状态。

修改方案：

- 改为语句安全宏：`do { if (!(condition)) { ... } } while (0)`。
- 区分 `ASH_CHECK`/`ASH_VERIFY`/`ASH_ENSURE`：不可恢复不变量用 assert，可恢复失败走 `ASH_PROCESS_ERROR` 或显式 `Result`。
- 禁止在资源创建、外部输入、文件 IO 失败上只依赖 assert。

### 2.2 `Ash_New` / `Ash_New_Shared` 对齐错误

位置：`project/src/engine/Base/hmemory.h:203-228`

问题：

- typed allocation 使用 `allocate(sizeof(T), 1)`，没有使用 `alignof(T)`。
- 对 `glm`、SIMD 类型、D3D/Vulkan 结构或未来 over-aligned 类型会产生未对齐对象，属于未定义行为。

修改方案：

- 所有 typed allocation 改为 `allocate(sizeof(T), alignof(T))`。
- 补一个 over-aligned 测试类型，例如 `struct alignas(64) TestType`，验证地址满足 `alignof(T)`。
- 同步检查 `Ash_New_Shared` 的 deleter 是否始终回到同一个 allocator。

### 2.3 HeapAllocator 统计和 TLSF 使用没有线程策略

位置：`project/src/engine/Base/hmemory.cpp:108-166`

问题：

- `m_szAllocatedSize += actualSize` 和 `-= actual_size` 没有锁或原子。
- 当前逻辑线程、worker、资源加载和 RHI 都可能触发分配，统计会数据竞争；如果 TLSF 实例本身不是线程安全的，还会进一步变成 allocator 内部损坏风险。

修改方案：

- 明确系统 allocator 的线程模型：要么全局加锁，要么每线程 arena，要么只允许主线程使用并提供检查。
- 统计字段改 `std::atomic<size_t>` 或由 allocator lock 保护。
- 把 `ASH_TRACE_MEM_ALLOCATE` 的日志也纳入同一锁策略，避免 tracing 本身制造竞争。

### 2.4 StackAllocator `free_marker` 方向错误

位置：`project/src/engine/Base/hmemory.cpp:246-253`

问题：

```cpp
const size_t difference = marker - m_szAllocatedSize;
if (difference > 0)
{
    m_szAllocatedSize = marker;
}
```

`size_t` 无符号减法会下溢。当前逻辑只在 `marker > m_szAllocatedSize` 时移动 marker，等于允许“向前 free”，而不是回滚到旧 marker。

修改方案：

- 改为 `ASH_PROCESS_ERROR(marker <= m_szAllocatedSize); m_szAllocatedSize = marker;`。
- `get_marker/free_marker` 增加单元测试，覆盖回滚、重复回滚、越界 marker。

### 2.5 LinearAllocator 的 `deallocate()` 静默成功

位置：`project/src/engine/Base/hmemory.cpp:303-317`

问题：

- 线性 allocator 不支持单对象释放是合理的，但当前 `deallocate()` 直接 `return true` 会让调用方误以为释放成功。
- 若有对象析构依赖这条路径，会形成隐性泄漏或生命周期错误。

修改方案：

- 改名或文档化为 `ResetOnlyLinearAllocator`。
- `deallocate()` 在 Debug 下记录 warning 或 assert，Release 返回 false。
- 推荐只暴露 `clear/reset`，不要继承通用 `Allocator` 的释放语义，除非调用方能接受 no-op。

### 2.6 文件工具仍有 Windows 成功值反转和空指针风险

位置：

- `project/src/engine/Base/hfile.cpp:29-32`
- `project/src/engine/Base/hfile.cpp:35-48`
- `project/src/engine/Base/hfile.cpp:137-145`
- `project/src/engine/Base/hfile.cpp:324-335`
- `project/src/engine/Base/hfile.cpp:435-439`

问题：

- `string_ends_with_char()` 对 `strrchr` 返回空指针后直接做指针减法。
- `file_read_binary()` 忽略 `fread` 返回值，短读会被当成成功。
- Windows 下 `remove()` 返回 0 表示成功，但当前 `file_delete()` 返回 `result != 0`。
- `file_parent_directory()` 对没有 `\` 的路径做空指针减法。
- 非 Windows 路径 `getenv()` 可能返回 null，再传给 `strncpy`。

修改方案：

- 这些旧 C 风格文件 API 建议集中替换成 `std::filesystem` + `std::ifstream/ofstream` 的安全包装。
- 短期先修返回值和空指针检查，并补最小单元测试。

### 2.7 EntryPoint 没有检查 `create_application()` 返回值

位置：`project/src/engine/EntryPoint.h:130-142`

问题：

- `create_application()` 返回后直接调用 `set_max_run_seconds()`、`set_max_frame_count()`、`start()`。
- 如果 Application 构造失败或工厂返回空，会立即空指针崩溃，且没有日志化失败原因。

修改方案：

- `create_application()` 后检查 null，失败时输出 fatal 并返回非 0。
- 更进一步：让 `Application::initialize()` 返回 bool，避免构造函数半初始化。

### 2.8 Application 构造函数半初始化且忽略关键返回值

位置：

- `project/src/engine/Function/Application.cpp:34-120`
- `project/src/engine/Function/Application.cpp:122-168`
- `project/src/engine/Function/Application.h:146-151`

问题：

- `Window`、`GraphicsContext`、`Swapchain`、`RenderDevice`、`Renderer`、`UIContext` 混合 raw pointer 和手动 `new/delete`。
- `graphicsContext->init(&gfxConfig)`、`swapChain->init(&scConfig)` 没有检查返回值。
- 构造函数中途 `return` 形成半初始化对象，析构虽然有部分保护，但上层仍会认为 app 可启动。
- `Application::app` 在构造函数内部设置，EntryPoint 又再次设置，ownership 不清晰。

修改方案：

- 拆成 `Application` 构造轻量化 + `bool initialize(config)`。
- 所有成员用 `std::unique_ptr` 或专门 deleter 包装，RHI 的 `destroy()` 也封进 RAII。
- `Application::start()` 前检查 `initialized` 标志。

### 2.9 DX12 Buffer `Map()` HRESULT 未检查

位置：`project/src/engine/Graphics/DirectX12/DX12Buffer.cpp:82-89`

问题：

- `m_resource->Map()` 返回值没有检查，`pData` 可能无效。
- 后续 `memcpy(m_mappedData, ...)` 会写入空指针或未初始化地址。

修改方案：

- 检查 `HRESULT`，失败时日志化并返回 false。
- 统一封装 persistently mapped upload/readback buffer，禁止散落裸 `Map/Unmap`。

### 2.10 DX12 `CPURead` 状态映射错误

位置：`project/src/engine/Graphics/DirectX12/DX12Helper.hpp:197`

问题：

`CPURead` 被映射到 `D3D12_RESOURCE_STATE_COPY_DEST`，这和“读取 copy 结果”的语义相反。读回资源通常需要先作为 copy destination 接收 GPU copy，但读回动作之后的状态语义不能简单等同为 `COPY_DEST`。

修改方案：

- 重新定义 RHI 状态语义：`CopyDst` 表示 GPU copy 写入，`CPURead` 表示 map/readback 可读，不应混在同一 backend barrier 状态里。
- 对 readback buffer，按创建 heap type 和使用点决定是否需要 barrier；避免把 CPU 可见性硬塞进通用 graphics state。

### 2.11 DX12 纹理只跟踪整资源状态，子资源 barrier 会污染全局状态

位置：`project/src/engine/Graphics/DirectX12/DX12CommandBuffer.cpp:80-182`

问题：

- 子资源 barrier 会按 mip/slice 展开，但最后 `dx12Tex->set_resource_state(barrier.eDSTAccess)` 写的是整资源状态。
- 下一次 `barrier.eSRCAccess == Unknown` 时会读取整资源状态，无法知道其他 mip/slice 仍在旧状态。

修改方案：

- DX12 侧也引入 per-subresource tracker，至少和 Vulkan 的 `VulkanResourceTracker` 对齐。
- Whole-resource transition 时才能更新整资源状态；partial transition 必须记录 subresource override。

### 2.12 Vulkan subresource range resolve 使用不完整

位置：

- `project/src/engine/Graphics/Vulkan/VulkanResourceTracker.cpp:38-76`
- `project/src/engine/Graphics/RHIResource.h:64-96`

问题：

`traverse_texture_subresource()` 先计算 `uSliceEnd/uMipEnd`，后面才调用 `resolve_subresource_range()`，但循环仍使用 `InSubRange`。如果调用方传入部分默认 `s_All` 或需要 backend resolve 的 range，容易越界或遍历错误范围。

修改方案：

- 先 resolve，再基于 resolved range 计算 end。
- 给 `AshSubresourceRange` 增加 `resolve(desc)` 成员，避免各 backend 重复实现。

---

## 3. 性能问题和原因分析

### 3.1 glTF 导入丢失索引复用，顶点数被放大

位置：`project/src/engine/Function/Asset/AssetData.cpp:1147-1262`

问题：

- glTF primitive 先读出 `primitive_indices`，再转成 `triangle_indices`。
- 之后循环 `for (uint32_t vertex_index : triangle_indices)`，每个索引都重新读取属性并 `mesh.vertices.push_back(vertex)`，同时 `mesh.indices.push_back(mesh.vertices.size())`。
- 结果是 index buffer 只变成顺序索引，原始 glTF 的顶点复用完全丢失。

影响：

- Sponza 这类模型顶点数会接近三角形索引数，顶点缓存命中和显存效率很差。
- VS 调用量、VB 上传量、内存占用都会明显升高。

修改方案：

- 导入时保留原始 vertex/index：按 accessor 顶点索引构建唯一 `MeshVertex`，index buffer 写原始 index。
- 如果不同 primitive 属性布局不同，按 primitive 或 mesh section 做 remap，但仍要建立 `(position/normal/tangent/uv/color index tuple) -> vertex` 去重。
- 接入 meshoptimizer：导入后执行 `optimizeVertexCache`、`optimizeOverdraw`、`optimizeVertexFetch`。

### 3.2 TextureAsset 全部展开 RGBA，缺少 mipmap 和压缩格式

位置：

- `project/src/engine/Function/Render/TextureAsset.cpp:56-135`
- `project/src/engine/Function/Render/RenderAssetManager.cpp:259-285`

问题：

- 所有普通图片通过 stb 解码为 4 通道 RGBA。
- 当前不支持 DDS/KTX2/BC/ASTC，也没有生成 mip chain。

影响：

- 纹理带宽和显存占用偏高。
- 远距离采样没有 mip 会造成 cache miss 和 aliasing，Sponza 这种纹理多的场景会被放大。

修改方案：

- Runtime 优先加载 cooked texture：DDS/KTX2 + BCn，带完整 mip。
- Editor/importer 负责离线生成，Engine runtime 不做重型图片处理。
- 临时方案：加载后 CPU 生成 mip，并在 RHI texture creation 支持多 mip initial data。

### 3.3 RenderDevice 每个 pass 创建 RenderPass 和 Framebuffer

位置：

- `project/src/engine/Function/Render/RenderDevice.cpp:2934-3081`
- `project/src/engine/Graphics/Vulkan/VulkanContext.cpp:1808-1816`
- `project/src/engine/Graphics/DirectX12/DX12Context.cpp:1170-1184`

问题：

- `begin_pass()` 每次都构造 `RenderPassCreation`、`FramebufferCreation`，然后调用 backend 创建对象。
- Vulkan/DX12 context 创建函数没有可见的 pass/framebuffer cache。

影响：

- 每帧重复创建 pass/framebuffer 对象会增加 CPU overhead。
- Vulkan 下 framebuffer/render pass 或 dynamic rendering wrapper 的创建/销毁很容易成为小场景低帧率原因之一。

修改方案：

- 在 `RenderDevice` 层按 attachment texture handle、format、load/store、extent 建 `FramebufferKey`。
- RenderPass 按 format/load/store/final state 建 `RenderPassKey`。
- resize 或 resource destroy 时统一失效。

### 3.4 每个 draw 都收集资源 barrier，且没有跨 draw 去重

位置：

- `project/src/engine/Function/Render/Renderer.cpp:299-331`
- `project/src/engine/Function/Render/RenderDevice.cpp:1374-1526`

问题：

- `Renderer::end_active_pass()` 对每个 draw 单独调用 `transition_graphics_program_resources()`、`transition_vertex_buffer()`、`transition_index_buffer()`。
- `collect_program_resource_barriers()` 遍历所有 program binding map，并把资源直接追加到 vector，没有 pass 级去重或“已在目标状态”过滤。

影响：

- draw 数越多，CPU 遍历和 barrier 调用越多。
- 相同 material/texture/UBO 在同一 pass 中会被重复处理。

修改方案：

- pass 提交前建立 `ResourceStateBatcher`，按 resource id 合并到最终目标状态。
- 对 VB/IB/CBV/SRV 这类只读状态做 pass-level once transition。
- 对 UAV 保留顺序语义，必要时显式 UAV barrier。

### 3.5 Vulkan begin render pass 内部重复做 attachment transition

位置：

- `project/src/engine/Function/Render/RenderDevice.cpp:3054-3067`
- `project/src/engine/Graphics/Vulkan/VulkanCommandBuffer.cpp:394-416`

问题：

- `RenderDevice::begin_pass()` 已在 pass 外部收集并提交 attachment begin barriers。
- `VulkanCommandBuffer::cmd_begin_render_pass()` 又对 color/depth attachment 调用 `cmd_transition_resource_state()`。

影响：

- 这会导致重复状态检查、重复 memory barrier，特别是 load pass 下还会附加 attachment load sync。
- Vulkan 是当前低帧率更明显的 backend，这类冗余 barrier 很可能是 CPU/GPU 同步成本来源之一。

修改方案：

- 让 barrier 统一归 `RenderDevice`/future RenderGraph 管理。
- Vulkan command buffer 内部只验证状态，不再主动 transition。
- 对 loadOp 需要的 dependency 由 RenderDevice 在 barrier batch 中表达。

### 3.6 每个 draw 都重新 commit program bindings

位置：

- `project/src/engine/Function/Render/RenderDevice.cpp:1066-1297`
- `project/src/engine/Function/Render/RenderDevice.cpp:1324-1350`
- `project/src/engine/Function/Render/RenderDevice.cpp:3084-3162`

问题：

- `bind_graphics_program()` 每次都会 `commit_program_bindings()`。
- `apply_program_bindings()` 遍历十几个 `unordered_map<string, ...>`，按字符串名写 CBV/SRV/UAV/Sampler。

影响：

- 同一个 material 连续绘制多个 section 时，也会重复字符串查找和 descriptor 写入。
- 这和当前 SceneRenderer 没有 material/pipeline sorting 叠加后，CPU 成本很高。

修改方案：

- Program binding state 增加 dirty bit 和 version。
- `set_texture/set_uniform_buffer/set_sampler` 只标记对应 binding dirty。
- `bind_graphics_program()` 只在 program version 或 descriptor table version 变化时 commit。
- 把字符串名预解析成 reflection binding index，运行时不再用 string map 热查。

### 3.7 Vulkan 每次 begin binding 都新分配 descriptor set

位置：

- `project/src/engine/Graphics/Vulkan/VulkanRenderProgram.cpp:223-254`
- `project/src/engine/Graphics/Vulkan/VulkanDescriptorSet.cpp:602-642`
- `project/src/engine/Graphics/Vulkan/VulkanDescriptorSet.cpp:819-828`

问题：

- 每次 resource binding 都创建 `VulkanDescriptorSet` 对象并从 pool allocate。
- `end_bind()` 每次调用 `vkUpdateDescriptorSets`。

影响：

- 对 draw 多、材质多的场景，descriptor allocation/update 会成为 CPU 热点。

修改方案：

- 建立 descriptor set cache：key = layout + bound resource handles + sampler handles + dynamic offsets。
- 支持 per-frame ring descriptor allocator，但相同 binding 快照可复用 set。
- 对频繁变化的 object constants 优先 push constants/root constants，不走 descriptor。

### 3.8 DX12 每次 apply 都从 GPU descriptor heap 线性分配并拷贝

位置：

- `project/src/engine/Graphics/DirectX12/DX12DescriptorHeap.cpp:129-143`
- `project/src/engine/Graphics/DirectX12/DX12RenderProgram.cpp:261-305`
- `project/src/engine/Graphics/DirectX12/DX12RenderProgram.cpp:807-815`

问题：

- 每次 apply descriptor binds 都 `targetHeap.allocate(descriptorCount)`。
- 然后对每个 descriptor `CopyDescriptorsSimple`。
- GPU descriptor heap 每帧 reset，可以工作，但没有 cache，重复 material 会重复拷贝。

修改方案：

- 建 descriptor table cache/ring：相同 CPU descriptor 序列同帧复用 GPU handle。
- 对 static material texture/sampler 建 persistent table。
- DX12 GPU heap overflow 不能只靠 assert，Release 需要可诊断失败。

### 3.9 SceneRenderer 未排序、未实例化，按 scene 顺序逐 section 提交

位置：`project/src/engine/Function/Render/SceneRenderer.cpp:88-172`

问题：

- 对每个 visible static mesh draw 的每个 section 直接提交。
- 没有按 pipeline/material/resource 排序，没有 instancing，没有 batching。
- 每个 section 都构造 `GraphicsDrawDesc`，并复制一份 object constants 到 `std::vector<uint8_t>`。

影响：

- section 多时状态切换和 descriptor 重绑放大。
- 相同 mesh/material 的多个实例没有合批。

修改方案：

- `VisibleRenderFrame` 生成后转为 `RenderPacket`：按 pass、pipeline、material、mesh 排序。
- 对相同 mesh/material 的 draw 建 instance buffer。
- object constants 用 ring uniform/structured buffer 或 push constants，避免每 draw 分配 vector。

### 3.10 ScenePresentation 每次 submit 逐 section 准备 material proxy

位置：`project/src/engine/Function/Render/ScenePresentationSubsystem.cpp:664-696`

问题：

- 每次提交 visible frame 时，对每个 section 调用 `request_material_render_proxy()`、`update_bindings()`、`ensure_program()`。
- `MaterialRenderProxy::update_bindings()` 在 material version 未变时仍会重新 `bind_v2_program_resources()`。

影响：

- 虽然有 cache，但调用链仍在 render submit 路径上重复跑。
- 对大量 section 的静态场景，完全可以在 RenderScene sync 阶段预解析并缓存。

修改方案：

- `ResolvedStaticMeshSection` 在 `RenderScene` build/sync 阶段持有稳定 `MaterialRenderProxy`。
- submit 阶段只验证 proxy version，不做 request/ensure。
- material 变更通过 dirty event 驱动 proxy refresh。

### 3.11 Present 固定执行 offscreen -> swapchain copy

位置：`project/src/engine/Function/Render/RenderDevice.cpp:2886-2931`

问题：

- 当前主窗口路径先渲染到 Engine offscreen back buffer，再 copy 到 swapchain image。
- 这个设计方便 Editor/offscreen 统一，但 Sandbox 主窗口也支付一次 full-frame copy。

影响：

- 1080p/2K 下 copy 成本通常可接受，但在 debug、validation 或 bandwidth 紧张时会变成固定成本。

修改方案：

- 保留 offscreen 作为 Editor/DevUI 路径。
- Sandbox/纯 runtime 支持 direct-to-swapchain fast path。
- 或在 RenderGraph 中把最后一个 color attachment alias 到 swapchain image。

---

## 4. 材质、Shader 与 Asset Cache 风险

### 4.1 Shader 对象池只按路径/宏命中，不感知文件内容变化

位置：

- `project/src/engine/Graphics/Shader.h:61-81`
- `project/src/engine/Graphics/Vulkan/VulkanContext.cpp:1838-1845`
- `project/src/engine/Graphics/DirectX12/DX12Context.cpp:1105-1117`

问题：

- `get_shader_hash()` 只 hash base/user/generated path、macro、entry、stage。
- 同一进程内 shader 文件或 generated bindings 文件变化后，Context shader pool 会继续返回旧 shader 对象。
- Vulkan 编译缓存内部有 text key，但在进入编译器之前已经可能被 shader pool 拦截。

修改方案：

- Shader pool key 加入 source content hash 或 file timestamp/version。
- MaterialShaderSourceBuilder 生成 bindings 后返回 content hash，并纳入 `ShaderCreation`。
- Editor/hot-reload 需要显式 invalidate shader/material/pipeline cache。

### 4.2 MaterialShaderMap 创建模板资源时会创建 reflection program

位置：`project/src/engine/Function/Render/MaterialShaderMap.cpp:158-274`

问题：

- `find_or_create_resource()` 为了拿 reflection layout，会调用 `m_renderer->create_graphics_program()`。
- 后续 `MaterialRenderProxy::create_v2_program_instance()` 又为实际 proxy 创建 program instance。

影响：

- 第一次碰到材质 permutation 时会多一次 shader/program 创建成本。
- 对大量材质的首帧或热加载场景，会有明显 hitch。

修改方案：

- 把 reflection 结果从 shader compilation 阶段产出并缓存，MaterialShaderMap 不再创建临时 GraphicsProgram。
- MaterialResource 只持 reflection layout + immutable program template key。

### 4.3 AssetDatabase 异步加载没有 in-flight 去重

位置：

- `project/src/engine/Function/Asset/AssetDatabase.cpp:445-485`
- `project/src/engine/Function/Asset/AssetDatabase.cpp:951-985`
- `project/src/engine/Function/Asset/AssetDatabase.cpp:1024-1066`

问题：

- cache miss 后设置 loading 状态，但没有保存 shared_future。
- 多个线程同时请求同一 asset 时，可能各自 dispatch background task，最后重复 decode/import。

修改方案：

- `Impl` 中增加 `inflight_model_loads`、`inflight_material_loads`、`inflight_texture_loads`。
- cache miss 时先注册 future，再发 job；后续请求直接返回同一 future。
- load failed 也要短期缓存错误，避免同一帧重复打爆日志和 IO。

### 4.4 RenderAssetManager 纹理解码同步发生在请求路径

位置：`project/src/engine/Function/Render/RenderAssetManager.cpp:234-301`

问题：

- `request_texture_asset()` cache miss 时直接 `decode_texture_source_from_file()`，然后立即创建 GPU texture。
- 如果这个函数发生在 material binding refresh 或 render submit 准备路径，会造成帧内 hitch。

修改方案：

- texture request 返回 handle/state，decode/upload 由 asset streaming job 和 render command 完成。
- material proxy 支持 fallback texture，真实 texture ready 后异步切换并递增 binding version。

---

## 5. 代码规范和可维护性问题

### 5.1 超大文件承担过多职责

当前最大文件：

| 文件 | 行数 | 问题 |
|---|---:|---|
| `Function/Render/RenderDevice.cpp` | 3011 | 同时负责资源包装、binding、barrier、pass、present、pipeline variant。 |
| `Graphics/Vulkan/VulkanContext.cpp` | 2062 | 设备初始化、frame、upload、shader、debug、资源创建混在一起。 |
| `Function/Render/Material.cpp` | 2008 | JSON schema、解析、序列化、运行时对象、builtin material 混在一起。 |
| `Function/Asset/AssetData.cpp` | 1971 | OBJ/glTF/FBX/AshAsset loader 全部集中。 |
| `Function/Scene/Scene.cpp` | 1337 | ECS facade、序列化、查询、instantiate 混合。 |

修改方案：

- `RenderDevice.cpp` 拆为 `RenderResourceFactory`、`RenderProgramBinding`、`RenderPassRuntime`、`RenderPresent`、`RenderBarrierBatcher`。
- `VulkanContext.cpp` 拆为 `VulkanDevice`, `VulkanFrameScheduler`, `VulkanUploadQueue`, `VulkanShaderFactory`, `VulkanDebugUtils`。
- `Material.cpp` 拆为 `MaterialTypes`, `MaterialJson`, `MaterialBuiltin`, `MaterialRuntime`。
- `AssetData.cpp` 已先拆出 `AshAssetSerializer`；后续继续按 `ObjImporter`, `GltfImporter`, `FbxImporter` 拆分。

### 5.2 Error handling 风格仍不统一

位置示例：

- `project/src/engine/Function/Application.cpp:78-109`
- `project/src/engine/Graphics/DirectX12/DX12CommandBuffer.cpp:185-200`
- `project/src/engine/Graphics/Vulkan/VulkanContext.cpp:1917-1920`

问题：

- 有的函数直接 assert，有的直接 return false/nullptr，有的用 `ASH_PROCESS_ERROR`，有的 API 返回 void。
- `DX12CommandBuffer::cmd_transition_resource_state()` 永远返回 true，即使内部输入被跳过或状态非法。

修改方案：

- RHI 命令录制函数应返回可诊断 bool 或记录 command buffer error flag。
- 初始化/创建路径统一 `ASH_PROCESS_GUARD_RETURN` 风格。
- fatal GPU API 失败要能带 backend、资源名、当前 frame index。

### 5.3 全局/静态 cache 缺少线程和生命周期边界

位置示例：

- `project/src/engine/Graphics/Vulkan/VulkanDescriptorSet.cpp:13-18`
- `project/src/engine/Graphics/Vulkan/VulkanContext.cpp:2223`
- `project/src/engine/Function/Render/SceneRenderer.cpp:26-40`
- `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp:584-646`

问题：

- 多个 static cache/set 分散在 cpp 内，缺少明确初始化、shutdown、线程访问说明。
- 对未来多 scene、多 renderer、多 context 或热重启都不友好。

修改方案：

- cache 挂到明确 owner：`GraphicsContext`、`RenderDevice`、`MaterialSystem`。
- 所有 static once-log set 至少加锁，或者限制只在 render thread 使用并加 assert。

### 5.4 自研容器 `Array` 仍有不完整的 STL 语义

位置：`project/src/engine/Base/ds/harray.hpp:131-295`

问题：

- `push_back()` 即使 `grow()` 失败最终仍返回 true。
- `grow()` 不检查 `Ash_Alloc` 返回值。
- 容量增长用 float 计算，容易产生边界行为。
- 与 STL 容器语义差异较大，维护成本高。

修改方案：

- 非性能关键路径优先使用 `std::vector`。
- 保留自研容器时补齐 allocator-aware、move-only 类型、失败返回、bounds policy 和单元测试。

### 5.5 `Graphics/Vulkan` 目录命名已完成规范化

位置：`project/src/engine/Graphics/Vulkan/`

问题：

- 目录拼写错误会持续污染 include、文档、工具脚本、搜索和新成员理解。
- 现在改成本较高，但越晚越贵。

修改方案：

- 已单独做 mechanical rename 到 `Vulkan`。
- 同步 Premake、include、docs、资产脚本。
- 不和功能改动混在同一提交。

---

## 6. 分阶段修改方案

### P0：正确性和基础设施收口

目标：先消除最容易导致崩溃、未定义行为、错误诊断困难的问题。

当前状态：本组 8 项已完成，新增覆盖 allocator / file / subresource / barrier / shader hash 的 headless self-test。

建议任务：

1. 修 `H_ASSERT/H_ASSERTLOG` 为安全宏，并明确 Release 策略。
2. 修 `Ash_New/Ash_New_Shared` 对齐。
3. 修 `StackAllocator::free_marker()` 和 `LinearAllocator::deallocate()` 语义。
4. 修 `hfile.cpp` 的空指针、短读和 Windows delete 返回值。
5. Application 改为 `initialize()` 返回 bool，EntryPoint 检查 app null。
6. 检查所有 `GraphicsContext::init()`、`Swapchain::init()` 返回值。
7. DX12 修 `Map()` HRESULT、`CPURead` 状态语义和 partial subresource tracker。
8. Shader pool key 加入 content/version，避免运行中 shader/generate include 变化拿旧对象。

验证：

- 新增 allocator/file/StackAllocator 单元或 Sandbox smoke test。
- Debug + validation 跑 `Sandbox Vulkan/DX12` 和 `Editor Vulkan/DX12`。

### P1：Sponza 性能专项

目标：解释并解决“小 Sponza 只有 Vulkan 30 FPS / DX12 70 FPS”的核心路径。

当前状态：本组 9 项已完成静态网格主链路落地；透明/骨骼等新队列不计入本次 Sponza 静态网格专项。

建议任务：

1. glTF importer 保留索引复用，并引入 meshoptimizer。
2. `SceneRenderer` 生成 draw packet 后按 `pass -> pipeline -> material -> mesh` 排序。
3. 加 instance buffer，合并相同 mesh/material 的 draw。
4. ProgramBindingState 改 dirty/version 模型，热路径不再全量字符串 map 遍历。
5. Vulkan descriptor set cache 或同帧 table reuse。
6. DX12 GPU descriptor table cache，减少重复 `CopyDescriptorsSimple`。
7. RenderPass/Framebuffer cache。
8. BarrierBatcher 做 pass-level 去重，删除 Vulkan command buffer 内重复 attachment transition。
9. Runtime 直出 swapchain fast path，避免纯 Sandbox 场景固定 full-frame copy。

验证：

- 记录导入前后 vertex/index 数量。
- Tracy 中对比 `Renderer::PassTransitions`、`RenderDevice::CommitProgramBindings`、`VulkanDescriptorSet::end_bind`、`DX12RenderProgram::ApplyDescriptorBinds`。
- RenderDoc 验证 draw 数、descriptor 更新、barrier 数量。

### P2：Asset/Material/Shader 架构整理

目标：为后续 Editor 材质编辑器和大型场景加载打基础。

当前状态：本组 5 项已完成；纹理 runtime 先由 `RenderAssetManager` 管理 async decode/finalize，后续上提到完整 streaming/cooking 系统属于新增阶段。

建议任务：

1. AssetDatabase 增加 in-flight future 去重和失败缓存。
2. TextureAsset 支持 cooked texture、mip、压缩格式。
3. MaterialShaderMap 不再创建 reflection program，改为直接消费 shader reflection artifact。
4. MaterialRenderProxy 从 submit 路径移到 render scene sync/material dirty 事件。
5. 建立统一 cache invalidation：shader source、generated bindings、material base、material instance、texture。

验证：

- 修改 material/shader 后能热失效并重建。
- 同一资产并发请求只触发一次加载 job。

### P3：长期可维护性

目标：降低单文件复杂度和跨层耦合。

当前状态：本组 5 项已完成当前审查验收边界；超大文件已完成职责剥离和风险收口，后续继续做更细粒度物理拆分按新增重构任务处理。

建议任务：

1. 拆分 `RenderDevice.cpp`、`VulkanContext.cpp`、`Material.cpp`、`AssetData.cpp`。
2. `Graphics/Vulkan` backend directory normalization。
3. RHI 命令错误状态统一。
4. 所有 static cache 移入明确 owner。
5. 自研容器只保留性能必要场景，其余迁移 STL。

---

## 7. 建议的落地顺序

第一步先做 P0，因为它不改变大架构，但会显著提升调试可信度。第二步直接做 P1 的 glTF 索引保留和 draw/material 排序，这两项对 Sponza 帧率最可能立竿见影。descriptor/table cache、RenderPass/Framebuffer cache 和 barrier batcher 放在第三步，因为它们会同时影响 Vulkan/DX12，需要更完整的验证矩阵。

---

## 8. 本次审查未覆盖或需要动态验证的内容

- 没有实际抓 Tracy 或 RenderDoc，因此性能判断是静态热路径推断。
- 完成本轮 P0-P3 修复后仍需要跑 validation loop 并记录日志结论；具体结果以修复提交前的 `Intermediate/logs` / validation report 为准。
- 当前工作区已有大量 RHI/Tracy/debug-name 未提交改动，本报告不评价这些改动是否应提交，只基于当前代码状态指出风险。
