# 材质节点数据文档

> 目标：定义材质节点图的核心数据结构、序列化边界、类型系统以及节点定义注册方式。

## 1. 核心原则

材质节点系统至少拆成四层：

1. 图数据
   - 图里实际保存的节点、连线、注释、视图状态
2. 节点定义
   - 节点模板、pin 模板、属性模板、分类信息
3. 节点语义
   - 类型推导、默认规则、常量折叠语义
4. 代码生成
   - IR 和 HLSL 生成

不要让图实例直接携带 shader 生成逻辑。

## 2. ID 类型

建议全部使用稳定强类型 ID：

```cpp
using MaterialNodeId = uint64_t;
using MaterialPinId = uint64_t;
using MaterialLinkId = uint64_t;
using MaterialCommentId = uint64_t;
using MaterialParameterId = uint64_t;
```

要求：

- 图内唯一
- 保存后稳定
- 节点重命名不影响引用关系

## 3. 图资产结构

```cpp
struct MaterialGraphAsset
{
    uint32_t version = 1;

    MaterialGraphHeader header{};
    MaterialGraphSettings settings{};

    std::vector<MaterialNodeInstance> nodes{};
    std::vector<MaterialLink> links{};
    std::vector<MaterialCommentBox> comments{};

    MaterialGraphViewState viewState{};
};
```

### 3.1 Header

```cpp
struct MaterialGraphHeader
{
    std::string name{};
    std::string assetPath{};
    std::string description{};
};
```

### 3.2 Settings

这部分映射图级静态材质属性：

```cpp
struct MaterialGraphSettings
{
    MaterialDomain domain = MaterialDomain::Surface;
    MaterialBlendMode blendMode = MaterialBlendMode::Opaque;
    MaterialShadingModel shadingModel = MaterialShadingModel::DefaultLit;

    bool twoSided = false;
    bool depthWrite = true;
    float alphaCutoff = 0.5f;

    std::vector<std::string> requiredCapabilities{};
    std::vector<MaterialStaticSwitchValue> staticSwitches{};
};
```

图级设置应只保存静态行为，不保存编译缓存或运行时句柄。

## 4. 节点实例结构

```cpp
enum class MaterialNodeKind : uint16_t
{
    Constant,
    Parameter,
    TextureSample,
    Math,
    Utility,
    FunctionCall,
    Output,
    Reroute
};
```

```cpp
struct MaterialNodeInstance
{
    MaterialNodeId id = 0;
    std::string typeName{};
    MaterialNodeKind kind = MaterialNodeKind::Utility;

    std::string title{};
    std::string category{};

    std::vector<MaterialPinInstance> inputPins{};
    std::vector<MaterialPinInstance> outputPins{};

    MaterialPropertyBag properties{};

    MaterialNodeLayout layout{};
    MaterialNodeEditorState editorState{};
};
```

### 4.1 为什么 `typeName` 仍然要保留字符串

因为它适合：

- 序列化
- 节点注册表查找
- 版本迁移
- 编辑器动态扩展

但运行态逻辑不应该只靠自由字符串到处硬编码比较，实际实现里仍建议有注册表和枚举辅助。

### 4.2 Layout / EditorState

```cpp
struct MaterialNodeLayout
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
```

```cpp
struct MaterialNodeEditorState
{
    bool selected = false;
    bool collapsed = false;
    bool previewExpanded = false;
    bool advancedExpanded = false;
};
```

这些状态只属于编辑器，不应进入运行时材质对象。

## 5. Pin 与连线

### 5.1 类型系统

```cpp
enum class MaterialValueType : uint8_t
{
    Unknown,
    Bool,
    Int,
    Float1,
    Float2,
    Float3,
    Float4,
    Texture2D,
    SamplerState,
    MaterialAttributes
};
```

### 5.2 Pin 方向和标记

```cpp
enum class MaterialPinDirection : uint8_t
{
    Input,
    Output
};
```

```cpp
enum class MaterialPinFlags : uint32_t
{
    None           = 0,
    Optional       = 1 << 0,
    Hidden         = 1 << 1,
    MultiInput     = 1 << 2,
    DynamicType    = 1 << 3,
    NoDefaultValue = 1 << 4
};
```

