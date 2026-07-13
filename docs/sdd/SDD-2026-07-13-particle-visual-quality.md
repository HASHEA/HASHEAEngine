# SDD-2026-07-13-particle-visual-quality: GPU 粒子外观与软相交补全（S2）

## Status

Review

## Context

GPU 粒子初版已经具备稳定 compute 压实、indirect billboard draw、场景组件与 Editor 编辑链路，但像素外观固定为 `(1-r²)²` 的程序化柔和圆点。`Particles.scene.json` 使用 `start_size=0.22`，且明确关闭 TAA、Bloom、AO 与体积光；引擎没有 DOF/Bokeh pass。现有偏大柔光圆点不是失焦或后处理问题，而是粒子外观能力不足。

本变更补齐 sprite texture、可调径向 falloff/sharpness 与 depth soft-particle，并把 Particle Inspector 改为 Unity 风格的折叠模块。变更跨 Scene schema、Asset/Render 提取、Render Pass/shader 与 Editor，且改变粒子 golden，因此按 S2 处理；本 SDD 经用户批准前不得修改代码或基线。

## Goals

- 每个 emitter 可选择 RGBA sprite texture；空路径使用始终可用的引擎默认纹理。
- 保留并参数化现有径向轮廓，支持纯贴图形状和程序化 falloff 的连续混合。
- 支持按世界空间深度距离淡出的 soft particles，消除透明 billboard 与不透明几何的硬交线。
- Particle Inspector 按 Main、Emission、Shape & Motion、Size Over Lifetime、Color Over Lifetime、Renderer 六个模块组织。
- 火花、烟雾、魔法光点三个 emitter 同场覆盖默认纹理、显式纹理、两种混合模式与 soft-depth。
- 所有新增行为 Vulkan/DX12 等价；不扩展 RHI，不超过 Vulkan 保底 128B push constant 契约。
- 外观字段变化不重置现存 GPU 粒子模拟。

## Non-goals

- 通用多关键点曲线/Gradient 数据模型与可交互曲线编辑器。
- Niagara 式可增删模块栈、节点图、粒子材质 Domain/Family 或 Material V2 集成。
- Flipbook/texture sheet animation、sprite rotation、stretched billboard、mesh/ribbon/trail 粒子。
- AlphaBlend 深度排序、粒子光照/阴影、运动向量、碰撞、力场、sub-emitter 或事件。
- RenderGraph buffer 一等资源化、RenderGraph 核心 API 或 Graphics/RHI 接口改动。
- 独立粒子资产编辑器或预览窗口；本期使用选中场景实体的实时 viewport 反馈。

## Current implementation

- Entry points:
  - `ParticleComponent` 与 scene v5 schema：`project/src/engine/Function/Scene/SceneComponents.h`、`Scene.cpp`。
  - 提取链：`Scene::extract_particle_entities` → `RenderScene::rebuild_particles_from_scene` → `VisibleParticleEmitter`。
  - 帧编排：`SceneRenderer` 在 Sky 之后、Volumetric 之前调用 `ParticleSystemPass::add_passes`。
  - GPU：`ParticleSystemPass.cpp` 每 emitter 四次 compute dispatch + 一次 indirect raster draw；shader 为 `Shaders/Particles/ParticleSystem.hlsl`。
  - Editor：`ParticleComponentEditor.cpp` 当前在单个折叠区内平铺全部字段。
- Modules: Function/Scene、Function/Asset/Render、Function/Render、Shaders、Editor/Panels/Inspector。
- Data flow: Scene 静态 emitter 参数随不可变 `VisibleRenderFrame` 进入 pass；per-emitter GPU 状态以 `{scene_runtime_id, entity_id}` 隔离。
- Known constraints:
  - draw root constants 已占满 128B。
  - draw pass 目前只以 `DepthTestOnly` 读取 depth，pixel shader 不采样 Scene Depth。
  - `RenderAssetManager` 已支持异步 2D texture、sRGB 解码、fallback 与 readiness activity epoch。
  - 共享 graphics program 的绑定在 pass end 解析，因此现有设计必须保持一 emitter 一 raster pass。
  - 当前 particles golden 只有无外部资产的 Additive fountain，场景没有不透明遮挡物，无法覆盖 soft-particle。

