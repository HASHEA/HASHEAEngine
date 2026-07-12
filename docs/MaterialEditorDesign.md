# 材质编辑器设计文档

> 目标：基于 AshEngine 现有 V2 材质运行时，实现一个接近 UE 使用体验的节点化材质编辑器，并支持实时预览、参数化、材质函数与后续扩展。

## 1. 现状判断

当前引擎已经具备可复用的材质运行时主干：

- `MaterialInterface / Material / MaterialInstance`
- `MaterialSystem`
- `MaterialShaderMap`
- `MaterialRenderProxy`
- 运行时材质资产：`.AshMat / .AshMatIns`
- 运行时 shader 组合方式：`host shader + material shader + generated bindings`

这意味着材质编辑器不应该推翻运行时，而应该新增一层作者工具与编译器：

- 编辑器负责图编辑、参数组织、错误提示、预览与编译触发
- 编译层负责把节点图翻译为材质 HLSL 和 `.AshMat`
- 运行时继续消费现有 V2 材质对象

## 2. 产品目标

第一阶段目标：

- 支持 `Surface` 域的节点化材质编辑
- 支持 `Opaque / Masked / Transparent`
- 支持 `DefaultLit / Unlit`
- 支持常用 UE 风格节点工作流
- 支持实时预览和错误定位
- 支持图源码与运行时产物分离

长期目标：

- 材质函数
- 材质属性打包
- 更完整的节点库
- 更强的调试视图
- 未来扩展到 `Decal / PostProcess / UI`

明确非目标：

- 第一版不追求 100% UE 节点覆盖
- 第一版不重写引擎材质运行时
- 第一版不做独立进程材质工具

## 3. 核心设计原则

### 3.1 源资产与编译产物分离

建议引入两类资产：

- `*.AshMatGraph`
  - 节点图源码
  - 只给编辑器和编译器使用
- `*.AshMat`
  - 编译后的运行时基材质
- `*.AshMatIns`
  - 继续沿用现有实例材质
- `*.generated.hlsl`
  - 节点图生成的用户材质 shader

这样可以保证：

- runtime 不依赖节点编辑器
- shader cache、diff、打包和回滚都更清晰
- 未来即使更换节点 UI，也不影响运行时材质格式

### 3.2 节点数据与 shader 逻辑分离

必须分开三层：

- 节点数据
  - 图里存什么
- 节点语义
  - 这个节点表达什么材质含义
- shader 生成
  - 如何把节点语义编译成 IR / HLSL

不要把 UI、序列化、类型检查、HLSL 拼接都塞进单个节点类。

### 3.3 预览走 ScenePresentationSubsystem

材质预览应复用当前 scene-driven 离屏输出路径：

- 每个材质编辑器维护一个 preview scene
- 预览输出使用 `Offscreen output + UISurfaceHandle`
- 编辑器 UI 只显示 `UISurfaceHandle`
- 不让上层面板直接持有 `RenderTarget`

这样可以和现有 Editor 视口体系保持一致，也更容易扩展到多个预览窗口。

## 4. 窗口与交互设计

建议材质编辑器支持：

- Dock 内嵌打开
- 弹出为独立顶层窗口
- 同进程共享项目、渲染器、资产系统和日志系统

第一版不建议做成独立进程。更合适的路线是：

- 逻辑独立
- 窗口可分离
- 进程先不分离

推荐主界面区域：

- Graph Canvas
  - 节点图、连线、注释、reroute
- Details Panel
  - 当前节点属性、参数元数据、默认值、资源选择
- Preview Viewport
  - 球、平面、立方体、自定义网格预览
- Palette / Search
  - 节点分类、搜索、最近使用
- Stats / Errors
  - 编译错误、指令统计、纹理采样数、shader 路径

## 5. 用户工作流

### 5.1 创建材质图

- 新建 `Material Graph`
- 选择 `Domain / Blend Mode / Shading Model`
- 默认生成 `Output` 节点和基础预览场景

### 5.2 搭建节点网络

- 从 Palette 拖入节点
- 支持搜索节点
- 支持常用快捷操作：
  - 拖线生成候选节点
  - 右键空白快速创建
  - reroute
  - comment box

### 5.3 实时编译与预览

- 图变更后进行 debounce 编译
- 编译成功：
  - 更新预览材质
  - 刷新 stats
- 编译失败：
  - 保留上一版可用预览
  - 面板显示错误并定位到节点或 pin

### 5.4 参数与实例工作流

