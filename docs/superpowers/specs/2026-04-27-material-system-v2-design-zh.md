# AshEngine 材质系统 V2 设计（统一材质编译框架）

**状态：** 提议  
**日期：** 2026-04-27  
**范围：** `project/src/engine/Function/Render`、`project/src/engine/Graphics`、shader 编译/反射/缓存路径、`SceneRenderer`、未来 `Decal` 与 `PostProcess` 渲染子系统  
**当前不纳入范围：** `UI` domain 的正式接入、Dear ImGui backend 重写

## 1. 背景

当前主干已经存在一套 V1 材质链路，核心对象包括：

- `MaterialInterface`
- `Material`
- `MaterialInstance`
- `MaterialRenderProxy`
- `SceneSurfacePBR.hlsl`

但这套系统的本质仍然是：

- engine-owned 固定 shader 承载材质语义
- 材质主要提供参数、贴图、sampler 与少量 shader macro 变体
- `SceneRenderer` 只真实支持 `Surface + StaticMesh + Opaque/Masked`

这意味着当前系统仍属于“参数化模板材质”，而不是“材质 shader 与 engine shader 编译期拼接”的统一框架。它在以下方向上会持续受限：

- `StaticMesh` 与 `SkeletalMesh` 难以复用同一套材质 shader 语义
- `Transparent`、`Decal`、`PostProcess` 只能继续在 renderer 内部扩散 ad-hoc 逻辑
- 未来材质编辑器难以围绕稳定 ABI 和编译框架展开
- shader 资源声明、资产绑定、program 缓存的职责边界不清晰

本设计的目标是把材质系统升级为一套真正的 **统一材质编译框架**：

- 材质资产声明 `MaterialShader`
- 引擎为不同宿主类型选择 `EngineShaderFamily`
- 引擎按不同 pass 选择 `PassFamily`
- 最终由引擎在编译期把三者拼接成后端可编译的 shader 单元

## 2. 目标与非目标

### 2.1 目标

- 建立统一的材质资产格式 `.AshMat`
- 建立统一的 `MaterialShader` 约定，使材质逻辑与具体宿主 shader 解耦
- 在同一 `MaterialDomain` 内支持一份材质代码复用于多个 `EngineShaderFamily`
- 正式把以下内容纳入同一材质编译框架：
  - `Surface.StaticMesh`
  - `Surface.SkeletalMesh`
  - `Surface` 下的 `Opaque / Masked / Transparent`
  - `Decal.Deferred`
  - `PostProcess.Fullscreen`
- 把编译期静态信息与运行时参数/资源绑定彻底拆开
- 为未来材质编辑器、材质图、更多 shader family 与 pass family 预留稳定扩展点

### 2.2 非目标

- 本次不把 `UI` 正式接入统一材质框架
- 本次不重写 Dear ImGui backend
- 本次不追求“一份材质源码跨所有 domain 通吃”
- 本次不把 family 私有输入直接暴露给通用材质 ABI
- 本次不把所有 renderer 合并成一个通用 renderer

## 3. 设计结论摘要

本设计的关键结论如下：

1. 统一的是 **材质框架**，不是所有 renderer 的 draw orchestration。
2. 材质复用的正式边界是 **同一 `MaterialDomain` 内复用**，而不是跨 domain 复用。
3. `StaticMesh`、`SkeletalMesh`、`Decal`、`PostProcess` 是不同的 `EngineShaderFamily`，不是不同的材质资产类型。
4. `Transparent` 不是新的 `MaterialDomain`，而是 `Surface` domain 下的 `BlendMode + PassFamily` 组合。
5. 同一 domain 内，材质可写的 `MainNode` 输出字段必须固定，不允许随 `EngineShaderFamily` 漂移。
6. family 差异通过 adapter 与 capability 吃掉，而不是暴露给材质源码。
7. `.AshMat` 是材质资产描述文件，不是某个具体宿主 shader 的配置文件。
8. `MaterialShader` 只负责材质逻辑，不直接声明引擎管理的资源寄存器、entry point 或 pass 逻辑。
9. 编译期 permutation 缓存与运行时 binding snapshot 必须分层。
10. `MaterialResource` 承担单 permutation 的只读编译结果；`MaterialRenderProxy` 只承担某个材质实例的 render-thread 绑定快照。

## 4. 三轴模型

