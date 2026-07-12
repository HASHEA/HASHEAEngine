# SDD-2026-07-10-gpu-particles: GPU 粒子系统（compute 模拟 + indirect 绘制）（S2）

## Status
Done（2026-07-12；实现、Editor 人工链路、用户画面确认、双后端 golden bless 与最终门禁均完成）

## Context
三大系统路线阶段 1，阶段 0（SDD-2026-07-09-indirect-draw-substrate，已 Done）的首个真实消费者。
用户定案：直接 GPU 版（compute 模拟 + indirect draw），不做 CPU 过渡版。

按代码核实的现状（缺口自下而上）：
- RHI 层：三个 indirect API 已就绪（阶段 0）；`AshResourceState::IndirectArgs` 双后端映射完整
- **Function 层无 indirect 管道**：`GraphicsDrawDesc`（Renderer.h:74）无 indirect 字段，
  `StorageBufferDesc`（RenderDevice.h:290）无 indirect usage 位，barrier 收集
  （`collect_vertex_buffer_barrier` 等，RenderDevice.h:639-647）无 IndirectArgs 路径
- RenderGraph **只管 texture**（render-graph.md 职责边界明示不管 buffer）；无图 texture 访问的
  副作用 compute pass 走 `NeverCull`（既有约定）
- 场景组件固定六项（scene.md），新增组件 = 动场景数据模型（S2 定级依据之一）
- `VisibleRenderFrame` 无 delta time 字段；无固定时间步基建；frame-dump 模式已有
  「TAA jitter 强制 (0,0)」确定性先例（render.md）
- compute 侧 storage buffer 按名反射绑定（`set_storage_buffer`/`set_rw_storage_buffer`）、
  UAV/SRV 状态转换经 `transition_compute_program_resources` 已有；TAA/Volumetric 是
  compute→graphics 同帧依赖 + ping-pong 历史的现成模板

## Goals
- 第 7 个场景组件 `ParticleComponent`：发射器参数 + 序列化 + Inspector 编辑
- GPU 粒子链路：spawn/simulate/compact（compute，ping-pong 池）→ GPU 写 indirect args →
  billboard instanced indirect draw，全程零 CPU 回读
- Function 层 indirect 管道补全（`GraphicsDrawDesc` 扩展 + StorageBuffer indirect usage +
  IndirectArgs barrier 收集），作为通用能力（植被复用）
- 确定性：固定随机种子（组件属性）+ frame-dump 模式固定模拟步长（对齐 TAA jitter 约定），
  支撑 RenderGate 粒子 golden
- 遵守阶段 0 契约：firstInstance 恒 0，实例数据经 storage buffer 按 `SV_InstanceID` 索引

## Non-goals
- 粒子贴图/材质资产绑定（首刀程序化软圆点；材质化等材质 V2 的粒子扩展另立项）
- 按深度排序（Additive 无需排序；AlphaBlend 无序伪影首刀接受）
- 碰撞、力场、sub-emitter、GPU 事件、ribbon/trail/mesh 粒子、粒子光照/阴影
- 粒子运动向量（TAA 对粒子按无 MV 处理，拖影首刀接受，记录于 feature spec）
- RenderGraph buffer 一等资源化（粒子 buffer 由 pass 自持 + 既有 program 绑定 barrier 机制
  覆盖，唯一缺口是 IndirectArgs 转换，本 SDD 以最小管道补齐；graph 化留给未来真实需求）
- indirect count buffer 变体（阶段 0 Non-goal 延续：instanceCount 由 GPU 写，drawCount 恒 1）

## Baseline and implementation entry points（批准时）
- 组件新增六步路径：SceneComponents.h 结构体 + 枚举 → Scene.cpp 描述符表/序列化 →
  Entity API → Inspector editor 注册（EnvironmentComponent 为最近先例）
- 提取链：`RenderScene::rebuild_from_scene` → `VisibleRenderFrame`（不可变帧快照）→
  `SceneRenderer::render_visible_frame` 按 render_config 组 graph
- pass 序列（render.md）：GBuffer → AO → Shadow → DeferredLighting → Environment/Composite →
  Sky → Volumetric → Bloom → TAA → ToneMap
- compute pass 模板：TemporalAAPass.cpp:275-328（add_compute_pass + 按名绑定 + dispatch）；
  跨帧资源 pass 类自持、external 注册、view key 隔离（temporal 状态既有约定）

## Proposal

### 数据流

```
ParticleComponent（Scene，静态参数+seed）
  → RenderScene 提取 VisibleParticleEmitter（entity_id 为 key）
  → VisibleRenderFrame.particle_emitters + delta_seconds（新增字段）
  → ParticleSystemPass（自持 per-emitter GPU 状态，view 无关、按 entity_id 隔离）
       compute: stable classify/count → block prefix scan → stable scatter
       compute: write args（AshDrawIndirectArgs{6, alive, 0, 0}，firstInstance=0）
       raster:  billboard indirect draw → SceneDeferredSceneHDRLinear
  → 后续 Volumetric/Bloom/TAA/ToneMap 照常作用于粒子
```

