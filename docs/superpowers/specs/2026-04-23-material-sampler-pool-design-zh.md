# 材质级 Sampler 与全局 Sampler 池设计

## 背景

当前 Engine 材质链路已经支持：

- 材质参数的标量、向量、贴图路径存储
- `RenderAssetManager` 负责解析贴图并创建 `TextureAsset`
- `MaterialRenderProxy` 在渲染前为每个材质生成独立 `GraphicsProgram` 并绑定贴图

但当前采样器语义存在两个根本问题：

1. `MaterialRenderProxy` 把场景 PBR shader 的 `LinearWrapSampler` 写死绑定到 `RenderSamplerState::Default`
2. `RenderSamplerState::Default` 在 DX12 和 Vulkan 下语义不一致

当前真实行为：

- DX12 默认 sampler：`Repeat + Linear min/mag/mip`
- Vulkan 默认 sampler：`ClampToEdge + Linear min/mag/mip`

因此同一份材质在双端并不共享同一采样语义。墙体这类需要重复采样的贴图在 DX12 下正常、在 Vulkan 下错误，属于该结构问题的直接表现。

此外，当前材质和导入数据只存贴图路径，不存 sampler 定义：

- `.material` / `.mat` 的 texture 参数默认值和 override 只有字符串路径
- `AssetData::MaterialSlot` 只有 `*_texture_path`
- glTF 导入只解析 texture index，不解析 sampler `wrapS / wrapT / minFilter / magFilter`

这导致：

- 材质不能显式声明“这张贴图要 repeat / clamp”
- `RenderAssetManager` 无法建立共享 sampler 缓存
- Vulkan 与 DX12 只能各自依赖 backend 默认值，语义继续漂移

## 目标

本设计的目标是：

- 在 Engine Function 层引入统一的材质级 sampler 描述，不把后端细节泄露到材质系统
- 为每个 texture binding 显式保存“贴图路径 + sampler 定义”
- 提供一个 Engine 全局 sampler 池，缓存已创建 sampler，生命周期贯穿整个进程
- 没有显式 sampler 定义的旧材质，统一走共享默认 sampler
- 默认 sampler 的地址模式统一为 `Repeat`
- 由材质系统在加载/更新材质绑定时解析 sampler，并通过池复用
- DX12 与 Vulkan 共享同一套高层材质与 sampler 语义

## 非目标

本设计明确不做以下事项：

- 不修改 `project/src/editor`
- 不把 Vulkan / DX12 类型直接暴露到 `Function/Render`
- 不在 V1 中引入完整的 RHI 原始 `SamplerCreation` 直通接口
- 不在 V1 中处理 comparison sampler、reduction sampler、PCF 阴影等专用采样器语义
- 不在 V1 中把 texture runtime 资产体系整体上提到 `AssetDatabase`

## 设计概览

推荐方案是在 `Function/Render` 引入三层新能力：

1. 材质数据层：为 texture 参数补充 sampler 描述
2. 高层渲染资源层：提供 `RenderSamplerDesc` 与 `RenderSampler`
3. 运行时缓存层：在 `RenderAssetManager` 中维护全局 sampler 池

整体数据流如下：

1. 材质文件、导入材质、运行时生成材质都写入 `MaterialTextureBinding`
2. `MaterialRenderProxy::update_bindings(...)` 读取每个 texture binding 的 sampler 信息
3. `RenderAssetManager` 用 `RenderSamplerDesc` 查全局池
4. 池命中则复用；未命中则通过 `RenderDevice` 创建新的高层 sampler
5. `MaterialRenderProxy` 把 texture 与 sampler 一起绑定到 `GraphicsProgram`

该设计保持“材质语义在 Function 层、后端创建在 Graphics 层”的边界不变。

## 核心数据结构

### 1. 高层 sampler 描述

在 `Function/Render` 定义高层枚举和描述结构：

- `RenderSamplerAddressMode`
  - `Repeat`
  - `MirroredRepeat`
  - `ClampToEdge`
  - `ClampToBorder`
  - `MirrorClampToEdge`
- `RenderSamplerFilter`
  - `Nearest`
  - `Linear`
- `RenderSamplerDesc`
  - `address_u`
  - `address_v`
  - `address_w`
  - `min_filter`
  - `mag_filter`
  - `mip_filter`

