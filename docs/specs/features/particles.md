---
owner: huyizhou
last_reviewed: 2026-07-13
status: active
---

# Feature Spec: GPU 粒子

## 行为

`ParticleComponent` 是第七个场景组件。每个组件描述一个世界空间发射器；渲染侧在 Sky 之后、Volumetric 之前执行 GPU 模拟和 billboard 绘制，因此粒子写入 HDR、参与后续体积光/Bloom/TAA/tone-map。粒子读取场景深度并做深度测试，不写深度。

粒子状态、随机生成候选、积分、压实和 indirect args 全部留在 GPU 且无 CPU 回读；CPU 只维护每 emitter 的发射余量并由 `spawn_rate × delta` 算出本帧 spawn count。GPU 链为 stable classify/count → block prefix scan → stable scatter → write indirect args。存活粒子保持原顺序，新生粒子稳定追加；一条 non-indexed indirect draw 生成 clip-space billboard，`firstInstance` 恒为 0。

billboard 采样 RGBA sprite，并将 lifecycle color 与 sprite color 相乘。径向项为 `radial = pow(saturate(1-dot(corner,corner)), max(radial_sharpness, 0.25))`，`shaped = lerp(1, radial, saturate(radial_falloff))`，基础 coverage 为 `lifecycle.a × sprite.a × shaped`。AlphaBlend 输出未预乘 RGB 与 coverage；Additive 输出 `RGB × coverage`。软粒子 variant 将 scene/particle device depth 用投影矩阵系数重建为线性 view depth，再以 `saturate((sceneLinear-particleLinear)/max(soft_fade_distance,0.001))` 衰减 coverage；normal/reverse-Z 都使用同一重建公式并分别识别 1/0 背景深度。

## 配置与序列化

- 容量/发射：`max_particles`（1..65536）、`spawn_rate`、`emitting`、`random_seed`。
- 生命周期/运动：`lifetime`、`lifetime_variance`、`initial_speed`、`spread_angle_degrees`、`constant_acceleration`；发射轴为实体局部 +Y。
- 外观：`start_size/end_size`、`start_color/end_color`、`blend_mode`。`sprite_texture_path` 是 RGBA sprite 资产引用；render/submit thread 按 sRGB 请求显式路径，空路径和加载失败降级为 White sprite。`radial_falloff`（默认 1.0，范围 0..1）是 sprite-only 与 analytic radial mask 的混合权重，`radial_sharpness`（默认 2.0，范围 0.25..8）是该径向 mask 的 power exponent；`soft_particles`（默认 true）控制粒子在 opaque scene depth 相交处淡出，`soft_fade_distance`（默认 0.25，范围 0.001..10）的单位是 world-space，表示软粒子的线性深度淡出区间。

scene JSON schema 当前为 version 6。`blend_mode` 写为 `Additive` / `AlphaBlend` 字符串，读取兼容旧整数；version 5 文件缺少上述五个外观键时使用各字段默认值。字符串或布尔字段类型错误时保留默认值；三个新增浮点字段先对非有限值回退默认值，再分别 clamp 到声明范围。`max_particles` 与 `random_seed` 只接受 JSON uint32 整数，负数、浮点和越界值无效。Scene add/set/load 使用相同 sanitize；随机流混入稳定序列化 `entity_id` 的高、低完整 64 位，因此只在高 32 位不同的实体也不会复用同一随机流。

## 实现

- Scene/提取：`Function/Scene/SceneComponents.h`、`Scene.h/.cpp`，以及 `RenderScene.h/.cpp` 的 `VisibleParticleEmitter`。
- 编排：`Function/Render/ParticleSystemPass.h/.cpp`；shader 为 `Shaders/Particles/ParticleSystem.hlsl`。
- 帧资产准备：logic thread 只复制组件/path；`ScenePresentationSubsystem::submit_presentations()` 在 render/submit thread 为可见 emitter 准备 sprite `TextureAsset`，并把 shared handle 固定到该帧快照。
- Editor：`Panels/Inspector/ParticleComponentEditor.*` 将草稿按固定顺序分为 Main、Emission、Shape & Motion、Size Over Lifetime、Color Over Lifetime、Renderer 六个折叠模块，其中 Main、Emission、Renderer 默认展开。Size/Color 模块各绘制一个只读的起点到终点预览，不引入额外可编辑曲线或关键帧。Renderer 的 sprite texture 字段复用通用 asset-path 控件，支持搜索、Asset Browser 拖放和 recent paths；空路径显示 `Using Default Particle Sprite (White)`，已知缺失路径警告但允许提交，已知非 Texture 类型阻止提交。Soft Particles 关闭时 Soft Fade Distance 置灰但保留原值。所有字段仍写入现有 Particle draft，经同一 sanitize 后提交 `SetParticleComponentCommand`；reset/restore/remove、保存重载、undo/redo 与连续字段 command merge 语义不变，组件继续参与 entity snapshot、复制和删除。
- 固定验证场景：`product/assets/scenes/Particles.scene.json` 同场覆盖默认 White sprite + Additive 的左侧 Spark、显式 RGBA sprite + AlphaBlend 的中间 Smoke，以及显式 RGBA sprite + Additive 的右侧 Magic。Smoke 穿过不透明 Avocado 遮挡物以验证 soft-depth，Magic 用朝右上的不对称三叉细节验证 UV 朝向；场景关闭 TAA、Bloom、AO、方向光阴影与体积光。