### 5.3 Pin 实例

```cpp
struct MaterialPinInstance
{
    MaterialPinId id = 0;
    std::string name{};
    MaterialPinDirection direction = MaterialPinDirection::Input;

    MaterialValueType declaredType = MaterialValueType::Unknown;
    MaterialValueType resolvedType = MaterialValueType::Unknown;

    uint32_t flags = 0;
    MaterialValue defaultValue{};
};
```

区分：

- `declaredType`
  - 节点模板声明类型
- `resolvedType`
  - 图求值后的实际类型

这对 `Add / Lerp / Append / Reroute` 这类动态 pin 很重要。

### 5.4 连线

```cpp
struct MaterialLink
{
    MaterialLinkId id = 0;

    MaterialNodeId fromNode = 0;
    MaterialPinId fromPin = 0;

    MaterialNodeId toNode = 0;
    MaterialPinId toPin = 0;
};
```

约束：

- output -> input
- 一般输入 pin 默认单连接
- 多连接输入 pin 由 flag 控制

## 6. 属性和值系统

### 6.1 通用值

```cpp
using MaterialValueVariant = std::variant<
    std::monostate,
    bool,
    int32_t,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    std::string
>;
```

```cpp
struct MaterialValue
{
    MaterialValueType type = MaterialValueType::Unknown;
    MaterialValueVariant value{};
};
```

这里的 `std::string` 主要用于：

- 资源路径
- sampler 名
- 轻量枚举值

### 6.2 Property Bag

```cpp
struct MaterialPropertyValue
{
    std::string name{};
    MaterialValue value{};
};
```

```cpp
struct MaterialPropertyBag
{
    std::vector<MaterialPropertyValue> values{};
};
```

作用：

- 节点实例的纯数据承载
- 避免每种节点都派生一个独立存档结构

后续如果属性增多、频繁查询，可以再加名字到索引的加速结构，但序列化真源仍建议保持简单稳定。

## 7. 参数元数据

参数节点不能只存一个字符串名字，否则后面做实例编辑、排序、重命名和分组会很脆。

建议单独抽参数元数据：

```cpp
struct MaterialParameterMetadata
{
    MaterialParameterId id = 0;
    std::string name{};
    std::string displayName{};
    std::string group{};

    MaterialValueType type = MaterialValueType::Unknown;
    MaterialValue defaultValue{};

    bool exposedToInstance = true;
    bool useSlider = false;
    float sliderMin = 0.0f;
    float sliderMax = 1.0f;

    int32_t sortPriority = 0;
};
```

用途：

- 生成 `.AshMat` 参数描述
- 驱动材质实例 UI
- 保持参数 identity 稳定

## 8. 注释、视图和诊断

### 8.1 注释框

```cpp
struct MaterialCommentBox
{
    MaterialCommentId id = 0;
    std::string title{};
    std::string colorStyle{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 300.0f;
    float height = 200.0f;
};
```

### 8.2 图视图状态

```cpp
struct MaterialGraphViewState
{
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;
};
```

### 8.3 编译诊断

```cpp
enum class MaterialDiagnosticSeverity : uint8_t
{
    Info,
    Warning,
    Error
};
```

```cpp
struct MaterialDiagnostic
{
    MaterialDiagnosticSeverity severity = MaterialDiagnosticSeverity::Error;
    MaterialNodeId nodeId = 0;
    MaterialPinId pinId = 0;
    std::string message{};
};
```

诊断应能同时服务于：

- 图面板高亮
- 错误列表
- 编译日志

## 9. 预览状态

```cpp
struct MaterialPreviewState
{
    bool realtime = true;
    bool showGrid = true;
    std::string previewMesh{};
    std::string previewMode{};
    bool channelPreviewEnabled = false;
    std::string previewChannel{};
};
```

建议：

- 预览状态属于编辑器文档，不属于运行时材质资产
- 可以存进图资产，也可以存进 workspace/layout 状态
- 第一版如果要简化，可以只保存必要预览配置

## 10. 节点定义注册表

图资产只存实例数据，模板信息应由节点定义注册表提供。

