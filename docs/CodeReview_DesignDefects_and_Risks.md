# AshEngine Code Review: Design Defects & Risk Analysis

> 文档状态：**历史参考**
>
> This review is a dated audit snapshot from `2026-04-13`.
>
> It is useful for tracing background decisions and long-tail risks, but it should not be treated as the current source of truth for the editor architecture or current task status.
>
> Prefer these documents for the current editor baseline:
>
> - `docs/EditorDeveloperGuide.md`
> - `docs/editor/README.md`
> - `docs/EditorArchitectureAndRequirements.md`

**Review Date**: 2026-04-13
**Scope**: `project/src/engine/` & `project/src/editor/`
**Files Reviewed**: ~1483 source files (.h/.hpp/.cpp)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [CRITICAL: Memory Safety Issues](#3-critical-memory-safety-issues)
4. [CRITICAL: Thread Safety Issues](#4-critical-thread-safety-issues)
5. [CRITICAL: DX12 Backend Issues](#5-critical-dx12-backend-issues)
6. [HIGH: Graphics Abstraction Design Defects](#6-high-graphics-abstraction-design-defects)
7. [HIGH: Error Handling Deficiencies](#7-high-error-handling-deficiencies)
8. [MEDIUM: API Design Problems](#8-medium-api-design-problems)
9. [MEDIUM: Resource Lifecycle Issues](#9-medium-resource-lifecycle-issues)
10. [LOW: Code Quality Issues](#10-low-code-quality-issues)
11. [Architecture Gap Analysis: Missing Engine Subsystems](#11-architecture-gap-analysis-missing-engine-subsystems)
12. [Risk Summary Matrix](#12-risk-summary-matrix)
13. [Recommended Action Plan](#13-recommended-action-plan)

---

## 1. Executive Summary

AshEngine is a graphics-first engine prototype with a well-structured rendering abstraction layer (RHI) supporting DirectX 12 and Vulkan backends. The codebase demonstrates solid architectural vision in its rendering pipeline design, including deferred pass recording, automatic resource barrier management, and transient resource pooling.

However, the review uncovered **significant defects across multiple categories** that must be addressed before production use:

| Severity | Count | Categories |
|----------|-------|------------|
| **CRITICAL** | 15+ | Memory corruption, data races, DX12 API misuse |
| **HIGH** | 12+ | Abstraction leaks, missing error handling, resource leaks |
| **MEDIUM** | 15+ | API design, lifecycle management, performance anti-patterns |
| **LOW** | 10+ | Code quality, naming inconsistency, dead code |

The engine also lacks fundamental game engine subsystems (ECS, Scene Graph, Asset Pipeline, Physics, Audio), making it currently a **rendering framework** rather than a complete engine.

---

## 2. Architecture Overview

```
project/src/
├── engine/
│   ├── Base/              # Foundation layer (memory, string, file, logging, containers)
│   │   ├── ds/            # Data structures (Array, HashMap)
│   │   ├── input/         # Input state management
│   │   └── window/        # Platform window abstraction
│   ├── Function/          # Application layer
│   │   └── Render/        # RenderDevice + Renderer
│   ├── Graphics/          # RHI abstraction
│   │   ├── DirectX12/     # DX12 backend (~25 files)
│   │   ├── Vullkan/       # Vulkan backend (~20 files)
│   │   └── DXC/           # Shader compilation
│   └── EntryPoint.h       # Main entry
└── editor/                # Editor application + demo renderer
```

**Dependency Flow:**
```
Application → Renderer → RenderDevice → GraphicsContext (RHI) → DX12/Vulkan
     ↓
   Window + InputState
```

---

## 3. CRITICAL: Memory Safety Issues

### 3.1 `memset` Parameter Inversion — Buffer Corruption

**File:** `Base/hstring.cpp:31`

```cpp
memset(m_pData, size, 0);  // BUG: should be memset(m_pData, 0, size)
```

**Impact:** Sets buffer bytes to the truncated value of `size` instead of zeroing. Every `StringBuffer::init()` call produces corrupted data.

**Fix:** Swap the 2nd and 3rd parameters.

---

### 3.2 Unchecked `strcpy` — Buffer Overflow

**File:** `Base/hstring.cpp:287`

```cpp
strcpy(m_pData + string_index, string);  // No bounds check
```

**Impact:** If the interned string exceeds remaining buffer capacity, memory beyond the buffer is overwritten. Classic buffer overflow vulnerability.

**Fix:** Add bounds check `m_uCurrentSize + length + 1 <= m_uBufferSize` before copy.

---

### 3.3 Stack Allocator `free_marker` — Logic Inversion

**File:** `Base/hmemory.cpp:248-251`

```cpp
auto StackAllocator::free_marker(size_t marker) -> bool {
    const size_t difference = marker - m_szAllocatedSize;
    if (difference > 0) {          // BUG: allows freeing FORWARD
        m_szAllocatedSize = marker; // Corrupts allocation state
    }
    return true;
}
```

**Impact:** Allows setting the allocation pointer forward past existing allocations, causing memory corruption when subsequent allocations overlap with live data.

**Fix:** Check `marker < m_szAllocatedSize` and set `m_szAllocatedSize = marker`.

---

### 3.4 Circular Reference in Buffer Default Views — Memory Leak

**File:** `Graphics/DirectX12/DX12Buffer.h:56-58`

```cpp
std::shared_ptr<BufferView> m_defaultCBV;  // Holds shared_ptr to parent
std::shared_ptr<BufferView> m_defaultSRV;
std::shared_ptr<BufferView> m_defaultUAV;
```

`Buffer` inherits `enable_shared_from_this` and creates default views that hold `shared_ptr` back to the parent buffer. This creates a circular reference that prevents destruction.

**Impact:** GPU buffer resources are never freed, causing progressive VRAM exhaustion.

**Fix:** Use `weak_ptr` in views, or move default view ownership outside the buffer.

---

### 3.5 Union-Based Barrier with Placement New — Corruption Risk

**File:** `Graphics/RHIResource.h:178-221`

```cpp
AshBarrier& operator=(const AshBarrier& other) {
    if (this->eType == EType::Texture) {
        this->pTexture.~shared_ptr<Texture>();  // Manual destructor
    }
    if (other.eType == EType::Texture) {
        new (&this->pTexture) std::shared_ptr<Texture>(other.pTexture);  // Placement new
    }
    return *this;
}
```

**Impact:** If the copy constructor of `shared_ptr` throws, the union is left in a corrupted state — partially destructed, partially constructed.

**Fix:** Replace the union with `std::variant<std::shared_ptr<Texture>, std::shared_ptr<Buffer>>`.

---

### 3.6 Missing `malloc` Failure Check in Allocators

**File:** `Base/hmemory.cpp:70-71`

```cpp
m_pMemory = (uint8_t*)malloc(size);
H_ASSERT(m_pMemory);  // Debug-only assertion
```

**Impact:** In release builds, `malloc` failure results in null pointer dereference. All subsequent memory operations corrupt address 0x0.

**Fix:** Return `false` from `init()` on allocation failure; check in all callers.

---

### 3.7 Null Pointer Arithmetic in File Path Functions

**File:** `Base/hfile.cpp:328-329`

```cpp
const char* last_directory_separator = strrchr(directory->path, '\\');
size_t index = last_directory_separator - directory->path;  // UB if null
```

**Impact:** `strrchr` can return `nullptr`; pointer arithmetic on null is undefined behavior.

**Fix:** Add null check before arithmetic.

---

### 3.8 `getenv` Null Dereference

**File:** `Base/hfile.cpp:437-438`

```cpp
const char* real_output = getenv(name);
strncpy(output, real_output, outputSize);  // Crashes if env var not set
```

**Impact:** Application crashes if the environment variable doesn't exist.

**Fix:** Check `real_output != nullptr` before `strncpy`.

---

## 4. CRITICAL: Thread Safety Issues

### 4.1 GPU Descriptor Heap Allocation — Data Race

**File:** `Graphics/DirectX12/DX12DescriptorHeap.cpp:110-120`

```cpp
DX12DescriptorHandle DX12GPUDescriptorHeap::allocate(uint32_t count) {
    H_ASSERT(m_currentOffset + count <= m_maxDescriptors);  // NOT ATOMIC
    // ...
    m_currentOffset += count;  // NOT ATOMIC
}
```

The CPU descriptor heap has a `std::mutex` (line 42), but the **GPU descriptor heap has NO synchronization**. If multiple threads allocate descriptors simultaneously, `m_currentOffset` is corrupted.

**Impact:** Descriptor handle overlap, GPU memory corruption, rendering artifacts or crashes.

**Fix:** Add mutex or use `std::atomic<uint32_t>` for `m_currentOffset`.

---

### 4.2 LRU Cache — Write Under Shared Lock

**File:** `Base/hcache.h:35-36`

```cpp
std::shared_lock lock(mutex_);                                    // READ lock
lru_list.splice(lru_list.begin(), lru_list, it->second.iter);     // WRITE operation!
it->second.access_time = Clock::now();                            // WRITE operation!
```

**Impact:** Multiple threads calling `get()` concurrently will corrupt the LRU list and timestamp data.

**Fix:** Use `std::unique_lock` (write lock) for operations that modify state.

---

### 4.3 Frame Index Race in DX12Context

**File:** `Graphics/DirectX12/DX12Context.cpp:292-307`

```cpp
auto& fr = m_frameResources[m_currentFrame];  // m_currentFrame not atomic
fr.fence->wait();
fr.cmdAllocator->reset();
```

`m_currentFrame` and `m_absoluteFrame` are modified in `end_frame()` without any synchronization primitive.

**Impact:** If `begin_frame()` and `end_frame()` race (e.g., resize callback), frame resources are corrupted.

**Fix:** Use `std::atomic` for frame indices, or enforce single-threaded frame lifecycle.

---

### 4.4 ServiceManager — Check-Then-Act Race

**File:** `Base/hserviceManager.h:22-24`

```cpp
template<typename T>
T* ServiceManager::get() {
    T* service = (T*)get_service(T::k_name);
    if (!service) {
        register_service(T::instance(), T::k_name);  // TOCTOU race
    }
}
```

**Impact:** Two threads can simultaneously register the same service, causing double initialization or map corruption.

**Fix:** Add mutex around `get()` + `register_service()` sequence, or use `std::call_once`.

---

### 4.5 Window Event Queue — Concurrent Modification

**File:** `Base/window/Window.h:97`

```cpp
std::vector<WindowEvent> pendingEvents{};
inline auto push_event(const WindowEvent& event) -> void {
    pendingEvents.push_back(event);  // No synchronization
}
```

**Impact:** GLFW callbacks may run on a different thread; concurrent push/pop corrupts the vector.

**Fix:** Use `std::mutex` + `std::lock_guard`, or a lock-free queue.

---

## 5. CRITICAL: DX12 Backend Issues

### 5.1 `Map()` HRESULT Not Checked

**Files:** `DX12Buffer.cpp:132`, `DX12StagingBuffer.cpp:50`

```cpp
m_resource->Map(0, &readRange, &pData);  // HRESULT ignored!
m_mappedData = static_cast<uint8_t*>(pData);
```

**Impact:** If `Map()` fails (GPU device lost, resource eviction), `pData` is uninitialized. All subsequent `memcpy` into `m_mappedData` writes to garbage address.

**Fix:** Check `SUCCEEDED(hr)` and handle failure gracefully.

---

### 5.2 Barrier Subresource Index Calculation — Incorrect

**File:** `Graphics/DirectX12/DX12CommandBuffer.cpp:86-87`

```cpp
d3dBarrier.Transition.Subresource = barrier.IsWholeResource() ?
    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : barrier.uBaseMipLevel;
```

D3D12 subresource index is `arraySlice * mipLevelCount + mipLevel`, not just `mipLevel`. Using only `uBaseMipLevel` is incorrect for array textures.

**Impact:** Wrong subresource transitions → GPU validation errors, undefined rendering behavior.

**Fix:** Compute correct subresource index using both array slice and mip level.

---

### 5.3 Resource State Mapping Error — `CPURead` → `COPY_DEST`

**File:** `Graphics/DirectX12/DX12Helper.hpp:200`

```cpp
if (is_set(state, AshResourceState::CPURead))
    d3dState |= D3D12_RESOURCE_STATE_COPY_DEST;  // WRONG!
```

`CPURead` should map to `D3D12_RESOURCE_STATE_COPY_SOURCE`, not `COPY_DEST`. `COPY_DEST` is for write destinations.

**Impact:** Incorrect resource barriers → GPU synchronization hazards, data corruption.

**Fix:** Map `CPURead` to `D3D12_RESOURCE_STATE_COPY_SOURCE`.

---

### 5.4 Resource State Desynchronization

**File:** `Graphics/DirectX12/DX12CommandBuffer.cpp:76-89`

```cpp
dx12Tex->set_resource_state(barrier.eDSTAccess);  // State updated immediately
// But if command list recording fails, state is out of sync with GPU
```

The texture's tracked state is updated before the barrier is actually submitted to the GPU. If the command list fails or is abandoned, the CPU-side state tracker diverges from the actual GPU state.

**Impact:** Subsequent barrier transitions are incorrect, causing GPU validation failures.

---

### 5.5 Orphaned `DX12ResourceTracker`

**File:** `Graphics/DirectX12/DX12ResourceTracker.cpp`

The `DX12ResourceTracker` class exists but is **never used** in the actual rendering pipeline. Buffers and textures manage their own state individually, bypassing the tracker entirely.

**Impact:** Dead code that creates false confidence in resource tracking. Wasted maintenance burden.

**Fix:** Either integrate it into the barrier system or remove it.

---

### 5.6 Framebuffer RTV Null Dereference

**File:** `Graphics/DirectX12/DX12Framebuffer.cpp:26-41`

```cpp
auto* dx12Tex = static_cast<DX12Texture*>(tex.get());
auto rtv = dx12Tex->get_default_rtv();      // Could be null
m_rtvHandles[m_rtvCount] = dx12View->get_descriptor_handle().cpuHandle;  // Crash
```

No null check on the RTV before dereferencing.

**Impact:** Crash when binding a texture that wasn't created with RTV support.

---

## 6. HIGH: Graphics Abstraction Design Defects

### 6.1 DX12 Specifics Leak Through Abstract Interfaces

Multiple abstract interface implementations expose DX12-specific types:

| File | Leaked Type |
|------|-------------|
| `DX12BufferView.h:22` | `DX12DescriptorHandle` via `get_descriptor_handle()` |
| `DX12TextureView.h:22` | `DX12DescriptorHandle` via `get_descriptor_handle()` |
| `DX12DescriptorSetLayout.h:18` | `ID3D12RootSignature*` via `get_root_signature()` |
| `DX12DescriptorSet.h:14` | `D3D12_GPU_DESCRIPTOR_HANDLE` via `get_gpu_handle()` |
| `DX12Pipeline.h:21` | `ID3D12PipelineState*` via `get_pso()` |

**Impact:** Any code using these methods is permanently coupled to DX12. A Vulkan backend cannot implement these interfaces correctly, violating the entire RHI abstraction premise.

**Fix:** Move DX12-specific accessors to DX12-internal code; use `static_cast` within the DX12 backend only.

---

### 6.2 Empty Abstract Classes — Broken Abstraction Contract

Several "abstract" classes define **no pure virtual methods**:

| Class | File | Methods |
|-------|------|---------|
| `Queue` | `Queue.h` | Zero methods (only virtual destructor) |
| `DescriptorSet` | `DescriptorSet.h` | Zero methods |
| `DescriptorSetLayout` | `DescriptorSetLayout.h` | Zero methods |
| `Pipeline` | `Pipeline.h` | Zero methods |

**Impact:** These classes serve no abstraction purpose. Backend implementations add their own unrelated methods, defeating polymorphism.

**Fix:** Define meaningful pure virtual interfaces, or remove the abstract base classes and use backend-specific types directly.

---

### 6.3 Descriptor Management Not Abstracted

The descriptor heap system (`DX12DescriptorHeap`, `DX12CPUDescriptorHeap`, `DX12GPUDescriptorHeap`) is entirely DX12-specific with no abstract counterpart. Vulkan uses a fundamentally different model (VkDescriptorPool + VkDescriptorSet).

**Impact:** The Vulkan backend will require a completely different resource binding architecture, likely breaking the `RenderProgramBinder` abstraction.

---

## 7. HIGH: Error Handling Deficiencies

### 7.1 Inconsistent Error Strategy

The codebase uses **four different error handling mechanisms** without a consistent policy:

| Mechanism | Where Used | Release Behavior |
|-----------|-----------|-----------------|
| `H_ASSERT` | Memory allocation, array bounds | Silently ignored |
| `H_ASSERTLOG` | Initialization failures | Silently ignored |
| `std::exception` | Filesystem operations | Propagates |
| `return nullptr` / `return false` | Resource creation | Silent failure |

**Impact:** In release builds, critical assertions are stripped, causing null pointer dereferences and memory corruption instead of graceful failure.

**Fix:** Adopt a single error strategy. Recommended: use `Result<T, Error>` return types for fallible operations; reserve assertions for invariant violations only.

---

### 7.2 File I/O — `fread` Return Value Ignored

**File:** `Base/hfile.cpp:38-48`

```cpp
fread(outData, 1, fileSize, file);  // Partial reads treated as success
```

**Impact:** On network drives or under memory pressure, partial reads produce corrupted data silently.

---

### 7.3 File Delete Return Value Inverted on Windows

**File:** `Base/hfile.cpp:140-141`

```cpp
#if defined(_WIN64)
    int result = remove(filePath);
    return result != 0;  // BUG: remove() returns 0 on success
#else
    return (result == 0);  // Correct on Linux
#endif
```

**Impact:** On Windows, successful file deletion reports failure, and failed deletion reports success.

**Fix:** Change Windows path to `return result == 0`.

---

### 7.4 Shader Wide String Conversion — Multibyte Unsafe

**File:** `Graphics/DirectX12/DX12Shader.cpp:60-65`

```cpp
std::wstring(shaderPath.begin(), shaderPath.end())  // Breaks on non-ASCII
```

**Impact:** Shader paths containing CJK characters, accented letters, or other multibyte chars produce corrupted wide strings.

**Fix:** Use `MultiByteToWideChar` or `std::filesystem::path::wstring()`.

---

## 8. MEDIUM: API Design Problems

### 8.1 Ambiguous Allocator Ownership

Throughout `hmemory.h`, `hstring.h`, `hfile.h` — `Allocator*` pointers are passed around without documenting ownership semantics. Callers don't know if they should free the allocator.

**Recommendation:** Use `Allocator&` for borrowed references, or `std::shared_ptr<Allocator>` for shared ownership.

---

### 8.2 File Path Functions Mutate Input

**File:** `Base/hfile.cpp:170-189`

```cpp
auto file_directory_from_path(char* path) -> void {
    *(last_separator + 1) = 0;  // Truncates input string!
}
```

**Impact:** Caller's string is silently modified. Violates principle of least surprise.

**Fix:** Take `const char*` and return a new string or `StringView`.

---

### 8.3 Serializer Dual-Mode Confusion

**File:** `Base/hserialization.h:147-224`

A single `Serializer` class serves as both reader and writer via `isReading`/`isWriting` flags. This state machine is error-prone — calling write methods while in read mode produces undefined results.

**Fix:** Split into separate `Serializer` and `Deserializer` classes.

---

### 8.4 `StringBuffer` Lacks RAII

**File:** `Base/hstring.h:20-44`

```cpp
struct StringBuffer {
    auto init(...) -> void;     // Manual init
    auto shutdown() -> void;    // Manual cleanup — easy to forget
};
```

**Impact:** Forgetting `shutdown()` leaks memory. No destructor protection.

**Fix:** Add destructor that calls `shutdown()`, or use constructor/destructor pattern.

---

### 8.5 Static Service Locator Pattern

**File:** `Function/Application.h:56-59`

```cpp
static auto get_renderer() -> Renderer*;
static auto get_input() -> InputState*;
```

**Impact:** Global mutable state, impossible to unit test, hidden dependencies.

**Recommendation:** Pass dependencies explicitly via constructor injection.

---

### 8.6 Window Event Queue Uses `vector::erase(begin())` — O(n)

**File:** `Base/window/Window.h:91`

```cpp
pendingEvents.erase(pendingEvents.begin());  // O(n) every frame
```

**Impact:** Linear cost per event poll; degrades with event volume.

**Fix:** Use `std::deque` or `std::queue`.

---

## 9. MEDIUM: Resource Lifecycle Issues

### 9.1 Deferred Deletion Not Applied Uniformly

Some GPU resources use frame-deferred deletion (`DX12Buffer`, `DX12Pipeline`), while others reset immediately. During shutdown, if the context is destroyed while deletion queues are non-empty, those resources leak.

---

### 9.2 Dangling Weak Pointers Without Null Check

**File:** `Graphics/DirectX12/DX12BufferView.h:27-30`

```cpp
auto get_parent_buffer() -> std::shared_ptr<Buffer> override {
    return std::static_pointer_cast<Buffer>(m_parentBuffer.lock());  // May return null
}
```

Callers don't check for null, risking dereference of expired parent resources.

---

### 9.3 Unbounded Shader and Sampler Caches

**Files:** `Graphics/DirectX12/DX12Context.h:123-124`

```cpp
Array<std::shared_ptr<Sampler>> m_samplerCache;           // Grows forever
FlatHashMap<uint64_t, std::shared_ptr<Shader>> m_shaderPool;  // Grows forever
```

**Impact:** Over long sessions or with hot-reload, memory usage grows without bound.

**Fix:** Add LRU eviction or periodic cleanup.

---

### 9.4 Transient Render Target Pool — No Size Limit

**File:** `Function/Render/RenderDevice.cpp:1738-1770`

The transient render target pool grows unbounded. If resolution changes frequently, old-resolution targets accumulate.

**Fix:** Add max pool size or periodic trimming.

---

### 9.5 `GraphicsPassContext` Can Outlive Renderer

**File:** `Function/Render/Renderer.h:45-69`

`GraphicsPassContext` holds a raw pointer to the renderer. If the pass context outlives the renderer (e.g., stored in a lambda), it becomes a dangling pointer.

---

## 10. LOW: Code Quality Issues

### 10.1 Duplicate Macro Definition

**File:** `Base/hcore.h:6-10, 17-21`

`NO_COPYABLE` is defined twice in the same file with no guard.

---

### 10.2 Linux File Path Uses Wrong Function

**File:** `Base/hfile.cpp:167`

```cpp
return readlink(path, outFullPath, maxSize);  // readlink is for symlinks
```

Should use `realpath()` for resolving full paths.

---

### 10.3 `safe_cast` Is Not Safe

**File:** `Base/hplatform.h:21-29`

```cpp
template <typename To, typename From>
To safe_cast(From a) {
    To result = (To)a;
    // ... assert in debug, return potentially invalid result anyway
    return result;
}
```

Returns the cast result even when overflow is detected.

---

### 10.4 Stub Files With No Content

**Files:** `Function/em.h`, `Function/em1.h`

Empty main functions with no purpose. Should be removed or documented.

---

### 10.5 Typo in Directory Name

**Directory:** `Graphics/Vullkan/` — "Vullkan" should be "Vulkan".

---

### 10.6 Inconsistent Smart Pointer Usage

The codebase mixes `shared_ptr`, `unique_ptr`, raw pointers, and `ComPtr` without a clear ownership policy:

- `std::shared_ptr<Shader>` in context cache
- `std::unique_ptr<IGraphicsRenderProgram>` in render programs
- Raw `DX12StagingBuffer*` in context
- `ComPtr<ID3D12Device>` for COM objects

---

## 11. Architecture Gap Analysis: Missing Engine Subsystems

As a game engine, AshEngine is missing several critical subsystems:

| Subsystem | Status | Impact |
|-----------|--------|--------|
| **ECS (Entity Component System)** | Not implemented | Cannot manage game objects |
| **Scene Graph / Spatial Hierarchy** | Not implemented | No transform hierarchy |
| **Asset Pipeline / Resource Manager** | Not implemented | No async loading, no caching |
| **Material System** | Not implemented | Direct GPU programming only |
| **Physics** | Not implemented | No collision, no dynamics |
| **Audio** | Not implemented | No sound |
| **Animation** | Not implemented | No skeletal/procedural animation |
| **Particle System** | Not implemented | No visual effects |
| **UI / ImGui Integration** | Stub only | `_on_gui()` is empty |
| **Scripting** | Not implemented | No gameplay scripting |
| **Networking** | Not implemented | No multiplayer |
| **Profiler** | Stub only | `hprofiler.h` is minimal |
| **LOD System** | Not implemented | No level of detail |
| **Texture Streaming** | Not implemented | All textures fully resident |

**Current State:** The engine functions as a **rendering framework** suitable for graphics demos, but lacks the game logic infrastructure needed for a complete engine.

---

## 12. Risk Summary Matrix

| ID | Issue | Severity | Category | Likelihood | Impact |
|----|-------|----------|----------|------------|--------|
| R01 | `memset` parameter swap in StringBuffer | CRITICAL | Memory Safety | Certain | Data corruption on every use |
| R02 | `strcpy` buffer overflow in string interning | CRITICAL | Memory Safety | High | Arbitrary memory write |
| R03 | Stack allocator `free_marker` logic inversion | CRITICAL | Memory Safety | High | Heap corruption |
| R04 | Circular shared_ptr in Buffer default views | CRITICAL | Memory Leak | Certain | Progressive VRAM exhaustion |
| R05 | GPU descriptor heap race condition | CRITICAL | Thread Safety | High | GPU crash / corruption |
| R06 | LRU cache write under shared lock | CRITICAL | Thread Safety | Medium | Data race / corruption |
| R07 | `Map()` HRESULT unchecked | CRITICAL | DX12 | Medium | Crash on device lost |
| R08 | Barrier subresource index wrong | HIGH | DX12 | Medium | Wrong GPU transitions |
| R09 | `CPURead` maps to `COPY_DEST` | HIGH | DX12 | Certain | Silent data corruption |
| R10 | DX12 abstraction leak in interfaces | HIGH | Architecture | Certain | Cannot implement Vulkan backend |
| R11 | Empty abstract classes | HIGH | Architecture | Certain | Abstraction provides no value |
| R12 | File delete return inverted on Windows | HIGH | Logic Error | Certain | Wrong success/failure reporting |
| R13 | No error handling in release builds | HIGH | Reliability | High | Crashes instead of recovery |
| R14 | Frame index race condition | MEDIUM | Thread Safety | Low | Frame corruption |
| R15 | ServiceManager TOCTOU race | MEDIUM | Thread Safety | Low | Double registration |
| R16 | Shader path multibyte conversion | MEDIUM | Correctness | Medium | Broken CJK file paths |
| R17 | Unbounded caches | MEDIUM | Resource | Low | Slow memory growth |
| R18 | Missing engine subsystems | MEDIUM | Architecture | Certain | Not a complete engine |

---

## 13. Recommended Action Plan

### Phase 1: Critical Fixes (Immediate)

1. **Fix `memset` parameter order** in `hstring.cpp:31`
2. **Add bounds checking** to `StringBuffer::append` / `StringArray::intern`
3. **Fix `free_marker` logic** in `hmemory.cpp:248`
4. **Break circular reference** in Buffer default views (use `weak_ptr`)
5. **Add mutex to GPU descriptor heap** `allocate()`
6. **Fix LRU cache** to use `unique_lock` for writes
7. **Check all `Map()` HRESULTs** in DX12Buffer and DX12StagingBuffer
8. **Fix `CPURead` → `COPY_SOURCE`** mapping in DX12Helper.hpp
9. **Fix file delete return value** on Windows in hfile.cpp
10. **Add null check for `getenv`** in hfile.cpp

### Phase 2: Architecture Hardening (Short-term)

1. **Define error handling policy** — pick one strategy and apply consistently
2. **Fix barrier subresource calculation** to include array slice
3. **Remove DX12 type leaks** from abstract interfaces
4. **Replace union barrier** with `std::variant`
5. **Add RAII** to `StringBuffer`, allocators, and other manual-lifecycle classes
6. **Add thread safety** to ServiceManager and Window event queue
7. **Remove or integrate** orphaned `DX12ResourceTracker`

### Phase 3: Engine Subsystems (Medium-term)

1. **Implement ECS** — Entity, Component, System framework
2. **Implement Scene Graph** — Transform hierarchy, scene serialization
3. **Implement Asset Pipeline** — Async loading, reference counting, caching
4. **Implement Material System** — Shader parameters, material instances
5. **Integrate ImGui** — Editor UI, property inspector
6. **Add profiling** — GPU timing, CPU profiling, memory tracking

### Phase 4: Polish (Long-term)

1. Add pool size limits and LRU eviction to caches
2. Implement texture streaming
3. Add LOD system
4. Physics and audio integration
5. Comprehensive unit test suite
6. Fix directory typo `Vullkan` → `Vulkan`

---

*End of Review Document*
