# AshEngine Engine 开发指南

> 面向 Engine / Runtime / RHI / 渲染基础设施开发者。本文档描述当前主干的真实架构、边界、配置、调试方式与维护约定。

---

## 0. 文档定位与维护约定

### 本文档的定位

`EngineDeveloperGuide.md` 是 **Engine 侧长期维护主文档**。今后只要发生以下类型的改动，都应同步更新本文档：

- Engine 模块分层与依赖边界变化
- Application 生命周期变化
- DynamicRHI / backend 选择逻辑变化
- Renderer / RenderDevice / 资源模型 / 渲染流程变化
- Shader 编译、缓存、反射、绑定规则变化
- 运行目录、配置项、日志路径、缓存路径变化
- Engine 暴露给 Editor / Game / Client 的公共能力变化

### 与其他文档的关系

- Editor 侧长期维护主文档：`docs/EditorDeveloperGuide.md`
- Engine UI 专题文档：`docs/EngineUIContext.md`
- Editor UI 分层建议：`docs/EditorUIFacadeProposal.md`

### 维护规则

- Engine 相关开发完成后：
  - 更新本文档
  - 若改动属于某个专题，再同步更新对应专题文档
- Editor 相关开发完成后：
  - 更新 `EditorDeveloperGuide.md`
  - 若改动涉及 Engine 暴露给 Editor 的边界，同时更新本文档

---

## 1. 仓库结构总览

```text
AshEngine/HASHEAEngine/
├── premake5.lua                     # 顶层 Premake workspace
├── docs/                            # 长期维护文档
├── assets/                          # 引擎/示例资源
├── product/                         # 运行期配置、日志、缓存、最终可执行目录
├── _BUILD/                          # VS/MSBuild 中间产物与 target 输出
└── project/
    ├── src/
    │   ├── engine/
    │   │   ├── Base/                # 日志、内存、断言、窗口、输入、序列化、时间等基础设施
    │   │   ├── Function/            # Engine-facing 高层能力
    │   │   │   ├── Application.*    # 应用生命周期与主循环
    │   │   │   ├── Render/          # RenderDevice / Renderer / 高层资源封装
    │   │   │   ├── Gui/             # ImGuiLayer + UIContext
    │   │   │   ├── Asset/           # AssetDatabase
    │   │   │   └── Scene/           # Scene / Entity / 组件描述
    │   │   └── Graphics/            # RHI 抽象与 Vulkan / DX12 后端
    │   └── editor/                  # Editor 可执行项目
    └── thirdparty/                  # 第三方库
```

---

## 2. 分层与依赖边界

### 2.1 Base 层

职责：

- 日志、断言、内存服务
- 窗口与输入
- 时间、字符串、文件、序列化、服务管理

原则：

- 不承载渲染后端语义
- 尽量被 Function / Graphics 复用

### 2.2 Graphics 层

职责：

- RHI 抽象
- Vulkan / DX12 后端实现
- Shader 编译、反射、缓存、管线对象、命令缓冲、资源状态转换

原则：

- 这是 Engine 内部底层，不应直接暴露给 Editor / Game / Client
- 新功能如果会影响公共上层，应先看是否需要由 Function 层重新封装
- 若改动共享抽象，默认需要同时验证 Vulkan 与 DX12

### 2.3 Function 层

职责：

- 把 Graphics 层能力整理成上层可用的 Engine-facing API
- 隔离具体后端对象与 RHI 细节
- 维护 `Renderer`、`RenderDevice`、`UIContext`、`Scene`、`AssetDatabase` 等高层能力

原则：

- Engine 对外的长期公共边界优先放在这里
- 不把 Vulkan / DX12 具体类型泄露给上层

### 2.4 Editor / Game / Client 与 Engine 的关系

- Editor 不应直接依赖 `Graphics/`
- Game / Client 以后也应优先依赖 Function 层暴露的能力
- 如果某个接口明显只描述 Editor 工作区语义，不应直接塞进 Engine 通用 facade

目前 UI 已按这个规则收口：

- `ImGuiLayer` 保持 Engine 内部实现
- `UIContext` 仅保留通用 DevUI 能力
- Editor-specific workspace / panel / property-grid 语义应放在更高一层

---

## 3. 构建系统与输出目录

### 3.1 构建系统