### 10.1 Pin 定义（V2）

```cpp
enum class DynamicPinBehavior : uint8_t
{
    Static,           // 固定类型，模板声明一次即定
    PolymorphicGroup, // 同组 pin 类型联动解析
    VariableArity,    // 用户可增减 pin（多输入 Add）
    PropertyDriven    // pin 由属性值动态生成（Custom Expression、FunctionCall）
};
```

```cpp
struct MaterialPinDefinitionV2
{
    std::string name{};
    MaterialPinDirection direction = MaterialPinDirection::Input;
    MaterialValueType type = MaterialValueType::Unknown;
    uint32_t flags = 0;
    MaterialValue defaultValue{};

    // 动态行为
    DynamicPinBehavior dynamicBehavior = DynamicPinBehavior::Static;
    std::string polymorphicGroupId{};       // 同 id 的 pin 共同解析类型
    MaterialValueType minType = MaterialValueType::Float1;
    MaterialValueType maxType = MaterialValueType::Float4;

    // Variable Arity
    uint32_t minArity = 0;
    uint32_t maxArity = 16;
    std::string arityPinNameTemplate{};     // "Input_{0}"
};
```

区分：

- `Static`：绝大多数节点的 pin，类型在模板中固定
- `PolymorphicGroup`：如 Add / Lerp / Multiply 的运算 pin，类型随连接推导
- `VariableArity`：如多输入 Add / Append 的可增删 pin
- `PropertyDriven`：如 Custom Expression 和 FunctionCall 的 pin，由属性或外部资产决定

### 10.2 节点属性描述

```cpp
struct MaterialNodePropertyDesc
{
    std::string name{};
    MaterialValueType type = MaterialValueType::Unknown;
    MaterialValue defaultValue{};
    std::string uiWidget{};                 // "slider", "color_picker", "dropdown", "code_editor"
    std::vector<std::string> enumValues{};  // dropdown 可选值
    float sliderMin = 0.0f;
    float sliderMax = 1.0f;
    std::string tooltip{};
};
```

### 10.3 节点代码生成描述

```cpp
enum class NodeCodegenStrategy : uint8_t
{
    Expression,       // 模板表达式："({A} + {B})"
    FunctionCall,     // 调用 HLSL 内置函数："lerp({A}, {B}, {Alpha})"
    InlineSnippet,    // 多行 HLSL 直接替换
    CustomExpression, // 用户自写 HLSL，存在 node property 里
    MaterialFunction, // 通过 MaterialFunctionLibrary 解析
    Intrinsic         // 由编译器特殊处理（Output、Reroute 等）
};

struct MaterialNodeCodegenDesc
{
    NodeCodegenStrategy strategy = NodeCodegenStrategy::Expression;
    std::string hlslTemplate{};             // Expression: "({A} + {B})"
    std::string hlslFunctionName{};         // FunctionCall: "lerp"
    std::string hlslIncludePath{};          // 需要的 include
    std::vector<std::string> hlslBody{};    // InlineSnippet: 多行 HLSL
};
```

### 10.4 节点定义（V2）

```cpp
struct MaterialNodeDefinitionV2
{
    // 身份
    std::string typeName{};
    uint32_t version = 1;
    MaterialNodeKind kind = MaterialNodeKind::Utility;

    // 显示
    std::string title{};
    std::string category{};                 // 层级分类："Math|Trigonometry"
    std::string tooltip{};
    std::string iconId{};
    std::vector<std::string> keywords{};    // palette 搜索关键词

    // 结构
    std::vector<MaterialPinDefinitionV2> inputs{};
    std::vector<MaterialPinDefinitionV2> outputs{};
    std::vector<MaterialNodePropertyDesc> properties{};

    // 代码生成
    MaterialNodeCodegenDesc codegen{};
    std::string typeResolutionRule{};        // "PromoteToLargest", "MatchFirst" 等
    bool supportsDynamicPins = false;
    bool supportsPreview = true;

    // 来源
    std::string sourceModule{};             // 注册此节点的 DLL/模块名
    std::string migrationHandler{};         // 版本迁移处理器
};
```

职责：