整套系统建议拆成三个互相独立的轴。

### 4.1 `MaterialDomain`

定义“材质在表达什么”。

本次正式纳入框架的 domain：

- `Surface`
- `Decal`
- `PostProcess`

预留但不在本次正式接入范围内的 domain：

- `UI`

### 4.2 `EngineShaderFamily`

定义“谁来承载这个材质”。

这是真正的宿主 shader family，不同 family 可以属于同一个 domain。

本次正式纳入的 family：

- `Surface.StaticMesh`
- `Surface.SkeletalMesh`
- `Decal.Deferred`
- `PostProcess.Fullscreen`

预留但不在本次正式接入范围内的 family：

- `UI.Quad`

### 4.3 `PassFamily`

定义“这次编出来是在哪个 pass 用”。

典型 family 包括：

- `DepthOnly`
- `BasePass`
- `ShadowDepth`
- `TransparentForward`
- `DeferredDecal`
- `PostProcessMain`

### 4.4 三者关系

- `MaterialDomain` 决定材质可见 ABI
- `EngineShaderFamily` 决定如何把宿主数据适配到该 ABI
- `PassFamily` 决定最终 host shader 如何消费材质输出、如何构建 render state 与 attachment 语义

最终编译键应表达为：

```text
MaterialAsset
+ MaterialShader
+ MaterialDomain
+ EngineShaderFamily
+ PassFamily
+ StaticSwitches
+ RequiredCapabilities
+ StaticRenderState
```

## 5. Domain Contract 设计

### 5.1 核心原则

同一 `MaterialDomain` 内：

- `VertexMainNode / PixelMainNode` 的 **可写输出字段必须固定**
- family 可以变化的是：
  - 材质可读输入的准备方式
  - 某个 pass 最终消费哪些输出

这条规则直接解决“同一份材质代码如何在多个 family 间复用”的核心问题：  
材质从不直接面向某个具体 `EngineShaderFamily` 的私有 struct，而是面向稳定的 domain contract。

### 5.2 `Surface` Contract

`Surface` 面向：

- `StaticMesh`
- `SkeletalMesh`
- `Opaque`
- `Masked`
- `Transparent`

建议固定输出为：

```hlsl
struct SurfaceVertexMainNode
{
    float3 world_position_offset;
};

struct SurfacePixelMainNode
{
    float3 base_color;
    float opacity;
    float opacity_mask;
    float3 normal_ts;
    float metallic;
    float roughness;
    float3 emissive;
    float ambient_occlusion;
    float pixel_depth_offset;
};
```

说明：

- `StaticMeshSurface` 与 `SkeletalMeshSurface` 都适配到该 contract
- `Transparent` 仍属于 `Surface` domain，只是其 `BlendMode` 与 `PassFamily` 不同
- 某些 pass 如 `DepthOnly` 会忽略一部分字段，但那是 pass 级消费差异，不是字段不存在

### 5.3 `Decal` Contract

`Decal` 面向：

- `Decal.Deferred`

建议固定输出为：

```hlsl
struct DecalVertexMainNode
{
    float3 decal_position_offset;
};

struct DecalPixelMainNode
{
    float3 base_color;
    float opacity;
    float3 normal_ts;
    float metallic;
    float roughness;
    float3 emissive;
};
```

说明：

- `Decal` 不复用 `Surface` contract
- 其 receiver 依赖 scene depth / scene normal / projector 空间等特殊语义，应由 family 适配层提供

### 5.4 `PostProcess` Contract

`PostProcess` 面向：

- `PostProcess.Fullscreen`

建议固定输出为：

```hlsl
struct PostProcessVertexMainNode
{
    float2 position_offset;
};

struct PostProcessPixelMainNode
{
    float4 color;
};
```

说明：

- 这是一套 screen-space image operation contract
- 不引入 surface/decal 语义

### 5.5 输入能力分层

材质可读输入建议分为三层：

1. domain 核心输入  
   例如 `Surface` 下的 `world_position`、`world_normal`、`uv0`、`view_direction`

2. 可标准化输入  
   例如 `uv1`、`vertex_color`。没有时可由 family 给稳定默认值，而不是直接变成 permutation

3. 真正的 capability  
   例如 `scene_depth`、`scene_normal`、`custom_primitive_data`、`previous_frame_position`

正式规则：

