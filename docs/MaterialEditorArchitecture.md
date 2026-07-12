# 材质编辑器架构文档

> 目标：定义材质编辑器在 AshEngine 中的模块边界、服务拆分、编译链路、预览链路与运行时接入方式。

## 1. 架构结论

材质编辑器采用三层结构：

1. 作者层
   - 材质图资产
   - 节点编辑器窗口
   - 参数与预览 UI
2. 编译层
   - 图校验
   - 类型推导
   - IR 生成
   - HLSL / `.AshMat` 生成
3. 运行时层
   - 继续使用现有 `Material / MaterialInstance / MaterialSystem / MaterialRenderProxy`

原则：

- 编辑器负责 authoring
- 编译器负责翻译
- 运行时负责消费结果

## 2. 顶层模块

推荐模块划分如下：

- `MaterialEditorHost`
  - 宿主接口
  - 向材质编辑器暴露项目、资产、日志、渲染、命令和通知能力
- `MaterialEditorModule`
  - 编辑器模块入口
  - 管理窗口注册、文档生命周期、服务装配
- `MaterialGraphDocumentService`
  - 管理 `AshMatGraph` 文档
  - 打开、保存、dirty、tab、恢复
- `MaterialNodeRegistryV2`
  - 节点定义注册表
  - 支持三种注册路径：C++ 静态注册、JSON 声明式、插件模块
  - 分类、pin 模板、属性模板、Codegen 描述、节点工厂
- `MaterialGraphCompilerService`
  - graph -> typed IR -> expression IR -> HLSL / `.AshMat`
  - 基于 Pass Manager 的可扩展编译管线
- `MaterialPreviewService`
  - 预览 scene、preview mesh、preview output、编译结果热更新
  - 通道预览和 per-node 缩略图
- `MaterialPaletteService`
  - 节点搜索、分类、收藏、最近使用
- `MaterialFunctionLibrary`
  - 材质函数资产管理
  - 依赖追踪与循环检测

### 2.1 节点插件系统

为支持第三方节点扩展，引入稳定 ABI 的插件模块接口：

```cpp
class IMaterialNodeModule
{
public:
    virtual ~IMaterialNodeModule() = default;
    virtual const char* GetModuleName() const = 0;
    virtual uint32_t GetModuleVersion() const = 0;
    virtual void RegisterNodes(MaterialNodeRegistryV2& registry) = 0;
};
```

DLL 入口约定：

```cpp
extern "C" IMaterialNodeModule* CreateMaterialNodeModule();
extern "C" void DestroyMaterialNodeModule(IMaterialNodeModule* pModule);
```

`MaterialNodeRegistryV2` 在初始化时按以下顺序加载节点：

1. C++ 内置节点（引擎核心，如 Output、Reroute、TextureSample）
2. JSON 节点包（`editor/MaterialEditor/NodeDefinitions/*.json`）
3. 插件模块（`plugins/MaterialNodes/*.dll`）

后加载的同名节点覆盖先前的定义（版本号更高时），这允许插件升级内置节点行为。

## 3. 窗口与 UI 组成

材质编辑器窗口建议拆成以下视图：

- `MaterialEditorWindow`
  - 窗口级装配
- `MaterialGraphCanvasView`
  - 节点图、拖线、注释、框选
- `MaterialSelectionDetailsView`
  - 当前节点或 pin 的属性面板
- `MaterialGraphParametersView`
  - 当前图的参数汇总
- `MaterialPreviewViewport`
  - 实时预览
- `MaterialCompileDiagnosticsView`
  - 编译错误、警告、stats

视图只负责：

- 绘制
- 收集输入
- 调用服务

不要在视图里直接做：

- 类型推导
- HLSL 生成
- 预览 scene 生命周期管理

## 4. 宿主接口

第一版不做独立进程，但要保留独立宿主能力。

建议引入宿主抽象：