- 告诉编辑器这个节点长什么样
- 告诉编译器它是什么节点类型、如何生成 HLSL
- 为新建节点实例提供默认模板
- 支持搜索和分类

### 10.5 JSON 声明式节点格式

支持从 JSON 文件批量注册节点，无需重新编译引擎：

```json
{
  "version": 1,
  "nodes": [
    {
      "typeName": "Math.Lerp",
      "version": 1,
      "kind": "Math",
      "title": "Lerp",
      "category": "Math|Interpolation",
      "keywords": ["lerp", "blend", "mix", "interpolate"],
      "inputs": [
        { "name": "A", "type": "DynamicType", "dynamicBehavior": "PolymorphicGroup", "polymorphicGroupId": "main" },
        { "name": "B", "type": "DynamicType", "dynamicBehavior": "PolymorphicGroup", "polymorphicGroupId": "main" },
        { "name": "Alpha", "type": "Float1", "default": 0.5 }
      ],
      "outputs": [
        { "name": "Result", "type": "DynamicType", "dynamicBehavior": "PolymorphicGroup", "polymorphicGroupId": "main" }
      ],
      "codegen": {
        "strategy": "FunctionCall",
        "hlslFunctionName": "lerp"
      },
      "typeResolutionRule": "PromoteToLargest"
    },
    {
      "typeName": "Math.Add",
      "version": 1,
      "kind": "Math",
      "title": "Add",
      "category": "Math|Basic",
      "keywords": ["add", "plus", "sum"],
      "inputs": [
        { "name": "A", "type": "DynamicType", "dynamicBehavior": "PolymorphicGroup", "polymorphicGroupId": "main" },
        { "name": "B", "type": "DynamicType", "dynamicBehavior": "PolymorphicGroup", "polymorphicGroupId": "main" }
      ],
      "outputs": [
        { "name": "Result", "type": "DynamicType", "dynamicBehavior": "PolymorphicGroup", "polymorphicGroupId": "main" }
      ],
      "codegen": {
        "strategy": "Expression",
        "hlslTemplate": "({A} + {B})"
      },
      "typeResolutionRule": "PromoteToLargest"
    }
  ]
}
```

注册表支持三种注册路径：

1. C++ 静态注册（引擎内置节点）
2. JSON 文件批量注册（数据驱动扩展）
3. 插件模块注册（第三方 DLL）

## 11. 类型系统形式化

### 11.1 类型提升规则

```cpp
enum class TypePromotion : uint8_t
{
    None,        // 不兼容
    Identity,    // 相同类型
    ScalarSplat, // Float1 -> Float2/3/4，广播为 "x.xxx"
    IntToFloat,  // Int -> Float1："float(x)"
    BoolToFloat  // Bool -> Float1："(x ? 1.0 : 0.0)"
};
```

### 11.2 提升矩阵

| Source → Target | Bool | Int | Float1 | Float2 | Float3 | Float4 |
|-----------------|------|-----|--------|--------|--------|--------|
| **Bool** | Identity | — | BoolToFloat(3) | — | — | — |
| **Int** | — | Identity | IntToFloat(2) | — | — | — |
| **Float1** | — | — | Identity | ScalarSplat(1) | ScalarSplat(1) | ScalarSplat(1) |
| **Float2** | — | — | — | Identity | **禁止** | **禁止** |
| **Float3** | — | — | — | **禁止** | Identity | **禁止** |
| **Float4** | — | — | — | **禁止** | **禁止** | Identity |

括号内数字为 cost，cost=255 为 None（不可提升）。

关键约束：

- 跨维度向量提升被禁止（Float2 不能自动变 Float3），必须显式使用 Append / ComponentMask
- Float1 是通用标量，可以 splat 到任何向量维度
- Texture2D / SamplerState / MaterialAttributes 不参与算术提升

### 11.3 多态 Pin 解析规则

编译器内置以下命名规则，节点定义通过 `typeResolutionRule` 字段引用：

| 规则名 | 行为 |
|--------|------|
| `PromoteToLargest` | 所有已连接 pin 中维度最大的类型作为组解析类型 |
| `MatchFirst` | 第一个已连接非空 pin 的类型作为组解析类型 |
| `FixedOutput` | 输出类型由定义声明固定，不随输入变化 |
| `CrossProduct` | 强制 Float3（如叉积节点），输入非 Float3 则报错 |