- 同一 domain 内，输出 contract 是强约束
- 输入 capability 是弱约束
- family 私有数据默认不进公共 ABI

## 6. `.AshMat` 资产模型

### 6.1 定位

`.AshMat` 是 **材质资产描述文件**，不是：

- 最终 shader 文件
- 某个具体 `EngineShaderFamily` 的配置文件
- 某个 pass 的最终编译单元

它只表达：

- 材质属于哪个 domain
- 使用哪份 `MaterialShader`
- 具备哪些静态开关、参数、资源、sampler 与材质级 render state

### 6.2 文件类型

建议继续保留两类对象：

- `Material`
- `MaterialInstance`

职责分别为：

- `Material`
  - 定义 `domain`
  - 定义 `materialShader`
  - 定义 `requiredCapabilities`
  - 定义 `staticSwitches`
  - 定义静态 `renderState`
  - 定义参数/资源/sampler 声明与默认值
- `MaterialInstance`
  - 引用 `parent`
  - 提供参数、资源、sampler 的运行时 override

普通 instance 不建议覆盖 compile-affecting 字段。

### 6.3 Schema 结构

建议 `.AshMat` 至少包含下列块：

- `version`
- `class`
- `name`
- `domain`
- `materialShader`
- `requiredCapabilities`
- `staticSwitches`
- `renderState`
- `samplers`
- `parameters`
- `resources`

### 6.4 `samplers` 的正式存储

`samplers` 不建议以原始 HLSL 文本字符串持久化。  
正式 schema 建议使用结构化 JSON：

```json
"samplers": {
  "WrapLinear": {
    "addressU": "Repeat",
    "addressV": "Repeat",
    "addressW": "Repeat",
    "minFilter": "Linear",
    "magFilter": "Linear",
    "mipFilter": "Linear"
  }
}
```

原因：

- 更利于后续材质编辑器结构化编辑
- 更利于 hash、缓存和合法性校验
- 更利于跨后端一致性维护

### 6.5 `renderState` 的边界

`renderState` 存的是 **材质级静态语义状态**，而不是完整 RHI pipeline state。  
建议正式支持：

- `blendMode`
- `twoSided`
- `cullMode`
- `depthWrite`
- `depthTest`
- `alphaCutoff`

### 6.6 示例

`Material` 示例：

```json
{
  "version": 2,
  "class": "Material",
  "name": "M_BrickWall",
  "domain": "Surface",
  "materialShader": "project/src/game/Shaders/M_BrickWall.hlsl",
  "requiredCapabilities": [
    "vertex_color"
  ],
  "staticSwitches": {
    "USE_DETAIL_NORMAL": true
  },
  "renderState": {
    "blendMode": "Opaque",
    "twoSided": false,
    "cullMode": "Back",
    "depthWrite": true,
    "depthTest": "LessEqual",
    "alphaCutoff": 0.5
  },
  "samplers": {
    "WrapLinear": {
      "addressU": "Repeat",
      "addressV": "Repeat",
      "addressW": "Repeat",
      "minFilter": "Linear",
      "magFilter": "Linear",
      "mipFilter": "Linear"
    }
  },
  "parameters": {
    "BaseColorTint": {
      "type": "float4",
      "value": [1.0, 1.0, 1.0, 1.0]
    },
    "RoughnessScale": {
      "type": "float",
      "value": 0.8
    }
  },
  "resources": {
    "BaseColorTex": {
      "type": "Texture2D",
      "path": "product/assets/textures/brick_albedo.png",
      "sampler": "WrapLinear",
      "colorSpace": "sRGB"
    }
  }
}
```

`MaterialInstance` 示例：

```json
{
  "version": 2,
  "class": "MaterialInstance",
  "name": "MI_BrickWall_Wet",
  "parent": "product/assets/materials/M_BrickWall.AshMat",
  "overrides": {
    "parameters": {
      "RoughnessScale": 0.15
    },
    "resources": {
      "BaseColorTex": {
        "path": "product/assets/textures/brick_wet_albedo.png"
      }
    }
  }
}
```

## 7. `MaterialShader` 源文件契约

### 7.1 定位

`MaterialShader` 是 **纯材质逻辑文件**，它：

