# SDD-2026-07-13-particle-visual-quality: GPU 粒子外观与软相交补全（S2）

## Status

Approved（2026-07-13，用户确认三节设计并明确批准 SDD）

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

背景 depth 等于当前 Z convention 的 far clip depth（normal-Z 为 1.0，reverse-Z 为 0.0）。重建必须覆盖 perspective、orthographic、normal-Z 与 reverse-Z；禁止直接比较非线性 device depth。硬件 depth test 仍开启且不写 depth。

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

`RenderScene::rebuild_particles_from_scene` 仍是 logic-thread-safe 的 CPU snapshot，只复制含 sprite path 的 `ParticleComponent`，不调用任何 GPU asset API。`ScenePresentationSubsystem::submit_presentations` 在 render thread 的 visible-frame prepare 阶段，为每个 emitter 请求 sprite `TextureAsset` 并把共享句柄写入 `VisibleParticleEmitter`。空路径直接请求 ready 的 White fallback；显式路径进入既有 async decode/finalize、activity epoch 与 failure 统计。

每次 render-thread submit 先由现有 `finalize_pending_assets()` 推进上一帧已请求 texture 的终态，再 prepare 当前 frame；新请求先持有 White resource，后续 submit 通过同一共享 `TextureAsset` 看到 resource 替换。draw execute 闭包按值捕获 `TextureAsset`，执行时读取其当前 `resource`。shader 反射资源 `ParticleSprite` 与 sampler 在所有 variant 中始终绑定；soft variant 额外绑定 `SceneDepth`。程序释放或 emitter/scene 回收时清空 texture/storage buffer/sampler 引用，避免资源被 program binding 延长生命周期。

`simulation_config_hash` 仍只包含 spawn/lifetime/speed/spread/acceleration/seed。新增 sprite/radial/soft 字段，以及既有 size/color/blend/emitting，均不得进入模拟重置条件。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Function/Scene | 新增字段、descriptor、sanitize、scene v6 JSON save/load 与 v5 默认兼容 | `SceneComponents.h`、`Scene.cpp` |
| Function/Render extraction | logic thread 复制路径；render-thread submit prepare 请求 sprite 并写入 emitter/frame 快照 | `RenderScene.h`、`ScenePresentationSubsystem.cpp` |
| Function/Render particle pass | 四个 draw variant、sprite/sampler/depth 绑定、128B constants 重排、外观字段不重置模拟 | `ParticleSystemPass.h/.cpp` |
| Shaders | sprite RGBA、径向参数、clip-space billboard、线性深度 soft fade | `Shaders/Particles/ParticleSystem.hlsl` |
| Editor | 六个折叠模块、Texture picker、两点 size/gradient 只读预览、警告/sanitize/比较/undo | `ParticleComponentEditor.cpp`、`InspectorPanelState.h`、`InspectorComponentEditorSupport.h/.cpp`、`EditorComponentComparison.cpp` |
| Tests | descriptor/schema/sanitize/legacy、四 depth convention、simulation fingerprint 与现有粒子回归 | `project/src/tests/Scene/particle_component_tests.cpp`、`project/src/tests/Function/particle_depth_reconstruction_tests.cpp` |
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
| 单元测试 | descriptor=19、v6 roundtrip、v5 defaults、malformed/sanitize、四种 depth convention、simulation fingerprint、现有 warmup/seed | `RunTests.bat Debug` |
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
2. 实现 Scene 字段、sanitize、serialization 与 render-thread frame asset prepare；验收为 `RunTests.bat Debug` 通过、空路径 ready fallback、有效纹理 Loading→Ready 与显式失败 fallback/readiness 行为符合契约。
3. 实现四个 draw variant、128B constants 重排、sprite/radial/soft-depth shader；验收为双后端构建通过，SoftOff 不声明 depth SRV，SoftOn validation 无错误。
4. 重组 Particle Inspector 六模块并补 picker、preview、warning、compare/sanitize；验收为保存重载与 undo/redo 手工链路正确。
5. 加入 smoke/magic sprite 与三 emitter showcase/遮挡物；验收为两后端候选图均清楚呈现三种外观和 soft intersection。
6. 跑完整验证矩阵，向用户提交候选图；只有确认后 bless particles golden，再重跑默认完整 RenderGate。
7. 每个行为 slice 同提交回写对应 `docs/specs/`；全部验证与 golden 发布完成后把本 SDD Status 改为 Done，并记录根因、影响范围、验证证据和任何 PerfGate WARN 判断。

## Risks

| Risk | Mitigation |
| --- | --- |
| constants 超过 Vulkan 128B 或反射布局不一致 | clip-space billboard + FP16 packing；C++ static_assert 与运行时 reflection byte-size 严格校验 |
| normal/reverse-Z 或 ortho 深度重建错误 | 共享 C++ coefficient/reference math + 四组合 doctest；八张候选图的同后端 normal/reverse 与跨后端 SSIM/目视检查；禁止 raw device-depth 差值 |
| 非圆形 sprite 被径向 mask 意外裁切 | `radial_falloff=0` 完全关闭程序化 radial；Smoke/Magic golden 覆盖 |
| sRGB/UV 约定造成后端色差或翻转 | sprite 固定 sRGB；同一 HLSL；使用非对称测试纹理内容并检查跨后端 diff |
| program 绑定残留延长 texture/buffer 生命周期 | 保持一 emitter 一 pass；shutdown/release/prune 时清空所有 variant bindings |
| 外观编辑错误清空模拟 | simulation fingerprint doctest 锁定字段分类；Editor 继续确认存活粒子连续性 |
| 缺失纹理被 fallback 隐藏 | 画面 fallback 与 Failed readiness/日志并存；Editor 显示非阻塞 warning |
| AlphaBlend 无排序产生伪影 | 保持已知限制；Smoke 控制密度与观察角度，不在本 SDD 引入排序 |
| 预期画面变化误当回归直接覆盖基线 | 先非 bless 候选 + heatmap + 用户目视确认；只能走事务式 `-BlessGolden` |
| 共享工作树已有用户修改被覆盖 | 不修改 `Sandbox.scene.json`、`product/config/editor/imgui.ini`、现有未跟踪文档/抓帧；只操作本 SDD 与批准后的明确文件范围 |

## Open questions

None. 用户已确认：首期同时完成 sprite、radial falloff/sharpness 与 soft particles；空路径使用默认纹理；验收同时包含 Spark、Smoke、Magic；Editor 采用模块化 Inspector；上述三节设计均已确认。

## Implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Rendering/content steps must also use `imagegen` for the two approved bitmap sprite assets.

**Goal:** 在不修改 RHI 的前提下，为现有 GPU 粒子增加 sprite、径向轮廓参数与双后端 soft-depth，并交付模块化 Inspector 和三效果 golden 场景。

**Architecture:** Scene v6 保存五个新增外观字段；RenderScene 只复制 sprite path，ScenePresentation 在 render-thread submit prepare 中用既有 RenderAssetManager 把 TextureAsset 句柄写入 VisibleParticleEmitter；ParticleSystemPass 保持每 emitter 四 dispatch + 一 indirect draw，并用四个 draw variant区分 blend/soft。draw constants 通过 clip-space billboard 与 FP16 color packing 保持 128B。

**Tech Stack:** C++17、HLSL/DXC、RenderGraph、Vulkan/DX12、doctest、UIContext、JSON、RenderGate、PerfGate。

### File structure

| File | Responsibility in this change |
| --- | --- |
| `project/src/engine/Function/Scene/SceneComponents.h` | ParticleComponent 五个新字段与默认值 |
| `project/src/engine/Function/Scene/Scene.cpp` | descriptor、sanitize、scene v6 load/save |
| `project/src/tests/Scene/particle_component_tests.cpp` | schema、legacy、malformed、sanitize 回归 |
| `project/src/tests/Function/particle_depth_reconstruction_tests.cpp` | projection/Z reconstruction 与 simulation reset fingerprint |
| `project/src/engine/Function/Render/RenderScene.h` | VisibleParticleEmitter 的 render-thread prepared sprite 句柄 |
| `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp` | render-thread visible-frame sprite asset prepare |
| `project/src/engine/Function/Render/ParticleSystemPass.h/.cpp` | program variants、128B constants、资源绑定与 pass depth mode |
| `project/src/engine/Shaders/Particles/ParticleSystem.hlsl` | sprite/radial coverage、clip billboard、soft-depth |
| `project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp` | 六模块 Inspector 与只读预览 |
| `project/src/editor/Panels/Inspector/InspectorPanelState.h` | sprite picker recent/search session state |
| `project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.h/.cpp` | Editor sanitize 与 sprite path warning/block policy |
| `project/src/editor/Core/EditorComponentComparison.cpp` | undo/redo draft equality |
| `product/assets/textures/particles/*.png` | Smoke/Magic 固定 RGBA sprites |
| `product/assets/scenes/Particles.scene.json` | Spark/Smoke/Magic + soft intersection showcase |
| `docs/specs/features/particles.md`、`docs/specs/modules/{scene,render,editor}.md` | 最终长期行为 |

### Task 1: Scene v6 contract with red-green doctest