每 emitter 持有两个 32-byte 粒子池、block counts/offsets、counter 和 indirect args。状态 key 为 `{scene_runtime_id, entity_id}`；场景解绑显式释放。capacity、scene content epoch 或模拟参数 fingerprint 改变时只重置对应 emitter；fingerprint 只含 spawn/lifetime/lifetime variance/speed/spread/acceleration/seed，sprite、径向、soft、size、color、blend 等外观字段及 `emitting` 切换不重置模拟。

Function 层 indirect 契约：`StorageBufferDesc::indirect_args` 申请 indirect usage；`GraphicsDrawDesc::indirect_args_buffer/offset` 选择 indirect draw；消费前转到 `AshResourceState::IndirectArgs`。粒子 compute/draw 常量均严格为 128 bytes；draw block 包含 VP、projection scale/size、四个 half2 packed color、深度重建系数、径向/soft 参数和 flags，draw 路径要求反射尺寸严格相等，compute 后端拒绝超过反射 cbuffer 的上传。

draw program 固定为 Additive/AlphaBlend × SoftOff/SoftOn 四个 variant。SoftOn 将 scene depth 同时声明为 depth-test 与 shader resource 并绑定 `SceneDepth`；SoftOff 只声明 `DepthTestOnly`，shader 不反射、不解析也不采样 `SceneDepth`。

## 确定性

普通运行使用相邻实际进入 scene render 的新 Application frame 之间的真实间隔和 Application render frame index；一旦进入 render，即使该调用随后失败也消费该帧时间，避免非事务式失败后重复积分；未进入 render 或重复同帧 submit 不消费。frame-dump 模式使用固定 `delta_seconds=1/60` 和固定 seed；稳定压实避免 atomic 竞争改变顺序。capture 不用固定预热帧，而在每个 emitter 累计成功模拟的 spawn 数达到 `min(max_particles, ceil(spawn_rate × (lifetime + lifetime_variance)))` 后发出视觉稳定信号；固定 showcase 还要求 Smoke/Magic sprite 完成资产 activity-epoch 握手后才允许抓帧。正式门槛仍为 golden 0.995 / cross 0.99。

## 约束与已知限制

- 当前没有粒子材质资产、深度排序、碰撞、力场、sub-emitter、事件、ribbon/trail/mesh 粒子、光照、阴影或运动向量。
- AlphaBlend 不排序；TAA 对粒子没有 motion vector，可能出现顺序伪影或拖影。
- 逐 emitter 四次 dispatch + 一次 draw，不做跨 emitter 合批；GPU 内存按 emitter 容量线性增长。
- RenderGraph 仍不把 buffer 当一等资源；buffer 状态由 program binding 与 explicit indirect barrier 管理。

## 验证

- `RunTests.bat Debug`：组件描述符、schema、sanitize、content epoch 与 engine self-test。
- `RunRenderGate.bat -Scenes particles`：固定 showcase 的粒子双后端 golden 与跨后端 diff；候选图只有在用户目视确认 Spark、Smoke、Magic 与 soft-depth 相交均正确后，才能通过事务式 `-BlessGolden` 发布，始终禁止直接编辑 golden。
- 同参数抓两次 Vulkan、一次 DX12，用 AshImageDiff 验证确定性。
- 双后端 validation/debug layer 粒子场景 smoke，日志不得有 barrier、lifetime 或泄漏错误。
- Editor 自动化证据由 `RunTests.bat Debug` 中的 Inspector source-contract 用例和 Editor readiness smoke 提供；真实 UI 操作只由人类执行，Agent 交付时将以下项目标记“待人工验收”且未经回报不得声称 PASS：六个模块的顺序与默认展开状态、sprite 搜索/拖放/recent、空路径 White fallback、缺失路径可提交警告、非 Texture 阻塞、Soft Fade Distance 禁用保值、尺寸/颜色只读预览，以及保存/重载、reset/restore/remove、undo/redo。固定 RenderGate 场景覆盖 White/Additive Spark、RGBA/AlphaBlend Smoke 穿过 opaque depth，以及带不对称 UV 证据的 RGBA/Additive Magic。
- `RunPerfGate.bat -Profile Standard` 验证无粒子默认矩阵可运行并满足绝对上限；只有报告中存在 baseline 时才能进一步声称相对基线无回归。

## 历史

- [SDD-2026-07-10-gpu-particles](../../sdd/SDD-2026-07-10-gpu-particles.md)
- [SDD-2026-07-13-particle-visual-quality](../../sdd/SDD-2026-07-13-particle-visual-quality.md)
