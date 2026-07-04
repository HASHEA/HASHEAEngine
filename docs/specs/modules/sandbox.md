---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Module Spec: Sandbox

## 职责与边界

引擎验证程序（`project/src/sandbox/`）：加载标准场景、提供自由相机漫游、作为 smoke test / PerfGate / RenderGate 的被测目标。管最小可运行的引擎使用示例与确定性抓帧约定；不管编辑功能、不新增引擎能力。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `Sandbox.h/.cpp` | `create_application` / `destroy_application` 入口：1920x1080、vsync off、3 buffer、启用逻辑线程 |
| `App/SandboxApplication.h/.cpp` | `SandboxApplication final : AshEngine::Application`，Application 生命周期钩子实现 |
| `App/SandboxStandardScene.h/.cpp` | 标准场景装载状态机（互斥锁保护 snapshot）与抓帧相机约定 |
| `App/SandboxFreeCameraController.h/.cpp` | WASD + 鼠标自由相机，驱动主相机实体 Transform |
| `Tests/SandboxTestRegistry.h/.cpp` | `SandboxDefaultRuntimeMode`（当前仅 StandardScene）与默认模式查询 |
| `Demos/CodexLogoDemoRenderer.h/.cpp` | compute + graphics 演示渲染器（保留的独立 demo，不在标准场景路径上） |
| `Shaders/` | demo 所用 HLSL（`CodexLogoComputeDemo.hlsl`、`SceneStaticMesh.hlsl`） |

## 公共接口

- `SandboxApplication`：覆写 `_on_startup/_on_shutdown/_on_update/_on_logic_startup/_on_logic_update/_on_gui/_on_render/_present`；启动时初始化 `AssetDatabase`（根 `product/assets`）与 RenderAssetManager，创建主场景 output/binding 注册到 `ScenePresentationSubsystem`；报告目录 `Intermediate/test-reports/sandbox`。
- `SandboxStandardScene`：
  - `get_standard_scene_path()` → `product/assets/scenes/Sandbox.scene.json`。
  - `start(AssetDatabase&)` / `reset()` / `update_logic(InputState, frame_index)`；状态机 `SandboxStandardSceneLoadState`（Idle/LoadingScene/Ready/Failed）；`snapshot()` 返回 `SandboxStandardSceneSnapshot`（scene、主相机实体 id、推荐移速）。
  - 装载流程：文件存在性检查 → `Scene::load_from_file` → 引用资产校验 → 找 primary camera → Ready。
- `SandboxFreeCameraController`：`bind_camera_entity` / `set_move_speed` / `update(scene, input, delta, out_error)`；默认移速 8.0、shift x4、鼠标灵敏度 0.12。
- `SandboxTestRegistry`：`get_default_sandbox_runtime_mode()` / `get_default_sandbox_runtime_mode_name()`。

## 约束与不变式

- **RenderGate 抓帧约定**（SDD-0001）：当 `Application::get_frame_dump_path()` 非空（即命令行带 `--dump-frame=`）时，场景装载后把 primary camera 位置固定为 **(0, 5, 0)**，保证抓帧画面确定性；改动此约定必须同步重新 bless golden。
- 标准场景是唯一默认运行模式；场景文件或引用资产缺失时进入 Failed 并给出 failure_detail，不静默降级。
- 场景渲染只走 `ScenePresentationSubsystem`（output + view binding），Sandbox 不直接调用 SceneRenderer。
- 逻辑线程开启（`enable_logic_thread = true`）：场景装载/相机更新在逻辑侧，`SandboxStandardScene` 内部用互斥锁保护快照。

## 验证

对齐 `docs/VERIFY.md`：

- 构建 + `run.bat sandbox vulkan Debug --smoke-test-seconds=5`（fast path）
- 影响画面或抓帧约定：`RunRenderGate.bat`（Sandbox 即被测目标）
- 性能敏感改动：`RunPerfGate.bat -Profile Standard`（Sandbox 是趋势基线 target）

## 历史

- `docs/sdd/SDD-0001-render-gate.md`（抓帧相机固定约定来源）
- `docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md`（标准场景配置，归档）