## Proposal

### Runtime component and coverage contract

`ParticleComponent` 新增五个外观字段：

| Field | Type | Default | Valid range / meaning |
| --- | --- | --- | --- |
| `sprite_texture_path` | `std::string` | empty | 相对 `product/assets` 的 Texture 路径；空值使用默认白色纹理 |
| `radial_falloff` | `float` | `1.0` | `[0, 1]`；0=只使用 sprite，1=完整叠加径向 mask |
| `radial_sharpness` | `float` | `2.0` | `[0.25, 8]`；径向 power curve 指数 |
| `soft_particles` | `bool` | `true` | 是否使用 Scene Depth 淡出 |
| `soft_fade_distance` | `float` | `0.25` | `[0.001, 10]` 世界空间单位 |

pixel shader 先计算生命周期 tint，再采样 sRGB sprite（采样后 RGB 为线性，Alpha 不做 sRGB 变换）。覆盖率契约为：

```text
radial = pow(saturate(1 - dot(corner, corner)), radial_sharpness)
coverage = lifetimeColor.a * sprite.a * lerp(1, radial, radial_falloff) * softDepthFade

AlphaBlend.rgb = sprite.rgb * lifetimeColor.rgb
AlphaBlend.a   = coverage
Additive.rgb   = sprite.rgb * lifetimeColor.rgb * coverage
Additive.a     = coverage
```

空路径使用 `RenderAssetManager::request_fallback_texture(TextureFallbackKind::White)`，因此默认值精确复现现有 `(1-r²)²` 轮廓。显式路径加载失败时继续使用 manager 提供的 fallback resource 以保持可见，但保留 Failed 状态、错误日志与 readiness failure，禁止静默吞掉资产错误。

### Soft-particle contract

soft variant 将 raster pass 的 depth 声明从 `DepthTestOnly` 改为 `DepthTestAndShaderResource`，执行时同时绑定 `SceneDepth`。pixel shader 用 `SV_Position.xy` 做整数 texel load，使用 projection 的四个深度系数把 scene device depth 与当前 particle device depth重建为同一线性 view-depth：

```text
softDepthFade = saturate(
    (sceneLinearDepth - particleLinearDepth) / max(soft_fade_distance, 0.001)
)
```

背景 depth 返回 1.0。重建必须覆盖 perspective、orthographic、normal-Z 与 reverse-Z；禁止直接比较非线性 device depth。硬件 depth test 仍开启且不写 depth。

创建四个 draw program variant：Additive/AlphaBlend × SoftOff/SoftOn。SoftOff 继续使用 `DepthTestOnly`，不声明、不绑定、不采样 Scene Depth，避免关闭功能时支付 depth load。

### 128-byte draw constants

不扩 `GraphicsDrawDesc`、RHI push constant 或后端能力。draw constant 保持严格 128B：

| Data | Bytes | Notes |
| --- | ---: | --- |
| view-projection matrix | 64 | 世界中心变换 |
| projection scale + start/end size | 16 | clip-space billboard 展开，替代 camera right/up 两个 vec4 |
| FP16 packed start/end RGBA | 16 | HLSL `f16tof32` 解包，保留足够颜色精度 |
| projection depth reconstruction coefficients | 16 | normal/reverse、perspective/ortho 共用公式 |
| radial/soft parameters + flags | 16 | falloff、sharpness、fade distance、soft/reverse flags |

shader reflection 的 `AshRootConstants.byte_size` 必须继续严格等于 C++ `ParticleDrawConstants`，并保留 `static_assert(sizeof(...) == 128)`。

### Asset and frame data flow

