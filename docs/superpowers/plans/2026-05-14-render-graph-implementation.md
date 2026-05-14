# Render Graph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first AshEngine Render Graph vertical slice and migrate the scene deferred GBuffer -> lighting -> composite path onto it.

**Architecture:** Add `RenderGraph` as a `Function/Render` frame-level orchestration layer above `Renderer`, with explicit texture access declarations, pass culling, lifetime compilation, and pre-pass barrier plans. Execute compiled passes through the existing `Renderer / RenderDevice` path so Vulkan and DX12 backend code remains stable while RDG starts owning scene pass ordering and transient resources.

**Tech Stack:** C++17, AshEngine `Function/Render`, existing `Renderer`, `RenderDevice`, `RHI::AshResourceState`, Premake globbed source inclusion, Sandbox engine self-test, Vulkan and DX12 smoke validation.

---

## Scope And Boundaries

- Do not modify `project/src/editor`.
- Keep `Graphics/Vulkan` and `Graphics/DirectX12` unchanged unless validation exposes a backend bug during execution.
- Keep the old direct `Renderer` path working; RDG is added first, then scene deferred migrates onto it.
- First implementation supports texture graph resources, raster passes, compute passes, pass culling, lifetime data, and pass-boundary barriers.
- First implementation does not support async compute, multi-queue, arbitrary pass reorder, transient aliasing, or shader parameter struct auto-binding.

## File Structure

- Create `project/src/engine/Function/Render/RenderGraphFwd.h` for lightweight handles and forward declarations.
- Create `project/src/engine/Function/Render/RenderGraphResource.h/.cpp` for texture descriptions, resource nodes, and access-to-RHI-state mapping.
- Create `project/src/engine/Function/Render/RenderGraphPass.h/.cpp` for pass flags, pass builders, resource usage declarations, and raster/compute context interfaces.
- Create `project/src/engine/Function/Render/RenderGraphCompiler.h/.cpp` for validation, pass culling, lifetime ranges, and barrier plan generation.
- Create `project/src/engine/Function/Render/RenderGraphBuilder.h/.cpp` for the public graph construction and execute entry point.
- Create `project/src/engine/Function/Render/RenderGraphExecutor.cpp` for compiled graph execution through `Renderer`.
- Create `project/src/engine/Function/Render/SceneDeferredGraphResources.h` for graph refs used by scene deferred.
- Create `project/src/engine/Function/Render/RenderGraph.h` as the public include.
- Modify `project/src/engine/Function/Render/RenderDevice.h/.cpp` only for optional attachment final states and a controlled graph barrier submit path.
- Modify `project/src/engine/Function/Render/Renderer.h/.cpp` to expose graph-only helper calls to the executor.
- Modify `project/src/engine/Function/Render/SceneRenderer.h/.cpp` to construct and execute a graph for deferred scene rendering.
- Modify `project/src/engine/Function/Render/DeferredLightingPass.h/.cpp` to register graph passes instead of directly beginning render passes.
- Modify `project/src/engine/Base/EngineSelfTests.cpp` for pure graph/compiler tests.
- Modify `README.md` and `docs/EngineDeveloperGuide.md` after behavior lands.

---

### Task 1: Add RenderGraph Core Types And Access Mapping

**Files:**
- Create: `project/src/engine/Function/Render/RenderGraphFwd.h`
- Create: `project/src/engine/Function/Render/RenderGraphResource.h`
- Create: `project/src/engine/Function/Render/RenderGraphResource.cpp`
- Create: `project/src/engine/Function/Render/RenderGraph.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [x] **Step 1: Write the failing access-mapping self-test**

Add this include near the other render includes in `project/src/engine/Base/EngineSelfTests.cpp`:

```cpp
#include "Function/Render/RenderGraph.h"
```

Add this test function inside the anonymous namespace, near the existing render self-tests:

```cpp
bool test_render_graph_access_maps_to_rhi_states()
{
    bool ok = true;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::GraphicsSRV) == RHI::AshResourceState::SRVGraphics;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::ComputeSRV) == RHI::AshResourceState::SRVCompute;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::GraphicsUAV) == RHI::AshResourceState::UAVGraphics;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::ComputeUAV) == RHI::AshResourceState::UAVCompute;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::ColorAttachmentWrite) == RHI::AshResourceState::RTV;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::DepthStencilWrite) == RHI::AshResourceState::DSVWrite;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::DepthStencilRead) == RHI::AshResourceState::DSVRead;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::CopySrc) == RHI::AshResourceState::CopySrc;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::CopyDst) == RHI::AshResourceState::CopyDst;
    ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::Present) == RHI::AshResourceState::Present;

    const RHI::AshResourceState depth_sample_state =
        render_graph_depth_read_state(RenderGraphDepthReadMode::DepthTestAndShaderResource);
    ok = ok && depth_sample_state == (RHI::AshResourceState::DSVRead | RHI::AshResourceState::SRVGraphics);

    return ok || report_self_test_failure("RenderGraph access mapping", "graph access did not map to the expected RHI state");
}
```

Call it from `run_engine_base_self_tests()` after `test_deferred_read_only_depth_attachment_state()`:

```cpp
all_passed = test_render_graph_access_maps_to_rhi_states() && all_passed;
```

- [x] **Step 2: Run the build to verify the test fails before implementation**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build fails because `Function/Render/RenderGraph.h` or `RenderGraphAccess` is not defined.

- [x] **Step 3: Add forward handles**

Create `project/src/engine/Function/Render/RenderGraphFwd.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include <cstdint>

namespace AshEngine
{
    class RenderGraphBuilder;
    class RenderGraphRasterPassBuilder;
    class RenderGraphComputePassBuilder;
    class RenderGraphRasterContext;
    class RenderGraphComputeContext;

    struct RenderGraphTextureRef
    {
        uint32_t index = UINT32_MAX;

        bool is_valid() const
        {
            return index != UINT32_MAX;
        }

        explicit operator bool() const
        {
            return is_valid();
        }
    };

    inline bool operator==(RenderGraphTextureRef lhs, RenderGraphTextureRef rhs)
    {
        return lhs.index == rhs.index;
    }

    inline bool operator!=(RenderGraphTextureRef lhs, RenderGraphTextureRef rhs)
    {
        return !(lhs == rhs);
    }
}
```

- [x] **Step 4: Add resource descriptors and state mapping declarations**

Create `project/src/engine/Function/Render/RenderGraphResource.h`:

```cpp
#pragma once

#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Graphics/RHIResource.h"
#include <cstdint>
#include <memory>
#include <string>

namespace AshEngine
{
    enum class RenderGraphAccess : uint16_t
    {
        Unknown = 0,
        GraphicsSRV,
        ComputeSRV,
        GraphicsUAV,
        ComputeUAV,
        ColorAttachmentWrite,
        DepthStencilWrite,
        DepthStencilRead,
        VertexBufferRead,
        IndexBufferRead,
        ConstantBufferRead,
        CopySrc,
        CopyDst,
        Present
    };

    enum class RenderGraphDepthReadMode : uint8_t
    {
        DepthTestOnly = 0,
        DepthTestAndShaderResource
    };

    struct RenderGraphTextureDesc
    {
        uint16_t width = 1;
        uint16_t height = 1;
        RenderTextureFormat format = RenderTextureFormat::Unknown;
        bool shader_resource = true;
        bool unordered_access = false;
        bool use_optimized_clear_value = false;
        RenderColorValue optimized_clear_color{};
        RenderDepthStencilValue optimized_clear_depth_stencil{};

        static RenderGraphTextureDesc from_render_target_desc(const RenderTargetDesc& desc);
        RenderTargetDesc to_render_target_desc(const char* name) const;
    };

    RHI::AshResourceState render_graph_access_to_rhi_state(RenderGraphAccess access);
    RHI::AshResourceState render_graph_depth_read_state(RenderGraphDepthReadMode mode);
    const char* render_graph_access_name(RenderGraphAccess access);
}
```

- [x] **Step 5: Implement the mapping**

Create `project/src/engine/Function/Render/RenderGraphResource.cpp`:

```cpp
#include "Function/Render/RenderGraphResource.h"

namespace AshEngine
{
    RenderGraphTextureDesc RenderGraphTextureDesc::from_render_target_desc(const RenderTargetDesc& desc)
    {
        RenderGraphTextureDesc result{};
        result.width = desc.width;
        result.height = desc.height;
        result.format = desc.format;
        result.shader_resource = desc.shader_resource;
        result.unordered_access = desc.unordered_access;
        result.use_optimized_clear_value = desc.use_optimized_clear_value;
        result.optimized_clear_color = desc.optimized_clear_color;
        result.optimized_clear_depth_stencil = desc.optimized_clear_depth_stencil;
        return result;
    }

