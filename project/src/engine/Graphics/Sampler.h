#pragma once
#include "RHIResource.h"
namespace RHI
{
	class Sampler;
	struct SamplerViewCreation
	{

	};
	class SamplerView : public RHIView	
	{
	public:
		SamplerView() = default;
		~SamplerView() = default;
	public:
		virtual std::shared_ptr<Sampler> get_parent_sampler() = 0;
	};
	struct SamplerCreation
	{
		/*combine for dx12_filter* separate for vk*/
		AshFilter				minFilter					= ASH_FILTER_NEAREST;
		AshFilter				magFilter					= ASH_FILTER_NEAREST;
		AshFilter				mipFilter					= ASH_FILTER_NEAREST;
		AshSamplerReductionMode reductionMode				= ASH_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
		/*****************************************/
		AshSamplerAddressMode	address_mode_u				= ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		AshSamplerAddressMode	address_mode_v				= ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		AshSamplerAddressMode	address_mode_w				= ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		AshBorderColor			border_color				= ASH_BORDER_COLOR_INT_OPAQUE_WHITE;
		AshCompareOp			compare_op					= ASH_COMPARE_OP_NEVER;
		bool					enable_anisotropy			= false;
		bool					enable_compare				= false;
		bool					unnormalized_coordinates	= false;
		float					min_lod						= 0;
		float					max_lod						= 16;
		float					mip_lod_bias				= 0;
		float					max_anisotropy				= 0;
		const char*				name						= nullptr;
	};
	class Sampler : public RHIResource
	{
	public:
		Sampler() = default;
		virtual ~Sampler() {}
	public:
		
	};

}