- 不定义 `VSMain / PSMain / CSMain`
- 不定义具体 pass 逻辑
- 不写 `register(t# / s# / b#)`
- 不直接声明由 `.AshMat` 管理的 GPU 资源
- 必须实现：
  - `CalculateVertexMainNode(...)`
  - `CalculatePixelMainNode(...)`

### 7.2 推荐函数签名

建议由引擎在拼接阶段提供统一别名：

```hlsl
void CalculateVertexMainNode(
    in AshVertexParameters params,
    inout AshVertexMainNode node);

void CalculatePixelMainNode(
    in AshPixelParameters params,
    inout AshPixelMainNode node);
```

其中：

- `AshVertexParameters`
- `AshPixelParameters`
- `AshVertexMainNode`
- `AshPixelMainNode`

都是引擎按 domain 注入的别名。

### 7.3 输出默认初始化

`MainNode` 必须由引擎先做规范默认初始化，材质只改自己关心的字段。  
例如 `SurfacePixelMainNode` 默认值可定义为：

- `base_color = (1, 1, 1)`
- `opacity = 1`
- `opacity_mask = 1`
- `normal_ts = (0, 0, 1)`
- `metallic = 0`
- `roughness = 0.5`
- `emissive = (0, 0, 0)`
- `ambient_occlusion = 1`
- `pixel_depth_offset = 0`

这样材质代码无需对每个字段显式赋值。

### 7.4 资源声明由 `.AshMat` 生成

`.AshMat` 中的：

- `parameters`
- `resources`
- `samplers`

由引擎在编译期自动生成 HLSL 声明。  
`MaterialShader` 直接使用这些名字，不再自己声明资源寄存器和常量缓冲。

### 7.5 允许与禁止内容

允许：

- 局部 helper 函数
- `CalculateVertexMainNode(...)`
- `CalculatePixelMainNode(...)`
- 使用引擎生成的参数、资源、sampler 名字
- 根据 capability / static switch 写条件分支

禁止：

- 自己声明承载资产资源的 `Texture2D / SamplerState / cbuffer`
- 自己写 `VSMain / PSMain`
- 自己写后端寄存器槽号
- 访问某个 `EngineShaderFamily` 私有输入结构
- 自己决定 pass output 或 attachment 语义

### 7.6 组合后的编译单元结构

引擎建议按以下五层生成最终编译单元：

1. compile environment
2. domain contract
3. generated bindings
4. user material shader
5. engine shader host

组合示意：

```hlsl
// 1. compile environment
#define ASH_DOMAIN_SURFACE 1
#define ASH_ENGINE_FAMILY_SURFACE_STATIC_MESH 1
#define ASH_PASS_BASE_PASS 1
#define ASH_CAP_VERTEX_COLOR 1
#define ASH_STATIC_USE_DETAIL_NORMAL 1

// 2. domain contract
#include "Engine/Generated/MaterialDomains/AshSurfaceDomain.hlsli"

// 3. generated bindings from .AshMat
#include "Engine/Generated/Materials/M_BrickWall.Bindings.generated.hlsli"

// 4. user material shader
#include "project/src/game/Shaders/M_BrickWall.hlsl"

// 5. engine shader host
#include "Engine/Generated/ShaderFamilies/Surface.StaticMesh.BasePass.generated.hlsli"
```

## 8. 编译与缓存模型

### 8.1 `AshMatCompileHash`

正式设计中需要区分：

- 影响编译结果的字段
- 只影响运行时绑定的字段

建议引入 `AshMatCompileHash`，只覆盖会改变编译结果的内容，例如：

- `domain`
- `materialShader`
- `requiredCapabilities`
- `staticSwitches`
- compile-affecting `renderState`
- `parameters/resources/samplers` 的名字与类型声明

不覆盖：

- 参数默认值
- 资源默认路径
- instance 运行时 override 值

### 8.2 两层 key

建议缓存拆成两层：

1. 源码组合层  
   决定最终拼接后的 HLSL 文本内容

2. 程序对象层  
   决定 DX12/Vulkan 下具体 program / pipeline 如何创建

### 8.3 组合层 key

组合层 key 应表达为：

```text
AshMatCompileHash
+ EngineShaderFamily
+ PassFamily
+ CapabilityMask
+ BackendCompileFlavor
```

### 8.4 程序对象层 key

程序对象层 key 应表达为：

```text
CombinedSourceHash
+ Backend
+ VertexInputLayoutKey
+ AttachmentFormatKey
+ StaticRenderStateKey
```