    RenderTargetDesc RenderGraphTextureDesc::to_render_target_desc(const char* name) const
    {
        RenderTargetDesc result{};
        result.width = width;
        result.height = height;
        result.format = format;
        result.shader_resource = shader_resource;
        result.unordered_access = unordered_access;
        result.name = name;
        result.use_optimized_clear_value = use_optimized_clear_value;
        result.optimized_clear_color = optimized_clear_color;
        result.optimized_clear_depth_stencil = optimized_clear_depth_stencil;
        return result;
    }

    RHI::AshResourceState render_graph_access_to_rhi_state(RenderGraphAccess access)
    {
        switch (access)
        {
        case RenderGraphAccess::GraphicsSRV:
            return RHI::AshResourceState::SRVGraphics;
        case RenderGraphAccess::ComputeSRV:
            return RHI::AshResourceState::SRVCompute;
        case RenderGraphAccess::GraphicsUAV:
            return RHI::AshResourceState::UAVGraphics;
        case RenderGraphAccess::ComputeUAV:
            return RHI::AshResourceState::UAVCompute;
        case RenderGraphAccess::ColorAttachmentWrite:
            return RHI::AshResourceState::RTV;
        case RenderGraphAccess::DepthStencilWrite:
            return RHI::AshResourceState::DSVWrite;
        case RenderGraphAccess::DepthStencilRead:
            return RHI::AshResourceState::DSVRead;
        case RenderGraphAccess::VertexBufferRead:
            return RHI::AshResourceState::VertexBuffer;
        case RenderGraphAccess::IndexBufferRead:
            return RHI::AshResourceState::IndexBuffer;
        case RenderGraphAccess::ConstantBufferRead:
            return RHI::AshResourceState::ConstBuffer;
        case RenderGraphAccess::CopySrc:
            return RHI::AshResourceState::CopySrc;
        case RenderGraphAccess::CopyDst:
            return RHI::AshResourceState::CopyDst;
        case RenderGraphAccess::Present:
            return RHI::AshResourceState::Present;
        case RenderGraphAccess::Unknown:
        default:
            return RHI::AshResourceState::Unknown;
        }
    }

    RHI::AshResourceState render_graph_depth_read_state(RenderGraphDepthReadMode mode)
    {
        RHI::AshResourceState state = RHI::AshResourceState::DSVRead;
        if (mode == RenderGraphDepthReadMode::DepthTestAndShaderResource)
        {
            state = state | RHI::AshResourceState::SRVGraphics;
        }
        return state;
    }

    const char* render_graph_access_name(RenderGraphAccess access)
    {
        switch (access)
        {
        case RenderGraphAccess::GraphicsSRV:
            return "GraphicsSRV";
        case RenderGraphAccess::ComputeSRV:
            return "ComputeSRV";
        case RenderGraphAccess::GraphicsUAV:
            return "GraphicsUAV";
        case RenderGraphAccess::ComputeUAV:
            return "ComputeUAV";
        case RenderGraphAccess::ColorAttachmentWrite:
            return "ColorAttachmentWrite";
        case RenderGraphAccess::DepthStencilWrite:
            return "DepthStencilWrite";
        case RenderGraphAccess::DepthStencilRead:
            return "DepthStencilRead";
        case RenderGraphAccess::VertexBufferRead:
            return "VertexBufferRead";
        case RenderGraphAccess::IndexBufferRead:
            return "IndexBufferRead";
        case RenderGraphAccess::ConstantBufferRead:
            return "ConstantBufferRead";
        case RenderGraphAccess::CopySrc:
            return "CopySrc";
        case RenderGraphAccess::CopyDst:
            return "CopyDst";
        case RenderGraphAccess::Present:
            return "Present";
        case RenderGraphAccess::Unknown:
        default:
            return "Unknown";
        }
    }
}
```

- [x] **Step 6: Add the umbrella include**

Create `project/src/engine/Function/Render/RenderGraph.h`:

```cpp
#pragma once

#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderGraphResource.h"
```

- [x] **Step 7: Run build and self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits with code `0`.

- [x] **Step 8: Commit Task 1**

```powershell
git add project/src/engine/Function/Render/RenderGraphFwd.h project/src/engine/Function/Render/RenderGraphResource.h project/src/engine/Function/Render/RenderGraphResource.cpp project/src/engine/Function/Render/RenderGraph.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add render graph core resource types"
```

---

### Task 2: Add RenderGraph Builder, Pass Builders, And Compile-Only Test Hooks

**Files:**
- Create: `project/src/engine/Function/Render/RenderGraphPass.h`
- Create: `project/src/engine/Function/Render/RenderGraphPass.cpp`
- Create: `project/src/engine/Function/Render/RenderGraphBuilder.h`
- Create: `project/src/engine/Function/Render/RenderGraphBuilder.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraph.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [x] **Step 1: Write the failing builder self-test**

Add this function near the Task 1 RenderGraph test:

```cpp
bool test_render_graph_builder_records_raster_usage()
{
    RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderGraphBuilderSelfTest");

    RenderTargetDesc external_desc{};
    external_desc.width = 64;
    external_desc.height = 64;
    external_desc.format = RenderTextureFormat::RGBA8_UNORM;
    RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(external_desc, "Output");

    RenderGraphTextureDesc transient_desc{};
    transient_desc.width = 64;
    transient_desc.height = 64;
    transient_desc.format = RenderTextureFormat::RGBA8_UNORM;
    transient_desc.shader_resource = true;
    RenderGraphTextureRef intermediate = graph.create_texture(transient_desc, "Intermediate");

    bool setup_called = false;
    bool added = graph.add_raster_pass(
        "SelfTestRasterPass",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            setup_called = true;
            pass.read_texture(intermediate, RenderGraphAccess::GraphicsSRV);
            pass.write_color(0, output, RenderLoadAction::Clear, {});
        },
        [](RenderGraphRasterContext&)
        {
            return true;
        });

    bool ok = added && setup_called;
    ok = ok && graph.get_texture_count_for_tests() == 2;
    ok = ok && graph.get_pass_count_for_tests() == 1;
    return ok || report_self_test_failure("RenderGraph builder raster usage", "builder did not record expected pass/resource usage");
}
```

Call it from `run_engine_base_self_tests()` after `test_render_graph_access_maps_to_rhi_states()`:

```cpp
all_passed = test_render_graph_builder_records_raster_usage() && all_passed;
```

- [x] **Step 2: Run build to verify the test fails**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build fails because `RenderGraphBuilder`, `RenderGraphPassFlags`, and pass builder classes do not exist.

- [x] **Step 3: Add pass declarations**

Create `project/src/engine/Function/Render/RenderGraphPass.h`:

```cpp
#pragma once

#include "Function/Render/Renderer.h"
#include "Function/Render/RenderGraphResource.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace AshEngine
{
    enum class RenderGraphPassFlags : uint8_t
    {
        None = 0,
        Raster = 1 << 0,
        Compute = 1 << 1,
        NeverCull = 1 << 2
    };

    inline RenderGraphPassFlags operator|(RenderGraphPassFlags lhs, RenderGraphPassFlags rhs)
    {
        return static_cast<RenderGraphPassFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    inline bool has_render_graph_pass_flag(RenderGraphPassFlags flags, RenderGraphPassFlags flag)
    {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    enum class RenderGraphPassKind : uint8_t
    {
        Raster = 0,
        Compute
    };

    struct RenderGraphTextureUsage
    {
        RenderGraphTextureRef texture{};
        RenderGraphAccess access = RenderGraphAccess::Unknown;
        uint8_t color_slot = UINT8_MAX;
        bool depth = false;
        RenderLoadAction load_action = RenderLoadAction::Load;
        RenderColorValue clear_color{};
        RenderDepthStencilValue clear_depth{};
        RenderGraphDepthReadMode depth_read_mode = RenderGraphDepthReadMode::DepthTestOnly;
    };

    class RenderGraphRasterContext;
    class RenderGraphComputeContext;

    struct RenderGraphPassNode
    {
        std::string name{};
        RenderGraphPassKind kind = RenderGraphPassKind::Raster;
        RenderGraphPassFlags flags = RenderGraphPassFlags::None;
        std::vector<RenderGraphTextureUsage> texture_usages{};
        std::function<bool(RenderGraphRasterContext&)> raster_execute{};
        std::function<bool(RenderGraphComputeContext&)> compute_execute{};
    };

    class RenderGraphRasterPassBuilder
    {
    public:
        explicit RenderGraphRasterPassBuilder(RenderGraphPassNode& pass);
        void read_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
        void write_color(uint8_t slot, RenderGraphTextureRef texture, RenderLoadAction load_action, RenderColorValue clear_color);
        void write_depth(RenderGraphTextureRef texture, RenderLoadAction load_action, RenderDepthStencilValue clear_value);
        void read_depth(RenderGraphTextureRef texture, RenderGraphDepthReadMode mode);

    private:
        RenderGraphPassNode* m_pass = nullptr;
    };

    class RenderGraphComputePassBuilder
    {
    public:
        explicit RenderGraphComputePassBuilder(RenderGraphPassNode& pass);
        void read_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
        void write_texture(RenderGraphTextureRef texture, RenderGraphAccess access);

    private:
        RenderGraphPassNode* m_pass = nullptr;
    };

    class RenderGraphRasterContext
    {
    public:
        virtual ~RenderGraphRasterContext() = default;
        virtual std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) = 0;
        virtual bool draw(const GraphicsDrawDesc& desc) = 0;
    };

    class RenderGraphComputeContext
    {
    public:
        virtual ~RenderGraphComputeContext() = default;
        virtual std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) = 0;
        virtual bool dispatch(const ComputeDispatchDesc& desc) = 0;
    };
}
```

