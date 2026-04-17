#include "Function/Render/StaticMeshRenderAsset.h"

namespace AshEngine
{
	bool StaticMeshRenderResource::is_valid() const
	{
		return vertex_buffer != nullptr &&
			index_buffer != nullptr &&
			vertex_count > 0 &&
			index_count > 0;
	}

	bool StaticMeshRenderAsset::is_cpu_ready() const
	{
		return state == StaticMeshRenderAssetState::CpuReady || state == StaticMeshRenderAssetState::GpuReady;
	}

	bool StaticMeshRenderAsset::is_gpu_ready() const
	{
		return state == StaticMeshRenderAssetState::GpuReady && resource != nullptr && resource->is_valid();
	}

	bool StaticMeshRenderAsset::has_failed() const
	{
		return state == StaticMeshRenderAssetState::Failed;
	}

	std::string StaticMeshRenderAsset::get_last_error() const
	{
		return last_error;
	}
}
