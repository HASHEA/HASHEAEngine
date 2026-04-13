#include "DX12Shader.h"
#include "DX12Context.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace RHI
{
	namespace
	{
		static ShaderParameterValueType dx12_variable_type_to_parameter_type(D3D_SHADER_VARIABLE_TYPE variableType)
		{
			switch (variableType)
			{
			case D3D_SVT_BOOL:
				return ShaderParameterValueType::Bool;
			case D3D_SVT_INT:
				return ShaderParameterValueType::Int;
			case D3D_SVT_UINT:
			case D3D_SVT_UINT8:
				return ShaderParameterValueType::UInt;
			case D3D_SVT_FLOAT:
			case D3D_SVT_FLOAT16:
			case D3D_SVT_DOUBLE:
				return ShaderParameterValueType::Float;
			default:
				return ShaderParameterValueType::Unknown;
			}
		}

		static AshVertexComponentFormat dx12_signature_to_vertex_format(BYTE mask, D3D_REGISTER_COMPONENT_TYPE componentType)
		{
			uint32_t componentCount = 0;
			for (uint32_t bit = 0; bit < 4; ++bit)
			{
				if ((mask & (1u << bit)) != 0)
				{
					++componentCount;
				}
			}

			switch (componentType)
			{
			case D3D_REGISTER_COMPONENT_FLOAT32:
				switch (componentCount)
				{
				case 1: return AshVertexComponentFormat::Float;
				case 2: return AshVertexComponentFormat::Float2;
				case 3: return AshVertexComponentFormat::Float3;
				case 4: return AshVertexComponentFormat::Float4;
				default: break;
				}
				break;
			case D3D_REGISTER_COMPONENT_UINT32:
				switch (componentCount)
				{
				case 1: return AshVertexComponentFormat::Uint;
				case 2: return AshVertexComponentFormat::Uint2;
				case 4: return AshVertexComponentFormat::Uint4;
				default: break;
				}
				break;
			case D3D_REGISTER_COMPONENT_SINT32:
				switch (componentCount)
				{
				case 1: return AshVertexComponentFormat::Uint;
				case 2: return AshVertexComponentFormat::Uint2;
				case 4: return AshVertexComponentFormat::Uint4;
				default: break;
				}
				break;
			default:
				break;
			}

			return AshVertexComponentFormat::FormatCount;
		}
	}

	bool DX12Shader::init(const ShaderCreation& ci)
	{
		m_stage = ci.type;
		m_name = ci.pBaseShaderPath ? ci.pBaseShaderPath : "unnamed";

		if (!_compile(ci))
			return false;

		return true;
	}

	bool DX12Shader::_compile(const ShaderCreation& ci)
	{
		// Load DXIL from cache or compile via DXC
		// First try to load precompiled DXIL cache
		std::string cachePath;
		if (ci.pBaseShaderPath)
		{
			// Include entry point in cache key to avoid collisions for multi-entry shaders
			std::string entryStr = ci.pEntryPoint ? ci.pEntryPoint : "main";
			cachePath = std::string(ci.pBaseShaderPath) + "." + entryStr + ".dxil";
			// TODO: Cache does not store reflection data, skip for now
		}

		// Compile using DXC
		ComPtr<IDxcCompiler3> compiler;
		ComPtr<IDxcUtils> utils;

		HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		if (FAILED(hr))
		{
			HLogError("DX12Shader: Failed to create DXC compiler. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
		if (FAILED(hr))
		{
			HLogError("DX12Shader: Failed to create DXC utils. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		// Load shader source
		std::string shaderPath;
		if (ci.pUserShaderPath)
			shaderPath = ci.pUserShaderPath;
		else if (ci.pBaseShaderPath)
			shaderPath = ci.pBaseShaderPath;

		ComPtr<IDxcBlobEncoding> sourceBlob;
		hr = utils->LoadFile(std::wstring(shaderPath.begin(), shaderPath.end()).c_str(), nullptr, &sourceBlob);
		if (FAILED(hr))
		{
			HLogError("DX12Shader: Failed to load shader file: {}", shaderPath);
			return false;
		}

		// Determine profile based on device's highest supported shader model
		D3D_SHADER_MODEL sm = D3D_SHADER_MODEL_6_5; // safe default
		if (auto* ctx = DX12Context::get())
			sm = ctx->get_highest_shader_model();
		int smMinor = (int)(sm & 0xF);

		wchar_t profileBuf[16];
		const wchar_t* stagePrefix = L"vs";
		switch (m_stage)
		{
		case ASH_SHADER_STAGE_VERTEX_BIT:   stagePrefix = L"vs"; break;
		case ASH_SHADER_STAGE_FRAGMENT_BIT: stagePrefix = L"ps"; break;
		case ASH_SHADER_STAGE_COMPUTE_BIT:  stagePrefix = L"cs"; break;
		case ASH_SHADER_STAGE_GEOMETRY_BIT: stagePrefix = L"gs"; break;
		case ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    stagePrefix = L"hs"; break;
		case ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: stagePrefix = L"ds"; break;
		case ASH_SHADER_STAGE_RAYGEN_BIT_KHR:
		case ASH_SHADER_STAGE_ANY_HIT_BIT_KHR:
		case ASH_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
		case ASH_SHADER_STAGE_MISS_BIT_KHR:
			stagePrefix = L"lib"; break;
		default: break;
		}
		swprintf_s(profileBuf, L"%s_6_%d", stagePrefix, smMinor);
		const wchar_t* profile = profileBuf;

		// Entry point
		std::wstring entryPointW = L"main";
		if (ci.pEntryPoint)
		{
			std::string ep(ci.pEntryPoint);
			entryPointW = std::wstring(ep.begin(), ep.end());
		}

		// Build arguments
		std::vector<LPCWSTR> args;
		args.push_back(L"-T");
		args.push_back(profile);
		args.push_back(L"-E");
		args.push_back(entryPointW.c_str());
		args.push_back(L"-Zi"); // Debug info

		// Add defines
		std::vector<std::wstring> defineStrs;
		if (ci.pShaderDef)
		{
			std::wstring def(ci.pShaderDef, ci.pShaderDef + strlen(ci.pShaderDef));
			defineStrs.push_back(def);
			args.push_back(L"-D");
			args.push_back(defineStrs.back().c_str());
		}
		if (ci.pShaderMacro)
		{
			std::wstring macro(ci.pShaderMacro, ci.pShaderMacro + strlen(ci.pShaderMacro));
			defineStrs.push_back(macro);
			args.push_back(L"-D");
			args.push_back(defineStrs.back().c_str());
		}

		// Include path from shader directory
		std::wstring includeDir;
		if (ci.pBaseShaderPath)
		{
			std::filesystem::path p(ci.pBaseShaderPath);
			includeDir = p.parent_path().wstring();
			if (!includeDir.empty())
			{
				args.push_back(L"-I");
				args.push_back(includeDir.c_str());
			}
		}

		DxcBuffer sourceBuffer = {};
		sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
		sourceBuffer.Size = sourceBlob->GetBufferSize();
		sourceBuffer.Encoding = DXC_CP_ACP;

		ComPtr<IDxcResult> result;
		hr = compiler->Compile(&sourceBuffer, args.data(), static_cast<UINT32>(args.size()),
		                       nullptr, IID_PPV_ARGS(&result));

		// Check for errors
		ComPtr<IDxcBlobUtf8> errors;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
		if (errors && errors->GetStringLength() > 0)
		{
			HLogError("DX12Shader: Compilation errors for '{}':\n{}", shaderPath, errors->GetStringPointer());
		}

		HRESULT compileStatus;
		result->GetStatus(&compileStatus);
		if (FAILED(compileStatus))
		{
			return false;
		}

		// Get compiled bytecode
		ComPtr<IDxcBlob> shaderBlob;
		result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
		if (shaderBlob)
		{
			m_bytecode.resize(shaderBlob->GetBufferSize());
			memcpy(m_bytecode.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
		}

		// Get reflection data directly from the compilation result
		// This works even without -Qstrip_reflect and gives us proper reflection
		ComPtr<IDxcBlob> reflectionBlob;
		result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr);
		if (reflectionBlob && reflectionBlob->GetBufferSize() > 0)
		{
			DxcBuffer reflectionBuffer = {};
			reflectionBuffer.Ptr = reflectionBlob->GetBufferPointer();
			reflectionBuffer.Size = reflectionBlob->GetBufferSize();
			reflectionBuffer.Encoding = 0;

			ComPtr<ID3D12ShaderReflection> reflection;
			hr = utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&reflection));
			if (SUCCEEDED(hr))
			{
				_reflect_from(reflection.Get());
			}
		}

		return true;
	}

	void DX12Shader::_reflect_from(ID3D12ShaderReflection* reflection)
	{
		if (!reflection) return;

		m_reflectionData = DX12ShaderReflectionData{};
		m_parameterBlockLayouts.clear();

		D3D12_SHADER_DESC shaderDesc;
		reflection->GetDesc(&shaderDesc);

		for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
		{
			D3D12_SHADER_INPUT_BIND_DESC bindDesc;
			reflection->GetResourceBindingDesc(i, &bindDesc);

			DX12ShaderBindingInfo info;
			info.name = bindDesc.Name;
			info.bindPoint = bindDesc.BindPoint;
			info.bindSpace = bindDesc.Space;
			info.bindCount = bindDesc.BindCount;
			info.type = bindDesc.Type;
			info.dimension = bindDesc.Dimension;

			switch (bindDesc.Type)
			{
			case D3D_SIT_CBUFFER:
			{
				ID3D12ShaderReflectionConstantBuffer* constantBuffer = reflection->GetConstantBufferByName(bindDesc.Name);
				ShaderParameterBlockLayout parameterBlock{};
				parameterBlock.name = bindDesc.Name ? bindDesc.Name : "";
				parameterBlock.bind_point = bindDesc.BindPoint;
				parameterBlock.bind_space = bindDesc.Space;
				if (constantBuffer)
				{
					D3D12_SHADER_BUFFER_DESC bufferDesc{};
					if (SUCCEEDED(constantBuffer->GetDesc(&bufferDesc)))
					{
						info.byteSize = bufferDesc.Size;
						parameterBlock.byte_size = bufferDesc.Size;

						for (UINT variableIndex = 0; variableIndex < bufferDesc.Variables; ++variableIndex)
						{
							ID3D12ShaderReflectionVariable* variable = constantBuffer->GetVariableByIndex(variableIndex);
							if (!variable)
							{
								continue;
							}

							D3D12_SHADER_VARIABLE_DESC variableDesc{};
							if (FAILED(variable->GetDesc(&variableDesc)))
							{
								continue;
							}

							ID3D12ShaderReflectionType* variableType = variable->GetType();
							if (!variableType)
							{
								continue;
							}

							D3D12_SHADER_TYPE_DESC typeDesc{};
							if (FAILED(variableType->GetDesc(&typeDesc)))
							{
								continue;
							}

							ShaderParameterMember member{};
							member.name = variableDesc.Name ? variableDesc.Name : "";
							member.offset = variableDesc.StartOffset;
							member.size = variableDesc.Size;
							member.array_size = typeDesc.Elements > 0 ? typeDesc.Elements : 1u;
							member.value_type = dx12_variable_type_to_parameter_type(typeDesc.Type);
							parameterBlock.members.push_back(std::move(member));
						}
					}
				}
				m_reflectionData.cbuffers.push_back(info);
				m_parameterBlockLayouts.push_back(std::move(parameterBlock));
				break;
			}
			case D3D_SIT_TEXTURE:
			case D3D_SIT_STRUCTURED:
			case D3D_SIT_BYTEADDRESS:
				m_reflectionData.srvs.push_back(info);
				break;
			case D3D_SIT_UAV_RWTYPED:
			case D3D_SIT_UAV_RWSTRUCTURED:
			case D3D_SIT_UAV_RWBYTEADDRESS:
			case D3D_SIT_UAV_APPEND_STRUCTURED:
			case D3D_SIT_UAV_CONSUME_STRUCTURED:
			case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
				m_reflectionData.uavs.push_back(info);
				break;
			case D3D_SIT_SAMPLER:
				m_reflectionData.samplers.push_back(info);
				break;
			default:
				break;
			}
		}

		if (m_stage == ASH_SHADER_STAGE_VERTEX_BIT)
		{
			for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
			{
				D3D12_SIGNATURE_PARAMETER_DESC inputDesc{};
				if (FAILED(reflection->GetInputParameterDesc(i, &inputDesc)))
				{
					continue;
				}

				// System-value inputs do not participate in the IA layout.
				if (inputDesc.SystemValueType != D3D_NAME_UNDEFINED)
				{
					continue;
				}

				DX12ShaderVertexInput input{};
				input.semanticName = inputDesc.SemanticName ? inputDesc.SemanticName : "";
				input.semanticIndex = inputDesc.SemanticIndex;
				input.registerIndex = inputDesc.Register;
				input.format = dx12_signature_to_vertex_format(inputDesc.Mask, inputDesc.ComponentType);
				m_reflectionData.vertexInputs.push_back(input);
			}

			std::sort(
				m_reflectionData.vertexInputs.begin(),
				m_reflectionData.vertexInputs.end(),
				[](const DX12ShaderVertexInput& lhs, const DX12ShaderVertexInput& rhs)
				{
					if (lhs.registerIndex != rhs.registerIndex)
					{
						return lhs.registerIndex < rhs.registerIndex;
					}
					if (lhs.semanticName != rhs.semanticName)
					{
						return lhs.semanticName < rhs.semanticName;
					}
					return lhs.semanticIndex < rhs.semanticIndex;
				});
		}

		// Get compute shader thread group size
		if (m_stage == ASH_SHADER_STAGE_COMPUTE_BIT)
		{
			reflection->GetThreadGroupSize(
				&m_reflectionData.threadGroupSizeX,
				&m_reflectionData.threadGroupSizeY,
				&m_reflectionData.threadGroupSizeZ);
		}
	}
}
