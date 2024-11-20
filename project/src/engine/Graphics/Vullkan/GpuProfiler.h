#pragma once
#include "Base/hplatform.h"
#include "Base/hmemory.h"
#include "Base/ds/harray.hpp"
using namespace HASHEAENGINE;
namespace RHI
{
    // A single timestamp query, containing indices for the pool, resolved time, name and color.
    struct GPUTimeQuery {

        double                              elapsed_ms;

        uint16_t                            start_query_index;  // Used to write timestamp in the query pool
        uint16_t                            end_query_index;    // Used to write timestamp in the query pool

        uint16_t                            parent_index;
        uint16_t                            depth;

        uint32_t                            color;
        uint32_t                            frame_index;

        const char* name;
    }; // struct GPUTimeQuery
    //
// Query tree used mainly per thread-frame to retrieve time data.
    struct GpuTimeQueryTree {

        void                            reset();
        void                            set_queries(GPUTimeQuery* time_queries, uint32_t count);

        GPUTimeQuery* push(const char* name);
        GPUTimeQuery* pop();

        ArrayView<GPUTimeQuery>         time_queries; // Allocated externally

        uint16_t                             current_time_query = 0;
        uint16_t                             allocated_time_query = 0;
        uint16_t                             depth = 0;

    }; // struct GpuTimeQueryTree

    //
    struct GpuPipelineStatistics {
        enum Statistics : uint8_t {
            VerticesCount,
            PrimitiveCount,
            VertexShaderInvocations,
            ClippingInvocations,
            ClippingPrimitives,
            FragmentShaderInvocations,
            ComputeShaderInvocations,
            Count
        };

        void                            reset();

        uint64_t                             statistics[Count];
    };

    struct FramePool;
    //
    //
    struct GPUTimeQueriesManager {

        void                            init(FramePool* thread_frame_pools, Allocator* allocator, uint16_t queries_per_thread, uint16_t num_threads, uint16_t max_frames);
        void                            shutdown();

        void                            reset();
        uint32_t                             resolve(uint32_t current_frame, GPUTimeQuery* timestamps_to_fill);    // Returns the total queries for this frame.

        Array<GpuTimeQueryTree>         query_trees;

        Allocator* allocator = nullptr;
        FramePool* thread_frame_pools = nullptr;
        GPUTimeQuery* timestamps = nullptr;

        GpuPipelineStatistics           frame_pipeline_statistics;  // Per frame statistics as sum of per-frame ones.

        uint32_t                             queries_per_thread = 0;
        uint32_t                             queries_per_frame = 0;
        uint32_t                             num_threads = 0;

        bool                            current_frame_resolved = false;    // Used to query the GPU only once per frame if get_gpu_timestamps is called more than once per frame.

    }; // struct GPUTimeQueriesManager

    // GpuVisualProfiler //////////////////////////////////////////////////////
    class GraphicsContext;
    //
    // Collect per frame queries from GpuProfiler and create a visual representation.
    struct GpuVisualProfiler {

        void                        init(Allocator* allocator, uint32_t max_frames, uint32_t max_queries_per_frame);
        void                        shutdown();

        void                        update(VulkanContext& context);

        void                        imgui_draw();

        Allocator* allocator;
        GPUTimeQuery* timestamps;     // Per frame timestamps collected from the profiler.
        uint16_t* per_frame_active;
        GpuPipelineStatistics* pipeline_statistics;    // Per frame collected pipeline statistics.

        uint32_t                        max_frames;
        uint32_t                        max_queries_per_frame;
        uint32_t                        current_frame;

        float                           max_time;
        float                           min_time;
        float                           average_time;

        float                           max_duration;
        bool                            paused;

    }; // struct GPUProfiler
};