```cpp
class MaterialEditorHost
{
public:
    virtual ~MaterialEditorHost() = default;

    virtual AshEngine::AssetDatabase* GetAssetDatabase() = 0;
    virtual AshEngine::UIContext* GetUiContext() = 0;
    virtual AshEngine::ScenePresentationSubsystem* GetScenePresentation() = 0;
    virtual void LogInfo(std::string_view message) = 0;
    virtual void LogWarning(std::string_view message) = 0;
    virtual void LogError(std::string_view message) = 0;
};
```

用途：

- 让材质编辑器逻辑独立于 `EditorApplication`
- 未来如果要做轻量独立材质工具，只需换一个 host 实现

## 5. 编译链路

### 5.1 Pass 管理器架构

编译管线采用 pass-based 设计，每个 pass 是一个独立可替换单元：

```cpp
enum class CompilerPassKind : uint8_t
{
    Validation,
    TypeResolution,
    Optimization,
    IRGeneration,
    CodeGeneration,
    ArtifactOutput
};

struct CompilerPassResult
{
    bool success = true;
    bool hasFatalError = false;
    uint32_t warningCount = 0;
    uint32_t errorCount = 0;
};

class ICompilerPass
{
public:
    virtual ~ICompilerPass() = default;
    virtual const char* GetPassName() const = 0;
    virtual CompilerPassKind GetKind() const = 0;
    virtual CompilerPassResult Execute(MaterialCompilationContext& context) = 0;
    virtual std::vector<std::string> GetDependencies() const { return {}; }
    virtual bool IsFatal() const { return true; }
};
```

Pass 管理器：

```cpp
class MaterialCompilerPassManager
{
public:
    void RegisterPass(std::unique_ptr<ICompilerPass> pass);
    void RemovePass(std::string_view passName);
    bool BuildExecutionOrder(std::string* outError = nullptr);
    CompilerPassResult ExecuteAll(MaterialCompilationContext& context);
    CompilerPassResult ExecuteIncremental(
        MaterialCompilationContext& context,
        const std::vector<MaterialNodeId>& changedNodes);
};
```

### 5.2 编译上下文

所有 pass 共享的状态容器：

```cpp
struct MaterialCompilationContext
{
    // 只读输入
    const MaterialGraphAsset& sourceGraph;
    const MaterialGraphSettings& settings;
    const MaterialNodeRegistryV2& nodeRegistry;
    const MaterialTypeSystem& typeSystem;
    const MaterialFunctionLibrary* pFunctionLibrary = nullptr;

    // 中间状态（各 pass 逐步填充）
    std::unique_ptr<TypedGraphIR> pTypedIR{};
    std::unique_ptr<MaterialExpressionIR> pExpressionIR{};
    std::unique_ptr<MaterialCodegenOutput> pCodegenOutput{};

    // 输出
    std::vector<MaterialDiagnostic> diagnostics{};
    MaterialCompilationStats stats{};

    // 增量编译状态
    uint64_t previousGraphHash = 0;
    bool incrementalEnabled = false;
    MaterialDependencyGraph* pDependencyGraph = nullptr;
};
```

### 5.3 默认 Pass 注册顺序

```text
1. GraphValidationPass          (Validation)
2. TopologicalSortPass          (Validation)
3. TypeResolutionPass           (TypeResolution)
4. StaticSwitchEvaluationPass   (Optimization)
5. ExpressionIRGenerationPass   (IRGeneration)
6. ConstantFoldingPass          (Optimization)
7. DeadCodeEliminationPass      (Optimization)
8. CommonSubExpressionPass      (Optimization)
9. OutputMaskingPass            (Optimization)
10. HLSLCodeGenerationPass      (CodeGeneration)
11. AshMatDescriptorOutputPass  (ArtifactOutput)
```

依赖关系：

- `TopologicalSortPass` 依赖 `GraphValidationPass`
- `TypeResolutionPass` 依赖 `TopologicalSortPass`
- `ExpressionIRGenerationPass` 依赖 `TypeResolutionPass` + `StaticSwitchEvaluationPass`
- 所有 Optimization pass 依赖 `ExpressionIRGenerationPass`
- `HLSLCodeGenerationPass` 依赖所有 Optimization pass
- `AshMatDescriptorOutputPass` 依赖 `HLSLCodeGenerationPass`