### 11.4 类型解析流程（TypeResolutionPass）

1. **Seed**：所有 pin 用 `declaredType` 初始化 `resolvedType`
2. **Topological**：按拓扑序遍历节点
3. **Collect**：对每个节点，收集所有输入 pin 的上游 `resolvedType`
4. **Resolve Group**：对 PolymorphicGroup 中的 pin，应用 `typeResolutionRule` 得到组类型
5. **Propagate**：将 `resolvedType` 写入同组所有 pin，向下游传播

重复直到稳定。仍为 `Unknown` 的 pin 生成诊断错误。

## 12. 与编译器的边界：Expression IR 规范

### 12.1 设计原则

- 图实例只存数据，编译行为全部在 IR 层
- IR 是纯数据结构，无虚函数分发
- 两层 IR：Typed Graph IR（保留图拓扑） → Expression IR（扁平表达式树）
- 不要在 `MaterialNodeInstance` 里保存 HLSL 文本、shader 函数名、编译期寄存器名、RHI 句柄

### 12.2 Typed Graph IR

```cpp
struct TypedInputSlot
{
    std::string name{};
    MaterialValueType resolvedType = MaterialValueType::Unknown;
    MaterialNodeId sourceNodeId = 0;        // 0 = 未连接
    std::string sourceOutputName{};
    MaterialValue constantValue{};          // 未连接时使用的默认值
    bool isConnected = false;
};

struct TypedOutputSlot
{
    std::string name{};
    MaterialValueType resolvedType = MaterialValueType::Unknown;
};

struct TypedGraphNode
{
    MaterialNodeId id = 0;
    std::string typeName{};
    std::vector<TypedInputSlot> inputs{};
    std::vector<TypedOutputSlot> outputs{};
    MaterialPropertyBag properties{};
    bool isConstant = false;                // 所有输入都是字面量
    bool isDead = false;                    // 被 DCE pass 标记
};

struct TypedGraphIR
{
    std::vector<TypedGraphNode> nodes{};
    std::vector<MaterialNodeId> topologicalOrder{};
    MaterialNodeId outputNodeId = 0;
};
```

### 12.3 Expression IR

```cpp
enum class ExpressionOp : uint16_t
{
    // 字面量
    Constant,
    Parameter,
    TextureSample,

    // 算术
    Add, Sub, Mul, Div, Mod, Negate,

    // 数学函数
    Abs, Saturate, Floor, Ceil, Frac, Sqrt, Pow, Log2, Exp2,
    Sin, Cos, Tan, Asin, Acos, Atan, Atan2,
    Min, Max, Clamp, Lerp, Step, SmoothStep,

    // 向量
    Dot, Cross, Normalize, Length, Reflect,
    Append, ComponentMask, VectorElement,

    // 类型转换
    TypeCast,

    // 控制流
    StaticBranch,
    Conditional,

    // 扩展
    FunctionCall,
    CustomCode,

    // 输入变量
    InputVariable,      // AshPixelParameters / AshVertexParameters 的字段

    // 临时变量（CSE 引入）
    TempVariable,
    Assign
};

struct ExpressionNode
{
    uint32_t id = 0;
    ExpressionOp op = ExpressionOp::Constant;
    MaterialValueType resultType = MaterialValueType::Unknown;
    std::vector<uint32_t> operands{};       // 指向 expressions 列表的索引

    // 各 op 专用字段（只有对应 op 时才有效）
    MaterialValue constantValue{};          // Constant
    std::string parameterName{};            // Parameter
    std::string textureName{};              // TextureSample
    std::string samplerName{};             // TextureSample
    std::string functionName{};             // FunctionCall
    std::string customCode{};               // CustomCode
    std::string fieldName{};                // InputVariable: "uv0", "worldPos" 等
    std::string tempName{};                 // TempVariable
    std::string componentMask{};            // ComponentMask: "xyz", "rg", "w"
    uint8_t componentIndex = 0;             // VectorElement
    std::string staticSwitchName{};         // StaticBranch
};

struct MaterialExpressionIR
{
    std::vector<ExpressionNode> expressions{};

    struct OutputBinding
    {
        std::string channelName{};          // "base_color", "metallic", "normal_ts" 等
        uint32_t expressionId = 0;
    };
    std::vector<OutputBinding> pixelOutputs{};
    std::vector<OutputBinding> vertexOutputs{};

    // 编译过程中收集的资源需求
    std::vector<MaterialResourceDesc> requiredResources{};
    std::vector<MaterialParameterDesc> requiredParameters{};
    std::vector<MaterialSamplerDefinition> requiredSamplers{};
};
```