**Files:**
- Modify: `project/src/tests/Scene/particle_component_tests.cpp`
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `docs/specs/features/particles.md`
- Modify: `docs/specs/modules/scene.md`

- [ ] **Step 1: Extend the component equality helper and descriptor test before implementation**

Add these checks to `CheckParticleComponentMatches` and change the expected property count from 14 to 19:

```cpp
CHECK(actual.sprite_texture_path == expected.sprite_texture_path);
CHECK(actual.radial_falloff == doctest::Approx(expected.radial_falloff));
CHECK(actual.radial_sharpness == doctest::Approx(expected.radial_sharpness));
CHECK(actual.soft_particles == expected.soft_particles);
CHECK(actual.soft_fade_distance == doctest::Approx(expected.soft_fade_distance));
```

Add descriptor assertions:

```cpp
CHECK(descriptor->property_count == 19u);
const AshEngine::ScenePropertyDesc* sprite = FindProperty(*descriptor, "sprite_texture_path");
REQUIRE(sprite != nullptr);
CHECK(sprite->type == AshEngine::ScenePropertyType::String);
CHECK(sprite->editor_hint == AshEngine::ScenePropertyEditorHint::AssetPath);
CHECK(sprite->asset_ref_kind == AshEngine::ScenePropertyAssetRefKind::Texture);
CheckPropertyRange(*descriptor, "radial_falloff", 0.0f, 1.0f);
CheckPropertyRange(*descriptor, "radial_sharpness", 0.25f, 8.0f);
CheckPropertyRange(*descriptor, "soft_fade_distance", 0.001f, 10.0f);
```

- [ ] **Step 2: Extend roundtrip, legacy-v5, sanitize, and malformed JSON inputs**

Set non-default roundtrip values:

```cpp
expected.sprite_texture_path = "textures/particles/T_ParticleSmoke.png";
expected.radial_falloff = 0.35f;
expected.radial_sharpness = 3.5f;
expected.soft_particles = false;
expected.soft_fade_distance = 0.75f;
```

Require scene v6 and serialized field names:

```cpp
CHECK(saved_json.at("version") == 6u);
const json& saved_particle = saved_json.at("entities")[0].at("particle");
CHECK(saved_particle.at("sprite_texture_path") == expected.sprite_texture_path);
CHECK(saved_particle.at("radial_falloff").get<float>() == doctest::Approx(expected.radial_falloff));
CHECK(saved_particle.at("radial_sharpness").get<float>() == doctest::Approx(expected.radial_sharpness));
CHECK(saved_particle.at("soft_particles") == expected.soft_particles);
CHECK(saved_particle.at("soft_fade_distance").get<float>() == doctest::Approx(expected.soft_fade_distance));
```

For legacy-v5 compatibility, set `version=5`, erase the five new keys, reload, and compare against default values:

```cpp
saved_json["version"] = 5u;
json& legacy_particle = saved_json["entities"][0]["particle"];
legacy_particle.erase("sprite_texture_path");
legacy_particle.erase("radial_falloff");
legacy_particle.erase("radial_sharpness");
legacy_particle.erase("soft_particles");
legacy_particle.erase("soft_fade_distance");
WriteJson(scene_path, saved_json);
AshEngine::Scene legacy_v5 = AshEngine::Scene::load_from_file(scene_path, &error_message);
REQUIRE_MESSAGE(legacy_v5.is_valid(), error_message);
const AshEngine::ParticleComponent legacy_value =
    legacy_v5.find_entity(emitter.get_id()).get_particle_component();
const AshEngine::ParticleComponent defaults{};
CHECK(legacy_value.sprite_texture_path.empty());
CHECK(legacy_value.radial_falloff == doctest::Approx(defaults.radial_falloff));
CHECK(legacy_value.radial_sharpness == doctest::Approx(defaults.radial_sharpness));
CHECK(legacy_value.soft_particles == defaults.soft_particles);
CHECK(legacy_value.soft_fade_distance == doctest::Approx(defaults.soft_fade_distance));
```

Add invalid values to the sanitize test and malformed JSON object:

```cpp
invalid.radial_falloff = -1.0f;
invalid.radial_sharpness = std::numeric_limits<float>::infinity();
invalid.soft_fade_distance = 11.0f;

{ "sprite_texture_path", json::array() },
{ "radial_falloff", -5.0f },
{ "radial_sharpness", "sharp" },
{ "soft_particles", 42 },
{ "soft_fade_distance", 100.0f },
```

Assert clamp/fallback results:

```cpp
CHECK(sanitized.radial_falloff == doctest::Approx(0.0f));
CHECK(sanitized.radial_sharpness == doctest::Approx(defaults.radial_sharpness));
CHECK(sanitized.soft_fade_distance == doctest::Approx(10.0f));

CHECK(malformed_value.sprite_texture_path.empty());
CHECK(malformed_value.radial_falloff == doctest::Approx(0.0f));
CHECK(malformed_value.radial_sharpness == doctest::Approx(defaults.radial_sharpness));
CHECK(malformed_value.soft_particles == defaults.soft_particles);
CHECK(malformed_value.soft_fade_distance == doctest::Approx(10.0f));
```

Bind `malformed_value` to the first malformed entity's particle component. Preserve the existing version-4 numeric `blend_mode` compatibility check after the new version-5 missing-field check.

- [ ] **Step 3: Run the focused test and record the red state**

Run:

```bat
RunTests.bat Debug --test-case="ParticleComponent*"
```

Expected: build fails because `ParticleComponent` does not yet contain the five fields. A failure caused by the new member references is the required red state; unrelated failures must be fixed before continuing.

- [ ] **Step 4: Add the five fields with approved defaults**

Append after `end_color` and before `blend_mode` in `ParticleComponent`:

```cpp
std::string sprite_texture_path{};
float radial_falloff = 1.0f;
float radial_sharpness = 2.0f;
bool soft_particles = true;
float soft_fade_distance = 0.25f;
```

- [ ] **Step 5: Add Scene descriptor entries and scene version 6**

Change `k_scene_file_version` to `6u`. Add to `k_particle_properties`:

```cpp
{ "sprite_texture_path", ScenePropertyType::String,
  static_cast<uint32_t>(offsetof(ParticleComponent, sprite_texture_path)),
  static_cast<uint32_t>(sizeof(std::string)), nullptr,
  "Sprite Texture", "RGBA sprite texture; empty uses the default white sprite.",
  ScenePropertyEditorHint::AssetPath, ScenePropertyAssetRefKind::Texture },
{ "radial_falloff", ScenePropertyType::Float,
  static_cast<uint32_t>(offsetof(ParticleComponent, radial_falloff)), sizeof(float), nullptr,
  "Radial Falloff", "Blend between sprite-only and the analytic radial mask.",
  ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.0f, 1.0f, true },
{ "radial_sharpness", ScenePropertyType::Float,
  static_cast<uint32_t>(offsetof(ParticleComponent, radial_sharpness)), sizeof(float), nullptr,
  "Radial Sharpness", "Power exponent for the analytic radial mask.",
  ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.25f, 8.0f, true },
{ "soft_particles", ScenePropertyType::Bool,
  static_cast<uint32_t>(offsetof(ParticleComponent, soft_particles)), sizeof(bool), nullptr,
  "Soft Particles", "Fade near opaque scene depth intersections." },
{ "soft_fade_distance", ScenePropertyType::Float,
  static_cast<uint32_t>(offsetof(ParticleComponent, soft_fade_distance)), sizeof(float), nullptr,
  "Soft Fade Distance", "World-space depth interval used by soft particles.",
  ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.001f, 10.0f, true },
```

- [ ] **Step 6: Extend engine sanitize and JSON load/save**

Add to `sanitize_particle_component`:

```cpp
component.radial_falloff = sanitize_particle_float(
    component.radial_falloff, defaults.radial_falloff, 0.0f, 1.0f);
component.radial_sharpness = sanitize_particle_float(
    component.radial_sharpness, defaults.radial_sharpness, 0.25f, 8.0f);
component.soft_fade_distance = sanitize_particle_float(
    component.soft_fade_distance, defaults.soft_fade_distance, 0.001f, 10.0f);
```

In particle JSON load, use the existing typed helper so malformed values preserve defaults:

```cpp
(void)try_get_particle_json_value(particle_json, "sprite_texture_path", particle.sprite_texture_path);
(void)try_get_particle_json_value(particle_json, "radial_falloff", particle.radial_falloff);
(void)try_get_particle_json_value(particle_json, "radial_sharpness", particle.radial_sharpness);
(void)try_get_particle_json_value(particle_json, "soft_particles", particle.soft_particles);
(void)try_get_particle_json_value(particle_json, "soft_fade_distance", particle.soft_fade_distance);
```

In save, emit these keys next to size/color/blend:

```cpp
{ "sprite_texture_path", particle->sprite_texture_path },
{ "radial_falloff", particle->radial_falloff },
{ "radial_sharpness", particle->radial_sharpness },
{ "soft_particles", particle->soft_particles },
{ "soft_fade_distance", particle->soft_fade_distance },
```

- [ ] **Step 7: Run the focused test and full unit suite**

Run:

```bat
RunTests.bat Debug --test-case="ParticleComponent*"
RunTests.bat Debug
```

