//#include "SpvHelper.h"
//#include "Base/ds/harray.hpp"
//#include "Base/hstring.h"
//#include "spirv.h"
//#include "spirv_cross.hpp"
//namespace RHI
//{
//	static const uint32_t        k_bindless_set_index = 0;
//	static const uint32_t        k_bindless_texture_binding = 10;
//
//	static void add_binding_if_unique(DescriptorSetLayoutCreation& creation, DescriptorSetLayoutCreation::Binding& binding) {
//		bool found = false;
//		for (uint32_t i = 0; i < creation.num_bindings; ++i) {
//			const DescriptorSetLayoutCreation::Binding& b = creation.bindings[i];
//			if (b.type == binding.type && b.index == binding.index) {
//				found = true;
//				break;
//			}
//		}
//
//		if (!found) {
//			creation.add_binding(binding);
//		}
//	}
//
//	void parse_binary_spv_cross(const uint32_t* data, size_t data_size, StringBuffer& name_buffer, ParseResult* parse_result)
//	{
//
//	}
//}