## 9. 运行时对象模型

建议正式分成以下五层对象。

### 9.1 `MaterialInterface / Material / MaterialInstance`

逻辑侧、资产侧对象。

- `Material`
  - 持有编译期静态描述和默认值
- `MaterialInstance`
  - 只持有运行时 override

### 9.2 `MaterialShaderMap`

单份材质的编译产物集合，按需懒生成不同 permutation。  
它不是单个 program，而是多个 `MaterialResource` 的容器。

### 9.3 `MaterialResource`

单个 permutation 的不可变编译结果，至少包含：

- 最终源码 hash
- 反射得到的参数布局
- 反射得到的资源绑定布局
- backend program / pipeline
- pass relevance
- permutation capability mask

正式设计中，backend program 应归属于 `MaterialResource`，而不是归属于 `MaterialRenderProxy`。

### 9.4 `MaterialRenderProxy`

某个具体 `MaterialInterface` 在 render thread 上的只读快照。  
它不负责 shader 编译，它负责：

- 持有 `MaterialShaderMap`
- 解析参数值
- 解析纹理 / sampler / buffer 资源
- 构建 render-thread 可直接消费的 binding snapshot

### 9.5 `MaterialBindingSnapshot`

建议显式建模或至少在设计上单独看待。  
它持有：

- 打包后的参数常量数据
- 已解析的 texture / sampler / buffer 句柄
- 与 `MaterialResource::ResourceBindingLayout` 对齐后的槽位表

## 10. 参数更新、热重载与版本号

### 10.1 参数变化分级

建议把变化分成三类：

1. 纯运行时值变化  
   例如 float/vector 参数、资源路径、sampler 引用  
   结果：只刷新 `MaterialBindingSnapshot`

2. 编译期静态变化  
   例如 `domain`、`materialShader`、`requiredCapabilities`、`staticSwitches`  
   结果：重建 `MaterialShaderMap`

3. 使用场景变化  
   例如第一次请求 `Surface.SkeletalMesh + BasePass` permutation  
   结果：按需在 `MaterialShaderMap` 中新增 permutation，而不是把材质视为已脏

### 10.2 建议维护的版本信息

建议至少维护：

- `AssetVersion`
- `CompileHash`
- `RuntimeBindingVersion`

分别表达：

- 资产语义是否变化
- 是否需要重建编译结果
- 是否只需要刷新运行时绑定快照

### 10.3 热重载时机

热重载采用保守策略：

- 当前 in-flight frame 继续使用旧对象
- 下一帧切换到新对象
- 不在同一帧中途强制替换

## 11. Renderer / Subsystem 接入方式

### 11.1 总体原则

统一的是材质框架，不是 renderer 本身。  
各 renderer / subsystem 只统一通过 `MaterialSystem` 请求 usage 解析结果。

### 11.2 `SceneRenderer`

`SceneRenderer` 未来应至少分成：

- `SurfaceOpaqueRenderer`
- `SurfaceTransparentRenderer`
- `SurfaceDepthRenderer`

primitive 不再只带“材质 proxy”，还应带自己的 primitive family 信息，例如：

- `StaticMesh`
- `SkeletalMesh`

使用时：

- `StaticMesh` primitive 请求 `Surface.StaticMesh`
- `SkeletalMesh` primitive 请求 `Surface.SkeletalMesh`

### 11.3 `Transparent`

`Transparent` 不是独立 domain，而是：

- `MaterialDomain = Surface`
- `BlendMode = Transparent`
- `PassFamily = TransparentForward`

这意味着：

- 材质 ABI 仍是 `Surface`
- 但 renderer 需要独立透明队列、排序与 pass orchestration

### 11.4 `Decal`

建议独立 `DecalRenderer` 或等价渲染子系统，负责：

- 收集 decal primitive / projector
- 提供 `Decal.Deferred` usage context
- 提供 scene depth / scene normal 等 capability
- 提交 decal pass

### 11.5 `PostProcess`

建议独立 `PostProcessSubsystem` 或 `ScreenPassRenderer`，负责：

- fullscreen triangle / quad
- `PostProcess.Fullscreen` family
- `PostProcessMain` 等 screen-space pass orchestration

### 11.6 `UI`

`UI` domain 保留在 schema 与 registry 预留层面，但本次设计不正式接入。  
本次不把 Dear ImGui backend 纳入材质框架改造范围。