### 5.4 各 Pass 职责

**GraphValidationPass**：

- 检查节点引用和连线完整性
- 检查 pin 类型兼容性
- 检查必填输入
- 检查输出节点是否满足域要求

**TopologicalSortPass**：

- Kahn 算法排序
- 检测环（graph validation 应已捕获，此处为安全保障）

**TypeResolutionPass**：

- 多态 pin 类型解析
- 隐式类型提升标记
- 详见 `MaterialNodeDataModel.md` 第 11 节

**StaticSwitchEvaluationPass**：

- 根据 `MaterialGraphSettings.staticSwitches` 确定分支
- 标记死分支节点为 `isDead`
- 最高影响的优化（N 个开关产生 2^N 种排列，但每次只编译一种）

**ExpressionIRGenerationPass**：

- 从 TypedGraphIR 生成 MaterialExpressionIR
- 处理 MaterialFunction 内联/展开
- 收集资源需求

**ConstantFoldingPass**：

- 所有操作数为 Constant 的表达式求值为单个 Constant
- 支持：算术、Min/Max/Clamp、Abs/Saturate/Floor/Ceil/Frac
- 不折叠：TextureSample、CustomCode、InputVariable

**DeadCodeEliminationPass**：

- 从 OutputBinding 反向标记可达表达式
- 移除不可达节点
- 移除当前 domain/blend 不使用的输出通道

**CommonSubExpressionPass**：

- 内容哈希：`hash(op, resultType, operand_hashes...)`
- 重复表达式引入 `TempVariable`
- 提升生成 HLSL 可读性，减少重复计算

**OutputMaskingPass**：

- 如果下游只消费 Float4 的 `.xy`，向上传播 mask
- TextureSample 可优化为 `.xy` 而非完整 float4
- 减少中间寄存器压力

**HLSLCodeGenerationPass**：

- 生成用户材质 shader（`CalculatePixelMainNode` + `CalculateVertexMainNode`）
- 输出到 `generated.hlsl`
- 通过 `IShaderBackendCodegen` 接口查询目标后端

**AshMatDescriptorOutputPass**：

- 从 IR 资源汇总生成 `parameters` / `resources` / `samplers`
- 写入 `.AshMat` 运行时描述

### 5.5 多后端抽象

```cpp
enum class ShaderBackend : uint8_t
{
    HLSL_SM6,       // 当前默认（DXC）
    HLSL_SM5,       // Legacy
    GLSL_450,       // 未来 Vulkan/OpenGL
    MSL_2_0,        // 未来 Metal
    SPIRV           // 未来交叉编译
};

class IShaderBackendCodegen
{
public:
    virtual ~IShaderBackendCodegen() = default;
    virtual ShaderBackend GetBackend() const = 0;
    virtual bool GenerateCode(
        const MaterialExpressionIR& ir,
        const MaterialCompilationContext& context,
        std::string& outSource,
        std::string* outError = nullptr) = 0;
};
```

当前只实现 `HLSL_SM6` 后端。`HLSLCodeGenerationPass` 查询 `ShaderBackendRegistry` 而非硬编码后端逻辑。未来切换到 GLSL 或 Metal 只需注册新后端实现。

### 5.6 编译统计

```cpp
struct MaterialCompilationStats
{
    uint32_t totalNodes = 0;
    uint32_t activeNodes = 0;
    uint32_t deadNodes = 0;
    uint32_t constantFolded = 0;
    uint32_t cseEliminated = 0;
    uint32_t textureSamples = 0;
    uint32_t arithmeticOps = 0;
    double compileTimeMs = 0.0;
    std::vector<std::pair<std::string, double>> passTimings{};
};
```

## 6. 资产边界

建议明确区分：

- 源资产
  - `*.AshMatGraph`
- 运行时编译产物
  - `*.AshMat`
  - `*.generated.hlsl`
- 运行时实例资产
  - `*.AshMatIns`

编辑器修改图资产时：

- 只直接保存 `AshMatGraph`
- 编译成功后写出运行时产物
- 编译失败时不覆盖上一版可用运行时产物，或采用 staging + replace 方式

