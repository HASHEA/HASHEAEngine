#pragma once
#include "GpuProfiler.h"
#include "VulkanContext.h"
#include "Base/ds/hhash_map.hpp"
namespace RHI
{
	// GPU task names to colors
	FlatHashMap<uint64_t, uint32_t>   name_to_color;
	static uint32_t      initial_frames_paused = 15;

    void GpuVisualProfiler::init(Allocator* allocator_, uint32_t max_frames_, uint32_t max_queries_per_frame_) {

        allocator = allocator_;
        max_frames = max_frames_;
        max_queries_per_frame = max_queries_per_frame_;
        timestamps = (GPUTimeQuery*)Hashea_Alloc(allocator,sizeof(GPUTimeQuery) * max_frames * max_queries_per_frame, 1);
        per_frame_active = (uint16_t*)Hashea_Alloc(allocator, sizeof(uint16_t) * max_frames, 1);

        max_duration = 16.666f;
        current_frame = 0;
        min_time = max_time = average_time = 0.f;
        paused = false;
        pipeline_statistics = nullptr;

        memset(per_frame_active, 0, sizeof(uint16_t) * max_frames);

        name_to_color.Init(allocator, 16);
        name_to_color.SetDefaultValue(UINT32_MAX);
    }


    void GpuVisualProfiler::shutdown() {

        name_to_color.Shutdown();

        Hashea_Free(allocator,timestamps);
        Hashea_Free(allocator,per_frame_active);
    }

};