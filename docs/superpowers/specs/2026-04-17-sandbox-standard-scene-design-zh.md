# Sandbox 标准场景设计

**状态：** 提议  
**日期：** 2026-04-17  
**范围：** `project/src/sandbox`，以及少量 Engine 侧 Scene / Render 集成点

## 1. 目标

把当前混合了 demo 和 smoke 的 Sandbox 默认流程，收口成一条真正的标准端到端场景流程，完整覆盖：

- 通过 `AssetDatabase` 加载真实资源
- 把资源实例化到逻辑 `Scene`
- 在逻辑线程更新 Scene
- 把逻辑场景桥接为渲染侧可见数据
- 通过 `SceneRenderer` 完成渲染
- 通过正常 present 路径显示到屏幕上

新的默认标准场景使用 **Sponza**。这里的目标不是“启动快”，而是“复杂度足够、能够长期作为引擎渲染回归基线”。

## 2. 为什么要改

当前 Sandbox 默认注册表混合了三种不同意图：

- 资产管线 smoke
- scene/render bridge smoke
- Codex Logo demo 渲染

它在桥接未完成阶段是合理的，但现在已经不适合作为长期基线。

现状的问题：

- 很难判断哪条路径才是未来真正的标准引擎流程
- `SceneRenderFlowSmokeTest` 仍然带有明显的过渡期 “hook smoke” 语义
- `CodexLogoRenderTest` 验证的是 demo 路径，不是标准场景路径
- `AssetPipelineSmokeTest` 作为管线专项测试有价值，但不应该再作为 Sandbox 的主要运行体验

现在的 Sandbox 应该代表引擎的标准集成渲染路径，而不是多个无关 demo 的集合。

## 3. 推荐方向

采用 **单一标准 Sandbox 场景模式** 作为默认运行路径。

推荐基线为：

- 默认资产：`product/assets/models/gltfs/Sponza/glTF/Sponza.gltf`
- 默认行为：交互式自由相机
- 线程模型：逻辑线程负责 Scene 和相机更新，渲染线程负责可见帧提交
- 渲染链路：`Scene -> RenderScene -> VisibleRenderFrame -> SceneRenderer -> Renderer -> present`

旧的 Sandbox demo 测试不再进入默认注册表。

## 4. 功能需求

### 4.1 标准场景加载

在 Sandbox 启动时：

- 初始化 `AssetDatabase`
- 定位 Sponza 资产路径
- 异步加载模型
- 将模型实例化到新的逻辑 Scene 中
- 在该 Scene 中创建主摄像机实体
- 保存运行期状态，使逻辑线程后续可以持续更新该场景

以下任意情况都应视为失败：

- 找不到资产
- 异步模型加载抛异常
- 加载出的 Model 无效
- Scene 实例化失败
- 摄像机实体或摄像机组件创建失败

### 4.2 交互式自由相机

新增一个 Sandbox 专用自由相机控制器，在逻辑线程运行，直接消费现有的 `InputState` 线程快照。

控制方式：

- `W` / `S`：前进 / 后退
- `A` / `D`：左移 / 右移
- `Q` / `E`：下移 / 上移
- 按住右键：启用鼠标视角
- 鼠标移动：yaw / pitch 旋转
- 滚轮：调整移动速度
- `Shift`：加速移动

### 4.3 CBuffer / View 更新验证

相机运动应该自然驱动：

- 逻辑摄像机 Transform 更新
- `SceneView` 重建
- view / projection 矩阵更新
- 最终 object/view/projection shader 常量更新

不额外设计专门的 “cbuffer 测试 hook”。标准场景本身就应该是 cbuffer 更新验证。

### 4.4 渲染提交

每帧需要完成：

- 从逻辑 Scene 生成渲染侧可见数据
- 渲染线程消费当前可见帧
- 通过正常 present 路径把结果显示到屏幕

只有当系统持续产生非空可见帧并完成提交时，这条标准流程才算成立。

### 4.5 Smoke-Test 兼容性

新的标准场景必须继续兼容当前仓库里的验证流程。

也就是说：

- 可以在无人值守情况下运行到 smoke 时长结束
- 不需要人工交互也能稳定渲染
- 现有自动退出路径触发时，能够正常退出

交互式自由相机用于人工验证；自动验证仍然依赖稳定的无人值守运行。

## 5. 架构设计

### 5.1 高层结构

Sandbox 的运行形态应从“测试注册表 + 多个独立 demo”转向“标准场景运行时 + 可选专项测试”。

建议结构：

- `SandboxApplication` 继续作为进程入口和生命周期所有者
- 新增一个标准场景运行时对象，统一持有：
  - 场景加载状态
  - 当前 Scene
  - 自由相机运行时状态
  - 可见帧桥接状态
- 逻辑线程负责：
  - 资产 / Scene 就绪
  - 相机控制器更新
  - Scene 到 VisibleFrame 的构建