### 11.7 `EngineShaderFamilyRegistry`

建议统一注册 family，而不是把 family 真相散落在各 renderer 中。  
每个 family 至少声明：

- `id`
- `domain`
- `debug_name`
- `host_template_path`
- `supported_pass_families`
- `provided_capabilities`
- `primitive_family`

## 12. 错误处理与 Fallback

### 12.1 资产层错误

例如：

- `.AshMat` 缺字段
- domain 非法
- 参数/资源名非法
- shader 路径不存在

处理：

- `AssetDatabase` 加载失败
- 记录明确错误日志
- 回退到对应 domain 的 fallback material

### 12.2 编译兼容性错误

例如：

- domain 与 family 不兼容
- `requiredCapabilities` 不满足
- `MaterialShader` 缺少必须的 calculate 函数
- 资源声明与 shader 使用不一致

处理：

- 当前 permutation 编译失败
- 日志必须包含：
  - material path
  - domain
  - family
  - pass family
  - backend
- 当前 permutation 回退到对应 domain 的 fallback permutation

### 12.3 运行时绑定错误

例如：

- texture 路径失效
- sampler 缺失
- binding snapshot 构建失败

处理：

- 回退到 fallback texture / fallback sampler / fallback values
- draw 可继续，但必须记录 warning

### 12.4 per-domain fallback

建议按 domain 分别提供 fallback：

- `Surface` fallback
- `Decal` fallback
- `PostProcess` fallback

不要所有错误都回退到同一个通用 surface 材质。

## 13. 迁移策略

建议采用“三阶段迁移”。

### 13.1 第一阶段

- 引入 `.AshMat`
- 引入 `MaterialSystem`
- 引入 `EngineShaderFamilyRegistry`
- 引入 `MaterialShaderMap / MaterialResource / MaterialBindingSnapshot`
- 先让 `Surface.StaticMesh` 跑通 V2 框架

### 13.2 第二阶段

继续接入：

- `Surface.SkeletalMesh`
- `Surface.Transparent`
- `Decal.Deferred`
- `PostProcess.Fullscreen`

### 13.3 第三阶段

- 逐步退役 V1 固定模板材质路径
- 旧 `.material / .mat` 走兼容加载或离线升级
- 最终统一到 `.AshMat`

### 13.4 旧资产兼容

建议短期：

- 继续兼容旧 `.material / .mat`
- 运行时映射成内部 V2 材质对象

建议中期：

- 补离线升级工具，把旧格式转成 `.AshMat`

## 14. 验证要求

### 14.1 构建验证

- `Engine`
- `Sandbox`
- `Editor`

### 14.2 运行验证矩阵

- `Sandbox + Vulkan`
- `Sandbox + DX12`
- `Editor + Vulkan`
- `Editor + DX12`

### 14.3 功能验证

至少覆盖：

- `Surface.StaticMesh`
- `Surface.SkeletalMesh`
- `Surface.Transparent`
- `Decal`
- `PostProcess`

### 14.4 失败判定

以下任一出现都算失败：

- shader compile fail
- 反射/绑定不匹配
- Vulkan validation error
- DX12 debug layer error
- 资源泄漏
- fallback material 被异常大范围触发
- 同材质在 static/skeletal 上行为不一致
- transparent 排序或深度行为明显错误
- postprocess 输出链异常
- decal capability 依赖路径错误

## 15. 最终范围结论

本次统一材质框架正式纳入：

- `Surface`
- `Decal`
- `PostProcess`

正式纳入的 family / pass 方向：

- `Surface.StaticMesh`
- `Surface.SkeletalMesh`
- `Surface` 下的 `Opaque / Masked / Transparent`
- `Decal.Deferred`
- `PostProcess.Fullscreen`

本次明确不纳入：

- `UI`
- Dear ImGui backend 重写
- 跨所有 domain 的单份材质源码复用
- family 私有输入直接暴露给公共材质 ABI

本设计的核心原则总结如下：

- 统一的是材质框架，不是所有 renderer
- 同一 domain 内输出 ABI 固定
- family 差异由 adapter + capability 吃掉
- `.AshMat` 是资产描述，不是具体宿主配置
- `MaterialShader` 只写材质逻辑
- 编译信息与运行时绑定严格分层