V1 不把 anisotropy、compare、border color 暴露到材质系统。原因是当前需求只覆盖普通 color/normal/PBR 贴图采样，而 Vulkan 设备特性启用路径里也没有现成的 sampler-anisotropy 统一保证。先把材质真正需要、且双端必须一致的字段收口到地址模式和基础过滤。

### 2. 材质贴图绑定结构

新增：

- `MaterialTextureBinding`
  - `std::string texture_path`
  - `bool has_explicit_sampler`
  - `RenderSamplerDesc sampler`

语义：

- `texture_path` 为空表示没有显式贴图，仍然按当前逻辑回退到 fallback texture
- `has_explicit_sampler == false` 表示该贴图使用 Engine 共享默认 sampler
- `has_explicit_sampler == true` 时，使用 `sampler` 中定义的显式采样器

### 3. 默认 sampler 规则

Engine 统一默认 sampler 定义为：

- `address_u = Repeat`
- `address_v = Repeat`
- `address_w = Repeat`
- `min_filter = Linear`
- `mag_filter = Linear`
- `mip_filter = Linear`

这是所有“没有显式 sampler 定义”的材质、导入材质和旧版材质文件的默认行为。

## 材质系统改动

### 1. `MaterialParameterDesc`

当前 `Texture` 参数的默认值是 `default_texture_path` 字符串。改为：

- `MaterialTextureBinding default_texture`

`Scalar` 和 `Vector4` 参数保持现状不变。

### 2. `MaterialInterface`

当前接口：

- `try_get_texture_parameter(const std::string& name, std::string& out_path) const`

改为：

- `try_get_texture_parameter(const std::string& name, MaterialTextureBinding& out_binding) const`

理由：

- texture 与 sampler 必须作为一个整体参数读取
- 继续只返回 path 会迫使 sampler 走平行存储，违背“贴图下面再存一个结构”的要求

### 3. `Material`

`Material` 对 `Texture` 参数的默认值存储为 `MaterialTextureBinding`。基础材质可以为每个 texture 参数提供：

- 默认贴图路径
- 可选显式 sampler

若未写 sampler，则按共享默认 sampler 解释。

### 4. `MaterialInstance`

当前 `MaterialInstance` 的 texture override 是：

- `std::unordered_map<std::string, std::string> m_texture_overrides`

改为：

- `std::unordered_map<std::string, MaterialTextureBinding> m_texture_overrides`

这样实例材质可以做到：

- 覆盖贴图，但沿用父材质 sampler
- 覆盖贴图并同时覆盖 sampler
- 表达“路径不变、仅 sampler 变化”的最终绑定结果

V1 为了降低复杂度，约定 texture override 作为整体值处理：

- 若实例材质为某个 texture 参数写了 override，则该 override 同时覆盖该参数的 path 和 sampler 状态
- 如果只想改 sampler 而不改 path，需要在 override 中把 path 也写出来

该约定简单、可序列化、可预测，后续如有需要再扩展成分离覆盖模型。

## 导入与运行时生成材质改动

### 1. `AssetData::MaterialSlot`

当前 `MaterialSlot` 中的：

- `base_color_texture_path`
- `normal_texture_path`
- `metallic_roughness_texture_path`
- `emissive_texture_path`

改为：

- `MaterialTextureBinding base_color_texture`
- `MaterialTextureBinding normal_texture`
- `MaterialTextureBinding metallic_roughness_texture`
- `MaterialTextureBinding emissive_texture`

### 2. glTF 导入

glTF 的每个 texture 引用都可能指向一个 sampler。导入时：

- 从 `material.*Texture.index` 找到 texture source path
- 从 texture 的 `sampler` 索引读取：
  - `wrapS -> address_u`
  - `wrapT -> address_v`
  - `address_w` 默认 `Repeat`
  - `magFilter -> mag_filter`
  - `minFilter -> min_filter + mip_filter`

映射原则：

- 缺失 sampler 索引：`has_explicit_sampler = false`
- `wrapS / wrapT` 缺失：使用 glTF 默认 `Repeat`
- `magFilter` 缺失：使用 glTF 默认 `Linear`
- `minFilter` 缺失：使用 glTF 默认 `Linear + Linear mip`

不支持的 glTF sampler 枚举值：

- 归一化到最接近的 V1 高层枚举
- 记录一次 warning
- 不阻塞材质导入

### 3. OBJ / FBX 导入

OBJ / FBX 当前导入链路没有稳定的通用 sampler 来源，因此 V1 规则为：