## 7. 实时预览架构

### 7.1 预览服务职责

`MaterialPreviewService` 负责：

- 创建 preview scene
- 管理 preview camera
- 管理 preview mesh
- 注册 `SceneOutputHandle`
- 注册 `SceneViewBindingHandle`
- 提供 `UISurfaceHandle`
- 把成功编译结果应用到 preview material
- 管理预览通道切换和 per-node 缩略图

### 7.2 为什么必须走 ScenePresentationSubsystem

因为当前 Editor 推荐约束是：

- 不让 UI 直接持有场景 RT
- scene-driven 视图统一走 `UISurfaceHandle`
- 离屏输出与绑定生命周期由 engine 管

因此材质预览也应复用这条路径，而不是再开一套直接对接 `SceneRenderer` 的旧链路。

### 7.3 预览刷新策略

- 图改动后进入 debounce
- 编译任务放后台
- 主线程只处理最新结果
- 编译成功后提交预览更新请求
- 编译失败时保留上一版成功预览

### 7.4 预览通道系统

```cpp
enum class MaterialPreviewMode : uint8_t
{
    FullLit,        // 正常光照渲染
    Unlit,          // 仅 BaseColor，无光照
    Channel,        // 单通道可视化
    NodeOutput,     // 选中节点的输出路由到屏幕
    Wireframe,      // 线框模式
    Checker         // UV 棋盘格模式
};

enum class MaterialPreviewChannel : uint8_t
{
    FinalColor,
    BaseColor,
    Metallic,
    Roughness,
    Normal,
    NormalWorldSpace,
    Emissive,
    AmbientOcclusion,
    Opacity,
    WorldPositionOffset,
    PixelDepth,
    CustomOutput        // per-node 预览
};

struct MaterialPreviewRequest
{
    MaterialPreviewMode mode = MaterialPreviewMode::FullLit;
    MaterialPreviewChannel channel = MaterialPreviewChannel::FinalColor;
    MaterialNodeId nodeId = 0;              // NodeOutput 模式下的目标节点
    std::string nodePinName{};
    bool grayscale = true;                  // 标量通道灰度显示
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
};
```

### 7.5 通道预览编译策略

通道预览通过修改 Expression IR 的输出路由实现，复用完整编译管线：

| 通道类型 | HLSL 路由策略 |
|----------|---------------|
| 颜色通道（BaseColor、Emissive） | 表达式路由到 `node.emissive`，禁用光照 |
| 标量通道（Metallic、Roughness、AO） | `node.emissive = scalarExpr.xxx`（灰度 splat） |
| 法线通道 | `node.emissive = normalExpr * 0.5 + 0.5`（重映射到 [0,1]） |
| 节点输出 | 从目标节点/pin 反向追溯表达式，路由到 `node.emissive` |

```cpp
class MaterialPreviewCompiler
{
public:
    bool CompileChannelPreview(
        const MaterialGraphAsset& graph,
        MaterialPreviewChannel channel,
        MaterialCompilationContext& context);

    bool CompileNodePreview(
        const MaterialGraphAsset& graph,
        MaterialNodeId nodeId,
        const std::string& pinName,
        MaterialCompilationContext& context);
};
```

### 7.6 Per-Node 缩略图

- 预览服务维护 64x64 小型离屏 `SceneOutputHandle` 池
- 优先级队列确保选中节点优先编译
- 只有输入表达式哈希变化的节点才重新编译缩略图
- 缩略图使用 Unlit 模式（无光照干扰）
- 内存预算约束缩略图池大小（建议最大 32 个活跃缩略图）

## 8. 线程、缓存与增量编译

### 8.1 线程模型

- 图编辑与 UI 在主线程
- 编译在 worker 线程
- 预览结果切换在主线程或受控同步点

### 8.2 缓存 Key

缓存建议按以下 key 组织：

- graph 内容 hash
- 静态开关 hash
- 依赖的函数图 hash
- 依赖的纹理和材质资源路径
- 目标 domain / blend / shading model

### 8.3 增量编译

对于实时编辑场景，全量编译开销较大。引入依赖图实现增量编译：

