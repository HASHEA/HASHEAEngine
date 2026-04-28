# AshEngine 材质系统 V2 设计（V1 退场与 V2 资产硬分流）

**状态：** 已评审  
**日期：** 2026-04-28  
**范围：** `project/src/engine/Function/Render`、`project/src/engine/Function/Asset`、`project/src/engine/Graphics`、`project/src/engine/Shaders/MaterialV2`、`project/src/sandbox`  
**明确不纳入：** `UI` 材质统一、`PostProcess` 材质统一、`project/src/editor`  
**关联文档：** 本文补充并覆盖 [2026-04-27-material-system-v2-design-zh.md](D:/workspace/AshEngine/HASHEAEngine/docs/superpowers/specs/2026-04-27-material-system-v2-design-zh.md) 中关于 V1 兼容保留、`.AshMat` 单后缀承载 `Material/MaterialInstance`、以及 phased migration 的相关结论。

## 1. 背景

截至当前主干，材质系统 V2 的 `Surface.StaticMesh` 垂直切片已经建立，但仍保留了以下过渡性设计：

- V1 `.material/.mat` 仍可加载
- `Material` 与 `MaterialInstance` 在 V2 下仍共用 `.AshMat` 后缀
- `MaterialRenderProxy` 仍保留 legacy fixed-PBR 兼容分支
- 运行时导入生成材质仍沿用旧的 V1 语义和命名

这些过渡设计会持续制造三类问题：

1. 资产语义不清晰  
   同一后缀既可能是基材，也可能是实例，文件名无法直接表达“能不能挂到物体上”。

2. 运行时路径混杂  
   `MaterialRenderProxy`、`RenderAssetManager`、默认 fallback、导入生成材质同时背着 V1/V2 两套分支，后续扩展 `Surface.Transparent`、`SkeletalMesh`、`Decal` 会越来越脆。

3. 编辑器规则难以硬化  
   即使编辑器侧限制“只能给物体选实例材质”，运行时仍会因为兼容逻辑而偷偷接收基材或旧资产，导致规则在数据层不闭合。

本设计的目标是一次性结束这些过渡状态，把材质系统收敛为一套**只有 V2、只有硬规则**的资产与运行时模型。

## 2. 范围与非目标

### 2.1 本次目标

- 删除 V1 `.material/.mat` 资产与运行时兼容路径
- 将 V2 `Material` 与 `MaterialInstance` 的文件后缀彻底分离
- 规定只有 `MaterialInstance(.AshMatIns)` 才能直接赋给物体
- 将默认材质、导入生成材质、运行时 fallback 全部迁移到 V2
- 用一个正式的引擎内建 `SurfacePBR` V2 基材替代旧 `SceneSurfacePBR` 语义

### 2.2 非目标

- 本次不把 `UI` 纳入统一材质框架
- 本次不把 `PostProcess` 纳入统一材质框架
- 本次不在编辑器内实现材质资产选择器或强校验 UI
- 本次不扩展新的 domain；仍只聚焦当前已落地的 `Surface.StaticMesh`

## 3. 关键结论摘要

本次设计的最终约束如下：

1. 材质系统进入 V2-only 状态，不再保留 V1 兼容资产、解析、保存和运行时分支。
2. `.AshMat` 只表示 `Material` 基材；`.AshMatIns` 只表示 `MaterialInstance`。
3. 文件后缀和 JSON `class` 必须严格一致；加载和保存都做硬校验。
4. 只有 `MaterialInstance(.AshMatIns)` 可以直接赋给物体。
5. 物体绑定入口如果解析到基材，运行时直接报错并回退，不做自动包装。
6. 默认 fallback、导入生成材质、Sandbox 测试覆盖全部统一到 V2 `MaterialInstance`。
7. 旧 `SceneSurfacePBR` fixed-PBR 兼容链路整体删除，由 V2 `M_SurfacePBR.AshMat` 取代。

## 4. V2 资产模型与后缀规则

### 4.1 唯一支持的材质资产版本

- 唯一支持 `version = 2`
- 唯一支持 JSON `class`：
  - `Material`
  - `MaterialInstance`

以下内容在本次变更后均视为非法输入：

- `.material`
- `.mat`
- `version = 1`
- legacy fixed-PBR schema

### 4.2 后缀与类型的硬映射

- `.ashmat` / `.AshMat`
  - 只能对应 `class = "Material"`
  - 只能承载 V2 基材

- `.ashmatins` / `.AshMatIns`
  - 只能对应 `class = "MaterialInstance"`
  - 只能承载 V2 材质实例

后缀比较统一按小写规范化处理，因此运行时语义上只看：

- `.ashmat`
- `.ashmatins`

### 4.3 加载时硬校验