项目使用 **Premake5 + Visual Studio/MSBuild**，不是 CMake。

关键 Premake 文件：

- 顶层 workspace：`premake5.lua`
- Engine 项目：`project/src/engine/premake5.lua`
- Editor 项目：`project/src/editor/premake5.lua`

### 3.2 当前主要输出目录

- 进程启动时，`EntryPoint.h` 会先把当前工作目录切回仓库根目录
- 中间文件 / target 输出：`_BUILD/<Config>-windows-x86_64/...`
- 运行目录：`product/bin64/<Config>-windows-x86_64/`
- 运行配置：`product/config/Engine.ini`
- 日志目录：`product/logs/`
- Shader Cache：`product/caches/ShaderCaches/`
- Pipeline Cache：`product/caches/PipelineCaches/`

### 3.3 当前编译能力

Windows Debug / Release 下，Engine 同时编入：

- `ASH_HAS_VULKAN`
- `ASH_HAS_DX12`
- `ASH_HAS_DXC`

非 Windows 构建会在 Premake 中移除：

- `Graphics/DirectX12/**`
- `Graphics/DXC/**`
- `imgui_impl_dx12.*`

这意味着 **DynamicRHI 是运行时切换**，但前提仍然是目标平台已编入对应后端。

---

## 4. Application 生命周期

### 4.1 入口与初始化

`Application` 是 Runtime 主入口，核心配置在 `EngineInitConfig`：

- 初始窗口大小
- swapchain buffer 数
- 标题
- vsync
- 期望 backend
- backend 配置文件路径

当前初始化顺序：

1. 初始化日志与内存服务
2. 读取 `Engine.ini`，解析 Runtime RHI 配置
3. 解析最终 backend
4. 创建窗口
5. 创建 `GraphicsContext`
6. 创建 `Swapchain`
7. 创建 `RenderDevice`
8. 创建 `Renderer`
9. 创建并初始化 `UIContext`

补充说明：

- `EntryPoint.h` 会先搜索仓库根目录，并把进程工作目录切到该根目录
- 因此默认的 `product/...` 相对路径会稳定落在仓库内的 `product` 树下
- `EntryPoint.h` 还支持 smoke-run 自动退出：
  - `ASH_ENGINE_SMOKE_TEST_FRAMES`
  - `ASH_ENGINE_SMOKE_TEST_SECONDS`
  - `--smoke-test=<frames>`
  - `--smoke-test-seconds=<seconds>`

### 4.2 主循环

`Application::start()` 的核心流程为：

1. `_pump_platform_events()`
2. `_tick_frame()`
3. `_render_frame()`
4. `_present_frame()`

补充说明：

- `UIContext::begin_frame()` 在事件泵阶段启动
- UI 绘制最终在 `Renderer::end_frame()` 内作为末尾 overlay 提交
- 窗口最小化时会跳过实际渲染

### 4.3 可重写钩子

当前主要虚函数：

- `_on_startup()`
- `_on_shutdown()`
- `_on_update()`
- `_on_gui()`
- `_on_render_debug()`
- `_on_render()`
- `_present()`

Engine 侧改动这些钩子的调用时机、职责或约束时，必须回写本文档与 Editor 文档。

---

## 5. DynamicRHI 与运行时后端切换

### 5.1 目标

当前引擎不再依赖 Premake 宏硬切换单一运行时后端，而是：

- 编译期决定“某后端是否被编入”
- 运行期通过配置决定“本次启动使用哪个后端”

### 5.2 关键入口

相关实现位于：

- `project/src/engine/Graphics/DynamicRHI.h`
- `project/src/engine/Graphics/DynamicRHI.cpp`

核心能力：

- `is_backend_compiled()`
- `get_default_backend()`
- `resolve_runtime_backend()`
- `load_runtime_rhi_config()`
- `GraphicsContext::create()`
- `Swapchain::create()`

### 5.3 当前默认策略

- Windows：优先 DX12，其次 Vulkan
- 非 Windows：优先 Vulkan，其次 DX12

如果配置请求了一个当前构建未编入的 backend，会：

- 输出 warning
- 回退到可用默认 backend

### 5.4 当前配置文件

当前运行配置文件为：

