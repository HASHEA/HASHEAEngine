---
owner: huyizhou
last_reviewed: 2026-07-12
status: active
---

# Feature Spec: GPU 粒子

## 行为

`ParticleComponent` 是第七个场景组件。每个组件描述一个世界空间发射器；渲染侧在 Sky 之后、Volumetric 之前执行 GPU 模拟和 billboard 绘制，因此粒子写入 HDR、参与后续体积光/Bloom/TAA/tone-map。粒子读取场景深度并做深度测试，不写深度。

粒子状态、随机生成候选、积分、压实和 indirect args 全部留在 GPU 且无 CPU 回读；CPU 只维护每 emitter 的发射余量并由 `spawn_rate × delta` 算出本帧 spawn count。GPU 链为 stable classify/count → block prefix scan → stable scatter → write indirect args。存活粒子保持原顺序，新生粒子稳定追加；一条 non-indexed indirect draw 生成软圆点 billboard，`firstInstance` 恒为 0。

## 配置与序列化

- 容量/发射：`max_particles`（1..65536）、`spawn_rate`、`emitting`、`random_seed`。
- 生命周期/运动：`lifetime`、`lifetime_variance`、`initial_speed`、`spread_angle_degrees`、`constant_acceleration`；发射轴为实体局部 +Y。
- 外观：`start_size/end_size`、`start_color/end_color`、`blend_mode`。

scene JSON schema 当前为 version 5。`blend_mode` 写为 `Additive` / `AlphaBlend` 字符串，读取兼容旧整数；`max_particles` 与 `random_seed` 只接受 JSON uint32 整数，负数、浮点和越界值无效。Scene add/set/load 使用相同 sanitize；随机流混入稳定序列化 `entity_id` 的高、低完整 64 位，因此只在高 32 位不同的实体也不会复用同一随机流。

## 实现

- Scene/提取：`Function/Scene/SceneComponents.h`、`Scene.h/.cpp`，以及 `RenderScene.h/.cpp` 的 `VisibleParticleEmitter`。
- 编排：`Function/Render/ParticleSystemPass.h/.cpp`；shader 为 `Shaders/Particles/ParticleSystem.hlsl`。
- Editor：`Panels/Inspector/ParticleComponentEditor.*` + `SetParticleComponentCommand`；组件参与 entity snapshot、复制、删除与 undo/redo。
- 验证场景：`product/assets/scenes/Particles.scene.json`，纯程序化 additive fountain，无外部资产依赖。

每 emitter 持有两个 32-byte 粒子池、block counts/offsets、counter 和 indirect args。状态 key 为 `{scene_runtime_id, entity_id}`；场景解绑显式释放。capacity、scene content epoch 或模拟参数 fingerprint 改变时只重置对应 emitter；外观字段和 `emitting` 切换不会错误清空其他 emitter。

Function 层 indirect 契约：`StorageBufferDesc::indirect_args` 申请 indirect usage；`GraphicsDrawDesc::indirect_args_buffer/offset` 选择 indirect draw；消费前转到 `AshResourceState::IndirectArgs`。粒子 compute/draw 常量当前均为 128 bytes；draw 路径显式要求反射尺寸严格相等，compute 后端拒绝超过反射 cbuffer 的上传。

## 确定性

普通运行使用相邻实际进入 scene render 的新 Application frame 之间的真实间隔和 Application render frame index；一旦进入 render，即使该调用随后失败也消费该帧时间，避免非事务式失败后重复积分；未进入 render 或重复同帧 submit 不消费。frame-dump 模式使用固定 `delta_seconds=1/60` 和固定 seed；稳定压实避免 atomic 竞争改变顺序。capture 不用固定预热帧，而在每个 emitter 累计成功模拟的 spawn 数达到 `min(max_particles, ceil(spawn_rate × (lifetime + lifetime_variance)))` 后发出视觉稳定信号。当前 asset-free 场景同后端重复抓帧 Vulkan/DX12 各自 exact；跨后端实测仅 1 pixel 的 1 级差值。正式门槛仍为 golden 0.995 / cross 0.99。

## 约束与已知限制

- 首版没有粒子贴图/材质资产、深度排序、碰撞、力场、sub-emitter、事件、ribbon/trail/mesh 粒子、光照、阴影或运动向量。
- AlphaBlend 不排序；TAA 对粒子没有 motion vector，可能出现顺序伪影或拖影。
- 逐 emitter 四次 dispatch + 一次 draw，不做跨 emitter 合批；GPU 内存按 emitter 容量线性增长。
- RenderGraph 仍不把 buffer 当一等资源；buffer 状态由 program binding 与 explicit indirect barrier 管理。

## 验证

- `RunTests.bat Debug`：组件描述符、schema、sanitize、content epoch 与 engine self-test。
- `RunRenderGate.bat -Scenes particles`：粒子双后端 golden 与跨后端 diff；基线已于 2026-07-12 经用户确认后由事务式 `-BlessGolden` 发布，后续仍禁止直接编辑 golden。
- 同参数抓两次 Vulkan、一次 DX12，用 AshImageDiff 验证确定性。
- 双后端 validation/debug layer 粒子场景 smoke，日志不得有 barrier、lifetime 或泄漏错误。
- Editor 手工覆盖添加组件、改参、保存/重载、`AlphaBlend` 与深度遮挡；默认 golden 只覆盖 Additive fountain，AlphaBlend 的无排序限制不由该图覆盖。
- `RunPerfGate.bat -Profile Standard` 验证无粒子默认矩阵可运行并满足绝对上限；只有报告中存在 baseline 时才能进一步声称相对基线无回归。

## 历史

- [SDD-2026-07-10-gpu-particles](../../sdd/SDD-2026-07-10-gpu-particles.md)