`RenderScene::rebuild_particles_from_scene` 接收 `RenderAssetManager&`，为每个 emitter 请求 sprite `TextureAsset`，并把共享句柄保存到 `VisibleParticleEmitter`。空路径直接请求 ready 的 White fallback；显式路径进入既有 async decode/finalize、activity epoch 与 failure 统计。

`VisibleRenderFrame` 构建前由现有 `finalize_pending_assets()` 推进 texture 终态，draw 时读取 `TextureAsset::resource` 的最新值。shader 反射资源 `ParticleSprite` 与 sampler 在所有 variant 中始终绑定；soft variant 额外绑定 `SceneDepth`。程序释放或 emitter/scene 回收时清空 texture/storage buffer/sampler 引用，避免资源被 program binding 延长生命周期。

`simulation_config_hash` 仍只包含 spawn/lifetime/speed/spread/acceleration/seed。新增 sprite/radial/soft 字段，以及既有 size/color/blend/emitting，均不得进入模拟重置条件。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Function/Scene | 新增字段、descriptor、sanitize、scene v6 JSON save/load 与 v5 默认兼容 | `SceneComponents.h`、`Scene.cpp` |
| Function/Render extraction | 请求 sprite 资产并随 emitter/frame 快照传递 | `RenderScene.h/.cpp`、`ScenePresentationSubsystem.cpp` |
| Function/Render particle pass | 四个 draw variant、sprite/sampler/depth 绑定、128B constants 重排、外观字段不重置模拟 | `ParticleSystemPass.h/.cpp` |
| Shaders | sprite RGBA、径向参数、clip-space billboard、线性深度 soft fade | `Shaders/Particles/ParticleSystem.hlsl` |
| Editor | 六个折叠模块、Texture picker、两点 size/gradient 只读预览、警告/sanitize/比较/undo | `ParticleComponentEditor.cpp`、`InspectorPanelState.h`、`InspectorComponentEditorSupport.h/.cpp`、`EditorComponentComparison.cpp` |
| Tests | descriptor/schema/sanitize/legacy/fallback 与现有粒子回归 | `project/src/tests/Scene/particle_component_tests.cpp` |
| Assets / scene | smoke、magic RGBA sprite；三 emitter showcase 与不透明遮挡物 | `product/assets/textures/particles/`、`product/assets/scenes/Particles.scene.json` |
| Specs | 回写最终 scene/render/editor/particle 现状 | `docs/specs/features/particles.md`、`docs/specs/modules/scene.md`、`render.md`、`editor.md` |

### API / contract changes

- Scene JSON version 从 5 升到 6；v5 或缺字段输入按上表 defaults 加载。
- `sprite_texture_path` descriptor 使用 `ScenePropertyType::String`、`ScenePropertyEditorHint::AssetPath`、`ScenePropertyAssetRefKind::Texture`。
- 纹理路径不存在允许 Scene 保存；Editor 显示警告，runtime fallback 可见，但自动化 readiness 仍失败。
- `VisibleParticleEmitter` 新增内部 `std::shared_ptr<TextureAsset>`，不向 Editor/Sandbox 暴露 Graphics 或 RHI 类型。
- Function 层沿用既有 named reflection binding；无 DynamicRHI、RenderGraph core 或 Material V2 合约变更。

### Inspector behavior

现有 `Particle` 外层 header 保留，内部按以下顺序绘制独立、稳定 ID 的折叠模块：

1. Main：Emitting、Max Particles、Random Seed。
2. Emission：Spawn Rate、Lifetime、Lifetime Variance。
3. Shape & Motion：Initial Speed、Spread Angle、Acceleration。
4. Size Over Lifetime：Start/End Size + 只读两点折线预览。
5. Color Over Lifetime：Start/End Color + 带 checkerboard 的只读 RGBA gradient 预览。
6. Renderer：Sprite Texture、Blend Mode、Radial Falloff、Radial Sharpness、Soft Particles、Soft Fade Distance。

Sprite picker 复用 `InspectorAssetPathWidgets` 的搜索、拖放和 recent paths，仅过滤 Texture。空路径显示 `Default Particle Sprite`。缺失路径给 warning 但不阻塞 commit；错误资产类型仍阻塞。Soft 关闭时 fade distance 控件置灰但保留值。