- 只导入贴图路径
- `has_explicit_sampler = false`
- 运行时统一使用共享默认 sampler

### 4. 运行时自动生成材质

`RenderAssetManager::request_generated_material_asset(...)` 生成的材质实例需要把 `MaterialSlot` 中的 `MaterialTextureBinding` 原样拷入实例 override，而不是只拷路径。

## 全局 sampler 池设计

### 1. 池的归属

全局 sampler 池放在 `RenderAssetManager`。

原因：

- 这是当前 Engine 的 render-asset 统一入口
- 贴图、材质代理、自动生成材质都已经在这里收口
- sampler 池本质上是“材质运行时资源缓存”，和 `TextureAsset` 缓存属于同一层职责

### 2. 池的 key

池的 key 使用归一化后的 `RenderSamplerDesc`。

要求：

- 实现稳定的 `operator==`
- 实现稳定 hash
- 不把 debug name 纳入 key

这样具有相同采样语义的材质会共享同一个 sampler 对象。

### 3. 生命周期

池内 sampler 为进程级缓存：

- 创建后常驻
- 不做中途回收
- `RenderAssetManager::shutdown()` 或程序退出时统一释放

该策略符合本次需求，也避免复杂的引用计数抖动与跨帧析构时序问题。

## 高层渲染资源设计

### 1. 新增 `RenderSampler`

为了保持 Engine / RHI 边界，在 `Function/Render` 增加高层包装对象：

- `RenderSampler`

职责与现有 `RenderTarget` / `UniformBuffer` 一致：

- 对 Function 层与材质系统隐藏 `RHI::Sampler`
- 由 `RenderDevice` 创建
- 由 `GraphicsProgram` / `ComputeProgram` 绑定

### 2. 新增 `RenderDevice::create_sampler(...)`

新增：

- `std::shared_ptr<RenderSampler> create_sampler(const RenderSamplerDesc& desc);`

`Renderer` 提供同名透传接口，供 `RenderAssetManager` 使用。

### 3. RHI 映射

`RenderDevice` 负责把 `RenderSamplerDesc` 转换为 RHI `SamplerCreation`：

- 高层地址模式映射到 `AshSamplerAddressMode`
- 高层过滤映射到 `AshFilter`

这一步是共享映射逻辑，必须在 DX12/Vulkan 上走同一套高层输入。

## `GraphicsProgram` 绑定改动

当前高层接口：

- `set_sampler(const char* name, RenderSamplerState sampler_state = RenderSamplerState::Default)`

改为同时支持对象绑定：

- `set_sampler(const char* name, const std::shared_ptr<RenderSampler>& sampler)`
- `set_sampler_array(const char* name, const std::vector<std::shared_ptr<RenderSampler>>& samplers)`

`RenderSamplerState` 及其枚举式默认 sampler 路径不再作为材质系统主路径。

兼容策略：

- 短期保留 `RenderSamplerState` 接口给其他旧调用点
- 其 `Default` 语义统一改成 `Repeat + Linear + Linear mip`
- 材质系统改用对象绑定，不再依赖 backend 内置 default sampler

这样做可以：

- 先解决 Vulkan / DX12 旧默认值不一致
- 再让材质系统完全切到显式 sampler 对象

## `MaterialRenderProxy` 改动

`MaterialRenderProxy` 不再只有 4 张 texture，还需要同时持有对应 sampler：

- `m_base_color_texture` + `m_base_color_sampler`
- `m_normal_texture` + `m_normal_sampler`
- `m_metallic_roughness_texture` + `m_metallic_roughness_sampler`
- `m_emissive_texture` + `m_emissive_texture_sampler`

`update_bindings(...)` 的新流程：

1. 解析每个 `MaterialTextureBinding`
2. 请求 texture 资源
3. 计算该 binding 最终要使用的 `RenderSamplerDesc`
4. 向 `RenderAssetManager` 请求全局 sampler
5. 更新 uniform / texture flags
6. 在 `bind_program_resources()` 中绑定 texture 与 sampler

shader 侧当前只有一个 `LinearWrapSampler`。V1 有两种实现选择：

- 保持 shader 不变，四张贴图统一绑定同一个 sampler
- shader 增加四个 sampler slot，让每张贴图使用自己的 sampler

V1 采用第二种。

原因：

- 用户要求是“每张贴图需要什么采样器，可以定义在材质里面”
- 如果 shader 仍只有一个 sampler，就只能全材质共享一个 sampler，达不到每张贴图独立定义