- 四阶段稳定压实先保持 survivor 顺序，再稳定追加新生粒子；spawn 数由 CPU 侧确定性
  累加器算出（rate×dt 取整，余量滚存）经 const data 传入
- 绘制：非索引 `draw_indirect`，`SV_VertexID` 展开 billboard 四边形两三角，
  `SV_InstanceID` 索引池 B（SRV）；深度只读测试（read_depth），不写深度；
  混合 Additive / AlphaBlend 二选一（组件枚举）
- 绘制插槽：Sky 之后、Volumetric 之前——粒子进 HDR，天然吃雾/泛光/TAA/色调映射
- 模拟 compute pass 不触图 texture → `NeverCull`；buffer 状态转换走既有
  `transition_compute_program_resources` + 新增的 indirect args barrier，均在 pass 外提交
  （满足「barrier 不进 active render pass」硬约束）

### Module changes
| Module | Change | Files |
| --- | --- | --- |
| Function/Render（indirect 管道） | `StorageBufferDesc` 加 `bool indirect_args`（透传 `ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT`）；`GraphicsDrawDesc` 加 `indirect_args_buffer/indirect_args_offset`（非空即走 `cmd_draw_indirect`，drawCount=1）；draw 路径新增 `collect_indirect_args_barrier`（→ `AshResourceState::IndirectArgs`），对齐 vertex/index buffer barrier 先例 | `Renderer.h/.cpp`、`RenderDevice.h/.cpp` |
| Function/Scene | `ParticleComponent` 结构体 + `SceneComponentType::Particle` + 描述符表 + JSON 序列化 + Entity API | `SceneComponents.h`、`Scene.h/.cpp` |
| Function/Render（提取） | `VisibleParticleEmitter` + frame 的 particle/delta/runtime/content 字段；`RenderScene` 独立 particle version 增量同步 | `RenderScene.h/.cpp`、`ScenePresentationSubsystem.cpp` |
| Function/Render（pass） | `ParticleSystemPass`：按 scene runtime + entity 隔离 GPU 状态，四阶段稳定压实与 indirect draw；场景解绑显式释放 | `ParticleSystemPass.h/.cpp`（新）、`SceneRenderer.h/.cpp` |
| Shaders | 模拟 + args 写入（CS）、billboard（VS/PS），粒子结构体布局 HLSL/C++ 双侧对齐 | `Shaders/Particles/ParticleSystem.hlsl`（新） |
| Editor | `ParticleComponentEditor` 注册进 Inspector | `Panels/Inspector/`（新 editor 类） |
| Config | `SceneRenderConfig` 不动（粒子按组件存在与否驱动，无全局开关；调试期用 RenderDebugView 通道看 HDR 即可） | — |

### ParticleComponent 参数（首刀）
`max_particles`（上限 65536，池内存 2×32B×N/emitter）、`spawn_rate`（个/秒）、
`lifetime` ± `lifetime_variance`、`initial_speed` ± 锥形 `spread_angle`（沿实体 +Y，随
Transform）、`constant_acceleration`（vec3，默认重力）、`start_size/end_size`、
`start_color/end_color`（RGBA，线性插值）、`blend_mode`（Additive/AlphaBlend）、
`random_seed`（uint，默认 0）、`emitting`（bool）。

### API / contract changes
- `GraphicsDrawDesc` 新字段全部默认空/0，既有调用点零改动；indirect 与非 indirect 路径互斥
  （indirect_args_buffer 非空时忽略 vertex_count/instance_count）
- 粒子池结构为 position/age/velocity/lifetime，C++ `ParticleGPUData` 与 HLSL 均为 32 bytes；
  颜色/尺寸等发射器参数随 128-byte root constants 传入
- 确定性契约：同 seed + 同步长 + 同模拟进度提供稳定结果；frame-dump 模式
  `delta_seconds` 强制 1/60，普通运行按相邻实际进入 render 的新 frame 计算真实间隔；
  entity seed 混入完整 64-bit entity id
- 遵守阶段 0 契约①②③：args 布局 engine 权威、firstInstance 恒 0、消费前转 IndirectArgs

### Backend impact
Function 层新代码后端无关；触及 RHI 的只有既有 API 调用（阶段 0 已双后端验证）。
shader 经 DXC 双后端编译，RenderGate 跨后端 diff 覆盖粒子画面等价性。

### Performance
既有场景（无粒子组件）：唯一新增开销是空列表判断，PerfGate 水位应无变化。
粒子场景：每 emitter 每帧 4 次 dispatch + 1 次 indirect draw；
首刀不进 PerfGate 矩阵（粒子 perf 水位待场景稳定后另立）。