- 参数节点自动生成材质参数描述
- 参数可配置：
  - 名字
  - 分组
  - 默认值
  - UI 范围
  - 是否暴露给实例
- 未来材质实例编辑器继续面向 `.AshMatIns`

## 6. 节点范围规划

第一阶段建议覆盖高频节点：

- 常量类
  - Scalar / Vector2 / Vector3 / Vector4
- 参数类
  - ScalarParameter / VectorParameter / TextureParameter / StaticSwitchParameter
- 纹理类
  - TextureObject / TextureSample / TexCoord
- 数学类
  - Add / Subtract / Multiply / Divide / Lerp / Clamp / Saturate / OneMinus / Power / Min / Max / Abs
- 向量类
  - Dot / Cross / Normalize / Append / ComponentMask / BreakOut
- 工具类
  - Time / Panner / Rotator / WorldPosition / ObjectPosition / CameraVector / VertexColor
- 输出类
  - BaseColor / Metallic / Roughness / Normal / Emissive / Opacity / OpacityMask / AO / WPO
- 自定义类（关键 escape-hatch）
  - Custom Expression（用户直接编写 HLSL 代码片段）

第二阶段再补：

- MaterialAttributes 类
  - MakeMaterialAttributes / BreakMaterialAttributes / BlendMaterialAttributes
- FunctionCall（前提：MaterialFunctionLibrary + 循环检测已实现）
- 材质函数资产（`.AshMatFunc`）
- 更完整的调试和通道预览
- Reroute 增强（类型标注）

第三阶段再考虑：

- Decal / PostProcess / UI
- Material Layer Stack
- 更高级的渲染特性节点
- Noise / Procedural 节点
- 大气 / 体积材质节点

## 7. 实时预览设计

预览要求：

- 预览场景和主场景隔离
- 预览 mesh 可切换
- 支持光照与非光照模式
- 支持常见通道查看：
  - Lit
  - Unlit
  - BaseColor
  - Normal
  - Roughness
  - Metallic
  - AO
  - Opacity

建议行为：

- 使用独立 preview scene
- 用临时 preview material instance 承接当前编译结果
- 保留最近一次成功编译结果，避免报错时黑屏
- compile debounce 建议 `150ms ~ 300ms`

## 8. 材质层与混合设计

### 8.1 MaterialAttributes 虚类型

`MaterialAttributes` 是一个仅存在于编辑器和 IR 层的虚类型，表示完整的表面属性集合（BaseColor + Metallic + Roughness + Normal + ...）。它不产生实际 HLSL struct，编译时被展开为 `AshPixelMainNode` 各字段的独立赋值。

### 8.2 核心节点

- `MakeMaterialAttributes`
  - 输入：所有表面通道（BaseColor: Float3, Metallic: Float1, Roughness: Float1, Normal: Float3, Emissive: Float3, Opacity: Float1, AO: Float1）
  - 输出：MaterialAttributes
  - 作用：将独立通道打包为统一的材质属性集

- `BreakMaterialAttributes`
  - 输入：MaterialAttributes
  - 输出：各通道独立 pin
  - 作用：解包材质属性集为独立通道

- `BlendMaterialAttributes`
  - 输入：Base（MaterialAttributes）、Layer（MaterialAttributes）、Alpha（Float1）
  - 输出：MaterialAttributes
  - 可选属性：BlendFunction（指定混合函数资产路径）
  - 默认行为：逐通道 lerp

### 8.3 材质层工作流

材质层函数是输出类型为 `MaterialAttributes` 的普通材质函数（`.AshMatFunc`）。工作流：

1. 创建材质函数，输出为 MaterialAttributes
2. 函数内部自由搭建节点图，输出接入 MakeMaterialAttributes
3. 在主材质图中使用 FunctionCall 节点引用该层函数
4. 多个层通过 BlendMaterialAttributes 级联混合
5. 最终 MaterialAttributes 连接到 Output 节点

### 8.4 材质层栈（Phase 3 概念）

```cpp
struct MaterialLayerStackEntry
{
    std::string layerFunctionPath{};
    std::string blendFunctionPath{};
    bool enabled = true;
    float blendWeight = 1.0f;
    std::vector<MaterialParameterMetadata> layerParameters{};
};

struct MaterialLayerStack
{
    std::string baseLayerPath{};
    std::vector<MaterialLayerStackEntry> layers{};
};
```

层栈是 Phase 3 的 UI 便捷资产，底层仍编译为 BlendMaterialAttributes 节点链。