`load_material_from_file(...)` 在解析 JSON 后，必须同时校验：

- 文件后缀
- JSON `class`

规则如下：

- `.ashmat` + `Material`：合法
- `.ashmatins` + `MaterialInstance`：合法
- 其他组合：直接加载失败

不允许：

- `MI_Foo.AshMat` 内写 `MaterialInstance`
- `M_Foo.AshMatIns` 内写 `Material`
- 自动猜测类型
- 自动改名后继续加载

### 4.4 保存时硬校验

`save_material_to_file(...)` 必须基于运行时对象类型反向检查目标路径：

- `Material` 只能保存到 `.AshMat`
- `MaterialInstance` 只能保存到 `.AshMatIns`

若路径与类型不一致：

- 直接返回错误
- 不自动改路径
- 不写出文件

## 5. 只允许实例材质赋给物体

### 5.1 规则边界

`AssetDatabase` 的职责是“材质资产可否加载”，不是“材质是否允许绑定到物体”。  
因此：

- `AssetDatabase` 仍允许加载 `Material(.AshMat)`，因为实例 `parent`、内建基材和 shader 编译都需要它
- “是否允许直接赋给物体”的强约束，统一放在 `RenderAssetManager` 的物体绑定入口

### 5.2 必须强制只接受实例的入口

以下入口最终解析到的材质若不是 `MaterialInstance`，则视为非法绑定：

- `MeshMaterialOverride.material_path`
- `ModelMaterialReference.material_path`
- `RenderAssetManager::resolve_static_mesh_primitive_sections(...)` 里的 override 材质
- `RenderAssetManager::resolve_default_section_material(...)` 返回给 section 的显式材质
- 其他所有“section/material slot 直接解析最终材质”的运行时入口

### 5.3 非法绑定时的运行时行为

若某个物体绑定入口解析到 `Material(.AshMat)`：

- 记录明确错误日志
- 当前非法材质不参与绘制绑定
- 按原有优先级继续向下回退

回退规则：

1. 非法 `mesh.material_overrides`  
   忽略该 override，继续使用 section 默认材质解析结果

2. 非法 `ModelMaterialReference`  
   忽略该显式引用，继续走该 slot 的 generated V2 instance

3. generated/imported 材质生成失败  
   回退到默认实例材质 `Engine/Materials/V2/MI_DefaultSurface.AshMatIns`

4. 最终兜底  
   永远只允许回退到 V2 默认实例，不再回退到任何 V1 资产

### 5.4 错误日志口径

日志必须明确指出：

- 资产路径
- 解析结果类型
- 违反的规则
- 实际回退目标

推荐文案风格：

```text
RenderAssetManager: material '<path>' resolved to base material 'Material'.
Only '.AshMatIns' material instances can be assigned directly to mesh sections.
Falling back to 'Engine/Materials/V2/MI_DefaultSurface.AshMatIns'.
```

## 6. V2 默认基材与默认实例

### 6.1 新的内建基材路径

引擎内建的通用 PBR 基材统一改为：

- `Engine/Materials/V2/M_SurfacePBR.AshMat`

它是**基材**，不能直接赋给物体。

### 6.2 新的默认实例路径

所有物体级 fallback、默认 section 材质和无材质兜底路径统一改为：

- `Engine/Materials/V2/MI_DefaultSurface.AshMatIns`

它是**实例材质**，允许直接赋给物体。

### 6.3 旧内建路径退场

以下旧路径整体删除：

- `Engine/Materials/M_SurfacePBR.material`
- `Engine/Materials/M_DefaultSurface.material`
- `Engine/Materials/V2/M_DefaultSurface.AshMat`

其中：

- 旧 V1 `M_SurfacePBR.material` 与 `M_DefaultSurface.material` 彻底退场
- 现有 V2 `M_DefaultSurface.AshMat` 的职责被新的 `M_SurfacePBR.AshMat` 和 `MI_DefaultSurface.AshMatIns` 取代

## 7. 统一的 V2 `SurfacePBR` 基材契约

### 7.1 设计目标

当前旧 runtime generated material 路径实际上承载的是一套“导入材质通用 PBR 语义”：

- `BaseColorFactor`
- `Metallic`
- `Roughness`
- `EmissiveColor`
- `BaseColorTexture`
- `NormalTexture`
- `MetallicRoughnessTexture`
- `EmissiveTexture`

本次不再保留旧 `SceneSurfacePBR` shader，而是把这套语义正式收敛到一个 V2 基材 `M_SurfacePBR.AshMat`。

### 7.2 静态状态

`M_SurfacePBR.AshMat` 默认静态状态固定为：

- `domain = Surface`
- `blendMode = Opaque`
- `twoSided = false`
- `cullMode = Back`
- `depthWrite = true`
- `depthTest = LessEqual`
- `alphaCutoff = 0.5`