- 渲染线程负责：
  - 可见帧消费
  - 通过 `SceneRenderer` 提交渲染

这样能让 Sandbox 与当前引擎的线程所有权模型保持一致，而不是继续堆更多测试专用 hook。

### 5.2 去掉过渡期 Hook 语义

现有 `SandboxSceneRenderFlowHooks` / `SandboxSceneRenderFlowState` 在 bridge 尚未完成时有价值，但现在应该逐步收口成真正的标准场景运行时状态，而不是继续保留 “hook smoke” 的抽象。

推荐方向：

- 继续保留逻辑线程和渲染线程之间的共享状态对象
- 但它应表达标准场景运行时状态，而不是泛化的测试 hook 状态
- 逐步去掉以下这类过渡期状态名：
  - `AwaitingLogicIntegration`
  - `AwaitingRenderIntegration`
- 替换为更贴近运行时含义的状态，例如：
  - loading
  - ready
  - frame-ready
  - rendering
  - failed

这样日志和故障状态会更容易理解。

### 5.3 相机所有权

相机仍然应该是标准 `Scene` 实体，使用正常组件：

- `TransformComponent`
- `CameraComponent`

自由相机控制器只负责修改这些组件，不允许绕开 `Scene`，也不应引入后端特化状态。

这样才能保证：

- Engine / Sandbox 边界清晰
- 后续 Editor / Game 复用的是同一套场景与视图链路

### 5.4 可见帧交接

逻辑线程到渲染线程之间的交接，仍然应该维持“最新帧 / 双缓冲式”的共享状态模型。

要求：

- 逻辑线程可以安全构建 `VisibleRenderFrame`
- 渲染线程可以消费一致快照
- 状态同步清晰、线程安全

这里优先考虑正确性和可调试性，不需要为了避免锁而做过度复杂的设计。

## 6. 运行时行为

### 6.1 人工交互运行

正常启动时：

- Sandbox 加载 Sponza
- 场景显示到屏幕上
- 可以通过 `WASD + QE` 移动相机
- 按住右键后用鼠标调整视角
- 滚轮调节移动速度
- 程序持续运行，直到用户关闭窗口或引擎请求退出

### 6.2 验证运行

在现有 smoke-test 环境下：

- 仍然加载同一个 Sponza 场景
- 不依赖人工输入
- 即使相机不动，也能稳定完成渲染
- 到达 smoke 时长后按既有路径正常退出

这样才能继续兼容当前的四路验证流程。

## 7. 范围边界

### In Scope

- 替换默认 Sandbox 运行路径
- 引入 Sandbox 自由相机控制器
- 使用 Sponza 作为默认标准验证场景
- 调整 Sandbox 生命周期与状态管理以支撑新流程
- 更新描述 Sandbox 角色的相关文档

### Out of Scope

- 修改 Editor 代码
- 构建通用场景浏览器或关卡选择 UI
- 做完整 gameplay camera 系统
- 仅为了 Sandbox 而去重做灯光、后处理或材质系统
- 把 Sandbox 做成通用 Sample Framework

## 8. 测试策略

### 8.1 功能检查

- Sandbox 能成功启动并加载 Sponza
- 场景能显示到屏幕
- 相机移动后视角变化正确
- 右键鼠标视角工作正常
- 移动过程中渲染持续稳定

### 8.2 回归检查

由于这次改动会影响共享的 scene/render 行为，最终验收仍然需要：

- 正常 Premake + MSBuild 重编译
- `Sandbox` Vulkan
- `Sandbox` DX12
- `Editor` Vulkan
- `Editor` DX12

共享风险代码的通过标准仍然是：

- 无 validation error
- 无 debug-layer error
- 无资源泄露
- 正常定时退出

## 9. 风险

### 9.1 Sponza 启动成本

Sponza 比当前小样本模型更重，启动和首帧准备会更慢。这是预期行为，但意味着：

- 加载错误处理必须足够明确
- 日志要能清晰区分“加载慢”和“加载失败”

### 9.2 输入线程所有权

相机控制器运行在逻辑线程，因此必须严格使用 `Application` 已经维护好的线程输入快照，不能直接去读渲染线程事件状态。

### 9.3 Render / Logic 交接仍有过渡期包袱

如果实现时继续大量保留当前 bridge-smoke 的过渡结构，那么新的标准场景仍然会继承旧语义上的含糊性。因此在实现阶段应尽量把状态模型收口成标准运行时，而不是原样保留 smoke 抽象。

## 10. 推荐结论

建议直接推进：

- 一个新的默认 Sandbox 标准场景运行时
- Sponza 作为标准场景
- 逻辑线程上的交互式自由相机
- 从默认注册表中移除 Codex Logo 和旧 AssetPipeline demo

这样可以让 Sandbox 真正变成引擎当前 Scene-to-Render 架构的标准验证入口，并为后续 Vulkan / DX12 双后端渲染修改提供稳定、复杂、可重复的集成基线。