## 9. 扩展点 UX 设计

### 9.1 声明式 JSON 节点包

- 美术/TA 可以在不重新编译引擎的情况下扩展节点库
- JSON 节点包放在 `editor/MaterialEditor/NodeDefinitions/` 目录
- 引擎启动时自动扫描加载
- Palette 面板自动显示新增节点（含搜索关键词和分类）
- 节点包可打包分发（项目间共享常用节点集）

### 9.2 Custom Expression 节点

UX 设计：

- Details Panel 显示 HLSL 代码编辑器（多行文本框 + 语法高亮）
- 用户可动态添加/删除输入 pin（PropertyDriven 行为）
- 输出类型由属性下拉框选择
- 编译时直接替换用户 HLSL 代码段
- 编译错误定位到节点级别（无法精确到代码行数）

适用场景：

- 快速原型节点（后续可升级为注册节点）
- 特殊数学函数
- 引擎特有功能调用
- 调试辅助

### 9.3 材质函数库 UI

- 函数资产在 Asset Browser 中可见
- 双击打开函数编辑器（与材质编辑器共享画布和 details 面板）
- 函数 Palette 区分：库函数（全局共享）和项目函数（本项目使用）
- 拖拽函数资产到画布自动创建 FunctionCall 节点
- 函数内部修改后，所有引用该函数的材质自动重编译

### 9.4 插件节点 DLL

面向引擎程序员和高级 TA：

- 实现 `IMaterialNodeModule` 接口
- 编译为 DLL 放入 `plugins/MaterialNodes/`
- 引擎启动时动态加载
- 支持完全自定义的 codegen 逻辑
- 适用场景：公司专有着色器效果、商业中间件集成

## 10. 分阶段落地

### Phase 1：闭环可用

- `AshMatGraph` 资产格式和序列化
- 节点画布（ImGui 或第三方 node editor）
- 基础节点库（常量、参数、纹理、数学、向量、工具、输出）
- Custom Expression 节点（escape-hatch）
- `MaterialNodeRegistryV2` + JSON 声明式节点支持
- `MaterialTypeSystem`（类型提升 + 多态解析）
- Pass-based 编译管线（Validation → TypeResolution → IRGen → Codegen → Output）
- 图编译到 `.AshMat + generated.hlsl`
- 材质预览视口（FullLit 模式）
- 错误定位（节点级 + pin 级诊断）
- Undo/Redo 命令系统
- 基础通道预览（BaseColor / Normal / Roughness）

### Phase 2：可生产使用

- 材质函数（`.AshMatFunc`）+ 函数库 + 循环检测
- MaterialAttributes 类节点（Make / Break / Blend）
- 参数分组和排序 UI
- 编译统计面板
- 完整通道预览（所有通道）
- Per-node 缩略图
- 增量编译
- 优化 pass（ConstantFolding + DCE + CSE）
- 更完整的资源拖拽与实例化入口
- 图 Diff（版本控制辅助）

### Phase 3：工程化增强

- 材质层栈 UI 和资产
- 插件 DLL 节点注册
- 更多域（Decal / PostProcess / UI）
- 多编译后端（GLSL / SPIRV）
- OutputMasking 优化 pass
- 更强的预览控制与调试视图
- 分布式 shader 编译
- 更多 Noise / Procedural 节点
- 材质 LOD 支持

## 11. 主要风险

- 当前运行时真正成熟的是 `Surface.StaticMesh`
- 运行时参数类型目前偏少，第一版需要更多依赖生成 HLSL 而不是运行时富参数系统
- 高级节点和跨域节点不能一次性承诺
- 若图资产与运行时产物边界不清，后续维护成本会快速上升
- ImGui 节点编辑器方案选型（imgui-node-editor vs imnodes vs 自研）影响 Phase 1 工期
- 多态类型解析在复杂图中可能出现歧义（需要明确优先级规则）
- 材质函数循环检测必须在 Phase 1 预留接口，否则 Phase 2 无法平滑接入
- Custom Expression 节点是安全风险点（用户 HLSL 可能导致 crash），需要编译期校验

## 12. 推荐阅读

做产品方案、范围收敛、排期拆解时，先读本文件。

继续往下时再补读：

- `docs/MaterialEditorArchitecture.md`
- `docs/MaterialNodeDataModel.md`
- `docs/EngineDeveloperGuide.md`
- `docs/ScenePresentationSubsystemGuide.md`