### 7.3 参数

`M_SurfacePBR.AshMat` 暴露的参数固定为：

- `BaseColorFactor : float4`
- `Metallic : float`
- `Roughness : float`
- `EmissiveColor : float4`

### 7.4 资源

`M_SurfacePBR.AshMat` 暴露的资源固定为：

- `BaseColorTexture`
  - `Texture2D`
  - `sRGB`

- `NormalTexture`
  - `Texture2D`
  - `Linear`

- `MetallicRoughnessTexture`
  - `Texture2D`
  - `Linear`

- `EmissiveTexture`
  - `Texture2D`
  - `sRGB`

### 7.5 shader 行为

配套引擎内建 `MaterialShader` 例如：

- `project/src/engine/Shaders/MaterialV2/Materials/M_SurfacePBR.hlsl`

它至少完成以下语义：

- `baseColor = BaseColorTexture * BaseColorFactor`
- `normal_ts = decode(NormalTexture)`
- `metallic = Metallic * MetallicRoughnessTexture.B`
- `roughness = Roughness * MetallicRoughnessTexture.G`
- `emissive = EmissiveTexture * EmissiveColor.rgb`
- `opacity = baseColor.a`
- `opacity_mask = baseColor.a`
- `ambient_occlusion = 1.0`

当前不单独提供 AO 贴图输入，因为现有 `MaterialSlot` 不携带这条导入数据。

### 7.6 样例材质定位

当前的：

- `project/src/sandbox/Shaders/MaterialV2/M_V2_DebugSurface.hlsl`
- `product/assets/materials/v2/M_V2_DebugSurface.AshMat`

保留为测试/样例资产，不再承担引擎默认基材职责。

## 8. 导入材质与 generated material 全量迁到 V2

### 8.1 generated material 的运行时对象

`RenderAssetManager::request_generated_material_asset(...)` 继续保留，但其产物统一变成：

- `MaterialInstance`
- `parent = Engine/Materials/V2/M_SurfacePBR.AshMat`

不再引用任何 V1 基材。

### 8.2 generated material 的虚拟路径后缀

当前 generated material key 是：

```text
__generated__/materials/<normalized-asset>#slot=<n>.material
```

本次统一改为：

```text
__generated__/materials/<normalized-asset>#slot=<n>.AshMatIns
```

这样 generated material 在：

- 缓存 key
- debug 输出
- 运行时类型语义

三个层面都明确表示“它是可绑定的实例材质”。

### 8.3 `MaterialSlot` 到 V2 instance 的映射

现有导入链路中的 `MaterialSlot` 字段保持不变，直接映射为 `M_SurfacePBR.AshMat` 的 instance override：

- `base_color_factor -> BaseColorFactor`
- `metallic_factor -> Metallic`
- `roughness_factor -> Roughness`
- `emissive_factor -> EmissiveColor`
- `base_color_texture -> BaseColorTexture`
- `normal_texture -> NormalTexture`
- `metallic_roughness_texture -> MetallicRoughnessTexture`
- `emissive_texture -> EmissiveTexture`

本次不再保留旧 runtime generated path 中独立的 `OpacityMask` override 命名。

### 8.4 导入静态状态的当前边界

当前 `MaterialSlot` 并不携带：

- `blendMode`
- `twoSided`
- `alphaCutoff`

因此导入生成的 V2 instance 在本次变更中：

- 静态状态全部继承自 `M_SurfacePBR.AshMat`
- 仍以 `Opaque / one-sided / alphaCutoff=0.5` 为默认行为

若后续需要从 glTF/FBX 正式映射 masked、transparent 或 two-sided，需要单独扩展 `MaterialSlot` 和导入路径，这不属于本次 V1 退场的必需范围。

## 9. Sampler 继承规则调整

### 9.1 当前问题

当前 `MaterialInstance::get_sampler_definitions()` 的规则是：

- 只要实例本地有 sampler 定义，就完全覆盖父材质 sampler 列表

这对 V2 基材不够健壮，因为：

- 基材会提供默认 sampler
- 导入或手填实例可能只想覆盖其中 1 个 sampler
- 完全覆盖会导致父材质默认 sampler 丢失

### 9.2 新规则

实例的 sampler 定义改为：

- 父材质 sampler 定义为基础集合
- 实例同名 sampler 覆盖父材质
- 实例新增 sampler 追加到集合

即：

- `Material` 决定默认 sampler 集合
- `MaterialInstance` 只做局部覆盖或补充

### 9.3 对 `SurfacePBR` 的意义

这样 `M_SurfacePBR.AshMat` 可以稳定提供：