## Verification plan
| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 双后端构建 + 单测 + 架构边界 | 编译/回归/红线 | `build_editor.bat Debug`、`build_sandbox.bat Debug`、`RunTests.bat Debug`、`RunArchGate.bat` |
| 既有 golden 无回归 | 无粒子场景画面不变 | `RunRenderGate.bat -Scenes sandbox` 现有三项 PASS |
| 粒子 golden（新增） | 粒子画面 + 跨后端等价 | 新粒子测试场景加入 RenderGate 矩阵，用户确认画面后 `-BlessGolden` |
| 确定性 | 当前 asset-free 场景同 seed 两次 dump exact | 同参数连跑两次 `--dump-frame` 对比；正式门槛仍按 RenderGate SSIM |
| validation | barrier/生命周期 | Engine.ini 开双后端 validation 各跑粒子场景 smoke，日志无报错 |
| 编辑器链路 | 组件增删改/序列化/Inspector | Editor 手动：加组件→调参→保存→重载 |
| PerfGate | 无粒子默认矩阵运行成功并满足绝对上限；有 baseline 时再判断相对回归 | `RunPerfGate.bat -Profile Standard` |

## Task breakdown
1. ✅ Function 层 indirect 管道（StorageBuffer usage + DrawDesc + barrier）
2. ✅ ParticleComponent + schema v5 序列化 + Entity API + 描述符
3. ✅ 提取链、scene runtime/content identity 与显式状态释放
4. ✅ 稳定四阶段 ParticleSystemPass + billboard indirect draw + 双后端画面
5. ✅ Inspector/undo/snapshot + 粒子测试场景
6. ✅ 确定性与参数推导 capture-ready；Editor 手工加组件→调参→保存→重载、AlphaBlend 深度遮挡、用户画面确认、粒子 golden bless 与最终门禁矩阵
7. ✅ 长期 spec 回写

## Verification evidence（2026-07-12）

- 单元与契约：36/36 cases、425/425 assertions PASS；覆盖 schema/sanitize、indirect 契约、语义 capture-ready、完整 64-bit entity seed 与 readiness/present/CLI 契约。
- 构建与架构：Editor、Sandbox Debug 构建 PASS；ArchGate PASS（只保留既有 legacy warning）。
- Additive 候选：同后端重复抓帧 Vulkan/DX12 各自 exact；跨后端 SSIM 1.0、max diff 1、1 pixel。用户确认画面后，`RunRenderGate.bat -Scenes particles -BlessGolden` 事务发布成功；报告 `Intermediate/test-reports/render-gate/20260712-155108-228-48200-fb1691a9/`，跨后端 SSIM 1.0。
- AlphaBlend：临时场景 Vulkan/DX12 readiness capture 均成功并正常退出；`Intermediate/test-reports/particles-alpha-{vulkan,dx12}.png` 的 SHA-256 相同，AshImageDiff 复核 cross SSIM 1.0、max diff 0；临时 scene 已删除。
- Editor 人工链路：在隔离场景中通过 Inspector 添加 Particle、设置 `spawn_rate=200` 与 `AlphaBlend`、保存并关闭；重新启动后 Inspector 与 scene JSON 均保留该组件和值。把发射器置于头盔后方观察到模型覆盖区域正确遮挡粒子，Console 为 Warn 0 / Error 0；验证副本与截图位于 `Intermediate/test-temp/editor-particle-validation/`。
- validation：Vulkan GPU-assisted/synchronization 与 DX12 GPU validation 下粒子 stable capture 均成功，无 barrier、lifetime 或泄漏错误（报告 `Intermediate/test-reports/validation/20260711-211129-009/`）。
- 最终渲染门禁：默认 `RunRenderGate.bat` 全矩阵 PASS；Sandbox Vulkan/DX12 SSIM 0.996278/0.996177、cross 0.999747，Particles 两后端与 golden 均为 1.0、cross 1.0（报告 `Intermediate/test-reports/render-gate/20260712-155130-523-57884-6a47628a/`）。
- 性能：PerfGate Standard 四组合满足运行与绝对上限，但四项 baseline 均缺失，不能据此声称相对基线无回归（fresh 报告 `Intermediate/test-reports/perf-gate/20260712-155843/`）。

## Risks
| Risk | Mitigation |
| --- | --- |
| TAA 对无 MV 粒子产生拖影/闪烁，粒子 golden 不稳定 | 确定性步长下静态收敛；capture-ready 从 `spawn_rate × (lifetime + variance)` 推导稳定窗口，不使用固定预热帧；仍不稳则 golden 场景关 TAA（config 可控） |
| ping-pong 池内存随 emitter 数线性膨胀 | max_particles 上限 65536 + Inspector 提示池内存；emitter 数首刀不设硬限，spec 记录风险 |
| 逐 emitter dispatch/draw 在多 emitter 场景开销大 | 首刀接受（验证基建为主）；合批/共享大池留给真实需求（植被 SDD 前评估） |
| CPU spawn 累加器与 GPU 压实交互出错（粒子泄漏/爆池） | 稳定 scan/scatter 统一按 capacity 截断，长时 smoke 与双后端 validation 检查 |
| delta_seconds 引入位置不当或 logic 高频 prepare 丢时 | 普通模式以实际进入 render 的新 frame 为时钟（失败调用也消费，避免重复积分）并使用 Application frame index；dump 模式深拷贝后覆盖为 1/60 |

## Open questions
- 无（绘制插槽、混合模式、确定性方案均已按现状约束选定；如需粒子受光照/阴影，另立 SDD）