### 12.4 IR 设计说明

- `ExpressionNode` 是纯数据，遍历表达式树只需按索引访问 `operands`
- `TempVariable` + `Assign` 由 CSE pass 引入，命名共享子表达式以减少重复计算
- `InputVariable` 的 `fieldName` 映射到 HLSL 的 `params.uv0`、`params.worldPosition` 等
- `StaticBranch` 在 codegen 时根据 `MaterialGraphSettings.staticSwitches` 求值，死分支不生成代码
- 新增 `ExpressionOp` 变体是向后兼容的，只需扩展枚举和 codegen 支持

## 13. 节点迁移系统

节点定义随引擎版本演进会发生变化（重命名 pin、增删属性、修改默认值）。迁移系统确保旧图资产能被正确升级。

### 13.1 版本标记

`MaterialNodeInstance` 增加 `version` 字段：

```cpp
struct MaterialNodeInstance
{
    MaterialNodeId id = 0;
    std::string typeName{};
    uint32_t version = 1;           // 节点创建时的定义版本
    // ... 其余字段不变
};
```

加载图资产时，如果 `node.version < currentDefinition.version`，触发迁移流程。

### 13.2 迁移处理器

```cpp
class INodeMigrationHandler
{
public:
    virtual ~INodeMigrationHandler() = default;
    virtual std::string_view GetTypeName() const = 0;
    virtual uint32_t GetSourceVersion() const = 0;
    virtual uint32_t GetTargetVersion() const = 0;
    virtual bool Migrate(MaterialNodeInstance& node,
                         MaterialMigrationContext& ctx) const = 0;
};
```

### 13.3 迁移上下文

```cpp
struct MaterialMigrationContext
{
    MaterialGraphAsset& graph;
    std::vector<MaterialDiagnostic>& diagnostics;

    bool RenamePinInput(MaterialNodeInstance& node,
                        std::string_view oldName, std::string_view newName);
    bool RenamePinOutput(MaterialNodeInstance& node,
                         std::string_view oldName, std::string_view newName);
    bool AddPinInput(MaterialNodeInstance& node,
                     const MaterialPinDefinitionV2& def);
    bool RemovePinInput(MaterialNodeInstance& node, std::string_view name);
    bool SetPropertyDefault(MaterialNodeInstance& node,
                            std::string_view propName,
                            const MaterialValue& value);
};
```

### 13.4 迁移执行策略

- 迁移按版本链逐步执行：v1→v2→v3（不跳跃）
- 迁移不可逆：保存后会写入新版本号
- 无迁移处理器的版本跳跃生成 Warning 诊断（不阻止加载）
- 连线引用的 pin 名如果被迁移重命名，`MaterialMigrationContext` 自动更新关联 link

## 14. 图操作命令系统

### 14.1 命令基类

```cpp
struct MaterialGraphCommandContext
{
    MaterialGraphAsset& graph;
    MaterialNodeRegistryV2& nodeRegistry;
    MaterialTypeSystem& typeSystem;
    MaterialGraphSelectionState& selection;
};

class MaterialGraphCommand
{
public:
    virtual ~MaterialGraphCommand() = default;
    virtual const char* GetLabel() const = 0;
    virtual bool Execute(MaterialGraphCommandContext& context) = 0;
    virtual bool Undo(MaterialGraphCommandContext& context) = 0;
    virtual bool TryMerge(const MaterialGraphCommand& subsequent) { return false; }
};
```

### 14.2 核心命令