模块展开状态为 Editor session-only，不序列化、不入 undo。全部字段仍写入 Particle draft，统一 sanitize 后经 `SetParticleComponentCommand` 提交；连续编辑沿用现有 command merge。Reset/Restore/Remove 覆盖新增字段。preview 只调用 `UIContext` 的 `dummy`、window rect/line 等稳定接口，Editor 不直接使用 ImGui/Graphics。

### Visual showcase

保留单个 `particles` RenderGate 场景，避免扩工具场景注册：

- Spark：左侧，空 `sprite_texture_path`、Additive、小尺寸、高 sharpness，覆盖默认纹理与径向 mask。
- Smoke：中间，显式透明烟雾 sprite、AlphaBlend、低/零 radial falloff、尺寸增大并淡出。
- Magic：右侧，显式彩色魔法 sprite、Additive，覆盖 sprite RGB × lifecycle tint。
- 复用仓库现有小型模型作为稳定不透明遮挡物，使 Smoke 穿过其深度边界并在同一图中明确展示 soft fade。

场景继续关闭 TAA、Bloom、AO、阴影与体积光，使 golden 直接观察 sprite 边缘、颜色和深度相交。烟雾/魔法 sprite 是仓库内固定 RGBA PNG；不使用运行时生成纹理或外部网络资产。

### Backend impact

不修改 `project/src/engine/Graphics/`。Vulkan/DX12 复用既有：

- D32 depth 的 DSV read + graphics SRV combined state；
- named texture/sampler reflection binding；
- HLSL 经 DXC 编译为 SPIR-V/DXIL；
- indirect draw 与 `firstInstance==0` 契约。

所有 shader 资源在各 variant 中必须完整绑定。任何跨后端 UV、sRGB、深度重建或边缘差异均视为 bug，以 RenderGate cross-backend diff 和 validation 定位，不允许后端特化泄漏到 Function/Scene。

### Performance

- Compute dispatch 数、粒子池结构与内存保持不变。
- Raster 仍是一 emitter 一 pass/一 draw。
- 所有粒子增加一次 sprite sample；SoftOn 每个存活 fragment 再增加一次 depth texel load，SoftOff 无 depth sample。
- 初始化 graphics program 从 2 个增至 4 个，只有 pipeline/program 常驻开销。
- 既有无粒子场景仅增加空列表判断后的零开销；`PerfGate Standard` 保护默认矩阵绝对上限，但当前无 baseline，不能据此宣称相对性能无回归。
- 不更新 perf baseline；若出现 FAIL 必须修复，WARN 必须在交付说明中解释。

## Verification plan