```cpp
struct MaterialDependencyGraph
{
    // node A -> 依赖 A 输出的节点集合
    std::unordered_map<MaterialNodeId, std::unordered_set<MaterialNodeId>> dependents{};
    // 函数资产路径 -> 函数图哈希
    std::unordered_map<std::string, uint64_t> functionHashes{};
    // 每个节点的输入哈希（所有输入 pin 解析值的哈希）
    std::unordered_map<MaterialNodeId, uint64_t> nodeInputHashes{};

    std::unordered_set<MaterialNodeId> GetAffectedNodes(
        const std::vector<MaterialNodeId>& changedNodes) const;
};
```

增量编译流程：

1. 图编辑命令完成后，`MaterialGraphDocumentService` 收集变更节点列表
2. 通过 `MaterialDependencyGraph::GetAffectedNodes()` 传播影响范围
3. 调用 `MaterialCompilerPassManager::ExecuteIncremental()`
4. Pass Manager 只对受影响节点重新执行 TypeResolution 和后续 pass
5. Expression IR 层面只重建受影响的子树

增量编译失效条件（回退全量编译）：

- 图设置变更（domain / blend mode / shading model）
- 新增或删除 Output 节点连线
- 依赖的 MaterialFunction 哈希变化
- 首次编译（无历史依赖图）

## 9. 与运行时材质系统的边界

不要做：

- 不要让图编辑器直接修改 `MaterialRenderProxy`
- 不要让节点系统知道 `GraphicsProgram`
- 不要在节点层塞 RHI 或 renderer 细节

应当做：

- 编译器生成现有运行时可消费的 `.AshMat`
- 运行时继续通过 `MaterialSystem + MaterialShaderMap + MaterialRenderProxy` 工作
- 预览服务只使用稳定的 Editor / Engine facade

## 10. 材质函数架构

### 10.1 资产格式

材质函数是独立的 `.AshMatFunc` 资产，复用图数据结构但有专属输入输出定义：

```cpp
struct MaterialFunctionInputDesc
{
    std::string name{};
    MaterialValueType type = MaterialValueType::Float3;
    MaterialValue defaultValue{};
    std::string description{};
    uint32_t sortOrder = 0;
};

struct MaterialFunctionOutputDesc
{
    std::string name{};
    MaterialValueType type = MaterialValueType::Float3;
    std::string description{};
    uint32_t sortOrder = 0;
};

struct MaterialFunctionAsset
{
    uint32_t version = 1;
    MaterialGraphHeader header{};
    std::vector<MaterialFunctionInputDesc> inputs{};
    std::vector<MaterialFunctionOutputDesc> outputs{};

    // 图体（复用材质图相同结构）
    std::vector<MaterialNodeInstance> nodes{};
    std::vector<MaterialLink> links{};

    std::string description{};
    std::string category{};
    std::vector<std::string> keywords{};
    bool isLibraryFunction = false;
};
```

函数图内使用两种特殊节点类型：

- `FunctionInput`：读取函数输入参数
- `FunctionOutput`：写入函数输出结果

### 10.2 函数库管理

```cpp
class MaterialFunctionLibrary
{
public:
    bool LoadFunction(const std::filesystem::path& path);
    void UnloadFunction(const std::string& functionPath);
    const MaterialFunctionAsset* FindFunction(const std::string& functionPath) const;
    std::vector<const MaterialFunctionAsset*> GetAllLibraryFunctions() const;

    // 循环检测
    bool WouldCreateCycle(const std::string& callerPath,
                          const std::string& calleePath) const;
    std::vector<std::string> GetTransitiveDependencies(
        const std::string& functionPath) const;
    uint64_t GetFunctionHash(const std::string& functionPath) const;
};
```

### 10.3 编译策略

编译过程中遇到 `FunctionCall` 节点时：

1. 从 `MaterialFunctionLibrary` 解析函数资产
2. 编译函数体为 IR 子树
3. 替换 `FunctionInput` 节点为调用方的输入表达式
4. 提取 `FunctionOutput` 节点的输出表达式