Expected: both commands exit 0; the focused suite reports all ParticleComponent cases passed.

- [ ] **Step 8: Update the scene contract specs and commit the green schema slice**

Record scene version 6, the five serialized fields, exact defaults/ranges, v5 missing-field behavior, empty-path White fallback, and malformed-value sanitation in `particles.md` and `scene.md`. Keep implementation and these long-term contract updates in the same commit.

```bat
git add project/src/tests/Scene/particle_component_tests.cpp project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.cpp docs/specs/features/particles.md docs/specs/modules/scene.md
git commit -m "feat(scene): add particle appearance schema"
```

### Task 2: Resolve sprite assets into visible emitters

**Files:**
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`

- [ ] **Step 1: Add the render-frame texture handle without changing logic-thread rebuild**

Change only `VisibleParticleEmitter`:

```cpp
struct ASH_API VisibleParticleEmitter
{
    EntityId entity_id = 0;
    ParticleComponent particle{};
    glm::mat4 world_transform{ 1.0f };
    std::shared_ptr<TextureAsset> sprite_texture = nullptr;
};
```

Keep `RenderScene::rebuild_particles_from_scene(const Scene&)` and all three existing call sites unchanged. The CPU rebuild copies `ParticleComponent::sprite_texture_path` as part of `emitter.particle` and leaves `sprite_texture` null.

- [ ] **Step 2: Prepare the approved fallback or explicit sRGB texture on the render thread**

Add this helper next to `prepare_visible_frame_material_proxies` in the anonymous namespace of `ScenePresentationSubsystem.cpp`:

```cpp
static bool prepare_visible_frame_particle_sprites(
    VisibleRenderFrame& frame,
    RenderAssetManager& asset_manager)
{
    ASH_PROFILE_SCOPE_NC(
        "ScenePresentation::PrepareParticleSprites",
        AshEngine::Profile::Color::Scene);
    ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

    for (VisibleParticleEmitter& emitter : frame.particle_emitters)
    {
        emitter.sprite_texture = emitter.particle.sprite_texture_path.empty()
            ? asset_manager.request_fallback_texture(TextureFallbackKind::White)
            : asset_manager.request_texture_asset(
                emitter.particle.sprite_texture_path,
                TextureColorSpace::SRGB,
                TextureFallbackKind::White);
        ASH_PROCESS_ERROR(emitter.sprite_texture != nullptr);
        ASH_PROCESS_ERROR(emitter.sprite_texture->resource != nullptr);
    }

    ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

`submit_presentations()` is the render-thread phase already used for GPU material/environment preparation. The helper intentionally keeps the fallback resource while RenderAssetManager retains Loading/Failed/readiness state for an explicit asset.

- [ ] **Step 3: Invoke particle sprite prepare before scene submission**

Immediately after `prepare_visible_frame_material_proxies` succeeds and before building `SceneRenderViewContext` / calling `SceneRenderer`, invoke:

```cpp
if (!prepare_visible_frame_particle_sprites(
        *packet.visible_frame,
        *m_impl->render_asset_manager))
{
    HLogError(
        "ScenePresentationSubsystem: particle sprite preparation failed for binding '{}'.",
        packet.debug_name);
    if (is_scene_packet)
    {
        ++submission.scene_packets_failed;
    }
    continue;
}
```

`finalize_pending_assets()` remains at the start of submit. A texture first requested by this helper therefore draws White while Loading, is finalized at the start of a later submit, and blocks readiness capture through the existing pending/activity epoch handshake. Do not request textures from logic-thread RenderScene rebuild or from Editor/Sandbox.

- [ ] **Step 4: Build both executable targets**

```bat
build_editor.bat Debug
build_sandbox.bat Debug
```

Expected: both exit 0. Shader appearance is unchanged in this slice; v5 particle scenes load with default fields.

- [ ] **Step 5: Check the uncommitted render slice**

Run `git diff --check` and inspect the two-file diff. Keep this asset extraction slice uncommitted until Task 4, so the render behavior and its `particles.md` / `render.md` contracts land atomically after sprite, radial, and soft-depth rendering are all functional.

### Task 3: Render sprite RGBA and configurable radial coverage

**Files:**
- Modify: `project/src/engine/Function/Render/ParticleSystemPass.h`
- Modify: `project/src/engine/Function/Render/ParticleSystemPass.cpp`
- Modify: `project/src/engine/Shaders/Particles/ParticleSystem.hlsl`

- [ ] **Step 1: Add a linear-clamp sampler owned by ParticleSystemPass**

Add to the pass:

```cpp
std::shared_ptr<RenderSampler> m_sprite_sampler = nullptr;
```

Create it after programs initialize:

```cpp
RenderSamplerDesc sampler_desc{};
sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
sampler_desc.min_filter = RenderSamplerFilter::Linear;
sampler_desc.mag_filter = RenderSamplerFilter::Linear;
sampler_desc.mip_filter = RenderSamplerFilter::Linear;
m_sprite_sampler = renderer.create_sampler(sampler_desc, "SceneParticleSpriteSampler");
ASH_PROCESS_ERROR(m_sprite_sampler != nullptr);
```

Reset it during shutdown after clearing program bindings.

- [ ] **Step 2: Replace draw constants with the exact 128-byte layout**

Use:

```cpp
struct ParticleDrawConstants
{
    glm::mat4 view_projection{ 1.0f };
    glm::vec4 projection_scale_start_end_size{ 1.0f, 1.0f, 0.0f, 0.0f };
    glm::uvec4 packed_start_end_color{ 0u };
    glm::vec4 depth_reconstruct{ 0.0f };
    glm::vec3 radial_soft_parameters{ 1.0f, 2.0f, 0.25f };
    uint32_t flags = 0;
};
static_assert(sizeof(ParticleDrawConstants) == 128u);
```

Include `<glm/gtc/packing.hpp>`. Pack colors and projection data in `make_draw_constants`:

```cpp
constants.projection_scale_start_end_size = glm::vec4(
    frame.projection[0][0], frame.projection[1][1],
    std::max(emitter.particle.start_size, 0.0f),
    std::max(emitter.particle.end_size, 0.0f));
constants.packed_start_end_color = glm::uvec4(
    glm::packHalf2x16(glm::vec2(emitter.particle.start_color)),
    glm::packHalf2x16(glm::vec2(emitter.particle.start_color.z, emitter.particle.start_color.w)),
    glm::packHalf2x16(glm::vec2(emitter.particle.end_color)),
    glm::packHalf2x16(glm::vec2(emitter.particle.end_color.z, emitter.particle.end_color.w)));
constants.depth_reconstruct = glm::vec4(
    frame.projection[2][2], frame.projection[2][3],
    frame.projection[3][2], frame.projection[3][3]);
constants.radial_soft_parameters = glm::vec3(
    emitter.particle.radial_falloff,
    emitter.particle.radial_sharpness,
    emitter.particle.soft_fade_distance);
constants.flags = frame.reverse_z ? 1u : 0u;
```

- [ ] **Step 3: Update the HLSL draw cbuffer and sprite resources**

Replace the draw cbuffer and add bindings:

```hlsl
cbuffer AshRootConstants : register(b0)
{
    float4x4 AshViewProjection;
    float4 AshProjectionScaleStartEndSize;
    uint4 AshPackedStartEndColor;
    float4 AshDepthReconstruct;
    float3 AshRadialSoftParameters;
    uint AshParticleFlags;
};

StructuredBuffer<AshParticleData> ParticlePool : register(t0);
Texture2D<float4> ParticleSprite : register(t1);
SamplerState ParticleSpriteSampler : register(s0);

float2 AshUnpackHalf2(uint packed_value)
{
    return float2(
        f16tof32(packed_value & 0xffffu),
        f16tof32(packed_value >> 16u));
}
```

- [ ] **Step 4: Expand the billboard in clip space and output UV**

In `VSMain`, replace camera-axis world offsets with:

```hlsl
const float size = lerp(
    AshProjectionScaleStartEndSize.z,
    AshProjectionScaleStartEndSize.w,
    life_t);
const float2 corner = k_particle_corners[vertex_id];
float4 clip_position = mul(AshViewProjection, float4(particle.position, 1.0));
clip_position.xy += corner * (size * 0.5) * AshProjectionScaleStartEndSize.xy;

const float4 start_color = float4(
    AshUnpackHalf2(AshPackedStartEndColor.x),
    AshUnpackHalf2(AshPackedStartEndColor.y));
const float4 end_color = float4(
    AshUnpackHalf2(AshPackedStartEndColor.z),
    AshUnpackHalf2(AshPackedStartEndColor.w));

output.position = clip_position;
output.color = lerp(start_color, end_color, life_t);
output.corner = corner;
output.uv = corner * float2(0.5, -0.5) + 0.5;
```

Add `float2 uv : TEXCOORD1;` to `VSOutput`.

- [ ] **Step 5: Multiply sprite and radial coverage in PSMain**

```hlsl
const float4 sprite = ParticleSprite.Sample(ParticleSpriteSampler, input.uv);
const float radius_sq = dot(input.corner, input.corner);
const float radial = pow(
    saturate(1.0 - radius_sq),
    max(AshRadialSoftParameters.y, 0.25));
const float shaped_alpha = lerp(1.0, radial, saturate(AshRadialSoftParameters.x));
const float coverage = saturate(input.color.a * sprite.a * shaped_alpha);
const float3 rgb = input.color.rgb * sprite.rgb;
#if defined(PARTICLE_ALPHA_BLEND)
    return float4(rgb, coverage);
#else
    return float4(rgb * coverage, coverage);
#endif
```

- [ ] **Step 6: Capture, bind, and clear sprite resources for both current draw programs**

Before `add_raster_pass`, copy the prepared asset handle. Never capture the loop's `emitter` reference in the deferred execute lambda:

```cpp
ASH_PROCESS_ERROR(emitter.sprite_texture != nullptr);
const std::shared_ptr<TextureAsset> sprite_texture = emitter.sprite_texture;
ASH_PROCESS_ERROR(sprite_texture->resource != nullptr);
```

Add `sprite_texture` to the execute lambda's by-value capture list, then read and bind its current resource before draw. This preserves the shared asset across deferred graph execution and observes any render-thread finalize that occurred before execution:

```cpp
ASH_PROCESS_ERROR(sprite_texture->resource != nullptr);
ASH_PROCESS_ERROR(draw_program->set_texture(
    "ParticleSprite", sprite_texture->resource));
ASH_PROCESS_ERROR(draw_program->set_sampler(
    "ParticleSpriteSampler", m_sprite_sampler));
```

In `clear_program_buffer_bindings`, clear `ParticlePool`, `ParticleSprite`, and `ParticleSpriteSampler` for both programs:

```cpp
auto clear_draw_bindings = [](GraphicsProgram* program)
{
    if (!program)
    {
        return;
    }
    program->set_storage_buffer("ParticlePool", nullptr);
    program->set_texture("ParticleSprite", nullptr);
    program->set_sampler(
        "ParticleSpriteSampler",
        std::shared_ptr<RenderSampler>{});
};
clear_draw_bindings(m_draw_additive_program.get());
clear_draw_bindings(m_draw_alpha_program.get());
```

- [ ] **Step 7: Build both backends and run the current particle scene smoke**

```bat
build_sandbox.bat Debug
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120
```

Expected: both smokes exit 0; logs contain no shader reflection, unbound resource, command-buffer, or fatal runtime errors. Current scene uses the white fallback and retains a round radial silhouette. Validation-layer evidence is collected in Task 7 rather than claimed by this ordinary smoke.

- [ ] **Step 8: Check the uncommitted sprite/radial slice**

Run `git diff --check` and inspect the shader/pass diff. Keep Tasks 2-3 uncommitted until the complete render contract and long-term specs are ready in Task 4.

### Task 4: Add soft-depth variants without charging SoftOff

**Files:**
- Create: `project/src/engine/Function/Render/ParticleSystemMath.h`
- Create: `project/src/tests/Function/particle_depth_reconstruction_tests.cpp`
- Modify: `project/src/engine/Function/Render/ParticleSystemPass.h`
- Modify: `project/src/engine/Function/Render/ParticleSystemPass.cpp`
- Modify: `project/src/engine/Shaders/Particles/ParticleSystem.hlsl`

- [ ] **Step 1: Write the four-convention depth reconstruction test and record red**

Create `particle_depth_reconstruction_tests.cpp`:

```cpp
#include <doctest/doctest.h>

#include "Function/Render/ParticleSystemMath.h"

#include <array>
#include <cmath>
#include <utility>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

namespace
{
    glm::mat4 MakeProjection(bool orthographic, bool reverse_z)
    {
        float near_plane = 0.1f;
        float far_plane = 100.0f;
        if (reverse_z)
        {
            std::swap(near_plane, far_plane);
        }
        return orthographic
            ? glm::orthoLH_ZO(-5.0f, 5.0f, -5.0f, 5.0f, near_plane, far_plane)
            : glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, near_plane, far_plane);
    }

    float ProjectDeviceDepth(const glm::mat4& projection, float view_depth)
    {
        const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, view_depth, 1.0f);
        return clip.z / clip.w;
    }
}

TEST_CASE("Particle system soft depth reconstruction covers projection and Z conventions")
{
    constexpr std::array<float, 4> k_view_depths{ 0.1f, 1.0f, 25.0f, 99.0f };
    constexpr std::array<bool, 2> k_bool_values{ false, true };
    for (const bool orthographic : k_bool_values)
    {
        for (const bool reverse_z : k_bool_values)
        {
            const glm::mat4 projection = MakeProjection(orthographic, reverse_z);
            const glm::vec4 coefficients =
                AshEngine::ParticleSystemInternal::make_depth_reconstruct_coefficients(projection);
            CHECK(ProjectDeviceDepth(projection, 0.1f) ==
                doctest::Approx(reverse_z ? 1.0f : 0.0f).epsilon(1e-5));
            CHECK(ProjectDeviceDepth(projection, 100.0f) ==
                doctest::Approx(reverse_z ? 0.0f : 1.0f).epsilon(1e-5));
            for (const float expected_depth : k_view_depths)
            {
                const float reconstructed =
                    AshEngine::ParticleSystemInternal::reconstruct_linear_view_depth(
                        coefficients,
                        ProjectDeviceDepth(projection, expected_depth));
                CHECK(std::abs(reconstructed - expected_depth) < 0.01f);
            }
        }
    }
}

TEST_CASE("Particle system simulation fingerprint excludes appearance fields")
{
    const AshEngine::ParticleComponent base{};
    const uint64_t base_hash =
        AshEngine::ParticleSystemInternal::build_simulation_config_hash(base);

    AshEngine::ParticleComponent appearance = base;
    appearance.sprite_texture_path = "textures/particles/test.png";
    appearance.radial_falloff = 0.25f;
    appearance.radial_sharpness = 6.0f;
    appearance.soft_particles = false;
    appearance.soft_fade_distance = 2.0f;
    appearance.start_size += 1.0f;
    appearance.end_size += 1.0f;
    appearance.start_color.x = 0.5f;
    appearance.end_color.y = 0.5f;
    appearance.blend_mode = AshEngine::ParticleBlendMode::AlphaBlend;
    appearance.emitting = !appearance.emitting;
    CHECK(AshEngine::ParticleSystemInternal::build_simulation_config_hash(appearance) == base_hash);

    const auto CheckSimulationChange = [&](auto mutate)
    {
        AshEngine::ParticleComponent changed = base;
        mutate(changed);
        CHECK(AshEngine::ParticleSystemInternal::build_simulation_config_hash(changed) != base_hash);
    };
    CheckSimulationChange([](auto& value) { value.spawn_rate += 1.0f; });
    CheckSimulationChange([](auto& value) { value.lifetime += 1.0f; });
    CheckSimulationChange([](auto& value) { value.lifetime_variance += 1.0f; });
    CheckSimulationChange([](auto& value) { value.initial_speed += 1.0f; });
    CheckSimulationChange([](auto& value) { value.spread_angle_degrees += 1.0f; });
    CheckSimulationChange([](auto& value) { value.constant_acceleration.x += 1.0f; });
    CheckSimulationChange([](auto& value) { value.random_seed += 1u; });
}
```

Run `RunTests.bat Debug --test-case="Particle system*"`. Expected red: the new internal math/header functions do not exist yet.

- [ ] **Step 2: Add the shared CPU coefficient/reference math and make the test green**

Create `ParticleSystemMath.h` as the SDD-required single source for C++ coefficient ordering and its CPU reference check:

```cpp
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>

#include "Function/Scene/SceneComponents.h"

namespace AshEngine::ParticleSystemInternal
{
    inline glm::vec4 make_depth_reconstruct_coefficients(const glm::mat4& projection)
    {
        return glm::vec4(
            projection[2][2], projection[2][3],
            projection[3][2], projection[3][3]);
    }

    inline float reconstruct_linear_view_depth(
        const glm::vec4& coefficients,
        float device_depth)
    {
        const float numerator = coefficients.z - device_depth * coefficients.w;
        const float denominator = device_depth * coefficients.y - coefficients.x;
        return std::abs(numerator /
            (std::abs(denominator) > 1e-6f ? denominator : 1e-6f));
    }

    inline uint32_t particle_float_bits(float value)
    {
        static_assert(sizeof(float) == sizeof(uint32_t));
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    inline void hash_simulation_value(uint64_t& hash_value, uint32_t value)
    {
        hash_value ^= static_cast<uint64_t>(value);
        hash_value *= 1099511628211ull;
    }

    inline uint64_t build_simulation_config_hash(const ParticleComponent& particle)
    {
        uint64_t hash_value = 1469598103934665603ull;
        hash_simulation_value(hash_value, particle_float_bits(particle.spawn_rate));
        hash_simulation_value(hash_value, particle_float_bits(particle.lifetime));
        hash_simulation_value(hash_value, particle_float_bits(particle.lifetime_variance));
        hash_simulation_value(hash_value, particle_float_bits(particle.initial_speed));
        hash_simulation_value(hash_value, particle_float_bits(particle.spread_angle_degrees));
        hash_simulation_value(hash_value, particle_float_bits(particle.constant_acceleration.x));
        hash_simulation_value(hash_value, particle_float_bits(particle.constant_acceleration.y));
        hash_simulation_value(hash_value, particle_float_bits(particle.constant_acceleration.z));
        hash_simulation_value(hash_value, particle.random_seed);
        return hash_value;
    }
}
```

Include it in `ParticleSystemPass.cpp`, delete the three old file-local simulation hash helpers, and call `ParticleSystemInternal::build_simulation_config_hash` from `ensure_emitter_state`. Replace the four manual depth-coefficient assignments from Task 3 with:

```cpp
constants.depth_reconstruct =
    ParticleSystemInternal::make_depth_reconstruct_coefficients(frame.projection);
```

Rerun the focused test. Expected: exit 0 for all four projection/Z convention pairs and the appearance-versus-simulation reset contract.

- [ ] **Step 3: Create explicit soft program members and macros**

Add:

```cpp
std::unique_ptr<GraphicsProgram> m_draw_additive_soft_program = nullptr;
std::unique_ptr<GraphicsProgram> m_draw_alpha_soft_program = nullptr;
```

Compile the existing two descriptors again with explicit creation between descriptor mutations:

```cpp
draw_desc.state.blend_mode = RenderBlendMode::Additive;
draw_desc.shader_macro = "PARTICLE_ADDITIVE=1;PARTICLE_SOFT_DEPTH=1";
draw_desc.name = "SceneParticleDrawAdditiveSoft";
m_draw_additive_soft_program = renderer.create_graphics_program(draw_desc);
ASH_PROCESS_ERROR(m_draw_additive_soft_program != nullptr);

draw_desc.state.blend_mode = RenderBlendMode::AlphaBlend;
draw_desc.shader_macro = "PARTICLE_ALPHA_BLEND=1;PARTICLE_SOFT_DEPTH=1";
draw_desc.name = "SceneParticleDrawAlphaSoft";
m_draw_alpha_soft_program = renderer.create_graphics_program(draw_desc);
ASH_PROCESS_ERROR(m_draw_alpha_soft_program != nullptr);
```

Require all four draw programs at the start of `add_passes` and reset both new programs during shutdown.

- [ ] **Step 4: Select the program and RenderGraph depth mode per emitter**

Use the component flag to choose exactly one program:

```cpp
GraphicsProgram* draw_program = nullptr;
if (emitter.particle.blend_mode == ParticleBlendMode::AlphaBlend)
{
    draw_program = emitter.particle.soft_particles
        ? m_draw_alpha_soft_program.get()
        : m_draw_alpha_program.get();
}
else
{
    draw_program = emitter.particle.soft_particles
        ? m_draw_additive_soft_program.get()
        : m_draw_additive_program.get();
}
```

In raster setup:

```cpp
pass.read_depth(
    depth,
    emitter.particle.soft_particles
        ? RenderGraphDepthReadMode::DepthTestAndShaderResource
        : RenderGraphDepthReadMode::DepthTestOnly);
```

- [ ] **Step 5: Bind and clear SceneDepth only for soft variants**

Capture the `depth` graph ref and `soft_particles` bool by value. In execute:

```cpp
if (soft_particles)
{
    std::shared_ptr<RenderTarget> depth_target = context.get_texture(depth);
    ASH_PROCESS_ERROR(depth_target != nullptr);
    ASH_PROCESS_ERROR(draw_program->set_texture("SceneDepth", depth_target));
}
```

Extend the Task 3 draw-binding cleanup to call it for all four programs. Then clear `SceneDepth` only on the two soft programs, because the non-soft programs do not reflect that binding:

```cpp
clear_draw_bindings(m_draw_additive_program.get());
clear_draw_bindings(m_draw_alpha_program.get());
clear_draw_bindings(m_draw_additive_soft_program.get());
clear_draw_bindings(m_draw_alpha_soft_program.get());

auto clear_soft_depth_binding = [](GraphicsProgram* program)
{
    if (program)
    {
        program->set_texture("SceneDepth", nullptr);
    }
};
clear_soft_depth_binding(m_draw_additive_soft_program.get());
clear_soft_depth_binding(m_draw_alpha_soft_program.get());
```

Set bit 1 in `ParticleDrawConstants::flags` only for soft emitters:

```cpp
constants.flags =
    (frame.reverse_z ? 1u : 0u) |
    (emitter.particle.soft_particles ? 2u : 0u);
```

- [ ] **Step 6: Add linear depth reconstruction and soft coverage to HLSL**

Declare depth only in the soft variant:

```hlsl
#if defined(PARTICLE_SOFT_DEPTH)
Texture2D<float> SceneDepth : register(t2);
#endif

float AshLinearViewDepth(float device_depth)
{
    const float numerator = AshDepthReconstruct.z - device_depth * AshDepthReconstruct.w;
    const float denominator = device_depth * AshDepthReconstruct.y - AshDepthReconstruct.x;
    return abs(numerator / (abs(denominator) > 1e-6 ? denominator : 1e-6));
}
```

In PSMain before final coverage:

```hlsl
float soft_depth_fade = 1.0;
#if defined(PARTICLE_SOFT_DEPTH)
    const uint2 pixel = uint2(input.position.xy);
    const float scene_device_depth = SceneDepth.Load(uint3(pixel, 0));
    const bool reverse_z = (AshParticleFlags & 1u) != 0u;
    const bool scene_is_background = reverse_z
        ? scene_device_depth <= 1e-6
        : scene_device_depth >= 0.999999;
    if (!scene_is_background)
    {
        const float scene_linear_depth = AshLinearViewDepth(scene_device_depth);
        const float particle_linear_depth = AshLinearViewDepth(input.position.z);
        soft_depth_fade = saturate(
            (scene_linear_depth - particle_linear_depth) /
            max(AshRadialSoftParameters.z, 0.001));
    }
#endif
const float coverage = saturate(
    input.color.a * sprite.a * shaped_alpha * soft_depth_fade);
```

- [ ] **Step 7: Verify reflection size and both depth paths**

```bat
build_sandbox.bat Debug
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120
```

Expected: both exits 0; the draw reflection reports 128B constants; logs contain no DSV/SRV state or unbound `SceneDepth` errors. Using `apply_patch`, add `"soft_particles": false` to the current emitter in `Particles.scene.json`, rerun both Vulkan and DX12 smoke commands above, then remove that one temporary key with `apply_patch`. Require `git diff -- product/assets/scenes/Particles.scene.json` to be empty before continuing; both SoftOff runs must have no `SceneDepth` binding or resource-state error.

- [ ] **Step 8: Update render specs and commit the complete render slice**

Update `particles.md` with the sprite/radial formula, sRGB + White fallback semantics, four program variants, 128B constants, linear-depth fade contract, SoftOff no-sample behavior, and the remaining unsorted AlphaBlend limitation. Update `render.md` with the conditional `DepthTestOnly` / `DepthTestAndShaderResource` declarations and unchanged pass ordering. Keep the Task 2-4 implementation and these specs in one commit.

```bat
git add project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/ScenePresentationSubsystem.cpp project/src/engine/Function/Render/ParticleSystemMath.h project/src/engine/Function/Render/ParticleSystemPass.h project/src/engine/Function/Render/ParticleSystemPass.cpp project/src/engine/Shaders/Particles/ParticleSystem.hlsl project/src/tests/Function/particle_depth_reconstruction_tests.cpp docs/specs/features/particles.md docs/specs/modules/render.md
git commit -m "feat(render): add particle sprite shaping and soft depth"
```

### Task 5: Rebuild the Particle Inspector as six modules

**Files:**
- Modify: `project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp`
- Modify: `project/src/editor/Panels/Inspector/InspectorPanelState.h`
- Modify: `project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.h`
- Modify: `project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.cpp`
- Modify: `project/src/editor/Core/EditorComponentComparison.cpp`
- Modify: `docs/specs/features/particles.md`
- Modify: `docs/specs/modules/editor.md`

- [ ] **Step 1: Add sprite picker session state and equality fields**

Add `#include "Widgets/EditorThemeColors.h"`, `#include "Widgets/InspectorAssetPathWidgets.h"`, plus `<algorithm>` and `<string>`, to `ParticleComponentEditor.cpp`. After binding the particle draft in `Draw`, bind the existing dependency bundle and initialize a commit blocker:

```cpp
AshEngine::ParticleComponent& particle = *refState.draftParticle.optCurrentValue;
const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
bool bCommitRequested = false;
bool bCommitBlocked = false;
```

In `InspectorPanelState`:

```cpp
std::vector<std::string> vecRecentParticleSpritePaths{};
std::string strParticleSpriteAssetPickerSearch{};
```

In `ParticleComponentsEqual` add all five fields using exact equality, matching existing component comparisons:

```cpp
refLeft.sprite_texture_path == refRight.sprite_texture_path &&
refLeft.radial_falloff == refRight.radial_falloff &&
refLeft.radial_sharpness == refRight.radial_sharpness &&
refLeft.soft_particles == refRight.soft_particles &&
refLeft.soft_fade_distance == refRight.soft_fade_distance &&
```

- [ ] **Step 2: Extend Editor-side sanitize**

In `SanitizeParticleComponent`:

```cpp
refComponent.radial_falloff = SanitizeClampedScalar(
    refComponent.radial_falloff, defaultValue.radial_falloff, 0.0f, 1.0f);
refComponent.radial_sharpness = SanitizeClampedScalar(
    refComponent.radial_sharpness, defaultValue.radial_sharpness, 0.25f, 8.0f);
refComponent.soft_fade_distance = SanitizeClampedScalar(
    refComponent.soft_fade_distance, defaultValue.soft_fade_distance, 0.001f, 10.0f);
```

- [ ] **Step 3: Add non-blocking missing-path validation**

Declare and define:

```cpp
bool TryGetParticleSpriteTextureValidationMessage(
    const InspectorPanelState::ParticleDraft& refDraft,
    const AssetDatabaseService* pAssetDatabaseService,
    std::string& strOutMessage);
```

Configure the generic validator as:

```cpp
InspectorAssetPathValidationDesc desc{};
desc.strAssetPath = refDraft.optCurrentValue->sprite_texture_path;
desc.strOriginalAssetPath = refDraft.optOriginalValue.has_value()
    ? refDraft.optOriginalValue->sprite_texture_path
    : std::string{};
desc.vecAllowedAssetTypes = { AshEngine::AssetType::Texture };
desc.pMissingAssetMessage = "The particle sprite path is missing; runtime will use Default Particle Sprite.";
desc.pUnsupportedAssetTypeMessage = "The particle sprite must be a texture resource.";
desc.pLoadStateProblemPrefix = "The particle sprite is currently ";
desc.bValidateOnlyWhenChanged = false;
desc.bBlockWhenEmpty = false;
desc.bBlockWhenMissingAsset = false;
desc.bBlockWhenUnsupportedAssetType = true;
desc.bBlockWhenLoadStateProblem = false;
return TryGetInspectorAssetPathValidationMessage(
    desc, pAssetDatabaseService, strOutMessage);
```

- [ ] **Step 4: Add two local read-only preview helpers through UIContext only**

Place stateless helpers in the anonymous namespace of `ParticleComponentEditor.cpp`:

```cpp
void DrawParticleSizeLifetimePreview(
    AshEngine::UIContext& refUi,
    float start_size,
    float end_size)
{
    refUi.dummy({ std::max(refUi.get_content_region_avail().x, 1.0f), 36.0f });
    const AshEngine::UIRect rect = refUi.get_item_rect();
    refUi.draw_window_rect_filled(rect, { 0.08f, 0.08f, 0.08f, 1.0f }, 3.0f);
    const float max_size = std::max({ start_size, end_size, 0.001f });
    const AshEngine::UIVec2 start{
        rect.x + 4.0f,
        rect.y + rect.height - 4.0f - (start_size / max_size) * (rect.height - 8.0f) };
    const AshEngine::UIVec2 end{
        rect.x + rect.width - 4.0f,
        rect.y + rect.height - 4.0f - (end_size / max_size) * (rect.height - 8.0f) };
    refUi.draw_window_line(start, end, { 0.25f, 0.75f, 1.0f, 1.0f }, 2.0f);
}
```

Use this complete color preview helper; it composites alpha over a two-row checkerboard and never writes the component:

```cpp
void DrawParticleColorLifetimePreview(
    AshEngine::UIContext& refUi,
    const glm::vec4& start_color,
    const glm::vec4& end_color)
{
    refUi.dummy({ std::max(refUi.get_content_region_avail().x, 1.0f), 36.0f });
    const AshEngine::UIRect rect = refUi.get_item_rect();
    constexpr uint32_t k_columns = 24u;
    constexpr uint32_t k_rows = 2u;
    const float cell_width = rect.width / static_cast<float>(k_columns);
    const float cell_height = rect.height / static_cast<float>(k_rows);
    for (uint32_t column = 0; column < k_columns; ++column)
    {
        const float t = (static_cast<float>(column) + 0.5f) /
            static_cast<float>(k_columns);
        const glm::vec4 sample = glm::mix(start_color, end_color, t);
        for (uint32_t row = 0; row < k_rows; ++row)
        {
            const float checker = ((column + row) & 1u) == 0u ? 0.28f : 0.12f;
            const glm::vec3 composited =
                glm::vec3(sample) * sample.a + glm::vec3(checker) * (1.0f - sample.a);
            const AshEngine::UIRect cell{
                rect.x + static_cast<float>(column) * cell_width,
                rect.y + static_cast<float>(row) * cell_height,
                cell_width + 1.0f,
                cell_height + 1.0f
            };
            refUi.draw_window_rect_filled(
                cell,
                { composited.r, composited.g, composited.b, 1.0f });
        }
    }
    refUi.draw_window_rect(rect, { 0.45f, 0.45f, 0.45f, 1.0f }, 3.0f);
}
```

- [ ] **Step 5: Split the existing flat fields into stable nested headers**

Keep the outer `Particle` header and action row. Draw the six inner headers in this exact order/ID:

```cpp
"Main##ParticleMain"
"Emission##ParticleEmission"
"Shape & Motion##ParticleShapeMotion"
"Size Over Lifetime##ParticleSizeLifetime"
"Color Over Lifetime##ParticleColorLifetime"
"Renderer##ParticleRenderer"
```

Use `DefaultOpen` for Main, Emission, and Renderer. Wrap the existing control calls without changing their field IDs or tooltips:

```cpp
if (refUi.collapsing_header(
        "Main##ParticleMain",
        AshEngine::UITreeNodeFlagBits::DefaultOpen))
{
    // Existing emitting, max_particles, random_seed calls move here unchanged.
}
if (refUi.collapsing_header(
        "Emission##ParticleEmission",
        AshEngine::UITreeNodeFlagBits::DefaultOpen))
{
    // Existing spawn_rate, lifetime, lifetime_variance calls move here unchanged.
}
if (refUi.collapsing_header("Shape & Motion##ParticleShapeMotion"))
{
    // Existing initial_speed, spread_angle_degrees, constant_acceleration calls move here unchanged.
}
if (refUi.collapsing_header("Size Over Lifetime##ParticleSizeLifetime"))
{
    // Existing start_size and end_size calls move here unchanged.
    DrawParticleSizeLifetimePreview(refUi, particle.start_size, particle.end_size);
}
if (refUi.collapsing_header("Color Over Lifetime##ParticleColorLifetime"))
{
    // Existing start_color and end_color calls move here unchanged.
    DrawParticleColorLifetimePreview(refUi, particle.start_color, particle.end_color);
}
if (refUi.collapsing_header(
        "Renderer##ParticleRenderer",
        AshEngine::UITreeNodeFlagBits::DefaultOpen))
{
    // New sprite/radial/soft controls and the existing blend_mode call live here.
}
```

The comments above identify literal existing call blocks; do not duplicate or rewrite those calls, because their current metadata, IDs, ranges, and command merge behavior are already tested manually and documented.

- [ ] **Step 6: Add the Renderer texture picker and new controls**

Use the existing picker:

```cpp
InspectorAssetPathWidgetState sprite_state{};
sprite_state.pVecRecentPaths = &refState.vecRecentParticleSpritePaths;
sprite_state.pStrSearch = &refState.strParticleSpriteAssetPickerSearch;
InspectorAssetPathFieldDesc sprite_desc = MakeInspectorSceneAssetPathFieldDesc(
    MakeInspectorSceneFieldDesc(
        AshEngine::SceneComponentType::Particle,
        "sprite_texture_path",
        "Sprite Texture",
        "RGBA sprite texture. Empty uses Default Particle Sprite.",
        "Default Particle Sprite",
        "Texture asset path or empty",
        "Type a relative path, browse, or drag-drop a Texture."),
    "Sprite Texture",
    "ParticleSpriteAssetPickerPopup",
    "Select Particle Sprite");
sprite_desc.vecAllowedAssetTypes = { AshEngine::AssetType::Texture };
bCommitRequested = DrawInspectorAssetPathField(
    refUi, particle.sprite_texture_path, sprite_desc, sprite_state,
    refDeps.pAssetDatabaseService, refDeps.pDragDropTransferService) || bCommitRequested;
if (particle.sprite_texture_path.empty())
{
    refUi.text_colored(
        GetEditorMutedTextColor(refUi),
        "Using Default Particle Sprite (White)");
}
```

Draw radial falloff `[0,1]`, sharpness `[0.25,8]`, Soft Particles bool, and fade distance `[0.001,10]`. Pass `particle.soft_particles` as the `bEnabled` argument for fade distance so disabling soft only greys the control and retains the value. Immediately after the picker, evaluate and draw validation with the same pattern as the Environment editor:

```cpp
std::string strSpriteValidationMessage{};
bCommitBlocked = TryGetParticleSpriteTextureValidationMessage(
    refState.draftParticle,
    refDeps.pAssetDatabaseService,
    strSpriteValidationMessage) || bCommitBlocked;
if (!strSpriteValidationMessage.empty())
{
    refUi.text_colored(
        GetEditorWarningTextColor(refUi),
        "%s",
        strSpriteValidationMessage.c_str());
}
```

At the existing final commit site, require `bCommitRequested && !bCommitBlocked`. Missing paths therefore warn and commit, while a known non-Texture asset keeps the draft visible but does not commit.

Use these exact controls after the sprite picker and before Blend Mode:

```cpp
bCommitRequested = DrawInspectorSceneDragFloatField(
    refUi,
    MakeInspectorSceneFieldDesc(
        AshEngine::SceneComponentType::Particle,
        "radial_falloff", "Radial Falloff",
        "Blend between texture-only coverage and the analytic radial mask.",
        "1", "[0, 1]", "Zero preserves arbitrary sprite silhouettes."),
    "Radial Falloff", particle.radial_falloff,
    0.01f, 0.0f, 1.0f) || bCommitRequested;
bCommitRequested = DrawInspectorSceneDragFloatField(
    refUi,
    MakeInspectorSceneFieldDesc(
        AshEngine::SceneComponentType::Particle,
        "radial_sharpness", "Radial Sharpness",
        "Power exponent applied to the radial mask.",
        "2", "[0.25, 8]", "Only affects the analytic radial contribution."),
    "Radial Sharpness", particle.radial_sharpness,
    0.05f, 0.25f, 8.0f) || bCommitRequested;
bCommitRequested = DrawInspectorSceneBoolField(
    refUi,
    MakeInspectorSceneFieldDesc(
        AshEngine::SceneComponentType::Particle,
        "soft_particles", "Soft Particles",
        "Fade sprite intersections against opaque scene depth.",
        "Enabled", "On / Off", "Off avoids the Scene Depth sample."),
    "Soft Particles", particle.soft_particles) || bCommitRequested;
bCommitRequested = DrawInspectorSceneDragFloatField(
    refUi,
    MakeInspectorSceneFieldDesc(
        AshEngine::SceneComponentType::Particle,
        "soft_fade_distance", "Soft Fade Distance",
        "World-space depth interval across which intersections fade.",
        "0.25", "[0.001, 10]", "Value is retained while Soft Particles is off."),
    "Soft Fade Distance", particle.soft_fade_distance,
    0.01f, 0.001f, 10.0f, "%.3f", particle.soft_particles) || bCommitRequested;
```

- [ ] **Step 7: Build and perform the Editor interaction checklist**

```bat
build_editor.bat Debug
run.bat editor
```

Manually verify: six headers; empty path displays Default; valid Texture search/drop; missing path warning commits, remains visible after reselecting the entity, and the interactive viewport continues rendering the White fallback; non-Texture blocks; Soft toggle greys but preserves distance; both full-width previews update; save/reload; Reset; Restore; remove; undo/redo. While particles are alive, change sprite/radial/soft/size/color/blend fields and confirm existing trajectories continue without a pool reset; then change `spawn_rate` once and confirm the documented simulation-field reset still occurs. Inspect `product/logs` for errors other than the deliberately induced missing-texture warning, then restore a valid/empty path and confirm subsequent logs are clean.

- [ ] **Step 8: Update Inspector specs and commit the Editor slice**

Record the six modules, default-open policy, read-only two-endpoint previews, picker/search/drop/recent behavior, missing-path warning versus wrong-type block, disabled-but-retained soft distance, and existing draft/command/undo semantics in `particles.md` and `editor.md`.

```bat
git add project/src/editor/Panels/Inspector/ParticleComponentEditor.cpp project/src/editor/Panels/Inspector/InspectorPanelState.h project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.h project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.cpp project/src/editor/Core/EditorComponentComparison.cpp docs/specs/features/particles.md docs/specs/modules/editor.md
git commit -m "feat(editor): modularize particle inspector"
```

### Task 6: Create fixed sprite assets and the three-effect showcase

**Files:**
- Create: `product/assets/textures/particles/T_ParticleSmoke.png`
- Create: `product/assets/textures/particles/T_ParticleMagic.png`
- Modify: `product/assets/scenes/Particles.scene.json`
- Modify: `docs/specs/features/particles.md`

- [ ] **Step 1: Use the imagegen skill for the smoke sprite**

Generate a square RGBA image with this prompt:

```text
Create a single centered realtime VFX smoke particle sprite, 256x256 RGBA, transparent background. Soft layered gray-white smoke puff with irregular wispy alpha edges, no lighting cast outside the puff, no text, no border, no ground shadow, no frame, one sprite only. Keep the outer 8 pixels fully transparent and make the silhouette asymmetric so vertical flipping is detectable. Game-engine particle texture, clean premultiplication-safe RGB at transparent edges.
```

Save the result as `product/assets/textures/particles/T_ParticleSmoke.png`, inspect with `view_image`, and reject any result with an opaque background or clipped edge.

- [ ] **Step 2: Use the imagegen skill for the magic sprite**

Generate with:

```text
Create a single centered realtime VFX magic particle sprite, 256x256 RGBA, transparent background. Cyan-violet luminous energy orb with a thin broken ring, bright compact core, asymmetric three-prong spark detail pointing upward-right, soft transparent falloff, no text, no border, no ground shadow, no frame, one sprite only. Keep the outer 8 pixels fully transparent and preserve saturated RGB inside translucent pixels for additive blending.
```

Save as `product/assets/textures/particles/T_ParticleMagic.png`; inspect transparency, asymmetry, and edge padding with `view_image`.

- [ ] **Step 3: Replace the single fountain with exact initial showcase values**

Set scene version 6 and arrange:

```json
{
  "name": "Spark",
  "particle": {
    "max_particles": 2048,
    "spawn_rate": 260.0,
    "lifetime": 1.6,
    "lifetime_variance": 0.25,
    "initial_speed": 4.8,
    "spread_angle_degrees": 18.0,
    "constant_acceleration": [0.0, -5.5, 0.0],
    "start_size": 0.07,
    "end_size": 0.015,
    "start_color": [1.0, 0.82, 0.25, 1.0],
    "end_color": [1.0, 0.12, 0.02, 0.0],
    "sprite_texture_path": "",
    "radial_falloff": 1.0,
    "radial_sharpness": 4.0,
    "soft_particles": true,
    "soft_fade_distance": 0.12,
    "blend_mode": "Additive",
    "random_seed": 1101,
    "emitting": true
  },
  "transform": { "position": [-2.5, 0.8, 6.0], "rotation_euler_degrees": [0,0,0], "scale": [1,1,1] }
}
```

```json
{
  "name": "Smoke",
  "particle": {
    "max_particles": 1024,
    "spawn_rate": 70.0,
    "lifetime": 3.5,
    "lifetime_variance": 0.5,
    "initial_speed": 1.25,
    "spread_angle_degrees": 28.0,
    "constant_acceleration": [0.0, 0.18, 0.0],
    "start_size": 0.32,
    "end_size": 1.15,
    "start_color": [0.62, 0.66, 0.72, 0.42],
    "end_color": [0.22, 0.25, 0.3, 0.0],
    "sprite_texture_path": "textures/particles/T_ParticleSmoke.png",
    "radial_falloff": 0.0,
    "radial_sharpness": 1.0,
    "soft_particles": true,
    "soft_fade_distance": 0.45,
    "blend_mode": "AlphaBlend",
    "random_seed": 2202,
    "emitting": true
  },
  "transform": { "position": [0.0, 0.65, 6.2], "rotation_euler_degrees": [0,0,0], "scale": [1,1,1] }
}
```

```json
{
  "name": "Magic",
  "particle": {
    "max_particles": 1536,
    "spawn_rate": 140.0,
    "lifetime": 2.4,
    "lifetime_variance": 0.3,
    "initial_speed": 2.0,
    "spread_angle_degrees": 34.0,
    "constant_acceleration": [0.0, -0.35, 0.0],
    "start_size": 0.24,
    "end_size": 0.08,
    "start_color": [0.7, 0.28, 1.0, 0.9],
    "end_color": [0.08, 0.75, 1.0, 0.0],
    "sprite_texture_path": "textures/particles/T_ParticleMagic.png",
    "radial_falloff": 0.0,
    "radial_sharpness": 1.0,
    "soft_particles": true,
    "soft_fade_distance": 0.25,
    "blend_mode": "Additive",
    "random_seed": 3303,
    "emitting": true
  },
  "transform": { "position": [2.5, 1.0, 6.0], "rotation_euler_degrees": [0,0,0], "scale": [1,1,1] }
}
```

Use entity IDs 3/4/5 for Spark/Smoke/Magic. Set the existing camera (ID 2) to position `[0.0, 2.4, -8.5]`, perspective projection, reverse-Z, FOV 60. Add the exact opaque occluder and light entities:

```json
{
  "id": 6,
  "name": "SoftParticleOccluder",
  "parent": 1,
  "mesh": {
    "asset_path": "models/gltfs/Avocado/glTF/Avocado.gltf",
    "mesh_index": 0,
    "visible": true,
    "mobility": 0,
    "layer_mask": 1
  },
  "transform": {
    "position": [0.0, 1.5, 6.0],
    "rotation_euler_degrees": [0.0, 0.0, 0.0],
    "scale": [8.0, 8.0, 8.0]
  }
}
```

```json
{
  "id": 7,
  "name": "ShowcaseLight",
  "parent": 1,
  "light": {
    "type": 0,
    "color": [1.0, 0.95, 0.88],
    "intensity": 3.0,
    "range": 10.0,
    "inner_cone_angle_degrees": 30.0,
    "outer_cone_angle_degrees": 45.0,
    "casts_shadow": false,
    "sunlight": false,
    "shadow_priority": 0,
    "shadow_distance": 0.0,
    "shadow_cascade_count": 0,
    "near_shadow_distance": 0.0
  },
  "transform": {
    "position": [0.0, 4.0, 2.0],
    "rotation_euler_degrees": [-35.0, 25.0, 0.0],
    "scale": [1.0, 1.0, 1.0]
  }
}
```

Set `next_entity_id` to 8. Keep AO, directional shadows, Bloom, TAA, and volumetric lighting disabled.

- [ ] **Step 4: Capture both backends without blessing**

```bat
RunRenderGate.bat -Scenes particles
```

Expected command exit: `1`, solely because both per-backend comparisons against the old particle golden are below `0.995`. The report must still show both runtime captures published, both runtime smokes/readiness successful, and Vulkan-vs-DX12 PASS at `0.99`; a missing capture, runtime failure, asset failure, timeout, or cross-backend failure is not an expected result. Inspect both candidate PNGs and heatmaps. The successful valid-texture captures also prove Smoke/Magic reached Loading → Ready and the asset-epoch handshake rejected any pre-finalize capture; Spark in the same images proves the empty-path ready White fallback.

- [ ] **Step 5: Enforce the visual acceptance checklist before commit**

The candidate must show all of these:

- left Spark is small and crisp, not the old large soft dot;
- center Smoke visibly uses a non-circular alpha texture and fades smoothly at the Avocado depth boundary;
- right Magic preserves the upward-right asymmetric prong on both backends, proving UV orientation;
- no square sprite borders, opaque PNG background, black fringe, hard depth seam, or backend-specific flip;
- TAA/Bloom remain disabled in scene JSON.

If a criterion fails, change only the three emitter values, camera, light, or occluder transform in `Particles.scene.json`, rerun the same non-bless command, and recheck the full list. Do not edit golden files.

- [ ] **Step 6: Record the regression scene contract and commit assets, not goldens**

In `particles.md`, record that the fixed `particles` RenderGate scene covers default White/Additive Spark, RGBA/AlphaBlend Smoke crossing opaque depth, and RGBA/Additive Magic with asymmetric UV evidence, with TAA/Bloom/AO/shadows/volumetrics disabled. Do not record candidate metrics as accepted golden results yet.

```bat
git add product/assets/textures/particles/T_ParticleSmoke.png product/assets/textures/particles/T_ParticleMagic.png product/assets/scenes/Particles.scene.json docs/specs/features/particles.md
git commit -m "feat(content): add particle visual showcase"
```

### Task 7: Full verification and user visual gate

**Files:**
- Read: `docs/VERIFY.md`
- Generated only: `Intermediate/test-reports/`, `product/logs/`, candidate captures

- [ ] **Step 1: Validate the dirty-path plan and architecture**

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
RunArchGate.bat
```

Expected: ValidatePlan includes Scene/Render/Editor/shader/content checks; ArchGate exits 0 with no new boundary violation.

- [ ] **Step 2: Run builds, unit tests, and lifecycle smoke**

```bat
build_editor.bat Debug
build_sandbox.bat Debug
RunTests.bat Debug
run.bat all Debug --smoke-test-seconds=120
```

Expected: all exit 0.

- [ ] **Step 3: Run both validation configurations**

Enable Vulkan `Enabled/GpuAssisted/SynchronizationValidation`, then run:

```bat
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120
```

Enable DX12 `Enabled/GpuValidation`, then run:

```bat
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=120
```

Restore `product/config/Engine.ini` after each temporary validation edit. Expected: exit 0 and no validation, resource state, descriptor, lifetime, or leak errors in `product/logs`.

- [ ] **Step 4: Capture and compare all projection/depth conventions against the occluder**

Create `Intermediate/particle-depth-projections/`. Using `apply_patch`, test the four camera pairs below in `Particles.scene.json`; after each patch, run both backend frame dumps with the corresponding output names:

```bat
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/perspective-normal-vulkan.png --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/perspective-normal-dx12.png --smoke-test-seconds=120
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/perspective-reverse-vulkan.png --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/perspective-reverse-dx12.png --smoke-test-seconds=120
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/orthographic-normal-vulkan.png --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/orthographic-normal-dx12.png --smoke-test-seconds=120
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/orthographic-reverse-vulkan.png --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --dump-frame=Intermediate/particle-depth-projections/orthographic-reverse-dx12.png --smoke-test-seconds=120
```

The patches are, in command order: `(projection=0, reverse_z=false)`, `(0,true)`, `(1,false)`, `(1,true)`. All eight dumps must exit 0 and publish their PNG. Run `AshImageDiff.exe` from `product/bin64/Debug-windows-x86_64/`:

- compare Vulkan vs DX12 for each of the four configurations at SSIM `0.99`;
- compare normal-Z vs reverse-Z within each projection and backend at SSIM `0.995`;
- generate heatmaps for any comparison below exact identity and inspect all eight PNGs with `view_image`.

Expected: every comparison passes; Smoke has the same smooth, non-inverted fade at the Avocado boundary, with no NaN/Inf color or hard seam. These captures are visual/math checks, not additional validation-layer runs; validation evidence comes from Step 3. Restore the committed camera values `projection=0`, `reverse_z=true` with `apply_patch` and require `git diff -- product/assets/scenes/Particles.scene.json` to be empty.

- [ ] **Step 5: Verify an explicit missing sprite uses the runtime fallback**

Using `apply_patch`, change Spark's committed `"sprite_texture_path": ""` to `"textures/particles/DOES_NOT_EXIST.png"`. Run bounded negative smokes on both backends:

```bat
run.bat sandbox vulkan Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=10
run.bat sandbox dx12 Debug --scene=product/assets/scenes/Particles.scene.json --smoke-test-seconds=10
```

Expected negative result: both exit nonzero because `TextureAssetState::Failed` propagates to `RenderAssetReadinessSnapshot::failed`; the log contains the expected missing/decode warning and readiness failure, but no crash, thread-guard, validation, descriptor, or resource-lifetime error. The Editor checklist in Task 5 separately proves the interactive viewport remains visible through the White resource fallback. An exit 0 or a timeout without the explicit asset-failure reason is a test failure. Restore the empty path with `apply_patch` and require `git diff -- product/assets/scenes/Particles.scene.json` to be empty.

- [ ] **Step 6: Run PerfGate and non-bless full RenderGate**

```bat
RunPerfGate.bat -Profile Standard
RunRenderGate.bat
```

Expected: PerfGate has no FAIL; any WARN is recorded with its report path and reason. `RunRenderGate.bat` exits `1` solely for the intentional particles old-golden comparisons. Its report must show sandbox PASS, both particle captures/runtime smokes/readiness successful, and particles Vulkan-vs-DX12 PASS; any other failing record is a blocker.

- [ ] **Step 7: Stop and request user confirmation of candidate images**

Show the Vulkan and DX12 particle candidates. State explicitly that golden has not been changed. Do not continue until the user confirms Spark, Smoke, Magic, and soft-depth intersection.

### Task 8: Bless, close specs, and final verification

**Files:**
- Update through tool only: `tools/render/goldens/particles/vulkan.png`, `tools/render/goldens/particles/dx12.png`
- Modify: `docs/sdd/SDD-2026-07-13-particle-visual-quality.md`

- [ ] **Step 1: Bless only after explicit user visual approval**

```bat
RunRenderGate.bat -Scenes particles -BlessGolden
```

Expected: transaction reports `BLESSED` and exits 0. Never edit, copy, or replace golden PNGs manually; staging files produced by a successful transactional bless is allowed and required for the closing commit.

- [ ] **Step 2: Run the complete RenderGate after publication**

```bat
RunRenderGate.bat
```

Expected: sandbox and particles pass per-backend golden thresholds and cross-backend threshold.

- [ ] **Step 3: Audit long-term specs and close the SDD with final evidence**

Confirm the behavior contracts were already committed with Tasks 1, 4, 5, and 6. Correct any implementation deviation in the same commit as the affected code before proceeding. Then record in this SDD:

- actual verification commands/results and report paths;
- RenderGate per-backend SSIM and cross-backend metrics;
- PerfGate PASS/WARN decision and any warning rationale;
- validation-log and Editor checklist results;
- the explicit user approval that authorized golden publication.

Change SDD Status to `Done（2026-07-13；...）` only when every required verification and golden publication succeeds.

- [ ] **Step 4: Run final documentation and repository checks**

```bat
git diff --check
git status --short
```

Expected: no whitespace error; status contains only intended particle/spec/golden changes plus the pre-existing unrelated user files listed in this SDD risk table.

- [ ] **Step 5: Commit specs and transaction-published goldens**

```bat
git add docs/sdd/SDD-2026-07-13-particle-visual-quality.md tools/render/goldens/particles/vulkan.png tools/render/goldens/particles/dx12.png
git commit -m "docs(render): close particle visual quality SDD"
```

- [ ] **Step 6: Verify the final commit set and report evidence**

```bat
git log --oneline --decorate -8
git status --short
```

Report every command/result, RenderGate SSIM/cross-backend metrics, PerfGate PASS/WARN status, validation log result, Editor manual checklist, and the fact that baseline publication followed explicit user approval.