| 命令 | Execute | Undo | TryMerge |
|------|---------|------|----------|
| `AddNodeCommand` | 从注册表定义创建节点，分配 id | 按 id 删除节点 | 否 |
| `RemoveNodesCommand` | 删除节点及所有关联 link | 恢复节点和 link | 否 |
| `CreateLinkCommand` | 创建连线，如输入已连接则断开旧线 | 删除新线，恢复旧线 | 否 |
| `RemoveLinkCommand` | 删除连线 | 重建连线 | 否 |
| `MoveNodesCommand` | 应用新坐标 | 恢复旧坐标 | 是（拖拽合并） |
| `ChangePropertyCommand` | 设置新属性值 | 恢复旧属性值 | 是（滑杆合并） |
| `ChangeGraphSettingsCommand` | 应用新设置 | 恢复旧设置 | 否 |
| `PasteNodesCommand` | 反序列化剪贴板，重映射 id，插入 | 删除粘贴的节点和 link | 否 |

### 14.3 Undo/Redo 服务

```cpp
class MaterialGraphUndoRedoService
{
public:
    bool Execute(std::unique_ptr<MaterialGraphCommand> command,
                 MaterialGraphCommandContext& context);
    bool Undo(MaterialGraphCommandContext& context);
    bool Redo(MaterialGraphCommandContext& context);

    bool BeginTransaction(std::string label);
    bool CommitTransaction();
    void CancelTransaction(MaterialGraphCommandContext& context);

    void Clear();
    void MarkSaved();
    bool CanUndo() const;
    bool CanRedo() const;
    bool IsDirty() const;
};
```

与全局 `UndoRedoService` 的关系：

- `MaterialGraphUndoRedoService` 独立管理材质图操作栈
- 可通过 adapter 包装为 `EditorCommand` 接入全局编辑器历史
- 材质图 tab 切换时通过 `MaterialGraphDocumentService` 切换活跃栈

### 14.4 剪贴板

```cpp
struct MaterialGraphClipboardData
{
    uint32_t version = 1;
    std::vector<MaterialNodeInstance> nodes{};
    std::vector<MaterialLink> internalLinks{};  // 仅被复制节点间的内部连线
};
```

粘贴行为：

- 所有 id 重新生成（避免冲突）
- 仅保留内部连线（引用外部节点的连线丢弃）
- 坐标偏移避免重叠
- 支持跨文档粘贴（相同引擎版本）
- 序列化为 JSON 写入系统剪贴板

### 14.5 图 Diff

```cpp
struct MaterialGraphDiff
{
    struct NodeDiff
    {
        MaterialNodeId id = 0;
        enum class Kind : uint8_t { Added, Removed, Modified } kind;
        std::vector<std::string> changedProperties{};
        bool positionChanged = false;
    };
    struct LinkDiff
    {
        MaterialLinkId id = 0;
        enum class Kind : uint8_t { Added, Removed } kind;
    };
    std::vector<NodeDiff> nodeDiffs{};
    std::vector<LinkDiff> linkDiffs{};
    bool settingsChanged = false;
};
```

用途：

- 版本控制 merge 辅助
- 增量编译触发（只重编译受影响节点）
- 协作冲突检测

## 15. JSON 序列化建议

建议：

- 用稳定字段名
- 保留 `version`
- 所有 ID 按整数保存
- `typeName` 作为迁移锚点保留
- 未知字段加载时尽量容忍

推荐最小 JSON 结构：

```json
{
  "version": 1,
  "header": {},
  "settings": {},
  "nodes": [],
  "links": [],
  "comments": [],
  "viewState": {}
}
```

## 16. 不变量

应长期保持的约束：

- 图实例只存数据，不存编译行为
- 节点定义统一从注册表读取
- 图级设置与节点级属性分开
- 参数 identity 独立于显示名
- 预览状态不污染运行时材质对象

## 17. 推荐阅读

做图模型、序列化、节点注册、类型系统设计时，先读本文件。

继续往下时再补读：

- `docs/MaterialEditorArchitecture.md`
- `docs/MaterialEditorDesign.md`
- `docs/EngineDeveloperGuide.md`
