#pragma once
#include "RHIResource.h"

namespace RHI
{
	class DescriptorSetLayout : public RHIResource
	{
	public:
		DescriptorSetLayout() = default;
		virtual ~DescriptorSetLayout() = default;
	};
}