```ini
[RHI]
Backend=DX12

[VulkanValidation]
Enabled=true
GpuAssisted=false
SynchronizationValidation=true
BreakOnValidationError=false

[DX12Validation]
Enabled=true
GpuValidation=true
```

当前有效配置项：

- `RHI.Backend`
- `VulkanValidation.Enabled`
- `VulkanValidation.GpuAssisted`
- `VulkanValidation.SynchronizationValidation`
- `VulkanValidation.BreakOnValidationError`
- `DX12Validation.Enabled`
- `DX12Validation.GpuValidation`

实现注意事项：

- `DynamicRHI.cpp` 的 INI 解析已兼容 UTF-8 BOM；不要假设配置文件一定是无 BOM 文本。
- 自动化验证脚本在切换 backend 时会重写 `Engine.ini`，并且会回读运行日志确认“请求 backend”和“实际启动 backend”一致。

以后新增或删除配置项，必须同步更新本文档。

---

## 6. 渲染栈总览

当前渲染栈分为四层：

1. `GraphicsContext`
2. `Swapchain`
3. `RenderDevice`
4. `Renderer`

### 6.1 GraphicsContext / Swapchain

职责：

- 设备、队列、命令池、交换链等底层后端对象生命周期
- Validation 层初始化
- 后端级资源创建入口

这是 RHI 层，不应直接暴露给 Editor / Game / Client。

### 6.2 RenderDevice

`RenderDevice` 是 Function 层里最靠近 RHI 的一层封装，职责包括：

- 创建高层资源包装对象
- 管理 pass begin / end
- 绑定 graphics / compute program
- 绑定 vertex / index buffer
- 设置 viewport / scissor
- 提交 draw / draw indexed / dispatch
- 维护 back buffer 与 transient render target 池
- 管理高层资源到 RHI 资源/视图的映射
- 在合适时机执行资源状态转换

### 6.3 Renderer

`Renderer` 是更高一层的帧 orchestration facade，职责包括：

- `begin_frame() / end_frame() / present()`
- 高层资源创建便捷入口
- `begin_pass()` + `GraphicsPassContext`
- 收集 draw call 并在 pass 结束时统一提交
- 统计 frame stats
- 在 `end_frame()` 时渲染 UI

### 6.4 当前 pass / dispatch 规则

当前规则非常重要：

- `dispatch()` 必须发生在 **没有 active graphics pass** 的时候
- `begin_pass()` 后的 draw 会先记录到 `GraphicsPassContext`
- 真正的资源 transition 会在 pass 提交前统一完成
- 然后才真正调用底层 `begin_pass()`

这样做的原因是：

- Vulkan 不允许把某些 `vkCmdPipelineBarrier` 调用塞进 render pass / dynamic rendering 活跃区间
- 因此共享高层渲染路径必须保证 barrier 在 pass 外部完成

如果以后改动这条规则，必须同时验证 Vulkan 与 DX12。

---

## 7. Function 层高层渲染资源模型

### 7.1 当前公开包装对象

当前高层资源包括：

- `RenderTarget`
- `UniformBuffer`
- `VertexBuffer`
- `IndexBuffer`
- `StorageBuffer`
- `GraphicsProgram`
- `ComputeProgram`

这些类型：

- 对上隐藏具体后端对象
- 内部通过 `Impl` 持有 RHI 资源
- 由 `Renderer` / `RenderDevice` 创建

### 7.2 RenderTarget

当前支持的典型用途：

- back buffer 包装
- color render target
- depth-stencil target
- shader resource
- unordered access target

### 7.3 GraphicsProgram / ComputeProgram

当前 `GraphicsProgram` / `ComputeProgram` 同时承担：

- pipeline state 载体
- shader entry / macro / state 描述
- 资源绑定入口
- root constants / constant block 设置
- 按名称绑定 buffer / texture / sampler

这符合当前引擎把 “Program 作为高层 pipeline + binding facade” 的设计。

### 7.4 资源绑定接口

当前支持的典型绑定能力：

- constant data block
- static int / uint / float
- uniform buffer
- storage buffer / rw storage buffer
- texture / rw texture
- 各类数组绑定
- sampler / sampler array

高层通过字符串名绑定，底层结合 shader reflection 消费这些绑定。

---

## 8. Shader 编译、反射与缓存

### 8.1 当前着色器主路径

当前主路径是：

