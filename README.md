# HASHEAEngine

HASHEAEngine 是一个以现代实时渲染和引擎架构实验为目标的 C++ 图形引擎项目。当前仓库包含 Engine DLL、Editor 可执行程序和 Sandbox 验证程序，主开发平台为 Windows x64，构建系统使用 Premake5 + Visual Studio/MSBuild。

项目仍处于持续研发阶段。当前重点是打通稳定的 Engine 基础设施、Vulkan / DX12 双后端 RHI、Scene-driven 静态网格渲染、材质 V2 框架和基础 Editor 工作流，而不是完整的生产级游戏引擎。

## 当前状态

当前主干已经具备：

- Engine 分层架构：`Base`、`Graphics`、`Function` 三层，Editor / Sandbox 通过 Function 层使用引擎能力。
- Vulkan 与 DX12 双后端：运行时通过 `product/config/Engine.ini` 选择后端，Windows Debug / Release 构建同时编入 Vulkan、DX12、DXC。
- Scene-driven 静态网格渲染：逻辑 `Scene` 通过 `ScenePresentationSubsystem` 转换为渲染线程可消费的不可变可见帧数据。
- 材质 V2 基础链路：支持 `Surface.StaticMesh` 的材质 shader 与 engine shader family 拼合，`.AshMat` 作为基材质，`.AshMatIns` 作为可直接赋给物体的材质实例。
- Asset 与 glTF 示例资源：支持示例模型加载，Sandbox 默认使用 Sponza 作为标准验证场景。
- Editor 基础壳：具备 dockspace、Scene/Game 视口、层级、属性、控制台、资产浏览等基础面板。
- 调试与性能工具：支持日志、Vulkan validation、DX12 debug layer、GPU debug names、Tracy CPU profiling、frame stats overlay、Vulkan VMA 泄露定位。

当前仍未完成或仅处于预留阶段：

- Skeletal mesh / animation 尚未完成。
- 完整 lighting、shadow、instancing、occlusion culling 尚未完成。
- Transparent blend mode 已进入材质静态状态和编译键，但正式透明队列尚未接入 SceneRenderer。
- PostProcess 与 UI 当前不纳入材质系统，后续应走各自的 shader/pass 与参数组织路径。
- Asset cooking、streaming、完整资源生命周期管理仍在演进中。
- Editor 还不是完整生产工具链，材质编辑器、复杂场景编辑、导入管线等仍待补齐。
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

当前已经实现或正在使用的能力：

- Vulkan / DX12 运行时后端选择。
- HLSL 主路径，通过 DXC 编译，Vulkan 消费 SPIR-V，DX12 消费 DXIL。
- shader 反射驱动 descriptor / root signature / descriptor set layout / parameter block layout。
- render pass 与 framebuffer 缓存。
- graphics program / pipeline variant 缓存。
- descriptor / program binding 缓存。
- pass 外资源状态转换，避免 Vulkan 在 render pass / dynamic rendering 活跃区间内提交非法 barrier。
- per-frame GPU upload command path，避免资源上传创建时强制同步等待。
- transient render target pool。
- draw 排序与只读资源 barrier 去重，用于降低静态网格场景 CPU 开销。
- Runtime frame stats overlay。

## Scene 与渲染主路径

当前 Scene 模块采用 facade + ECS-style 内部存储：

- 公共层保留 `Scene` / `Entity` 易用接口。
- 内部基于 entt 风格 registry 管理实体、层级和组件。
- 当前公开组件包括 `Name`、`Transform`、`Camera`、`Light`、`Mesh`。
- `MeshComponent` 支持资产路径、mesh index、section 材质覆盖、可见性、mobility、layer mask。

Scene 到渲染的主路径：

- `Application` 持有 `ScenePresentationSubsystem`。
- 上层声明 `Scene + Camera + Output + View Overrides`。
- `ScenePresentationSubsystem` 维护 per-scene `RenderScene` cache、构建 `SceneView` 和 `VisibleRenderFrame`。
- 逻辑线程负责 scene ownership 和可见帧构建。
- worker 线程执行 CPU frustum culling。
- render thread 只消费不可变 frame packet 并提交 draw。
- Editor Scene/Game viewport 使用 engine-owned offscreen output，通过 `UISurfaceHandle` 交给 UI 展示。
- Sandbox 主窗口使用 window output + persistent binding，作为共享渲染路径验证入口。

第一阶段正式支持静态网格主链路。skeletal mesh、完整灯光、阴影、instancing、occlusion culling 和动态材质实例仍是后续阶段。

## 材质系统

当前主干使用材质 V2：

- `.AshMat`：基材质资产，描述材质 shader、资源声明、参数、render state、宏等。
- `.AshMatIns`：材质实例资产，允许覆盖参数、贴图、采样器和部分实例级设置。
- 只有 `.AshMatIns` 可以直接赋给 mesh section、`MeshComponent.material_overrides` 或模型默认材质槽。
- 如果运行时解析到直接绑定 `.AshMat` 基材质，会报错并回退到 generated/default instance。
- `MaterialSystem` 负责 domain / family / pass 的验证、fallback 和 resource template 获取。
- `MaterialShaderMap` 负责不可变编译资源。
- `MaterialRenderProxy` 在 render thread submit phase 准备材质参数、贴图、sampler、graphics program 和 binding。
- 当前正式主路径为 `Surface.StaticMesh.BasePass` 与 `DepthOnly`。