- [x] **Step 4: Implement pass builders**

Create `project/src/engine/Function/Render/RenderGraphPass.cpp`:

```cpp
#include "Function/Render/RenderGraphPass.h"

namespace AshEngine
{
    RenderGraphRasterPassBuilder::RenderGraphRasterPassBuilder(RenderGraphPassNode& pass)
        : m_pass(&pass)
    {
    }

    void RenderGraphRasterPassBuilder::read_texture(RenderGraphTextureRef texture, RenderGraphAccess access)
    {
        if (m_pass)
        {
            m_pass->texture_usages.push_back({ texture, access });
        }
    }

    void RenderGraphRasterPassBuilder::write_color(uint8_t slot, RenderGraphTextureRef texture, RenderLoadAction load_action, RenderColorValue clear_color)
    {
        if (m_pass)
        {
            RenderGraphTextureUsage usage{};
            usage.texture = texture;
            usage.access = RenderGraphAccess::ColorAttachmentWrite;
            usage.color_slot = slot;
            usage.load_action = load_action;
            usage.clear_color = clear_color;
            m_pass->texture_usages.push_back(usage);
        }
    }

    void RenderGraphRasterPassBuilder::write_depth(RenderGraphTextureRef texture, RenderLoadAction load_action, RenderDepthStencilValue clear_value)
    {
        if (m_pass)
        {
            RenderGraphTextureUsage usage{};
            usage.texture = texture;
            usage.access = RenderGraphAccess::DepthStencilWrite;
            usage.depth = true;
            usage.load_action = load_action;
            usage.clear_depth = clear_value;
            m_pass->texture_usages.push_back(usage);
        }
    }

    void RenderGraphRasterPassBuilder::read_depth(RenderGraphTextureRef texture, RenderGraphDepthReadMode mode)
    {
        if (m_pass)
        {
            RenderGraphTextureUsage usage{};
            usage.texture = texture;
            usage.access = RenderGraphAccess::DepthStencilRead;
            usage.depth = true;
            usage.depth_read_mode = mode;
            usage.load_action = RenderLoadAction::Load;
            m_pass->texture_usages.push_back(usage);
        }
    }

    RenderGraphComputePassBuilder::RenderGraphComputePassBuilder(RenderGraphPassNode& pass)
        : m_pass(&pass)
    {
    }

    void RenderGraphComputePassBuilder::read_texture(RenderGraphTextureRef texture, RenderGraphAccess access)
    {
        if (m_pass)
        {
            m_pass->texture_usages.push_back({ texture, access });
        }
    }

    void RenderGraphComputePassBuilder::write_texture(RenderGraphTextureRef texture, RenderGraphAccess access)
    {
        if (m_pass)
        {
            m_pass->texture_usages.push_back({ texture, access });
        }
    }
}
```

- [x] **Step 5: Add builder declarations**

Create `project/src/engine/Function/Render/RenderGraphBuilder.h`:

```cpp
#pragma once

#include "Function/Render/RenderGraphPass.h"
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
    struct RenderGraphTextureNode
    {
        std::string name{};
        RenderGraphTextureDesc desc{};
        std::shared_ptr<RenderTarget> external_texture = nullptr;
        bool external = false;
        bool extracted = false;
    };

    class RenderGraphBuilder
    {
    public:
        RenderGraphBuilder(Renderer& renderer, const char* name);
        static RenderGraphBuilder create_headless_for_tests(const char* name);

        RenderGraphTextureRef register_external_texture(
            const std::shared_ptr<RenderTarget>& texture,
            const char* name,
            RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
        RenderGraphTextureRef register_external_texture_desc_for_tests(const RenderTargetDesc& desc, const char* name);
        RenderGraphTextureRef create_texture(const RenderGraphTextureDesc& desc, const char* name);
        void extract_texture(RenderGraphTextureRef texture);

        bool add_raster_pass(
            const char* name,
            RenderGraphPassFlags flags,
            const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
            const std::function<bool(RenderGraphRasterContext&)>& execute);

        bool add_compute_pass(
            const char* name,
            RenderGraphPassFlags flags,
            const std::function<void(RenderGraphComputePassBuilder&)>& setup,
            const std::function<bool(RenderGraphComputeContext&)>& execute);

        bool execute();

        size_t get_texture_count_for_tests() const;
        size_t get_pass_count_for_tests() const;
        const std::vector<RenderGraphTextureNode>& get_textures_for_tests() const;
        const std::vector<RenderGraphPassNode>& get_passes_for_tests() const;

    private:
        RenderGraphBuilder(Renderer* renderer, const char* name);

        Renderer* m_renderer = nullptr;
        std::string m_name{};
        std::vector<RenderGraphTextureNode> m_textures{};
        std::vector<RenderGraphPassNode> m_passes{};
    };
}
```

- [x] **Step 6: Implement builder recording**

Create `project/src/engine/Function/Render/RenderGraphBuilder.cpp`:

```cpp
#include "Function/Render/RenderGraphBuilder.h"
#include "Base/hlog.h"

namespace AshEngine
{
    RenderGraphBuilder::RenderGraphBuilder(Renderer& renderer, const char* name)
        : RenderGraphBuilder(&renderer, name)
    {
    }

    RenderGraphBuilder::RenderGraphBuilder(Renderer* renderer, const char* name)
        : m_renderer(renderer)
        , m_name(name ? name : "RenderGraph")
    {
    }

    RenderGraphBuilder RenderGraphBuilder::create_headless_for_tests(const char* name)
    {
        return RenderGraphBuilder(nullptr, name);
    }

    RenderGraphTextureRef RenderGraphBuilder::register_external_texture(
        const std::shared_ptr<RenderTarget>& texture,
        const char* name,
        RenderGraphAccess)
    {
        if (!texture)
        {
            HLogError("RenderGraph '{}': cannot register null external texture '{}'.", m_name, name ? name : "<unnamed>");
            return {};
        }

        RenderGraphTextureNode node{};
        node.name = name ? name : "ExternalTexture";
        node.external_texture = texture;
        node.external = true;
        node.desc.width = static_cast<uint16_t>(texture->get_width());
        node.desc.height = static_cast<uint16_t>(texture->get_height());
        node.desc.format = texture->get_format();
        node.desc.shader_resource = !texture->is_depth_stencil();
        node.desc.unordered_access = false;
        m_textures.push_back(std::move(node));
        return { static_cast<uint32_t>(m_textures.size() - 1u) };
    }

    RenderGraphTextureRef RenderGraphBuilder::register_external_texture_desc_for_tests(const RenderTargetDesc& desc, const char* name)
    {
        RenderGraphTextureNode node{};
        node.name = name ? name : "ExternalTextureForTests";
        node.external = true;
        node.desc = RenderGraphTextureDesc::from_render_target_desc(desc);
        m_textures.push_back(std::move(node));
        return { static_cast<uint32_t>(m_textures.size() - 1u) };
    }

    RenderGraphTextureRef RenderGraphBuilder::create_texture(const RenderGraphTextureDesc& desc, const char* name)
    {
        RenderGraphTextureNode node{};
        node.name = name ? name : "RenderGraphTexture";
        node.desc = desc;
        node.external = false;
        m_textures.push_back(std::move(node));
        return { static_cast<uint32_t>(m_textures.size() - 1u) };
    }

    void RenderGraphBuilder::extract_texture(RenderGraphTextureRef texture)
    {
        if (texture.index < m_textures.size())
        {
            m_textures[texture.index].extracted = true;
        }
    }

    bool RenderGraphBuilder::add_raster_pass(
        const char* name,
        RenderGraphPassFlags flags,
        const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
        const std::function<bool(RenderGraphRasterContext&)>& execute)
    {
        RenderGraphPassNode pass{};
        pass.name = name ? name : "RasterPass";
        pass.kind = RenderGraphPassKind::Raster;
        pass.flags = flags | RenderGraphPassFlags::Raster;
        pass.raster_execute = execute;
        RenderGraphRasterPassBuilder builder(pass);
        if (setup)
        {
            setup(builder);
        }
        m_passes.push_back(std::move(pass));
        return true;
    }

    bool RenderGraphBuilder::add_compute_pass(
        const char* name,
        RenderGraphPassFlags flags,
        const std::function<void(RenderGraphComputePassBuilder&)>& setup,
        const std::function<bool(RenderGraphComputeContext&)>& execute)
    {
        RenderGraphPassNode pass{};
        pass.name = name ? name : "ComputePass";
        pass.kind = RenderGraphPassKind::Compute;
        pass.flags = flags | RenderGraphPassFlags::Compute;
        pass.compute_execute = execute;
        RenderGraphComputePassBuilder builder(pass);
        if (setup)
        {
            setup(builder);
        }
        m_passes.push_back(std::move(pass));
        return true;
    }

    bool RenderGraphBuilder::execute()
    {
        HLogError("RenderGraph '{}': execute called before compiler/executor implementation is linked.", m_name);
        return false;
    }

    size_t RenderGraphBuilder::get_texture_count_for_tests() const
    {
        return m_textures.size();
    }

    size_t RenderGraphBuilder::get_pass_count_for_tests() const
    {
        return m_passes.size();
    }

    const std::vector<RenderGraphTextureNode>& RenderGraphBuilder::get_textures_for_tests() const
    {
        return m_textures;
    }

    const std::vector<RenderGraphPassNode>& RenderGraphBuilder::get_passes_for_tests() const
    {
        return m_passes;
    }
}
```