- 上层使用 HLSL
- 通过 DXC 编译
- Vulkan 路径消费 SPIR-V / SPIR-V 反射
- DX12 路径消费 DXIL + DXC / D3D12 reflection

### 8.2 反射驱动的内容

当前反射会参与：

- descriptor / resource binding 信息提取
- Vulkan descriptor set layout 生成与缓存
- DX12 root signature / descriptor range 生成
- vertex input 生成
- thread group size / parameter block 元信息提取

### 8.3 ShaderParameterBlockLayout

当前引擎已经把 `ShaderParameterBlockLayout` 作为一条共享设计路径：

- DX12 后端消费它
- Vulkan 后端也应保持同一消费语义
- 高层 `GraphicsProgram` / `ComputeProgram` 会把 root constants / const block 绑定到这条路径上

如果以后改动 parameter block 规则，需要同时看两套后端是否仍一致。

### 8.4 当前缓存目录

当前缓存目录为：

- Shader Cache：
  - `product/caches/ShaderCaches/dx12`
  - `product/caches/ShaderCaches/vulkan`
- Vulkan Pipeline Cache：
  - `product/caches/PipelineCaches/AshVulkanPipelineCache.pipelineCacheVK`

当前 `ShaderCache` 相关声明在：

- `project/src/engine/Graphics/ShaderCache.h`

### 8.5 Shader 规则变更时的文档要求

如果发生以下任一变化，必须更新本文档：

- shader 源格式变化
- compiler 参数变化
- reflection 规则变化
- binding 验证规则变化
- cache key / cache 目录变化
- vertex input 生成规则变化

---

## 9. Vulkan / DX12 双后端开发规则

### 9.1 默认规则

以后所有新功能、渲染 bug 修复、底层抽象改动，默认按以下规则执行：

- 如果只改纯 Vulkan 私有实现，可只验证 Vulkan
- 只要改到了共享抽象、Function 层、Shader 绑定、资源状态、Renderer、RenderDevice、UI、配置或公共 API，就应视为 **双后端改动**
- 双后端改动默认需要验证 Vulkan + DX12

### 9.2 典型高风险改动类型

- `Renderer` / `RenderDevice`
- `Graphics/RenderProgram.*`
- `Graphics/Shader.*`
- `Graphics/DescriptorSet*`
- `Graphics/GraphicsContext.*`
- `Function/Gui/*`
- DynamicRHI 与配置项

### 9.3 非 Windows 兼容约束

做公共改动时要注意：

- 不要把 DX12 / DXC 头文件泄露到未做平台保护的公共路径
- 非 Windows 构建仍需要能通过 premake 的文件裁剪正常编译

---

## 10. UI 系统

### 10.1 当前分层

当前 UI 分层为：

- `ImGuiLayer`：Engine 内部后端桥接层
- `UIContext`：Engine 对上的通用 DevUI facade

### 10.2 当前边界

`UIContext` 当前只适合承载：

- 通用 immediate-mode widget
- 窗口 / 子窗口 / 菜单 / tabs / tables / popup
- RenderTarget 显示
- 输入捕获查询

它 **不再承载**：

- workspace / dockspace policy
- panel 系统语义
- property-grid / inspector 语义

相关专题说明见：

- `docs/EngineUIContext.md`
- `docs/EditorUIFacadeProposal.md`

### 10.3 后续原则

- Engine 继续维护 `ImGuiLayer` 与 `UIContext`
- Editor 如需更高层工作区接口，应在其上方再包 editor-specific facade

---

## 11. Asset 与 Scene 基础模块

### 11.1 AssetDatabase

`AssetDatabase` 当前提供：

- root path 管理
- 目录扫描 / refresh
- `AssetInfo` 列表
- 按 id / path 查询
- text / binary 加载
- load state 与 last error 查询

它更偏向 **编辑器资源浏览与轻量内容访问基础设施**，但目前仍位于 Engine 内维护。

### 11.2 Scene / Entity

`Scene` 当前提供：

- entity 创建、查找、销毁
- 父子层级
- 序列化 / 反序列化
- dirty 状态

`Entity` 当前提供：

- Name
- Transform
- Camera
- Light
- Mesh

并有组件 / 枚举描述接口，供上层做反射式编辑与展示。

### 11.3 当前定位