| 验证 | 覆盖 | 命令 / 方法 |
| --- | --- | --- |
| 计划/路径矩阵 | dirty path 与必跑项 | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan` |
| 双目标 Debug 构建 | Engine/Editor/Sandbox 编译与 shader 冷编译入口 | `build_editor.bat Debug`、`build_sandbox.bat Debug` |
| 单元测试 | descriptor=19、v6 roundtrip、v5 defaults、malformed/sanitize、现有 warmup/seed | `RunTests.bat Debug` |
| 架构边界 | Base ← Graphics ← Function ← Editor/Sandbox | `RunArchGate.bat` |
| 生命周期 smoke | scene/asset/editor 全链 | `run.bat all Debug --smoke-test-seconds=120` |
| Vulkan validation | combined depth read、descriptor、lifetime | 开启 Vulkan validation/GPU assisted/sync validation 后：`run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120`；检查日志 |
| DX12 validation | depth SRV/DSV、descriptor、resource state | 开启 DX12 validation/GPU validation 后：`run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120`；检查日志 |
| 性能门禁 | 默认四组合绝对上限 | `RunPerfGate.bat -Profile Standard` |
| 候选视觉 | 双后端预期 golden fail、cross-backend diff、heatmap | `RunRenderGate.bat`；保存候选图供用户目视确认，不 bless |
| Editor 人工 | 六模块、picker/drop、warning/fallback、save/reload、Reset/Restore、undo/redo | `run.bat editor` 手工操作粒子组件 |
| Golden 发布 | 仅在用户确认 Spark/Smoke/Magic 与 soft fade 正确后 | `RunRenderGate.bat -Scenes particles -BlessGolden` |
| 最终渲染门禁 | 新 golden、sandbox 无回归、跨后端 diff | `RunRenderGate.bat` |
| 文本质量 | whitespace / patch | `git diff --check` |

候选图未获用户确认时，任务停在等待确认状态；禁止直接编辑 `tools/render/goldens/`，也禁止提前运行 `-BlessGolden`。

## Task breakdown

1. 先扩展 `particle_component_tests.cpp`，锁定五个新字段、19 个 descriptor 属性、v6/v5/malformed/sanitize 契约；验收为新增测试先失败且失败原因与缺失字段一致。
2. 实现 Scene 字段、sanitize、serialization 与 extraction asset request；验收为 `RunTests.bat Debug` 通过、空路径 ready fallback 与显式失败 fallback 行为符合契约。
3. 实现四个 draw variant、128B constants 重排、sprite/radial/soft-depth shader；验收为双后端构建通过，SoftOff 不声明 depth SRV，SoftOn validation 无错误。
4. 重组 Particle Inspector 六模块并补 picker、preview、warning、compare/sanitize；验收为保存重载与 undo/redo 手工链路正确。
5. 加入 smoke/magic sprite 与三 emitter showcase/遮挡物；验收为两后端候选图均清楚呈现三种外观和 soft intersection。
6. 跑完整验证矩阵，向用户提交候选图；只有确认后 bless particles golden，再重跑默认完整 RenderGate。
7. 把最终行为回写 `docs/specs/`，将本 SDD Status 改为 Done；提交说明记录根因、影响范围、验证证据和任何 PerfGate WARN 判断。

## Risks

| Risk | Mitigation |
| --- | --- |
| constants 超过 Vulkan 128B 或反射布局不一致 | clip-space billboard + FP16 packing；C++ static_assert 与运行时 reflection byte-size 严格校验 |
| normal/reverse-Z 或 ortho 深度重建错误 | 统一 projection coefficient 公式；showcase + 双后端 validation/cross diff；禁止 raw device-depth 差值 |
| 非圆形 sprite 被径向 mask 意外裁切 | `radial_falloff=0` 完全关闭程序化 radial；Smoke/Magic golden 覆盖 |
| sRGB/UV 约定造成后端色差或翻转 | sprite 固定 sRGB；同一 HLSL；使用非对称测试纹理内容并检查跨后端 diff |
| program 绑定残留延长 texture/buffer 生命周期 | 保持一 emitter 一 pass；shutdown/release/prune 时清空所有 variant bindings |
| 外观编辑错误清空模拟 | 新字段不进入 simulation hash；Editor 手工确认存活粒子连续性 |
| 缺失纹理被 fallback 隐藏 | 画面 fallback 与 Failed readiness/日志并存；Editor 显示非阻塞 warning |
| AlphaBlend 无排序产生伪影 | 保持已知限制；Smoke 控制密度与观察角度，不在本 SDD 引入排序 |
| 预期画面变化误当回归直接覆盖基线 | 先非 bless 候选 + heatmap + 用户目视确认；只能走事务式 `-BlessGolden` |
| 共享工作树已有用户修改被覆盖 | 不修改 `Sandbox.scene.json`、`product/config/editor/imgui.ini`、现有未跟踪文档/抓帧；只操作本 SDD 与批准后的明确文件范围 |

## Open questions

None. 用户已确认：首期同时完成 sprite、radial falloff/sharpness 与 soft particles；空路径使用默认纹理；验收同时包含 Spark、Smoke、Magic；Editor 采用模块化 Inspector；上述三节设计均已确认。