材质 shader 由三部分拼合：

1. Engine shader family host，例如 `SurfaceStaticMeshBasePass.hlsl`。
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
- 支持 glTF 示例模型，包含 Sponza、DamagedHelmet、BoomBox、Avocado。
- 贴图 decode 覆盖 `png`、`jpg`、`jpeg`、`tga`、`bmp`、`hdr`。
- `RenderAssetManager` 负责 static mesh CPU/GPU 资源桥接、贴图请求、fallback texture、sampler cache、材质代理准备。
- 模型导入材质槽会生成或解析 `.AshMatIns`，draw-time 绑定由 `MaterialRenderProxy + MaterialSystem` 处理。

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

Editor 仍在开发中，不应视为完整关卡编辑器或生产工具链。

## Sandbox

Sandbox 是 Engine 侧测试/验证可执行项目，目标是避免把引擎验证逻辑长期塞进 Editor 生命周期中。

当前 Sandbox：

- 默认加载 Sponza 作为标准场景。
- 走逻辑 Scene -> ScenePresentationSubsystem -> SceneRenderer 的正式链路。
- 用于验证静态网格渲染、材质 V2、资源加载、双后端 smoke test 和性能回归。

## 构建与运行

### 环境

- Windows x64。
- Visual Studio 2022 或包含 MSBuild 的 Build Tools。
- 支持 Vulkan 或 DX12 的显卡与驱动。
- 仓库根目录自带 `premake5.exe`。

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

## 后端配置

运行时后端由 `product/config/Engine.ini` 控制：

```ini
[RHI]
Backend=Vulkan

[VulkanValidation]
Enabled=true
GpuAssisted=false
SynchronizationValidation=true
BreakOnValidationError=false

[DX12Validation]
Enabled=true
GpuValidation=true
```

可选后端：

- `Vulkan`
- `DX12`

Validation 开关只在 Debug 配置下生效。Release 构建中即使配置文件打开 Vulkan 或 DX12 validation，引擎也会强制关闭 validation / debug layer。

## 验证与调试

常用验证入口：

```bat
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=5
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

常用调试输出：

- 运行日志：`product/logs/`
- Shader cache：`product/caches/ShaderCaches/`
- Pipeline cache：`product/caches/PipelineCaches/`
- 本地构建、cdb、测试、性能分析报告：`Intermediate/`

调试与性能工具：

- spdlog 日志。
- Vulkan validation layer。
- DX12 debug layer / GPU validation。
- RenderDoc 抓帧。
- Tracy profiling。
- RHI native GPU object debug name。
- Vulkan VMA leak tracking。
- Runtime frame stats overlay。

共享渲染、RHI、Scene、Asset、Application 生命周期或配置相关改动，默认至少需要覆盖：

- Sandbox + Vulkan。
- Sandbox + DX12。
- Editor + Vulkan。
- Editor + DX12。

## 开发进度

| 模块 | 当前进度 |
| --- | --- |
| Engine 基础设施 | 日志、断言、窗口输入、文件、时间、服务、线程等基础能力已具备，仍在规范化错误处理和生命周期细节。 |
| RHI | Vulkan / DX12 双后端可运行，shader 编译反射、资源状态、pipeline、descriptor、debug name 和 validation 正在持续完善。 |
| Renderer | 已有 frame、pass、draw、dispatch、transient RT、frame stats、UI submit 等高层封装，热路径性能仍在优化。 |
| Scene | ECS-style 内部存储和 Scene facade 已具备，静态网格 scene-driven 渲染链路已打通。 |
| Material | V2 `Surface.StaticMesh` 主路径已接入，`.AshMat` / `.AshMatIns` 资产格式已建立，透明、骨骼、decal 等仍待后续阶段。 |
| Asset | glTF 示例模型、贴图 decode、static mesh render asset 桥接已具备，asset cooking / streaming 尚未完成。 |
| Editor | 基础 workspace 和常用面板已具备，当前更偏向引擎验证与工具雏形，完整编辑器能力仍在开发。 |
| Sandbox | 已作为标准 Engine 验证程序，默认加载 Sponza 并走正式 ScenePresentation 渲染链。 |
| Profiling / Debug | Tracy、validation、debug name、日志、frame stats、VMA leak tracking 已接入，粒度和自动化验收仍在扩展。 |

## 文档入口

- Engine 开发指南：[`docs/EngineDeveloperGuide.md`](docs/EngineDeveloperGuide.md)
- Editor 开发指南：[`docs/EditorDeveloperGuide.md`](docs/EditorDeveloperGuide.md)
- Scene Presentation 指南：[`docs/ScenePresentationSubsystemGuide.md`](docs/ScenePresentationSubsystemGuide.md)
- Engine UIContext：[`docs/EngineUIContext.md`](docs/EngineUIContext.md)
- Editor UI 分层提案：[`docs/EditorUIFacadeProposal.md`](docs/EditorUIFacadeProposal.md)
- 静态代码审查与风险记录：[`docs/EngineStaticCodeReview_2026-05-06.md`](docs/EngineStaticCodeReview_2026-05-06.md)

如果 README 与详细文档存在冲突，以 `docs/EngineDeveloperGuide.md` 和 `docs/EditorDeveloperGuide.md` 中的最新约定为准。