- [x] **Step 7: Update umbrella include**

Modify `project/src/engine/Function/Render/RenderGraph.h`:

```cpp
#pragma once

#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderGraphResource.h"
#include "Function/Render/RenderGraphPass.h"
#include "Function/Render/RenderGraphBuilder.h"
```

- [x] **Step 8: Run build and self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits with code `0`.

- [x] **Step 9: Commit Task 2**

```powershell
git add project/src/engine/Function/Render/RenderGraphPass.h project/src/engine/Function/Render/RenderGraphPass.cpp project/src/engine/Function/Render/RenderGraphBuilder.h project/src/engine/Function/Render/RenderGraphBuilder.cpp project/src/engine/Function/Render/RenderGraph.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add render graph builder model"
```

---

### Task 3: Implement Pass Culling, Lifetime, And Barrier Compilation

**Files:**
- Create: `project/src/engine/Function/Render/RenderGraphCompiler.h`
- Create: `project/src/engine/Function/Render/RenderGraphCompiler.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphBuilder.h`
- Modify: `project/src/engine/Function/Render/RenderGraphBuilder.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraph.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [x] **Step 1: Write failing compiler self-tests**

Add this function in `EngineSelfTests.cpp`:

```cpp
bool test_render_graph_compiler_culls_dead_passes_and_keeps_roots()
{
    RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderGraphCompilerSelfTest");

    RenderTargetDesc output_desc{};
    output_desc.width = 64;
    output_desc.height = 64;
    output_desc.format = RenderTextureFormat::RGBA8_UNORM;
    RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "Output");

    RenderGraphTextureDesc temp_desc{};
    temp_desc.width = 64;
    temp_desc.height = 64;
    temp_desc.format = RenderTextureFormat::RGBA8_UNORM;
    temp_desc.shader_resource = true;
    RenderGraphTextureRef live_temp = graph.create_texture(temp_desc, "LiveTemp");
    RenderGraphTextureRef dead_temp = graph.create_texture(temp_desc, "DeadTemp");

    graph.add_raster_pass(
        "LiveProducer",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            pass.write_color(0, live_temp, RenderLoadAction::Clear, {});
        },
        [](RenderGraphRasterContext&) { return true; });

    graph.add_raster_pass(
        "LiveConsumer",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            pass.read_texture(live_temp, RenderGraphAccess::GraphicsSRV);
            pass.write_color(0, output, RenderLoadAction::Clear, {});
        },
        [](RenderGraphRasterContext&) { return true; });

    graph.add_raster_pass(
        "DeadProducer",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            pass.write_color(0, dead_temp, RenderLoadAction::Clear, {});
        },
        [](RenderGraphRasterContext&) { return true; });

    graph.add_compute_pass(
        "SideEffectCompute",
        RenderGraphPassFlags::NeverCull,
        [](RenderGraphComputePassBuilder&) {},
        [](RenderGraphComputeContext&) { return true; });

    RenderGraphCompileResult result{};
    const bool compiled = graph.compile_for_tests(result);
    bool ok = compiled;
    ok = ok && result.live_pass_indices.size() == 3;
    ok = ok && result.live_pass_indices[0] == 0;
    ok = ok && result.live_pass_indices[1] == 1;
    ok = ok && result.live_pass_indices[2] == 3;
    ok = ok && result.texture_lifetimes[live_temp.index].first_pass == 0;
    ok = ok && result.texture_lifetimes[live_temp.index].last_pass == 1;
    ok = ok && result.texture_lifetimes[dead_temp.index].used == false;
    return ok || report_self_test_failure("RenderGraph compiler culling", "compiler did not cull dead passes or preserve roots");
}
```

Call it after the builder self-test:

```cpp
all_passed = test_render_graph_compiler_culls_dead_passes_and_keeps_roots() && all_passed;
```

- [x] **Step 2: Run build to verify failure**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build fails because `RenderGraphCompileResult` and `compile_for_tests()` are undefined.

- [x] **Step 3: Add compiler declarations**

Create `project/src/engine/Function/Render/RenderGraphCompiler.h`:

```cpp
#pragma once

#include "Function/Render/RenderGraphBuilder.h"
#include "Graphics/RHIResource.h"
#include <vector>

namespace AshEngine
{
    struct RenderGraphTextureLifetime
    {
        bool used = false;
        uint32_t first_pass = UINT32_MAX;
        uint32_t last_pass = UINT32_MAX;
    };

    struct RenderGraphPassBarrierPlan
    {
        std::vector<RHI::AshBarrier> barriers{};
        std::vector<RHI::AshResourceState> texture_states{};
    };

    struct RenderGraphCompileResult
    {
        std::vector<uint32_t> live_pass_indices{};
        std::vector<RenderGraphTextureLifetime> texture_lifetimes{};
        std::vector<RenderGraphPassBarrierPlan> pass_barriers{};
    };

    class RenderGraphCompiler
    {
    public:
        static bool compile(
            const std::vector<RenderGraphTextureNode>& textures,
            const std::vector<RenderGraphPassNode>& passes,
            RenderGraphCompileResult& out_result);
    };
}
```

- [x] **Step 4: Implement culling and lifetimes**

Create `project/src/engine/Function/Render/RenderGraphCompiler.cpp`:

```cpp
#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"
#include <algorithm>

namespace AshEngine
{
    namespace
    {
        bool usage_is_write(const RenderGraphTextureUsage& usage)
        {
            return usage.access == RenderGraphAccess::ColorAttachmentWrite ||
                usage.access == RenderGraphAccess::DepthStencilWrite ||
                usage.access == RenderGraphAccess::GraphicsUAV ||
                usage.access == RenderGraphAccess::ComputeUAV ||
                usage.access == RenderGraphAccess::CopyDst;
        }

        bool usage_is_read(const RenderGraphTextureUsage& usage)
        {
            return usage.access == RenderGraphAccess::GraphicsSRV ||
                usage.access == RenderGraphAccess::ComputeSRV ||
                usage.access == RenderGraphAccess::DepthStencilRead ||
                usage.access == RenderGraphAccess::VertexBufferRead ||
                usage.access == RenderGraphAccess::IndexBufferRead ||
                usage.access == RenderGraphAccess::ConstantBufferRead ||
                usage.access == RenderGraphAccess::CopySrc ||
                usage.access == RenderGraphAccess::Present;
        }
    }

    bool RenderGraphCompiler::compile(
        const std::vector<RenderGraphTextureNode>& textures,
        const std::vector<RenderGraphPassNode>& passes,
        RenderGraphCompileResult& out_result)
    {
        out_result = {};
        out_result.texture_lifetimes.resize(textures.size());
        out_result.pass_barriers.resize(passes.size());

        std::vector<int32_t> producer_for_texture(textures.size(), -1);
        std::vector<std::vector<uint32_t>> pass_dependencies(passes.size());
        bool valid = true;

        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            const RenderGraphPassNode& pass = passes[pass_index];
            bool pass_reads = false;
            bool pass_writes = false;
            for (const RenderGraphTextureUsage& usage : pass.texture_usages)
            {
                if (!usage.texture || usage.texture.index >= textures.size())
                {
                    HLogError("RenderGraphCompiler: pass '{}' references an invalid texture.", pass.name);
                    valid = false;
                    break;
                }

                if (usage_is_read(usage))
                {
                    pass_reads = true;
                    const int32_t producer = producer_for_texture[usage.texture.index];
                    if (producer >= 0)
                    {
                        pass_dependencies[producer].push_back(pass_index);
                    }
                    else if (!textures[usage.texture.index].external)
                    {
                        HLogError(
                            "RenderGraphCompiler: pass '{}' reads transient texture '{}' before it is produced.",
                            pass.name,
                            textures[usage.texture.index].name);
                        valid = false;
                    }
                }

                if (usage_is_write(usage))
                {
                    pass_writes = true;
                    producer_for_texture[usage.texture.index] = static_cast<int32_t>(pass_index);
                }
            }

            if (!valid)
            {
                break;
            }

            if (!pass_reads && !pass_writes && !has_render_graph_pass_flag(pass.flags, RenderGraphPassFlags::NeverCull))
            {
                HLogError("RenderGraphCompiler: pass '{}' has no resource usage and is not NeverCull.", pass.name);
                valid = false;
                break;
            }
        }

