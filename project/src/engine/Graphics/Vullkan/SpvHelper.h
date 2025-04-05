//#pragma once
//#include "Base/hplatform.h"
//#include "VulkanHelper.hpp"
//#include "Base/hstring.h"
//using namespace AshEngine;
//namespace RHI
//{
//	static const uint32_t                k_max_count = 8;
//	static const uint32_t                k_max_specialization_constants = 4;
//
//
//	struct ConstantValue {
//
//		enum class Type : uint8_t {
//			Type_i32 = 0,
//			Type_u32,
//			Type_f32,
//			Type_count
//		}; // enum Type
//
//		union Value {
//
//			int32_t                         value_i;
//			uint32_t                         value_u;
//			float                         value_f;
//		}; // union Value
//
//		Value                           value;
//		Type                            type;
//	}; // struct ConstantValue
//
//	struct SpecializationConstant {
//
//		uint16_t                         binding = 0;
//		uint16_t                         byte_stride = 0;
//
//		ConstantValue               default_value;
//
//	}; // struct SpecializationConstant
//
//	struct SpecializationName {
//		char                        name[32];
//	}; // struct SpecializationName
//
//	struct DescriptorSetLayoutCreation {
//
//		//
//		// A single descriptor binding. It can be relative to one or more resources of the same type.
//		//
//		struct Binding {
//
//			VkDescriptorType            type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
//			uint16_t                         index = 0;
//			uint16_t                         count = 0;
//			const char* name = nullptr;  // Comes from external memory.
//		}; // struct Binding
//
//		Binding                         bindings[k_max_descriptors_per_set];
//		uint32_t                              num_bindings = 0;
//		uint32_t                             set_index = 0;
//		bool                            bindless = false;
//		bool                            dynamic = false;
//
//		const char* name = nullptr;
//		// Building helpers
//		inline DescriptorSetLayoutCreation& add_binding(const Binding& binding)
//		{
//			bindings[num_bindings++] = binding;
//			return *this;
//		}
//		inline DescriptorSetLayoutCreation& add_binding(VkDescriptorType type, uint32_t index, uint32_t count, const char* name)
//		{
//			bindings[num_bindings++] = { type, (uint16_t)index, (uint16_t)count, name };
//			return *this;
//		}
//		inline DescriptorSetLayoutCreation& add_binding_at_index(const Binding& binding, int index)
//		{
//			bindings[index] = binding;
//			num_bindings = (index + 1) > num_bindings ? (index + 1) : num_bindings;
//			return *this;
//		}
//		inline DescriptorSetLayoutCreation& set_set_index(uint32_t index)
//		{
//			set_index = index;
//			return *this;
//		}
//
//	}; // struct DescriptorSetLayoutCreation
//	struct ComputeLocalSize {
//
//		uint32_t                             x : 10;
//		uint32_t                             y : 10;
//		uint32_t                             z : 10;
//		uint32_t                             pad : 2;
//	}; // struct ComputeLocalSize
//	struct ParseResult {
//		uint32_t                         set_count = 0;
//		uint32_t                         specialization_constants_count = 0;
//		uint32_t                         push_constants_stride = 0;
//
//		DescriptorSetLayoutCreation sets[k_max_count];
//		SpecializationConstant      specialization_constants[k_max_specialization_constants];
//		SpecializationName          specialization_names[k_max_specialization_constants];
//
//		ComputeLocalSize            compute_local_size;
//	}; // struct ParseResult
//
//	void                            parse_binary_spv_cross(const uint32_t* data, size_t data_size, StringBuffer& name_buffer, ParseResult* parse_result);
//
//}