因此 `SceneSurfacePBR.hlsl` 中将为：

- `BaseColorTexture` 使用独立 sampler
- `NormalTexture` 使用独立 sampler
- `MetallicRoughnessTexture` 使用独立 sampler
- `EmissiveTexture` 使用独立 sampler

## 材质文件序列化兼容

### 1. 新格式

Texture 参数默认值和 instance override 在 JSON 中使用对象：

```json
{
  "path": "product/assets/textures/wall_albedo.png",
  "sampler": {
    "address_u": "Repeat",
    "address_v": "Repeat",
    "address_w": "Repeat",
    "min_filter": "Linear",
    "mag_filter": "Linear",
    "mip_filter": "Linear"
  }
}
```

如果 `sampler` 缺失，则表示没有显式 sampler，走共享默认 sampler。

### 2. 旧格式兼容

旧材质文件中 texture 默认值或 override 若仍是字符串：

- 直接解释为 `MaterialTextureBinding`
- `texture_path = <string>`
- `has_explicit_sampler = false`

因此旧工程无需迁移即可继续加载，且默认会获得新的 `Repeat` 共享默认 sampler。

## Vulkan / DX12 一致性要求

本设计实施后，必须满足以下语义：

1. 相同 `RenderSamplerDesc` 在 DX12 和 Vulkan 下创建出的 sampler 地址模式一致
2. 没有显式 sampler 的材质，DX12 和 Vulkan 都走 `Repeat` 默认 sampler
3. `MaterialRenderProxy` 不再依赖 backend 内置“默认 sampler 是什么”
4. Vulkan 旧 `ASH_SAMPLER_STATE_DEFAULT` 的 `ClampToEdge` 实现必须修正，避免旧路径继续漂移

## 调试与错误处理

### 1. 解析失败

若材质文件中的 sampler 字段非法：

- 记录一次明确错误
- 整个材质加载失败，保持与当前材质 JSON 解析错误一致的处理级别

### 2. 导入侧 unsupported sampler

若 glTF sampler 枚举超出 V1 映射范围：

- 记录 warning 一次
- 归一化到最近的高层支持值
- 不阻塞导入

### 3. 运行时创建失败

若 sampler 池 miss 后创建失败：

- 记录错误
- 回退到共享默认 sampler
- 若默认 sampler 也创建失败，则视为材质绑定失败

## 验证要求

实现完成后必须完成以下验证：

1. 构建通过
2. 旧材质文件仍可加载
3. glTF 导入的重复贴图在 Vulkan 下表现正确
4. DX12 与 Vulkan 对同一材质的 wrap 行为一致
5. 没有显式 sampler 的材质走 `Repeat`
6. 显式声明 `ClampToEdge` 的材质在两端都生效
7. `Sandbox + Vulkan`
8. `Sandbox + DX12`
9. `Editor + Vulkan`
10. `Editor + DX12`

重点人工验证样例：

- 墙体/地面这类需要 repeat 的 base color 贴图
- normal / metallic-roughness 贴图与 base color 使用不同 sampler 的情况
- 没有 texture 的 fallback 材质

## 风险与约束

### 1. Shader 绑定变化

从“单 sampler”切到“每张贴图一个 sampler”会改变 shader 反射结果和 program 绑定数量，属于共享渲染路径改动，必须双端验证。

### 2. 材质接口签名变化

`try_get_texture_parameter(...)` 返回类型变化会影响：

- `Material`
- `MaterialInstance`
- `MaterialRenderProxy`
- 材质文件加载/保存
- 自动生成材质

需要一次性改全，避免遗留旧字符串路径接口。

### 3. Vulkan 采样器 debug name 生命周期

当前 Vulkan sampler 实现直接保存 `const char*` 名字。动态 sampler 池启用后，debug name 不能继续依赖外部临时字符串，必须把名称生命周期收口到 sampler 对象内部，避免悬空指针。

## 最终结论

采用“材质级 texture binding + 全局 sampler 池 + 高层 `RenderSampler` 包装”的方案。

关键结论如下：

- 采样器定义属于材质语义，放在 `Function/Render`
- 全局池属于运行时 render asset 缓存，放在 `RenderAssetManager`
- 后端只负责消费统一的 `RenderSamplerDesc`
- 没有显式 sampler 的材质统一走 `Repeat` 默认 sampler
- Vulkan 现有错误 default 实现必须先被修正，作为兼容基线的一部分