        if (!valid)
        {
            return false;
        }

        std::vector<bool> live_passes(passes.size(), false);
        auto mark_live = [&](auto& self, uint32_t pass_index) -> void
        {
            if (pass_index >= passes.size() || live_passes[pass_index])
            {
                return;
            }
            live_passes[pass_index] = true;
            for (const RenderGraphTextureUsage& usage : passes[pass_index].texture_usages)
            {
                if (!usage_is_read(usage) || usage.texture.index >= producer_for_texture.size())
                {
                    continue;
                }
                const int32_t producer = producer_for_texture[usage.texture.index];
                if (producer >= 0)
                {
                    self(self, static_cast<uint32_t>(producer));
                }
            }
        };

        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            if (has_render_graph_pass_flag(passes[pass_index].flags, RenderGraphPassFlags::NeverCull))
            {
                mark_live(mark_live, pass_index);
            }
        }

        for (uint32_t texture_index = 0; texture_index < textures.size(); ++texture_index)
        {
            if (!textures[texture_index].external && !textures[texture_index].extracted)
            {
                continue;
            }

            const int32_t producer = producer_for_texture[texture_index];
            if (producer >= 0)
            {
                mark_live(mark_live, static_cast<uint32_t>(producer));
            }
        }

        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            if (!live_passes[pass_index])
            {
                HLogInfo("RenderGraphCompiler: culled pass '{}'.", passes[pass_index].name);
                continue;
            }

            out_result.live_pass_indices.push_back(pass_index);
            for (const RenderGraphTextureUsage& usage : passes[pass_index].texture_usages)
            {
                RenderGraphTextureLifetime& lifetime = out_result.texture_lifetimes[usage.texture.index];
                lifetime.used = true;
                lifetime.first_pass = std::min(lifetime.first_pass, pass_index);
                lifetime.last_pass = lifetime.last_pass == UINT32_MAX ? pass_index : std::max(lifetime.last_pass, pass_index);

                RenderGraphPassBarrierPlan& barrier_plan = out_result.pass_barriers[pass_index];
                if (barrier_plan.texture_states.size() < textures.size())
                {
                    barrier_plan.texture_states.resize(textures.size(), RHI::AshResourceState::Unknown);
                }
                if (usage.depth && usage.access == RenderGraphAccess::DepthStencilRead)
                {
                    barrier_plan.texture_states[usage.texture.index] = render_graph_depth_read_state(usage.depth_read_mode);
                }
                else
                {
                    barrier_plan.texture_states[usage.texture.index] = render_graph_access_to_rhi_state(usage.access);
                }
            }
        }

        return true;
    }
}
```

- [x] **Step 5: Expose compile_for_tests and update includes**

Modify `project/src/engine/Function/Render/RenderGraphBuilder.h`:

```cpp
struct RenderGraphCompileResult;
```

Add this public method:

```cpp
bool compile_for_tests(RenderGraphCompileResult& out_result) const;
```

Modify `project/src/engine/Function/Render/RenderGraphBuilder.cpp`:

```cpp
#include "Function/Render/RenderGraphCompiler.h"
```

Add:

```cpp
bool RenderGraphBuilder::compile_for_tests(RenderGraphCompileResult& out_result) const
{
    return RenderGraphCompiler::compile(m_textures, m_passes, out_result);
}
```

Modify `project/src/engine/Function/Render/RenderGraph.h`:

```cpp
#include "Function/Render/RenderGraphCompiler.h"
```

- [x] **Step 6: Run build and self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits with code `0`.

- [x] **Step 7: Commit Task 3**

```powershell
git add project/src/engine/Function/Render/RenderGraphCompiler.h project/src/engine/Function/Render/RenderGraphCompiler.cpp project/src/engine/Function/Render/RenderGraphBuilder.h project/src/engine/Function/Render/RenderGraphBuilder.cpp project/src/engine/Function/Render/RenderGraph.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add render graph compiler culling"
```

---

### Task 4: Add RenderDevice Final-State And Barrier Hooks For Graph Execution

**Files:**
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
- Modify: `project/src/engine/Function/Render/Renderer.h`
- Modify: `project/src/engine/Function/Render/Renderer.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write the failing final-state self-test**

Add this function in `EngineSelfTests.cpp`:

```cpp
bool test_render_pass_attachment_final_state_defaults_to_unknown()
{
    PassColorAttachment color{};
    PassDepthAttachment depth{};
    bool ok = color.final_state == RHI::AshResourceState::Unknown;
    ok = ok && depth.final_state == RHI::AshResourceState::Unknown;
    color.final_state = RHI::AshResourceState::SRVGraphics;
    depth.final_state = RHI::AshResourceState::DSVRead | RHI::AshResourceState::SRVGraphics;
    ok = ok && color.final_state == RHI::AshResourceState::SRVGraphics;
    ok = ok && depth.final_state == (RHI::AshResourceState::DSVRead | RHI::AshResourceState::SRVGraphics);
    return ok || report_self_test_failure("RenderGraph attachment final state", "attachment final states are not configurable");
}
```

Call it after `test_render_graph_compiler_culls_dead_passes_and_keeps_roots()`:

```cpp
all_passed = test_render_pass_attachment_final_state_defaults_to_unknown() && all_passed;
```

- [ ] **Step 2: Run build to verify failure**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build fails because `PassColorAttachment::final_state` and `PassDepthAttachment::final_state` do not exist.

- [ ] **Step 3: Add final state fields**

Modify `project/src/engine/Function/Render/RenderDevice.h`:

```cpp
struct PassColorAttachment
{
    std::shared_ptr<RenderTarget> render_target = nullptr;
    RenderLoadAction load_action = RenderLoadAction::Clear;
    RenderColorValue clear_color{};
    RHI::AshResourceState final_state = RHI::AshResourceState::Unknown;
};

struct PassDepthAttachment
{
    std::shared_ptr<RenderTarget> render_target = nullptr;
    RenderLoadAction load_action = RenderLoadAction::Clear;
    RenderDepthStencilValue clear_value{};
    bool read_only = false;
    RHI::AshResourceState final_state = RHI::AshResourceState::Unknown;
};
```

- [ ] **Step 4: Use final state in RenderDevice begin_pass**

In `RenderDevice::begin_pass()` color attachment setup, replace the final-state argument with:

```cpp
const RHI::AshResourceState color_final_state =
    attachment.final_state != RHI::AshResourceState::Unknown ?
    attachment.final_state :
    attachment.render_target->m_impl->get_final_resource_state();
render_pass_creation.add_attachment(texture->get_format(), color_final_state, to_rhi_load_action(attachment.load_action));
```

In depth setup, compute final state before `set_depth_stencil_texture`:

```cpp
const RHI::AshResourceState depth_attachment_state = get_depth_attachment_resource_state(
    desc.depth_attachment.read_only,
    desc.depth_attachment.render_target->m_impl->shader_resource);
const RHI::AshResourceState depth_final_state =
    desc.depth_attachment.final_state != RHI::AshResourceState::Unknown ?
    desc.depth_attachment.final_state :
    depth_attachment_state;
render_pass_creation.set_depth_stencil_texture(depth_texture->get_format(), depth_final_state);
```

- [ ] **Step 5: Add graph barrier submit helpers**

Modify `project/src/engine/Function/Render/RenderDevice.h` private section:

```cpp
bool submit_graph_resource_barriers(const std::vector<RHI::AshBarrier>& barriers);
```

Modify `project/src/engine/Function/Render/RenderDevice.cpp`:

```cpp
bool RenderDevice::submit_graph_resource_barriers(const std::vector<RHI::AshBarrier>& barriers)
{
    ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
    ASH_PROCESS_ERROR(m_impl && m_impl->current_command_buffer && !m_impl->current_framebuffer);
    bResult = submit_rhi_resource_barriers(m_impl->current_command_buffer, barriers);
    ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

Modify `project/src/engine/Function/Render/Renderer.h` private or graph-only section:

```cpp
bool submit_graph_resource_barriers(const std::vector<RHI::AshBarrier>& barriers);
```

Modify `project/src/engine/Function/Render/Renderer.cpp`:

```cpp
bool Renderer::submit_graph_resource_barriers(const std::vector<RHI::AshBarrier>& barriers)
{
    return m_render_device && m_render_device->submit_graph_resource_barriers(barriers);
}
```

Add `friend class RenderGraphBuilder;` and `friend class RenderGraphExecutor;` to `Renderer` if the helper remains non-public. Prefer friend access over making raw barrier submission a general public API.

- [ ] **Step 6: Run build and self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits with code `0`.

- [ ] **Step 7: Commit Task 4**

```powershell
git add project/src/engine/Function/Render/RenderDevice.h project/src/engine/Function/Render/RenderDevice.cpp project/src/engine/Function/Render/Renderer.h project/src/engine/Function/Render/Renderer.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Allow render graph attachment final states"
```

---

### Task 5: Implement RenderGraph Execution Through Renderer

**Files:**
- Create: `project/src/engine/Function/Render/RenderGraphExecutor.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphBuilder.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphCompiler.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphCompiler.h`

- [ ] **Step 1: Add compile barrier materialization**

Modify `RenderGraphPassBarrierPlan` in `RenderGraphCompiler.h`:

```cpp
struct RenderGraphTextureTransition
{
    RenderGraphTextureRef texture{};
    RHI::AshResourceState state = RHI::AshResourceState::Unknown;
};

struct RenderGraphPassBarrierPlan
{
    std::vector<RenderGraphTextureTransition> transitions{};
    std::vector<RHI::AshResourceState> texture_states{};
};
```

In `RenderGraphCompiler.cpp`, when adding each live usage state, also push:

```cpp
if (state != RHI::AshResourceState::Unknown)
{
    barrier_plan.transitions.push_back({ usage.texture, state });
}
```

Use a local `state` variable so `texture_states` and `transitions` match:

```cpp
RHI::AshResourceState state = RHI::AshResourceState::Unknown;
if (usage.depth && usage.access == RenderGraphAccess::DepthStencilRead)
{
    state = render_graph_depth_read_state(usage.depth_read_mode);
}
else
{
    state = render_graph_access_to_rhi_state(usage.access);
}
barrier_plan.texture_states[usage.texture.index] = state;
if (state != RHI::AshResourceState::Unknown)
{
    barrier_plan.transitions.push_back({ usage.texture, state });
}
```

- [ ] **Step 2: Run build to catch compile errors**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build succeeds after compiler struct users are updated. If it fails, update all references from `barriers` to `transitions`.

- [ ] **Step 3: Add executor contexts**

Create `project/src/engine/Function/Render/RenderGraphExecutor.cpp`:

```cpp
#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"

namespace AshEngine
{
    namespace
    {
        class RasterContext final : public RenderGraphRasterContext
        {
        public:
            RasterContext(Renderer& renderer, Renderer::GraphicsPassContext& pass_context, std::vector<RenderGraphTextureNode>& textures)
                : m_renderer(renderer)
                , m_pass_context(pass_context)
                , m_textures(textures)
            {
            }

            std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) override
            {
                if (texture.index >= m_textures.size())
                {
                    return nullptr;
                }
                return m_textures[texture.index].external_texture;
            }

            bool draw(const GraphicsDrawDesc& desc) override
            {
                return m_pass_context.draw(desc);
            }

        private:
            Renderer& m_renderer;
            Renderer::GraphicsPassContext& m_pass_context;
            std::vector<RenderGraphTextureNode>& m_textures;
        };

        class ComputeContext final : public RenderGraphComputeContext
        {
        public:
            ComputeContext(Renderer& renderer, std::vector<RenderGraphTextureNode>& textures)
                : m_renderer(renderer)
                , m_textures(textures)
            {
            }

            std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) override
            {
                if (texture.index >= m_textures.size())
                {
                    return nullptr;
                }
                return m_textures[texture.index].external_texture;
            }

            bool dispatch(const ComputeDispatchDesc& desc) override
            {
                return m_renderer.dispatch(desc);
            }

        private:
            Renderer& m_renderer;
            std::vector<RenderGraphTextureNode>& m_textures;
        };

        bool texture_is_depth_format(RenderTextureFormat format)
        {
            return format == RenderTextureFormat::D24_UNORM_S8_UINT || format == RenderTextureFormat::D32_SFLOAT;
        }
    }

    bool execute_render_graph(Renderer& renderer, std::vector<RenderGraphTextureNode>& textures, const std::vector<RenderGraphPassNode>& passes)
    {
        RenderGraphCompileResult compiled{};
        if (!RenderGraphCompiler::compile(textures, passes, compiled))
        {
            return false;
        }

        for (uint32_t texture_index = 0; texture_index < textures.size(); ++texture_index)
        {
            RenderGraphTextureNode& texture = textures[texture_index];
            const bool should_allocate = !texture.external &&
                texture_index < compiled.texture_lifetimes.size() &&
                compiled.texture_lifetimes[texture_index].used;
            if (!should_allocate)
            {
                continue;
            }

            RenderTargetDesc desc = texture.desc.to_render_target_desc(texture.name.c_str());
            texture.external_texture = renderer.create_render_target(desc);
            if (!texture.external_texture)
            {
                HLogError("RenderGraph: failed to allocate transient texture '{}'.", texture.name);
                return false;
            }
        }

        for (uint32_t pass_index : compiled.live_pass_indices)
        {
            const RenderGraphPassNode& pass = passes[pass_index];
            if (pass.kind == RenderGraphPassKind::Compute)
            {
                ComputeContext context(renderer, textures);
                if (!pass.compute_execute || !pass.compute_execute(context))
                {
                    HLogError("RenderGraph: compute pass '{}' failed.", pass.name);
                    return false;
                }
                continue;
            }

            PassDesc pass_desc{};
            pass_desc.name = pass.name.c_str();
            for (const RenderGraphTextureUsage& usage : pass.texture_usages)
            {
                if (!usage.texture || usage.texture.index >= textures.size())
                {
                    HLogError("RenderGraph: raster pass '{}' has invalid texture usage.", pass.name);
                    return false;
                }

                std::shared_ptr<RenderTarget> target = textures[usage.texture.index].external_texture;
                if (!target)
                {
                    HLogError("RenderGraph: raster pass '{}' references unallocated texture '{}'.", pass.name, textures[usage.texture.index].name);
                    return false;
                }

                if (usage.access == RenderGraphAccess::ColorAttachmentWrite)
                {
                    if (texture_is_depth_format(target->get_format()))
                    {
                        HLogError("RenderGraph: pass '{}' uses depth format as color attachment.", pass.name);
                        return false;
                    }
                    if (pass_desc.color_attachments.size() <= usage.color_slot)
                    {
                        pass_desc.color_attachments.resize(static_cast<size_t>(usage.color_slot) + 1u);
                    }
                    PassColorAttachment& attachment = pass_desc.color_attachments[usage.color_slot];
                    attachment.render_target = target;
                    attachment.load_action = usage.load_action;
                    attachment.clear_color = usage.clear_color;
                    attachment.final_state = RHI::AshResourceState::SRVGraphics;
                }
                else if (usage.access == RenderGraphAccess::DepthStencilWrite || usage.access == RenderGraphAccess::DepthStencilRead)
                {
                    if (!texture_is_depth_format(target->get_format()))
                    {
                        HLogError("RenderGraph: pass '{}' uses color format as depth attachment.", pass.name);
                        return false;
                    }
                    pass_desc.depth_attachment.render_target = target;
                    pass_desc.depth_attachment.load_action = usage.load_action;
                    pass_desc.depth_attachment.clear_value = usage.clear_depth;
                    pass_desc.depth_attachment.read_only = usage.access == RenderGraphAccess::DepthStencilRead;
                    pass_desc.depth_attachment.final_state =
                        usage.access == RenderGraphAccess::DepthStencilRead ?
                        render_graph_depth_read_state(usage.depth_read_mode) :
                        RHI::AshResourceState::DSVWrite;
                }
            }

            Renderer::GraphicsPassContext pass_context{};
            if (!renderer.begin_pass(pass_desc, pass_context))
            {
                HLogError("RenderGraph: begin raster pass '{}' failed.", pass.name);
                return false;
            }

            RasterContext context(renderer, pass_context, textures);
            const bool pass_result = pass.raster_execute && pass.raster_execute(context);
            pass_context.end();
            if (!pass_result)
            {
                HLogError("RenderGraph: raster pass '{}' failed.", pass.name);
                return false;
            }
        }

        return true;
    }
}
```

- [ ] **Step 4: Wire builder execute**

In `RenderGraphBuilder.cpp`, add a forward declaration above methods:

```cpp
namespace AshEngine
{
    bool execute_render_graph(Renderer& renderer, std::vector<RenderGraphTextureNode>& textures, const std::vector<RenderGraphPassNode>& passes);
}
```

Replace `RenderGraphBuilder::execute()` with:

```cpp
bool RenderGraphBuilder::execute()
{
    if (!m_renderer)
    {
        HLogError("RenderGraph '{}': execute requires a renderer.", m_name);
        return false;
    }
    return execute_render_graph(*m_renderer, m_textures, m_passes);
}
```

- [ ] **Step 5: Run build and smoke test old direct path**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: build succeeds, self-test exits with code `0`, and Sandbox smoke exits normally. At this point scene still uses the old direct path, so this validates that adding RDG did not break existing rendering.

- [ ] **Step 6: Commit Task 5**

```powershell
git add project/src/engine/Function/Render/RenderGraphExecutor.cpp project/src/engine/Function/Render/RenderGraphBuilder.cpp project/src/engine/Function/Render/RenderGraphCompiler.h project/src/engine/Function/Render/RenderGraphCompiler.cpp
git commit -m "Execute render graph through renderer"
```

---

### Task 6: Migrate Scene Deferred Rendering Onto RenderGraph

**Files:**
- Create: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`

