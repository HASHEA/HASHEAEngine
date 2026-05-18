#include "Core/SceneComponentSerialization.h"

#include <json.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace AshEditor::SceneComponentSerialization
{
	namespace
	{
		using json = nlohmann::json;

		template<typename TValue>
		bool TryReadTrivialValue(const uint8_t* pData, const uint32_t uSize, TValue& refValue)
		{
			if (uSize != sizeof(TValue))
			{
				return false;
			}

			std::memcpy(&refValue, pData, sizeof(TValue));
			return true;
		}

		template<typename TValue>
		bool TryWriteTrivialValue(uint8_t* pData, const uint32_t uSize, const TValue& refValue)
		{
			if (uSize != sizeof(TValue))
			{
				return false;
			}

			std::memcpy(pData, &refValue, sizeof(TValue));
			return true;
		}

		json SerializeVec2(const uint8_t* pPropertyData, const uint32_t uSize)
		{
			glm::vec2 vecValue{};
			return TryReadTrivialValue(pPropertyData, uSize, vecValue)
				? json::array({ vecValue.x, vecValue.y })
				: json{};
		}

		json SerializeVec3(const uint8_t* pPropertyData, const uint32_t uSize)
		{
			glm::vec3 vecValue{};
			return TryReadTrivialValue(pPropertyData, uSize, vecValue)
				? json::array({ vecValue.x, vecValue.y, vecValue.z })
				: json{};
		}

		json SerializeVec4(const uint8_t* pPropertyData, const uint32_t uSize)
		{
			glm::vec4 vecValue{};
			return TryReadTrivialValue(pPropertyData, uSize, vecValue)
				? json::array({ vecValue.x, vecValue.y, vecValue.z, vecValue.w })
				: json{};
		}

		bool TryDeserializeVec2(const json& refValue, uint8_t* pPropertyData, const uint32_t uSize)
		{
			if (!refValue.is_array() || refValue.size() != 2)
			{
				return false;
			}

			glm::vec2 vecValue{};
			vecValue.x = refValue[0].get<float>();
			vecValue.y = refValue[1].get<float>();
			return TryWriteTrivialValue(pPropertyData, uSize, vecValue);
		}

		bool TryDeserializeVec3(const json& refValue, uint8_t* pPropertyData, const uint32_t uSize)
		{
			if (!refValue.is_array() || refValue.size() != 3)
			{
				return false;
			}

			glm::vec3 vecValue{};
			vecValue.x = refValue[0].get<float>();
			vecValue.y = refValue[1].get<float>();
			vecValue.z = refValue[2].get<float>();
			return TryWriteTrivialValue(pPropertyData, uSize, vecValue);
		}

		bool TryDeserializeVec4(const json& refValue, uint8_t* pPropertyData, const uint32_t uSize)
		{
			if (!refValue.is_array() || refValue.size() != 4)
			{
				return false;
			}

			glm::vec4 vecValue{};
			vecValue.x = refValue[0].get<float>();
			vecValue.y = refValue[1].get<float>();
			vecValue.z = refValue[2].get<float>();
			vecValue.w = refValue[3].get<float>();
			return TryWriteTrivialValue(pPropertyData, uSize, vecValue);
		}

		uint64_t ReadUnsignedIntegral(const uint8_t* pData, const uint32_t uSize)
		{
			switch (uSize)
			{
			case 1:
			{
				uint8_t uValue = 0;
				return TryReadTrivialValue(pData, uSize, uValue) ? uValue : 0;
			}
			case 2:
			{
				uint16_t uValue = 0;
				return TryReadTrivialValue(pData, uSize, uValue) ? uValue : 0;
			}
			case 4:
			{
				uint32_t uValue = 0;
				return TryReadTrivialValue(pData, uSize, uValue) ? uValue : 0;
			}
			case 8:
			{
				uint64_t uValue = 0;
				return TryReadTrivialValue(pData, uSize, uValue) ? uValue : 0;
			}
			default:
				return 0;
			}
		}

		int64_t ReadSignedIntegral(const uint8_t* pData, const uint32_t uSize)
		{
			switch (uSize)
			{
			case 1:
			{
				int8_t iValue = 0;
				return TryReadTrivialValue(pData, uSize, iValue) ? iValue : 0;
			}
			case 2:
			{
				int16_t iValue = 0;
				return TryReadTrivialValue(pData, uSize, iValue) ? iValue : 0;
			}
			case 4:
			{
				int32_t iValue = 0;
				return TryReadTrivialValue(pData, uSize, iValue) ? iValue : 0;
			}
			case 8:
			{
				int64_t iValue = 0;
				return TryReadTrivialValue(pData, uSize, iValue) ? iValue : 0;
			}
			default:
				return 0;
			}
		}

		void WriteUnsignedIntegral(uint8_t* pData, const uint32_t uSize, const uint64_t uValue)
		{
			switch (uSize)
			{
			case 1:
				TryWriteTrivialValue(pData, uSize, static_cast<uint8_t>(uValue));
				return;
			case 2:
				TryWriteTrivialValue(pData, uSize, static_cast<uint16_t>(uValue));
				return;
			case 4:
				TryWriteTrivialValue(pData, uSize, static_cast<uint32_t>(uValue));
				return;
			case 8:
				TryWriteTrivialValue(pData, uSize, static_cast<uint64_t>(uValue));
				return;
			default:
				return;
			}
		}

		void WriteSignedIntegral(uint8_t* pData, const uint32_t uSize, const int64_t iValue)
		{
			switch (uSize)
			{
			case 1:
				TryWriteTrivialValue(pData, uSize, static_cast<int8_t>(iValue));
				return;
			case 2:
				TryWriteTrivialValue(pData, uSize, static_cast<int16_t>(iValue));
				return;
			case 4:
				TryWriteTrivialValue(pData, uSize, static_cast<int32_t>(iValue));
				return;
			case 8:
				TryWriteTrivialValue(pData, uSize, static_cast<int64_t>(iValue));
				return;
			default:
				return;
			}
		}

		void SerializeProperty(
			json& refComponentJson,
			const AshEngine::ScenePropertyDesc& refPropertyDesc,
			const uint8_t* pPropertyData)
		{
			switch (refPropertyDesc.type)
			{
			case AshEngine::ScenePropertyType::Bool:
			{
				bool bValue = false;
				if (TryReadTrivialValue(pPropertyData, refPropertyDesc.size, bValue))
				{
					refComponentJson[refPropertyDesc.name] = bValue;
				}
				break;
			}
			case AshEngine::ScenePropertyType::Int32:
				refComponentJson[refPropertyDesc.name] = ReadSignedIntegral(pPropertyData, refPropertyDesc.size);
				break;
			case AshEngine::ScenePropertyType::UInt32:
			case AshEngine::ScenePropertyType::Enum:
				refComponentJson[refPropertyDesc.name] = ReadUnsignedIntegral(pPropertyData, refPropertyDesc.size);
				break;
			case AshEngine::ScenePropertyType::Float:
			{
				float fValue = 0.0f;
				if (TryReadTrivialValue(pPropertyData, refPropertyDesc.size, fValue))
				{
					refComponentJson[refPropertyDesc.name] = fValue;
				}
				break;
			}
			case AshEngine::ScenePropertyType::Vec2:
			{
				const json vecJson = SerializeVec2(pPropertyData, refPropertyDesc.size);
				if (!vecJson.is_null())
				{
					refComponentJson[refPropertyDesc.name] = vecJson;
				}
				break;
			}
			case AshEngine::ScenePropertyType::Vec3:
			{
				const json vecJson = SerializeVec3(pPropertyData, refPropertyDesc.size);
				if (!vecJson.is_null())
				{
					refComponentJson[refPropertyDesc.name] = vecJson;
				}
				break;
			}
			case AshEngine::ScenePropertyType::Vec4:
			{
				const json vecJson = SerializeVec4(pPropertyData, refPropertyDesc.size);
				if (!vecJson.is_null())
				{
					refComponentJson[refPropertyDesc.name] = vecJson;
				}
				break;
			}
			case AshEngine::ScenePropertyType::String:
			{
				const std::string& strValue = *reinterpret_cast<const std::string*>(pPropertyData);
				refComponentJson[refPropertyDesc.name] = strValue;
				break;
			}
			default:
				break;
			}
		}

		void DeserializeProperty(
			const json& refComponentJson,
			const AshEngine::ScenePropertyDesc& refPropertyDesc,
			uint8_t* pPropertyData)
		{
			if (!refComponentJson.contains(refPropertyDesc.name))
			{
				return;
			}

			const json& refPropertyJson = refComponentJson[refPropertyDesc.name];
			switch (refPropertyDesc.type)
			{
			case AshEngine::ScenePropertyType::Bool:
				TryWriteTrivialValue(pPropertyData, refPropertyDesc.size, refPropertyJson.get<bool>());
				break;
			case AshEngine::ScenePropertyType::Int32:
				WriteSignedIntegral(pPropertyData, refPropertyDesc.size, refPropertyJson.get<int64_t>());
				break;
			case AshEngine::ScenePropertyType::UInt32:
			case AshEngine::ScenePropertyType::Enum:
				WriteUnsignedIntegral(pPropertyData, refPropertyDesc.size, refPropertyJson.get<uint64_t>());
				break;
			case AshEngine::ScenePropertyType::Float:
				TryWriteTrivialValue(pPropertyData, refPropertyDesc.size, refPropertyJson.get<float>());
				break;
			case AshEngine::ScenePropertyType::Vec2:
				TryDeserializeVec2(refPropertyJson, pPropertyData, refPropertyDesc.size);
				break;
			case AshEngine::ScenePropertyType::Vec3:
				TryDeserializeVec3(refPropertyJson, pPropertyData, refPropertyDesc.size);
				break;
			case AshEngine::ScenePropertyType::Vec4:
				TryDeserializeVec4(refPropertyJson, pPropertyData, refPropertyDesc.size);
				break;
			case AshEngine::ScenePropertyType::String:
				*reinterpret_cast<std::string*>(pPropertyData) = refPropertyJson.get<std::string>();
				break;
			default:
				break;
			}
		}
	}

	std::string SerializeComponentPayload(
		const void* pComponentData,
		const AshEngine::SceneComponentDesc& refComponentDesc)
	{
		// Keep the payload schema stable so undo/redo snapshots and scene clone helpers stay compatible.
		json componentJson = json::object();
		const uint8_t* pBase = static_cast<const uint8_t*>(pComponentData);
		for (uint32_t uPropertyIndex = 0; uPropertyIndex < refComponentDesc.property_count; ++uPropertyIndex)
		{
			const AshEngine::ScenePropertyDesc& refPropertyDesc = refComponentDesc.properties[uPropertyIndex];
			SerializeProperty(componentJson, refPropertyDesc, pBase + refPropertyDesc.offset);
		}

		return componentJson.dump();
	}

	bool DeserializeComponentPayload(
		std::string_view svPayloadJson,
		const AshEngine::SceneComponentDesc& refComponentDesc,
		void* pComponentData)
	{
		const json componentJson = json::parse(svPayloadJson.begin(), svPayloadJson.end(), nullptr, false);
		if (componentJson.is_discarded())
		{
			return false;
		}

		uint8_t* pBase = static_cast<uint8_t*>(pComponentData);
		for (uint32_t uPropertyIndex = 0; uPropertyIndex < refComponentDesc.property_count; ++uPropertyIndex)
		{
			const AshEngine::ScenePropertyDesc& refPropertyDesc = refComponentDesc.properties[uPropertyIndex];
			DeserializeProperty(componentJson, refPropertyDesc, pBase + refPropertyDesc.offset);
		}

		return true;
	}
}
