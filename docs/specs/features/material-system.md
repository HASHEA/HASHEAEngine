---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: 材质系统 V2（.AshMat / .AshMatIns）

## 行为

材质 V2 是静态网格 surface 渲染的材质通道：美术/工具产出 `.AshMat` 基材质与 `.AshMatIns` 材质实例（均为 JSON），运行时按「Domain + Family + Pass」拼合生成 HLSL 并经 DXC 编译为 `GraphicsProgram`，供 `SceneRenderer` 的 GBuffer / BasePass / DepthOnly（shadow）静态网格绘制路径使用。同一材质按 `MaterialUsageDesc`（domain × family × pass × capability_mask）产生多个 permutation，编译结果全局缓存。基材质编译失败时回退到 domain fallback（Surface → `materials/v2/MI_DefaultSurface.AshMatIns`，其 parent 为 `materials/v2/M_SurfacePBR.AshMat`）。

## 配置（资产格式）

- `.AshMat`（`"class": "Material"`，`"version": 2`）：`domain`（Surface/…）、`shading_model`（`ggx`/`unlit`/`blinnphong`）、`materialShader`（用户 HLSL 路径，如 `M_SurfacePBR.hlsl`）、`requiredCapabilities`（`VertexColor`/`UV1`）、`staticSwitches`、`renderState`（blendMode/twoSided/cullMode/depthWrite/depthTest/alphaCutoff）、`samplers`（name + shader_sampler_name + 寻址/过滤）、`parameters`（`float`/`float4` + 默认值）、`resources`（Texture2D + sampler + colorSpace）。
- `.AshMatIns`（`"class": "MaterialInstance"`）：`parent`（.AshMat 或另一个 .AshMatIns 路径）+ 可选 scalar/vector/resource override 与本地 sampler 定义；未覆盖项沿 parent 链解析。
- 加载/保存：`load_material_from_file()` / `save_material_to_file()`；内置材质 `make_builtin_material(virtual_path)`。

## 实现

职责链（全部在 `project/src/engine/Function/Render/`）：

| 类 / 文件 | 职责 |
| --- | --- |
| `MaterialInterface` / `Material` / `MaterialInstance`（`Material.h/.cpp`） | 资产层数据模型；`MaterialInterface` 是统一只读接口（domain、blend、shading model、参数/资源/采样器 desc、`get_compile_hash()`、`resolve_base_material()`）；instance 沿 parent 链合并 override |
| `MaterialSystem`（`MaterialSystem.h/.cpp`） | 入口：持有 `EngineShaderFamilyRegistry` + `MaterialShaderMap` + domain fallback 表；`get_or_create_resource(material, usage)` 失败时降级到 fallback 材质 |
| `EngineShaderFamilyRegistry`（`EngineShaderFamilyRegistry.h/.cpp`） | Family 注册表：`EngineShaderFamily::SurfaceStaticMesh` × `PassFamily{DepthOnly, BasePass, GBuffer}` → host shader 路径；校验 usage 与 capability（`MaterialCapability::VertexColor/UV1`） |
| `MaterialShaderMap`（`MaterialShaderMap.h/.cpp`) | permutation 缓存：key = (compile_hash, `MaterialUsageDesc`)；产出 `MaterialResource`（shader 路径三元组、macro、`GraphicsProgramState`、binding layout、parameter block `AshMaterialParameters` 布局、模板 `GraphicsProgram`、`MaterialPassRelevance`）；生成物根目录 `product/caches/ShaderGenerated/Materials` |
| `MaterialShaderSourceBuilder`（`MaterialShaderSourceBuilder.h/.cpp`） | 按 usage 生成 `GeneratedMaterialBindings.hlsli`（`ASH_DOMAIN_SURFACE` / `ASH_ENGINE_FAMILY_*` / `ASH_PASS_*` / GBuffer 布局宏、参数 cbuffer、纹理与采样器声明），返回 include 路径 + combined source hash |
| `MaterialRenderProxy`（`MaterialRenderProxy.h/.cpp`) | 渲染代理：从 MaterialSystem 取三个 pass family（BasePass/DepthOnly/GBuffer）的模板资源，实例化各自 `GraphicsProgram` 与 material uniform buffer；`update_bindings()` 按版本号打包参数、解析纹理（经 `RenderAssetManager`/`TextureAsset`）与 sampler；节流检查 shader 文件签名实现热重载 |
| `RenderAssetManager` | `request_material_asset()` / `request_material_render_proxy()` 缓存资产与代理 |

### Shader 拼合流程

Host shader `Shaders/MaterialV2/Families/SurfaceStaticMesh{BasePass,DepthOnly,GBuffer}.hlsl` 内 include：

1. `Domains/AshSurfaceDomain.hlsli` —— Surface domain 契约：`SurfaceVertexParameters` / `SurfacePixelParameters` / `SurfaceVertexMainNode`（world_position_offset）/ `SurfacePixelMainNode`（base_color、opacity、normal_ts、metallic、roughness、emissive、AO 等）。
2. `Includes/GeneratedMaterialBindings.hlsli` —— 占位文件，DXC include handler 重定向到 `MaterialShaderSourceBuilder` 生成的 per-permutation bindings（经 `GraphicsProgramDesc::generated_bindings_path`）。
3. `Includes/UserShader.hlsli` —— 占位文件，重定向到材质的 `materialShader` HLSL（`GraphicsProgramDesc::user_shader_path`），用户在其中实现 vertex/pixel main node。

程序缓存键含 `source_hash`（材质 compile hash + usage + 生成源文本）与 shader 文件签名 hash，任一变化触发重建。

## 约束与已知限制

- 当前唯一 Family 是 `SurfaceStaticMesh`；Domain 枚举含 Decal/PostProcess/UI 但仅 Surface 有 fallback 与运行路径。
- 资源类型仅 `Texture2D`；参数仅 `float`/`float4`。
- `MaterialResource::program` 是模板（共享编译产物），实例级绑定由 `MaterialRenderProxy` 的独立 program 副本承载。
- GBuffer permutation 固定 `ASH_GBUFFER_LAYOUT_DEFERRED_HQ`，与 `GBufferLayout.cpp` 布局耦合，改布局需同步生成宏。
- 参数 block 名固定 `AshMaterialParameters`（`k_material_parameter_block_name`）。

## 验证

对齐 `docs/VERIFY.md`「渲染 Pass / shader / 材质」行：构建 + `RunRenderGate.bat` + `RunPerfGate.bat -Profile Standard`，检查 `product/logs` 无 validation 报错。`Sandbox.exe --engine-self-test` 覆盖材质资产解析、instance override、shading model、shader map permutation 等 self-test。材质表现异常时用 `[RenderDebugView]` 看 GBuffer 分通道。

## 历史

- `docs/superpowers/specs/2026-04-21-material-system-design-zh.md`（V1 设计）
- `docs/superpowers/specs/2026-04-23-material-sampler-pool-design-zh.md`（sampler 池）
- `docs/superpowers/specs/2026-04-27-material-system-v2-design-zh.md`（V2 设计）
- `docs/superpowers/specs/2026-04-28-material-system-v2-v1-retirement-design-zh.md`（V1 退役）