- [ ] **Step 1: Add graph resource carrier**

Create `project/src/engine/Function/Render/SceneDeferredGraphResources.h`:

```cpp
#pragma once

#include "Function/Render/RenderGraphFwd.h"
#include <vector>

namespace AshEngine
{
    struct SceneDeferredGraphResources
    {
        std::vector<RenderGraphTextureRef> gbuffer_targets{};
        RenderGraphTextureRef depth{};
        RenderGraphTextureRef lighting_accum{};
    };
}
```

- [ ] **Step 2: Change DeferredLightingPass interface**

Modify `DeferredLightingPass.h` to include graph types:

```cpp
#include "Function/Render/RenderGraph.h"
#include "Function/Render/SceneDeferredGraphResources.h"
```

Replace the public `render` declaration with:

```cpp
bool add_passes(
    RenderGraphBuilder& graph,
    const VisibleRenderFrame& frame,
    const SceneDeferredGraphResources& deferred_resources,
    RenderGraphTextureRef output_target,
    const SceneRenderViewContext& view_context);
```

- [ ] **Step 3: Implement DeferredLightingPass graph registration**

In `DeferredLightingPass.cpp`, replace `DeferredLightingPass::render` with an `add_passes` implementation that registers two raster passes. Use this structure:

```cpp
bool DeferredLightingPass::add_passes(
    RenderGraphBuilder& graph,
    const VisibleRenderFrame& frame,
    const SceneDeferredGraphResources& deferred_resources,
    RenderGraphTextureRef output_target,
    const SceneRenderViewContext& view_context)
{
    ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
    ASH_PROCESS_ERROR(m_base_emissive_program && m_directional_program && m_point_program && m_spot_program && m_composite_program);
    ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
    ASH_PROCESS_ERROR(deferred_resources.depth);
    ASH_PROCESS_ERROR(deferred_resources.lighting_accum);
    ASH_PROCESS_ERROR(output_target);

    ASH_PROCESS_ERROR(graph.add_raster_pass(
        "SceneDeferredLightingAccumPass",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
            {
                pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
            }
            pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
            pass.write_color(0, deferred_resources.lighting_accum, RenderLoadAction::Clear, k_lighting_accum_clear_color);
        },
        [this, &frame, &deferred_resources, &view_context, output_target](RenderGraphRasterContext& context) -> bool
        {
            const std::vector<RenderGraphTextureRef>& gbuffer_refs = deferred_resources.gbuffer_targets;
            std::shared_ptr<RenderTarget> gbuffer_a = context.get_texture(gbuffer_refs[0]);
            std::shared_ptr<RenderTarget> gbuffer_b = context.get_texture(gbuffer_refs[1]);
            std::shared_ptr<RenderTarget> gbuffer_c = context.get_texture(gbuffer_refs[2]);
            std::shared_ptr<RenderTarget> gbuffer_d = context.get_texture(gbuffer_refs[3]);
            std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(gbuffer_refs[4]);
            std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
            std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
            ASH_PROCESS_ERROR(gbuffer_a && gbuffer_b && gbuffer_c && gbuffer_d && gbuffer_e && depth && output);

            for (GraphicsProgram* program : {
                m_base_emissive_program.get(),
                m_directional_program.get(),
                m_point_program.get(),
                m_spot_program.get()
            })
            {
                ASH_PROCESS_ERROR(program != nullptr);
                ASH_PROCESS_ERROR(program->set_texture("SceneGBufferA", gbuffer_a));
                ASH_PROCESS_ERROR(program->set_texture("SceneGBufferB", gbuffer_b));
                ASH_PROCESS_ERROR(program->set_texture("SceneGBufferC", gbuffer_c));
                ASH_PROCESS_ERROR(program->set_texture("SceneGBufferD", gbuffer_d));
                ASH_PROCESS_ERROR(program->set_texture("SceneGBufferE", gbuffer_e));
                ASH_PROCESS_ERROR(program->set_texture("SceneDepth", depth));
                ASH_PROCESS_ERROR(program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
            }

            const DeferredLightingRootConstants base_constants = make_common_root_constants(frame, output);
            ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_base_emissive_program.get(), base_constants, view_context)));

            for (const VisibleLightData& light : frame.lights)
            {
                if (light.type == LightType::Directional)
                {
                    ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
                        m_directional_program.get(),
                        make_directional_constants(frame, output, light),
                        view_context)));
                }
                else if (light.type == LightType::Point)
                {
                    ASH_PROCESS_ERROR(context.draw(create_volume_draw(
                        m_point_program.get(),
                        m_sphere_vertex_buffer,
                        m_sphere_index_buffer,
                        m_sphere_index_count,
                        make_point_constants(frame, output, light),
                        view_context)));
                }
                else if (light.type == LightType::Spot)
                {
                    ASH_PROCESS_ERROR(context.draw(create_volume_draw(
                        m_spot_program.get(),
                        m_cone_vertex_buffer,
                        m_cone_index_buffer,
                        m_cone_index_count,
                        make_spot_constants(frame, output, light),
                        view_context)));
                }
            }
            ASH_PROCESS_GUARD_RETURN_END(bResult, false);
        }));

    ASH_PROCESS_ERROR(graph.add_raster_pass(
        "SceneDeferredCompositePass",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            pass.read_texture(deferred_resources.lighting_accum, RenderGraphAccess::GraphicsSRV);
            pass.write_color(0, output_target, view_context.color_load_action, view_context.color_clear_value);
        },
        [this, &frame, &deferred_resources, &view_context, output_target](RenderGraphRasterContext& context) -> bool
        {
            std::shared_ptr<RenderTarget> lighting = context.get_texture(deferred_resources.lighting_accum);
            std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
            ASH_PROCESS_ERROR(lighting && output);
            ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneLightingAccum", lighting));
            ASH_PROCESS_ERROR(m_composite_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
            ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
                m_composite_program.get(),
                make_common_root_constants(frame, output),
                view_context,
                false)));
            ASH_PROCESS_GUARD_RETURN_END(bResult, false);
        }));

    ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

Keep helper functions such as `create_fullscreen_draw`, `create_volume_draw`, and constant builders unchanged.

- [ ] **Step 4: Add graph-backed deferred path in SceneRenderer**

Modify `SceneRenderer.cpp` includes:

```cpp
#include "Function/Render/RenderGraph.h"
#include "Function/Render/SceneDeferredGraphResources.h"
```

In `SceneRenderer::render_visible_frame()`, replace the `m_use_deferred_static_mesh_path` block with:

```cpp
if (m_use_deferred_static_mesh_path)
{
    const uint32_t output_width = view_context.output_target->get_width();
    const uint32_t output_height = view_context.output_target->get_height();
    const GBufferLayoutDesc& layout = get_deferred_hq_gbuffer_layout();

    RenderGraphBuilder graph(*m_renderer, view_context.debug_name ? view_context.debug_name : "SceneRenderGraph");
    RenderGraphTextureRef output = graph.register_external_texture(view_context.output_target, "SceneOutput");

    SceneDeferredGraphResources graph_resources{};
    graph_resources.gbuffer_targets.reserve(layout.attachments.size());
    for (size_t attachment_index = 0; attachment_index < layout.attachments.size(); ++attachment_index)
    {
        const GBufferAttachmentDesc& attachment = layout.attachments[attachment_index];
        RenderGraphTextureDesc desc{};
        desc.width = static_cast<uint16_t>(output_width);
        desc.height = static_cast<uint16_t>(output_height);
        desc.format = attachment.format;
        desc.shader_resource = true;
        desc.unordered_access = false;
        desc.use_optimized_clear_value = true;
        desc.optimized_clear_color = {};
        graph_resources.gbuffer_targets.push_back(graph.create_texture(desc, attachment.name.c_str()));
    }

    RenderGraphTextureDesc depth_desc{};
    depth_desc.width = static_cast<uint16_t>(output_width);
    depth_desc.height = static_cast<uint16_t>(output_height);
    depth_desc.format = RenderTextureFormat::D32_SFLOAT;
    depth_desc.shader_resource = true;
    depth_desc.unordered_access = false;
    depth_desc.use_optimized_clear_value = true;
    depth_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
    graph_resources.depth = graph.create_texture(depth_desc, "SceneDeferredDepth");

    RenderGraphTextureDesc lighting_desc{};
    lighting_desc.width = static_cast<uint16_t>(output_width);
    lighting_desc.height = static_cast<uint16_t>(output_height);
    lighting_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
    lighting_desc.shader_resource = true;
    lighting_desc.unordered_access = false;
    lighting_desc.use_optimized_clear_value = true;
    lighting_desc.optimized_clear_color = {};
    graph_resources.lighting_accum = graph.create_texture(lighting_desc, "SceneDeferredLightingAccum");

    ASH_PROCESS_ERROR(graph.add_raster_pass(
        "SceneGBufferPass",
        RenderGraphPassFlags::None,
        [&](RenderGraphRasterPassBuilder& pass)
        {
            for (uint8_t index = 0; index < graph_resources.gbuffer_targets.size(); ++index)
            {
                pass.write_color(index, graph_resources.gbuffer_targets[index], RenderLoadAction::Clear, {});
            }
            pass.write_depth(graph_resources.depth, RenderLoadAction::Clear, view_context.depth_clear_value);
        },
        [this, &frame, &view_context](RenderGraphRasterContext& context) -> bool
        {
            return render_static_meshes_to_pass(frame, view_context, context, PassFamily::GBuffer);
        }));

    ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_passes(
        graph,
        frame,
        graph_resources,
        output,
        view_context));

    ASH_PROCESS_ERROR(graph.execute());
    break;
}
```

- [ ] **Step 5: Overload static mesh submission for graph raster context**

Modify `SceneRenderer.h` to change `render_static_meshes_to_pass` parameter type from concrete `Renderer::GraphicsPassContext&` to a small shared abstraction. The least invasive option is to add an overload:

```cpp
bool render_static_meshes_to_pass(
    const VisibleRenderFrame& frame,
    const SceneRenderViewContext& view_context,
    RenderGraphRasterContext& pass_context,
    PassFamily pass_family);