Asset / Scene 当前更像 **运行时基础数据模型 + 编辑器支撑基础设施**，不是完整 gameplay framework。

如果以后这里的职责扩大，需要在本文档中补上：

- ownership 模型
- 线程模型
- 生命周期
- 序列化格式约束

---

## 12. 日志、验证、调试与泄露定位

### 12.1 日志

当前日志输出路径：

- `product/logs/AshEngineLogFile_*.logfile`
- `product/logs/AshAppLogFile_*.logfile`

日志系统在 `Base/hlog.cpp` 中初始化。

### 12.2 Vulkan Validation / DX12 Validation

当前 validation 开关来自 `product/config/Engine.ini`。

建议配合本地 skill `ash-engine-validation-loop` 做最终验收，它会执行：

- Premake 重新生成
- 编译
- Vulkan 运行 25 秒，并通过 `ASH_ENGINE_SMOKE_TEST_SECONDS` 走引擎内的优雅退出路径
- DX12 运行 25 秒，并通过 `ASH_ENGINE_SMOKE_TEST_SECONDS` 走引擎内的优雅退出路径
- 校验 validation / debug layer / 泄露 / backend 错配

Vulkan 支持：

- 开关
- GPU Assisted
- Synchronization Validation
- BreakOnValidationError

DX12 支持：

- Debug Layer
- GPU Validation
- 对带 `initial_data` 的 GPU-only buffer，立即上传必须使用独立的临时 command allocator / command list；不要复用当前帧正在录制的 allocator，否则会命中 `ID3D12CommandAllocator::Reset` debug-layer 错误

### 12.3 Vulkan VMA 泄露定位

当前 Vulkan 侧已经有基于宏开关的 VMA 泄露跟踪能力，关键实现位于：

- `Graphics/Vullkan/VulkanContext.h`
- `Graphics/Vullkan/VulkanContext.cpp`

当前能力包括：

- 分配点记录
- 释放点 untrack
- allocator shutdown 前 dump live allocations
- 可选 stack trace 采集

如果调整这套宏、日志格式或采样方式，需要更新本文档。

### 12.4 调试建议

遇到渲染 / validation / 泄露问题时，优先顺序建议：

1. 看 `product/logs`
2. 按 backend 打开 validation
3. RenderDoc 抓帧看资源内容、layout、pass 输出
4. Vulkan 问题看 resource tracker / barrier 位置
5. 泄露问题看 VMA leak dump

---

## 13. 当前公共开发约束

### 13.1 不要直接把后端对象暴露给上层

高层接口应通过 Function 层包装对象暴露，不要要求 Editor / Game 直接持有：

- `Vk*`
- `ID3D12*`
- `RHI::*` 后端细节对象

### 13.2 改动共享渲染路径时，优先看 Vulkan 合法性

原因不是 Vulkan 更重要，而是它对 barrier / render pass 区间约束更严格。共享设计如果先满足 Vulkan，通常更不容易在 DX12 上走偏。

### 13.3 配置、路径、运行目录变更要同步文档

尤其是：

- `product/config`
- `product/logs`
- `product/caches`
- `product/bin64/<Config>-windows-x86_64`

### 13.4 专题文档可以补充，不要替代总文档

可以继续新增专题文档，但不应让核心事实只存在于零散专题里。公共开发者首先应能从本文档理解当前主干引擎。

---

## 14. 推荐的文档更新方式

每次做完 Engine 任务后，按下面顺序检查是否需要回写文档：

1. 是否改了公共 API 或分层边界
2. 是否改了配置项、目录、构建产物位置
3. 是否改了渲染路径、状态转换、资源绑定规则
4. 是否改了 UI 分层或 Editor 可见边界
5. 是否需要增加一个新的专题文档

若答案为“是”，至少更新：

- `docs/EngineDeveloperGuide.md`

若还影响 Editor 使用方式，再更新：

- `docs/EditorDeveloperGuide.md`

---

## 15. 当前相关文档索引

- Engine 总览：`docs/EngineDeveloperGuide.md`
- Editor 总览：`docs/EditorDeveloperGuide.md`
- Engine UI 分层：`docs/EngineUIContext.md`
- Editor UI 提案：`docs/EditorUIFacadeProposal.md`
- 历史设计问题记录：`docs/CodeReview_DesignDefects_and_Risks.md`