- 一套默认 linear wrap sampler
- 必要时再提供 normal/linear 专用 sampler

而导入生成实例只需覆盖它实际需要修改的 sampler，不会破坏父材质默认资源绑定完整性。

## 10. 需要删除的 V1 代码与资源

### 10.1 资产与解析

删除以下内容：

- `.material` / `.mat` 扩展名识别
- `version = 1` 解析/保存分支
- legacy fixed-PBR schema 读写
- V1 内建材质路径与 factory

### 10.2 运行时兼容路径

删除以下旧链路：

- `MaterialRenderProxy` 的 legacy fixed-PBR 分支
- `SceneSurfacePBR.hlsl` 以及围绕它的 program 构建逻辑
- legacy `MaterialUniformData`、旧 sampler 宏编译链路
- 任何“先试 V2，再退回 V1”式的兼容 fallback

### 10.3 历史语义残留

删除或重构以下仅服务 V1 的概念：

- `MaterialFixedPBRSurfaceInputs`
- 仅为 V1 parameter/texture 命名兼容服务的辅助逻辑
- 旧默认材质路径常量
- 对 V1 导入 generated material 的命名和 fallback 分支

### 10.4 保留的核心对象

以下对象保留，但进入 V2-only 状态：

- `MaterialInterface`
- `Material`
- `MaterialInstance`
- `MaterialSystem`
- `MaterialShaderMap`
- `MaterialRenderProxy`

## 11. 迁移范围

### 11.1 必须同步改名的现有资产

至少包括：

- `product/assets/materials/v2/MI_V2_DebugSurface_Tint.AshMat`
  - 改为 `product/assets/materials/v2/MI_V2_DebugSurface_Tint.AshMatIns`

基材：

- `product/assets/materials/v2/M_V2_DebugSurface.AshMat`
  - 保持不变

### 11.2 必须同步改路径的代码

至少包括：

- Sandbox 注入测试材质路径
- 默认材质路径常量
- generated material key
- 所有 section/material fallback 路径
- 所有 built-in material factory path

### 11.3 breaking change 声明

本次变更是明确的 breaking change：

- 旧 `.material/.mat` 资产不再可加载
- 旧 `.AshMat` 的 `MaterialInstance` 资产全部失效，必须改名为 `.AshMatIns`
- 任何直接把基材挂到物体上的数据都会在运行时被拒绝并回退

不提供运行时兼容过渡期。

## 12. 验证要求

### 12.1 构建验证

至少验证：

- `Engine`
- `Sandbox`
- `Editor`

### 12.2 运行验证矩阵

必须按共享渲染路径标准验证：

- `Sandbox + Vulkan`
- `Sandbox + DX12`
- `Editor + Vulkan`
- `Editor + DX12`

### 12.3 功能验证点

至少覆盖以下结果：

1. `M_SurfacePBR.AshMat` 和 `MI_DefaultSurface.AshMatIns` 能被正确构建并参与 fallback
2. 运行时 generated/imported 材质全部变为 V2 `MaterialInstance`
3. `MaterialInstance(.AshMatIns)` 直接赋给物体正常工作
4. `Material(.AshMat)` 直接赋给物体时被明确拒绝并回退
5. Sandbox 的手填测试实例资产改名后仍能命中 V2 绘制链路
6. Vulkan 与 DX12 下没有 legacy V1 分支残留导致的绑定或反射错误

### 12.4 失败判定

以下任一出现都视为未完成：

- `.material/.mat` 仍能被当作合法材质资产加载
- `.AshMat` 的 `MaterialInstance` 仍被接受
- 物体绑定入口仍能直接使用基材
- `MaterialRenderProxy` 仍保留 legacy fixed-PBR 渲染分支
- 默认 fallback 仍指向任何 V1 资产
- 导入生成材质仍产出 `.material` 风格虚拟路径
- Vulkan 或 DX12 验证出现新的 shader 绑定/反射错误

## 13. 最终范围结论

本设计把材质系统收敛到以下最终状态：

- **只有 V2**
- **只有 `version = 2`**
- **只有 `.AshMat` 与 `.AshMatIns` 两种 V2 后缀**
- **只有 `MaterialInstance(.AshMatIns)` 可以直接赋给物体**
- **只有 V2 默认实例承担最终 fallback**

引擎侧的统一方向是：

- 以 `M_SurfacePBR.AshMat` 承担导入材质通用语义
- 以 `MI_DefaultSurface.AshMatIns` 承担物体级默认兜底
- 以 generated V2 instance 承担模型材质槽运行时实例化
- 彻底删除 V1 资产、V1 shader、V1 runtime compatibility path

这是后续继续扩展：

- `Surface.Transparent`
- `Surface.SkeletalMesh`
- `Decal`

所需的前置清理步骤。