```

In `SceneRenderer.cpp`, implement the overload by extracting the common body into a local templated helper at file scope:

```cpp
template <typename PassContextT>
bool render_static_meshes_to_pass_impl(
    SceneRenderer& scene_renderer,
    const VisibleRenderFrame& frame,
    const SceneRenderViewContext& view_context,
    PassContextT& pass_context,
    PassFamily pass_family)
{
    return scene_renderer.render_static_meshes_to_pass_body(frame, view_context, pass_context, pass_family);
}
```

If a private templated member is clearer, add this to `SceneRenderer.h` private section:

```cpp
template <typename PassContextT>
bool render_static_meshes_to_pass_body(
    const VisibleRenderFrame& frame,
    const SceneRenderViewContext& view_context,
    PassContextT& pass_context,
    PassFamily pass_family);
```

Move the existing implementation body into `render_static_meshes_to_pass_body`, and make both overloads call it:

```cpp
bool SceneRenderer::render_static_meshes_to_pass(
    const VisibleRenderFrame& frame,
    const SceneRenderViewContext& view_context,
    Renderer::GraphicsPassContext& pass_context,
    PassFamily pass_family)
{
    return render_static_meshes_to_pass_body(frame, view_context, pass_context, pass_family);
}

bool SceneRenderer::render_static_meshes_to_pass(
    const VisibleRenderFrame& frame,
    const SceneRenderViewContext& view_context,
    RenderGraphRasterContext& pass_context,
    PassFamily pass_family)
{
    return render_static_meshes_to_pass_body(frame, view_context, pass_context, pass_family);
}
```

The shared body must call only `pass_context.draw(draw_desc)` for submission; both context types support that method.

- [ ] **Step 6: Build and run Sandbox smoke**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=8
```

Expected: build succeeds, self-test exits with code `0`, and Sandbox starts, renders the default scene, and exits normally.

- [ ] **Step 7: Run Editor smoke**

Run:

```powershell
./build_editor.bat Debug x64
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=8
```

Expected: build succeeds and Editor exits normally. Scene/Game viewport rendering should still be owned by Engine `ScenePresentationSubsystem`.

- [ ] **Step 8: Commit Task 6**

```powershell
git add project/src/engine/Function/Render/SceneDeferredGraphResources.h project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp
git commit -m "Migrate scene deferred path to render graph"
```

---

### Task 7: Documentation, Cleanup, And Full Validation

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h/.cpp` if `SceneDeferredResources` is no longer referenced by the deferred path
- Modify: `project/src/engine/Function/Render/SceneDeferredResources.h/.cpp` only if it becomes unused and can be removed safely

- [ ] **Step 1: Check for stale direct deferred resource usage**

Run:

```powershell
rg -n "SceneDeferredResources|m_deferred_resources|DeferredLightingPass::render\\(" project/src/engine/Function/Render docs README.md
```

Expected: `SceneDeferredResources` is not used by the active deferred path. If only legacy forward fallback code references it, leave the file in place and document that it is retained for fallback. If there are no code references, remove `SceneDeferredResources.h/.cpp`.

- [ ] **Step 2: Update README Render/RHI status**

In `README.md`, update the Renderer bullet in the development progress table to mention RDG:

```markdown
| Renderer | 已有 frame、pass、draw、dispatch、transient RT、frame stats、UI submit 等高层封装；`RenderGraph` 第一版已接入 Function/Render，支持 texture graph、raster/compute pass 声明、pass culling、lifetime 编译和 scene deferred 主路径迁移；`RenderFormatUtils` 统一维护高层格式到 RHI 的映射，Vulkan upload queue 实现已从 context 主文件拆分，DeferredHQ GBuffer、第一版 deferred lighting、静态网格 draw 排序、instance batching、单可见静态网格 fast path、barrier 去重和 pass/framebuffer cache 已接入。 |
```

- [ ] **Step 3: Update Engine developer guide**

In `docs/EngineDeveloperGuide.md`, update the render stack section to add `RenderGraph` above `Renderer`:

```markdown
当前渲染栈分为五层：

1. `GraphicsContext`
2. `Swapchain`
3. `RenderDevice`
4. `Renderer`
5. `RenderGraph`
```

Add this subsection under the Renderer section:

````markdown
### RenderGraph

`RenderGraph` 是 Function/Render 层的帧级声明式编排系统。第一版负责 texture graph、raster / compute pass 声明、pass culling、transient texture lifetime 编译，以及 pass 边界 resource state 计划。Graph 执行仍通过现有 `Renderer / RenderDevice`，不直接暴露 Vulkan、DX12 或 backend-specific RHI 类型给 Editor / Game / Client。

Scene deferred 主路径现在通过 graph 表达：

```text
SceneGBufferPass -> SceneDeferredLightingAccumPass -> SceneDeferredCompositePass
```

External output 和 extracted texture 是 culling root；`NeverCull` pass 保留；无 root 依赖的 pass 会被 compiler 剔除。Vulkan 合法性要求所有 graph barrier 都在 active render pass 外提交。
````

- [ ] **Step 4: Run full Debug validation matrix**

Use the project validation baseline. If the local automation skill is available, run the validation-loop workflow. Otherwise run these commands after setting `product/config/Engine.ini` backend for each pass:

```powershell
./build_sandbox.bat Debug x64
./build_editor.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected:

- `Sandbox --engine-self-test` exits with code `0`.
- `Sandbox + Vulkan` exits normally and logs the requested Vulkan backend.
- `Sandbox + DX12` exits normally and logs the requested DX12 backend.
- `Editor + Vulkan` exits normally and logs the requested Vulkan backend.
- `Editor + DX12` exits normally and logs the requested DX12 backend.
- Logs contain no Vulkan validation errors, DX12 error/corruption messages, command-buffer error state, or shutdown leak report.

- [ ] **Step 5: Inspect logs**

Run:

```powershell
Get-ChildItem product/logs -Filter *.logfile | Sort-Object LastWriteTime -Descending | Select-Object -First 8 FullName
```

For each newest validation log, search:

```powershell
Select-String -Path product/logs/*.logfile -Pattern "validation|ERROR|CORRUPTION|VUID|leak|CommandBuffer" -CaseSensitive
```

Expected: no new RenderGraph, Vulkan, DX12, command-buffer, or leak failures. Existing unrelated warnings should be recorded in the final handoff if they remain.

- [ ] **Step 6: Commit Task 7**

```powershell
git add README.md docs/EngineDeveloperGuide.md project/src/engine/Function/Render
git commit -m "Document render graph scene path"
```

---

## Final Acceptance

- `RenderGraph` files are in `project/src/engine/Function/Render`.
- The direct `Renderer` path still builds and remains available.
- Scene deferred rendering uses `RenderGraphBuilder`.
- Pass culling keeps external outputs, extracted resources, and `NeverCull` passes.
- Culled pass resources are not allocated during execution.
- Attachment final states can be explicitly provided by graph execution.
- Vulkan and DX12 validation pass for Sandbox and Editor.
- README and Engine developer guide describe the new render graph layer.