内联策略：

```cpp
struct FunctionInliningPolicy
{
    uint32_t maxInlineNodeCount = 16;   // <= 16 节点直接内联
    bool forceInlineAll = false;        // debug 用
};
```

- 小函数（<= 16 节点）：直接内联到调用方 IR
- 大函数（> 16 节点）：emit 命名 HLSL 辅助函数，通过函数调用引用

### 10.4 循环检测

递归调用会导致无限展开。双重保障：

1. **编辑时**：用户在 FunctionCall 节点设置函数路径时，调用 `WouldCreateCycle()` 进行 DFS 检查。如果会产生环则拒绝设置并显示错误
2. **编译时**：`GraphValidationPass` 再次检查所有 FunctionCall 节点的调用链，确保已保存的图不含循环

### 10.5 函数与材质层

材质函数输出类型为 `MaterialAttributes` 时，即为材质层函数。编译器将 `MaterialAttributes` 展开为 `AshPixelMainNode` 各字段赋值。详见 `MaterialEditorDesign.md` 材质层设计。

## 11. 推荐目录结构

建议后续源码按以下方向组织：

```text
project/src/editor/MaterialEditor/
    App/
    Documents/
    Graph/
    Compiler/
    Preview/
    Views/
    Widgets/
    NodeDefinitions/
```

其中：

- `App/`
  - host、module、window 装配
- `Documents/`
  - graph 文档和保存逻辑
- `Graph/`
  - 图模型、节点定义、连线、选择、命令系统
- `Compiler/`
  - pass manager、validation、type resolution、IR、codegen
  - `Passes/` 子目录按功能拆分各 pass
  - `Backends/` 子目录放各 shader 后端实现
- `Preview/`
  - 预览服务、通道预览、per-node 缩略图、scene presentation 接入
- `Views/`
  - 各面板视图
- `Widgets/`
  - 节点画布、pin 渲染、连线渲染等自定义 widget
- `NodeDefinitions/`
  - JSON 声明式节点文件
  - 内置节点 C++ 注册

```text
project/src/editor/MaterialEditor/Compiler/
    MaterialCompilerPassManager.h/cpp
    MaterialCompilationContext.h
    MaterialTypeSystem.h/cpp
    Passes/
        GraphValidationPass.h/cpp
        TopologicalSortPass.h/cpp
        TypeResolutionPass.h/cpp
        StaticSwitchEvaluationPass.h/cpp
        ExpressionIRGenerationPass.h/cpp
        ConstantFoldingPass.h/cpp
        DeadCodeEliminationPass.h/cpp
        CommonSubExpressionPass.h/cpp
        OutputMaskingPass.h/cpp
        HLSLCodeGenerationPass.h/cpp
        AshMatDescriptorOutputPass.h/cpp
    Backends/
        IShaderBackendCodegen.h
        HLSLBackend.h/cpp
    IR/
        TypedGraphIR.h
        MaterialExpressionIR.h
```

## 12. 未来扩展点

已由架构支持、待实现的扩展：

- 材质函数资产（`.AshMatFunc`）
- 材质层栈资产和混合函数
- 共享节点库（JSON 节点包分发）
- 第三方插件 DLL 注册自定义节点
- graph diff / merge（版本控制友好）
- 子图重用和折叠
- 预览 profile 与指令统计
- 多编译目标（GLSL / Metal / SPIRV）
- 更丰富的域与 pass（Decal / PostProcess / UI）
- Custom Expression 节点（Phase 1 必须）
- 增量编译优化

需要架构层进一步设计的扩展：

- 运行时材质参数动画系统
- 材质 LOD（距离相关复杂度降级）
- 基于 GPU feedback 的 shader 热替换
- 分布式编译（shader farm）

## 13. 推荐阅读

做实现、拆模块、定边界时，先读本文件。

继续往下时再补读：

- `docs/MaterialNodeDataModel.md`
- `docs/MaterialEditorDesign.md`
- `docs/EngineDeveloperGuide.md`
- `docs/ScenePresentationSubsystemGuide.md`
- `docs/EditorCodeStyleGuide.md`
