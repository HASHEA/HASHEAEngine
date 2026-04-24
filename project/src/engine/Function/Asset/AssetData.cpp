#include "AssetData.h"

#include "Base/hlog.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#ifdef VOID
#undef VOID
#endif
#include <ofbx.h>

#include <json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		constexpr uint32_t k_ashasset_file_version = 2;

		static auto make_error(std::string* out_error, std::string_view message) -> bool
		{
			if (out_error)
			{
				*out_error = std::string(message);
			}
			return false;
		}

		static auto clear_error(std::string* out_error) -> void
		{
			if (out_error)
			{
				out_error->clear();
			}
		}

		static auto to_lower_copy(std::string value) -> std::string
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		static auto clamp01(float value) -> float
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		static auto to_json_vec3(const glm::vec3& value) -> json
		{
			return json::array({ value.x, value.y, value.z });
		}

		static auto from_json_vec3(const json& value, const glm::vec3& fallback) -> glm::vec3
		{
			if (!value.is_array() || value.size() != 3)
			{
				return fallback;
			}

			glm::vec3 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			result.z = value[2].get<float>();
			return result;
		}

		static auto serialize_material_overrides(const std::vector<MeshMaterialOverride>& overrides) -> json
		{
			json result = json::array();
			for (const MeshMaterialOverride& override_desc : overrides)
			{
				if (override_desc.material_slot == k_invalid_material_slot || override_desc.material_path.empty())
				{
					continue;
				}

				result.push_back(
				{
					{ "material_slot", override_desc.material_slot },
					{ "material_path", override_desc.material_path },
				});
			}
			return result;
		}

		static auto deserialize_material_overrides(const json& value) -> std::vector<MeshMaterialOverride>
		{
			std::vector<MeshMaterialOverride> overrides{};
			if (!value.is_array())
			{
				return overrides;
			}

			overrides.reserve(value.size());
			for (const json& entry : value)
			{
				if (!entry.is_object())
				{
					continue;
				}

				MeshMaterialOverride override_desc{};
				override_desc.material_slot = entry.value("material_slot", k_invalid_material_slot);
				override_desc.material_path = entry.value("material_path", std::string{});
				if (override_desc.material_slot == k_invalid_material_slot || override_desc.material_path.empty())
				{
					continue;
				}

				overrides.push_back(std::move(override_desc));
			}

			return overrides;
		}

		static auto ensure_model_default_material_references(Model& model) -> void
		{
			std::unordered_map<uint32_t, std::string> explicit_material_paths{};
			explicit_material_paths.reserve(model.default_materials.size());
			for (const ModelMaterialReference& reference : model.default_materials)
			{
				if (reference.material_slot == k_invalid_material_slot || reference.material_slot >= model.material_slots.size())
				{
					continue;
				}

				explicit_material_paths.emplace(reference.material_slot, reference.material_path);
			}

			model.default_materials.clear();
			model.default_materials.reserve(model.material_slots.size());
			for (uint32_t material_slot = 0; material_slot < model.material_slots.size(); ++material_slot)
			{
				ModelMaterialReference reference{};
				reference.material_slot = material_slot;
				if (const auto found = explicit_material_paths.find(material_slot); found != explicit_material_paths.end())
				{
					reference.material_path = found->second;
				}
				model.default_materials.push_back(std::move(reference));
			}
		}

		static auto resolve_embedded_resource_path(const std::filesystem::path& base_path, std::string_view uri) -> std::string
		{
			if (uri.empty())
			{
				return {};
			}

			if (uri.rfind("data:", 0) == 0)
			{
				return std::string(uri);
			}

			std::filesystem::path resource_path{ std::string(uri) };
			if (!resource_path.is_absolute())
			{
				resource_path = (base_path.parent_path() / resource_path).lexically_normal();
			}
			return resource_path.generic_string();
		}

		static auto recalculate_mesh_bounds(Mesh& mesh) -> void
		{
			if (mesh.vertices.empty())
			{
				mesh.bounds_min = glm::vec3(0.0f);
				mesh.bounds_max = glm::vec3(0.0f);
				return;
			}

			glm::vec3 minimum = mesh.vertices.front().position;
			glm::vec3 maximum = mesh.vertices.front().position;
			for (const MeshVertex& vertex : mesh.vertices)
			{
				minimum = glm::min(minimum, vertex.position);
				maximum = glm::max(maximum, vertex.position);
			}

			mesh.bounds_min = minimum;
			mesh.bounds_max = maximum;
		}

		static auto generate_missing_normals(Mesh& mesh) -> void
		{
			if (mesh.vertices.empty() || mesh.indices.size() < 3)
			{
				return;
			}

			for (MeshVertex& vertex : mesh.vertices)
			{
				vertex.normal = glm::vec3(0.0f);
			}

			for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
			{
				const uint32_t i0 = mesh.indices[index + 0];
				const uint32_t i1 = mesh.indices[index + 1];
				const uint32_t i2 = mesh.indices[index + 2];
				if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
				{
					continue;
				}

				const glm::vec3& p0 = mesh.vertices[i0].position;
				const glm::vec3& p1 = mesh.vertices[i1].position;
				const glm::vec3& p2 = mesh.vertices[i2].position;
				glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
				if (glm::length2(normal) > 0.0f)
				{
					normal = glm::normalize(normal);
				}
				else
				{
					normal = glm::vec3(0.0f, 0.0f, 1.0f);
				}

				mesh.vertices[i0].normal += normal;
				mesh.vertices[i1].normal += normal;
				mesh.vertices[i2].normal += normal;
			}

			for (MeshVertex& vertex : mesh.vertices)
			{
				if (glm::length2(vertex.normal) > 0.0f)
				{
					vertex.normal = glm::normalize(vertex.normal);
				}
				else
				{
					vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
				}
			}

			mesh.has_normals = true;
		}

		static auto compose_trs(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale) -> glm::mat4
		{
			glm::mat4 result = glm::translate(glm::mat4(1.0f), translation);
			result *= glm::mat4_cast(rotation);
			result = glm::scale(result, scale);
			return result;
		}

		static auto matrix_to_transform_component(const glm::mat4& matrix) -> TransformComponent
		{
			TransformComponent transform{};
			glm::vec3 scale{};
			glm::quat rotation{};
			glm::vec3 translation{};
			glm::vec3 skew{};
			glm::vec4 perspective{};
			if (glm::decompose(matrix, scale, rotation, translation, skew, perspective))
			{
				transform.position = translation;
				transform.scale = scale;
				transform.rotation_euler_degrees = glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
			}
			return transform;
		}

		static auto data_view_to_string(const ofbx::DataView& view) -> std::string
		{
			if (!view.begin || !view.end || view.end <= view.begin)
			{
				return {};
			}
			return std::string(reinterpret_cast<const char*>(view.begin), reinterpret_cast<const char*>(view.end));
		}

		static auto ofbx_matrix_to_glm(const ofbx::Matrix& matrix) -> glm::mat4
		{
			glm::mat4 result(1.0f);
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					result[column][row] = static_cast<float>(matrix.m[column * 4 + row]);
				}
			}
			return result;
		}

		static auto make_material_texture_binding(std::string texture_path) -> MaterialTextureBinding
		{
			MaterialTextureBinding binding{};
			binding.texture_path = std::move(texture_path);
			return binding;
		}

		static auto append_mesh_instance(const Mesh& source, const glm::mat4& transform, std::string_view name_prefix, Mesh& destination) -> void
		{
			const uint32_t base_vertex = static_cast<uint32_t>(destination.vertices.size());
			const uint32_t base_index = static_cast<uint32_t>(destination.indices.size());
			const glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));

			destination.has_normals = destination.has_normals || source.has_normals;
			destination.has_tangents = destination.has_tangents || source.has_tangents;
			destination.has_uv0 = destination.has_uv0 || source.has_uv0;
			destination.has_uv1 = destination.has_uv1 || source.has_uv1;
			destination.has_vertex_colors = destination.has_vertex_colors || source.has_vertex_colors;

			for (const MeshVertex& source_vertex : source.vertices)
			{
				MeshVertex vertex = source_vertex;
				vertex.position = glm::vec3(transform * glm::vec4(source_vertex.position, 1.0f));
				if (source.has_normals)
				{
					glm::vec3 normal = normal_matrix * source_vertex.normal;
					vertex.normal = glm::length2(normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0.0f, 0.0f, 1.0f);
				}
				if (source.has_tangents)
				{
					const glm::vec3 tangent_xyz = normal_matrix * glm::vec3(source_vertex.tangent);
					vertex.tangent = glm::vec4(glm::length2(tangent_xyz) > 0.0f ? glm::normalize(tangent_xyz) : glm::vec3(1.0f, 0.0f, 0.0f), source_vertex.tangent.w);
				}
				destination.vertices.push_back(vertex);
			}

			for (uint32_t source_index : source.indices)
			{
				destination.indices.push_back(base_vertex + source_index);
			}

			for (const MeshSection& source_section : source.sections)
			{
				MeshSection section = source_section;
				section.vertex_offset = base_vertex + source_section.vertex_offset;
				section.index_offset = base_index + source_section.index_offset;
				if (!name_prefix.empty())
				{
					section.name = section.name.empty() ? std::string(name_prefix) : std::string(name_prefix) + "/" + section.name;
				}
				destination.sections.push_back(std::move(section));
			}
		}

		static auto merge_model_meshes(const Model& model) -> Mesh
		{
			Mesh merged{};
			merged.name = model.name.empty() ? "MergedMesh" : model.name;
			merged.source_path = model.source_path;

			if (model.nodes.empty())
			{
				for (const Mesh& mesh : model.meshes)
				{
					append_mesh_instance(mesh, glm::mat4(1.0f), mesh.name, merged);
				}
			}
			else
			{
				std::vector<glm::mat4> world_transforms(model.nodes.size(), glm::mat4(1.0f));
				std::vector<bool> computed(model.nodes.size(), false);
				std::function<glm::mat4(uint32_t)> compute_world_transform = [&](uint32_t index) -> glm::mat4
				{
					if (computed[index])
					{
						return world_transforms[index];
					}

					glm::mat4 transform = model.nodes[index].local_transform;
					if (model.nodes[index].parent_index >= 0 && static_cast<size_t>(model.nodes[index].parent_index) < model.nodes.size())
					{
						transform = compute_world_transform(static_cast<uint32_t>(model.nodes[index].parent_index)) * transform;
					}

					world_transforms[index] = transform;
					computed[index] = true;
					return transform;
				};

				for (uint32_t node_index = 0; node_index < model.nodes.size(); ++node_index)
				{
					const ModelNode& node = model.nodes[node_index];
					if (node.mesh_index < 0 || static_cast<size_t>(node.mesh_index) >= model.meshes.size())
					{
						continue;
					}

					append_mesh_instance(model.meshes[static_cast<size_t>(node.mesh_index)], compute_world_transform(node_index), node.name, merged);
				}
			}

			recalculate_mesh_bounds(merged);
			if (!merged.has_normals)
			{
				generate_missing_normals(merged);
			}
			return merged;
		}

		static auto ensure_default_mesh_section(Mesh& mesh) -> void
		{
			if (!mesh.sections.empty())
			{
				return;
			}

			MeshSection section{};
			section.name = mesh.name;
			section.vertex_offset = 0;
			section.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
			section.index_offset = 0;
			section.index_count = static_cast<uint32_t>(mesh.indices.size());
			section.material_slot = k_invalid_material_slot;
			section.topology = MeshPrimitiveTopology::Triangles;
			mesh.sections.push_back(std::move(section));
		}

		static auto import_obj_model(const std::filesystem::path& path, Model& out_model, std::string* out_error) -> bool
		{
			tinyobj::ObjReaderConfig config{};
			config.triangulate = true;
			config.mtl_search_path = path.parent_path().string();

			tinyobj::ObjReader reader{};
			if (!reader.ParseFromFile(path.string(), config))
			{
				const std::string message = reader.Error().empty() ? "Failed to parse OBJ file." : reader.Error();
				return make_error(out_error, message);
			}

			if (!reader.Warning().empty())
			{
				HLogWarning("OBJ import warning: {}", reader.Warning());
			}

			const tinyobj::attrib_t& attrib = reader.GetAttrib();
			const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
			const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();
			if (shapes.empty())
			{
				return make_error(out_error, "OBJ file does not contain any shapes.");
			}

			Model model{};
			model.name = path.stem().string();
			model.source_path = path;

			model.material_slots.reserve(materials.size());
			for (size_t material_index = 0; material_index < materials.size(); ++material_index)
			{
				const tinyobj::material_t& material = materials[material_index];
				MaterialSlot slot{};
				slot.name = material.name.empty() ? ("Material_" + std::to_string(material_index)) : material.name;
				slot.base_color_factor = glm::vec4(material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0f);
				slot.emissive_factor = glm::vec3(material.emission[0], material.emission[1], material.emission[2]);
				slot.metallic_factor = clamp01(material.metallic);
				slot.roughness_factor = material.roughness > 0.0f ? clamp01(material.roughness) : 1.0f;
				slot.base_color_texture = make_material_texture_binding(resolve_embedded_resource_path(path, material.diffuse_texname));
				slot.normal_texture = make_material_texture_binding(resolve_embedded_resource_path(path, material.normal_texname));
				slot.emissive_texture = make_material_texture_binding(resolve_embedded_resource_path(path, material.emissive_texname));
				model.material_slots.push_back(std::move(slot));
			}

			auto emit_vertex = [&](Mesh& mesh, const tinyobj::index_t& source_index) -> void
			{
				MeshVertex vertex{};

				if (source_index.vertex_index >= 0)
				{
					const size_t position_offset = static_cast<size_t>(source_index.vertex_index) * 3;
					if (position_offset + 2 < attrib.vertices.size())
					{
						vertex.position = glm::vec3(
							attrib.vertices[position_offset + 0],
							attrib.vertices[position_offset + 1],
							attrib.vertices[position_offset + 2]);
					}

					const size_t color_offset = static_cast<size_t>(source_index.vertex_index) * 3;
					if (color_offset + 2 < attrib.colors.size())
					{
						vertex.color = glm::vec4(
							attrib.colors[color_offset + 0],
							attrib.colors[color_offset + 1],
							attrib.colors[color_offset + 2],
							1.0f);
						mesh.has_vertex_colors = true;
					}
				}

				if (source_index.normal_index >= 0)
				{
					const size_t normal_offset = static_cast<size_t>(source_index.normal_index) * 3;
					if (normal_offset + 2 < attrib.normals.size())
					{
						vertex.normal = glm::vec3(
							attrib.normals[normal_offset + 0],
							attrib.normals[normal_offset + 1],
							attrib.normals[normal_offset + 2]);
						mesh.has_normals = true;
					}
				}

				if (source_index.texcoord_index >= 0)
				{
					const size_t texcoord_offset = static_cast<size_t>(source_index.texcoord_index) * 2;
					if (texcoord_offset + 1 < attrib.texcoords.size())
					{
						vertex.uv0 = glm::vec2(
							attrib.texcoords[texcoord_offset + 0],
							attrib.texcoords[texcoord_offset + 1]);
						mesh.has_uv0 = true;
					}
				}

				mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
				mesh.vertices.push_back(vertex);
			};

			for (size_t shape_index = 0; shape_index < shapes.size(); ++shape_index)
			{
				const tinyobj::shape_t& shape = shapes[shape_index];
				Mesh mesh{};
				mesh.name = shape.name.empty() ? (model.name + "_Mesh_" + std::to_string(shape_index)) : shape.name;
				mesh.source_path = path;

				size_t index_offset = 0;
				int current_material = std::numeric_limits<int>::min();
				MeshSection* current_section = nullptr;

				for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); ++face_index)
				{
					const int vertex_count = shape.mesh.num_face_vertices[face_index];
					if (vertex_count < 3)
					{
						index_offset += static_cast<size_t>(vertex_count);
						continue;
					}

					const int face_material = face_index < shape.mesh.material_ids.size() ? shape.mesh.material_ids[face_index] : -1;
					if (!current_section || current_material != face_material)
					{
						current_material = face_material;
						mesh.sections.push_back({});
						current_section = &mesh.sections.back();
						current_section->name = face_material >= 0 && static_cast<size_t>(face_material) < model.material_slots.size()
							? model.material_slots[static_cast<size_t>(face_material)].name
							: mesh.name;
						current_section->material_slot = face_material >= 0 ? static_cast<uint32_t>(face_material) : k_invalid_material_slot;
						current_section->vertex_offset = static_cast<uint32_t>(mesh.vertices.size());
						current_section->index_offset = static_cast<uint32_t>(mesh.indices.size());
						current_section->topology = MeshPrimitiveTopology::Triangles;
					}

					const tinyobj::index_t first = shape.mesh.indices[index_offset + 0];
					tinyobj::index_t previous = shape.mesh.indices[index_offset + 1];
					for (int vertex = 2; vertex < vertex_count; ++vertex)
					{
						const tinyobj::index_t current = shape.mesh.indices[index_offset + vertex];
						emit_vertex(mesh, first);
						emit_vertex(mesh, previous);
						emit_vertex(mesh, current);
						previous = current;
					}

					if (current_section)
					{
						current_section->vertex_count = static_cast<uint32_t>(mesh.vertices.size()) - current_section->vertex_offset;
						current_section->index_count = static_cast<uint32_t>(mesh.indices.size()) - current_section->index_offset;
					}

					index_offset += static_cast<size_t>(vertex_count);
				}

				if (!mesh.has_normals)
				{
					generate_missing_normals(mesh);
				}
				recalculate_mesh_bounds(mesh);
				ensure_default_mesh_section(mesh);
				if (!mesh.has_geometry())
				{
					continue;
				}

				const uint32_t mesh_index = static_cast<uint32_t>(model.meshes.size());
				model.meshes.push_back(std::move(mesh));

				ModelNode node{};
				node.name = model.meshes.back().name;
				node.mesh_index = static_cast<int32_t>(mesh_index);
				model.root_nodes.push_back(static_cast<uint32_t>(model.nodes.size()));
				model.nodes.push_back(std::move(node));
			}

			if (!model.is_valid())
			{
				return make_error(out_error, "OBJ file did not produce any mesh geometry.");
			}

			out_model = std::move(model);
			clear_error(out_error);
			return true;
		}

		static auto convert_gltf_component_to_double(const uint8_t* data, int component_type, bool normalized) -> double
		{
			switch (component_type)
			{
			case TINYGLTF_COMPONENT_TYPE_BYTE:
			{
				const int8_t value = *reinterpret_cast<const int8_t*>(data);
				return normalized ? std::max(-1.0, static_cast<double>(value) / 127.0) : static_cast<double>(value);
			}
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			{
				const uint8_t value = *reinterpret_cast<const uint8_t*>(data);
				return normalized ? static_cast<double>(value) / 255.0 : static_cast<double>(value);
			}
			case TINYGLTF_COMPONENT_TYPE_SHORT:
			{
				const int16_t value = *reinterpret_cast<const int16_t*>(data);
				return normalized ? std::max(-1.0, static_cast<double>(value) / 32767.0) : static_cast<double>(value);
			}
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			{
				const uint16_t value = *reinterpret_cast<const uint16_t*>(data);
				return normalized ? static_cast<double>(value) / 65535.0 : static_cast<double>(value);
			}
			case TINYGLTF_COMPONENT_TYPE_INT:
			{
				const int32_t value = *reinterpret_cast<const int32_t*>(data);
				return normalized ? std::max(-1.0, static_cast<double>(value) / 2147483647.0) : static_cast<double>(value);
			}
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			{
				const uint32_t value = *reinterpret_cast<const uint32_t*>(data);
				return normalized ? static_cast<double>(value) / 4294967295.0 : static_cast<double>(value);
			}
			case TINYGLTF_COMPONENT_TYPE_FLOAT:
				return static_cast<double>(*reinterpret_cast<const float*>(data));
			case TINYGLTF_COMPONENT_TYPE_DOUBLE:
				return *reinterpret_cast<const double*>(data);
			default:
				return 0.0;
			}
		}

		template <size_t N>
		static auto read_gltf_components(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t element_index, std::array<float, N>& out_components) -> bool
		{
			out_components.fill(0.0f);

			if (accessor.sparse.isSparse)
			{
				return false;
			}
			if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size())
			{
				return false;
			}
			if (element_index >= accessor.count)
			{
				return false;
			}

			const tinygltf::BufferView& buffer_view = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
			if (buffer_view.buffer < 0 || static_cast<size_t>(buffer_view.buffer) >= model.buffers.size())
			{
				return false;
			}

			const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(buffer_view.buffer)];
			const int component_count = tinygltf::GetNumComponentsInType(accessor.type);
			const int component_size = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
			if (component_count <= 0 || component_size <= 0)
			{
				return false;
			}

			int byte_stride = accessor.ByteStride(buffer_view);
			if (byte_stride <= 0)
			{
				byte_stride = component_count * component_size;
			}

			const size_t base_offset = buffer_view.byteOffset + accessor.byteOffset + (element_index * static_cast<size_t>(byte_stride));
			const size_t required_bytes = static_cast<size_t>(component_count) * static_cast<size_t>(component_size);
			if (base_offset + required_bytes > buffer.data.size())
			{
				return false;
			}

			const uint8_t* source = buffer.data.data() + base_offset;
			const size_t read_count = std::min(N, static_cast<size_t>(component_count));
			for (size_t component_index = 0; component_index < read_count; ++component_index)
			{
				const uint8_t* component_data = source + component_index * static_cast<size_t>(component_size);
				out_components[component_index] = static_cast<float>(convert_gltf_component_to_double(component_data, accessor.componentType, accessor.normalized));
			}

			return true;
		}

		static auto read_gltf_index(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t element_index, uint32_t& out_index) -> bool
		{
			out_index = 0;

			if (accessor.sparse.isSparse)
			{
				return false;
			}
			if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size())
			{
				return false;
			}
			if (element_index >= accessor.count)
			{
				return false;
			}

			const tinygltf::BufferView& buffer_view = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
			if (buffer_view.buffer < 0 || static_cast<size_t>(buffer_view.buffer) >= model.buffers.size())
			{
				return false;
			}

			const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(buffer_view.buffer)];
			const int component_size = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
			if (component_size <= 0)
			{
				return false;
			}

			int byte_stride = accessor.ByteStride(buffer_view);
			if (byte_stride <= 0)
			{
				byte_stride = component_size;
			}

			const size_t base_offset = buffer_view.byteOffset + accessor.byteOffset + (element_index * static_cast<size_t>(byte_stride));
			if (base_offset + static_cast<size_t>(component_size) > buffer.data.size())
			{
				return false;
			}

			const uint8_t* source = buffer.data.data() + base_offset;
			switch (accessor.componentType)
			{
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				out_index = static_cast<uint32_t>(*reinterpret_cast<const uint8_t*>(source));
				return true;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				out_index = static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(source));
				return true;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				out_index = *reinterpret_cast<const uint32_t*>(source);
				return true;
			default:
				return false;
			}
		}

		static auto get_gltf_texture_path(const tinygltf::Model& model, const std::filesystem::path& asset_path, int texture_index) -> std::string
		{
			if (texture_index < 0 || static_cast<size_t>(texture_index) >= model.textures.size())
			{
				return {};
			}

			const tinygltf::Texture& texture = model.textures[static_cast<size_t>(texture_index)];
			if (texture.source < 0 || static_cast<size_t>(texture.source) >= model.images.size())
			{
				return {};
			}

			const tinygltf::Image& image = model.images[static_cast<size_t>(texture.source)];
			if (!image.uri.empty())
			{
				return resolve_embedded_resource_path(asset_path, image.uri);
			}

			return image.name;
		}

		static auto try_map_gltf_wrap_mode(
			int value,
			RenderSamplerAddressMode& out_mode) -> bool
		{
			switch (value)
			{
			case TINYGLTF_TEXTURE_WRAP_REPEAT:
				out_mode = RenderSamplerAddressMode::Repeat;
				return true;
			case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
				out_mode = RenderSamplerAddressMode::ClampToEdge;
				return true;
			case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
				out_mode = RenderSamplerAddressMode::MirroredRepeat;
				return true;
			default:
				return false;
			}
		}

		static auto try_map_gltf_mag_filter(
			int value,
			RenderSamplerFilter& out_filter) -> bool
		{
			switch (value)
			{
			case TINYGLTF_TEXTURE_FILTER_NEAREST:
				out_filter = RenderSamplerFilter::Nearest;
				return true;
			case TINYGLTF_TEXTURE_FILTER_LINEAR:
				out_filter = RenderSamplerFilter::Linear;
				return true;
			default:
				return false;
			}
		}

		static auto try_map_gltf_min_filter(
			int value,
			RenderSamplerFilter& out_min_filter,
			RenderSamplerFilter& out_mip_filter) -> bool
		{
			switch (value)
			{
			case TINYGLTF_TEXTURE_FILTER_NEAREST:
			case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
				out_min_filter = RenderSamplerFilter::Nearest;
				out_mip_filter = RenderSamplerFilter::Nearest;
				return true;
			case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
				out_min_filter = RenderSamplerFilter::Linear;
				out_mip_filter = RenderSamplerFilter::Nearest;
				return true;
			case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
				out_min_filter = RenderSamplerFilter::Nearest;
				out_mip_filter = RenderSamplerFilter::Linear;
				return true;
			case TINYGLTF_TEXTURE_FILTER_LINEAR:
			case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
				out_min_filter = RenderSamplerFilter::Linear;
				out_mip_filter = RenderSamplerFilter::Linear;
				return true;
			default:
				return false;
			}
		}

		static auto build_generated_sampler_name(size_t sampler_index) -> std::string
		{
			return "Sampler" + std::to_string(sampler_index);
		}

		static auto ensure_material_slot_sampler_definition(
			MaterialSlot& material_slot,
			const RenderSamplerDesc& desc) -> std::string
		{
			for (const MaterialSamplerDefinition& definition : material_slot.sampler_definitions)
			{
				if (definition.desc == desc)
				{
					return definition.name;
				}
			}

			const std::string sampler_name = build_generated_sampler_name(material_slot.sampler_definitions.size());
			material_slot.sampler_definitions.push_back(MaterialSamplerDefinition{
				sampler_name,
				sampler_name,
				desc
			});
			return sampler_name;
		}

		static auto get_gltf_texture_binding(
			const tinygltf::Model& model,
			const std::filesystem::path& asset_path,
			int texture_index,
			MaterialSlot& material_slot) -> MaterialTextureBinding
		{
			MaterialTextureBinding binding{};
			binding.texture_path = get_gltf_texture_path(model, asset_path, texture_index);
			if (texture_index < 0 || static_cast<size_t>(texture_index) >= model.textures.size())
			{
				return binding;
			}

			const tinygltf::Texture& texture = model.textures[static_cast<size_t>(texture_index)];
			if (texture.sampler < 0 || static_cast<size_t>(texture.sampler) >= model.samplers.size())
			{
				return binding;
			}

			RenderSamplerDesc sampler_desc{};

			const tinygltf::Sampler& sampler = model.samplers[static_cast<size_t>(texture.sampler)];
			if (sampler.wrapS >= 0 && !try_map_gltf_wrap_mode(sampler.wrapS, sampler_desc.address_u))
			{
				HLogWarning(
					"glTF import warning: unsupported sampler wrapS {} for '{}' texture {}; using Repeat.",
					sampler.wrapS,
					asset_path.generic_string(),
					texture_index);
			}
			if (sampler.wrapT >= 0 && !try_map_gltf_wrap_mode(sampler.wrapT, sampler_desc.address_v))
			{
				HLogWarning(
					"glTF import warning: unsupported sampler wrapT {} for '{}' texture {}; using Repeat.",
					sampler.wrapT,
					asset_path.generic_string(),
					texture_index);
			}
			if (sampler.magFilter >= 0 && !try_map_gltf_mag_filter(sampler.magFilter, sampler_desc.mag_filter))
			{
				HLogWarning(
					"glTF import warning: unsupported sampler magFilter {} for '{}' texture {}; using Linear.",
					sampler.magFilter,
					asset_path.generic_string(),
					texture_index);
			}
			if (sampler.minFilter >= 0 &&
				!try_map_gltf_min_filter(
					sampler.minFilter,
					sampler_desc.min_filter,
					sampler_desc.mip_filter))
			{
				HLogWarning(
					"glTF import warning: unsupported sampler minFilter {} for '{}' texture {}; using Linear/Linear.",
					sampler.minFilter,
					asset_path.generic_string(),
					texture_index);
			}

			binding.sampler_name = ensure_material_slot_sampler_definition(material_slot, sampler_desc);
			return binding;
		}

		static auto convert_gltf_primitive_to_triangles(const std::vector<uint32_t>& source_indices, int primitive_mode, std::vector<uint32_t>& out_triangles) -> bool
		{
			out_triangles.clear();

			switch (primitive_mode)
			{
			case -1:
			case TINYGLTF_MODE_TRIANGLES:
			{
				if (source_indices.size() < 3)
				{
					return false;
				}
				const size_t triangle_count = source_indices.size() / 3;
				out_triangles.reserve(triangle_count * 3);
				for (size_t index = 0; index + 2 < source_indices.size(); index += 3)
				{
					out_triangles.push_back(source_indices[index + 0]);
					out_triangles.push_back(source_indices[index + 1]);
					out_triangles.push_back(source_indices[index + 2]);
				}
				return !out_triangles.empty();
			}
			case TINYGLTF_MODE_TRIANGLE_STRIP:
			{
				if (source_indices.size() < 3)
				{
					return false;
				}
				for (size_t index = 2; index < source_indices.size(); ++index)
				{
					const uint32_t a = source_indices[index - 2];
					const uint32_t b = source_indices[index - 1];
					const uint32_t c = source_indices[index];
					if (a == b || b == c || a == c)
					{
						continue;
					}

					if ((index & 1u) == 0u)
					{
						out_triangles.push_back(a);
						out_triangles.push_back(b);
						out_triangles.push_back(c);
					}
					else
					{
						out_triangles.push_back(b);
						out_triangles.push_back(a);
						out_triangles.push_back(c);
					}
				}
				return !out_triangles.empty();
			}
			case TINYGLTF_MODE_TRIANGLE_FAN:
			{
				if (source_indices.size() < 3)
				{
					return false;
				}
				for (size_t index = 2; index < source_indices.size(); ++index)
				{
					const uint32_t a = source_indices[0];
					const uint32_t b = source_indices[index - 1];
					const uint32_t c = source_indices[index];
					if (a == b || b == c || a == c)
					{
						continue;
					}

					out_triangles.push_back(a);
					out_triangles.push_back(b);
					out_triangles.push_back(c);
				}
				return !out_triangles.empty();
			}
			default:
				return false;
			}
		}

		static auto import_gltf_model(const std::filesystem::path& path, Model& out_model, std::string* out_error) -> bool
		{
			tinygltf::Model gltf_model{};
			tinygltf::TinyGLTF loader{};
			std::string warning{};
			std::string error{};

			const std::string extension = to_lower_copy(path.extension().string());
			const bool loaded = (extension == ".glb")
				? loader.LoadBinaryFromFile(&gltf_model, &error, &warning, path.string())
				: loader.LoadASCIIFromFile(&gltf_model, &error, &warning, path.string());
			if (!warning.empty())
			{
				HLogWarning("glTF import warning: {}", warning);
			}
			if (!loaded)
			{
				return make_error(out_error, error.empty() ? "Failed to parse glTF file." : error);
			}

			Model model{};
			model.name = path.stem().string();
			model.source_path = path;

			model.material_slots.reserve(gltf_model.materials.size());
			for (size_t material_index = 0; material_index < gltf_model.materials.size(); ++material_index)
			{
				const tinygltf::Material& material = gltf_model.materials[material_index];
				MaterialSlot slot{};
				slot.name = material.name.empty() ? ("Material_" + std::to_string(material_index)) : material.name;
				if (material.pbrMetallicRoughness.baseColorFactor.size() >= 4)
				{
					slot.base_color_factor = glm::vec4(
						static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]),
						static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]),
						static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]),
						static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]));
				}
				slot.metallic_factor = clamp01(static_cast<float>(material.pbrMetallicRoughness.metallicFactor));
				slot.roughness_factor = clamp01(static_cast<float>(material.pbrMetallicRoughness.roughnessFactor));
				if (material.emissiveFactor.size() >= 3)
				{
					slot.emissive_factor = glm::vec3(
						static_cast<float>(material.emissiveFactor[0]),
						static_cast<float>(material.emissiveFactor[1]),
						static_cast<float>(material.emissiveFactor[2]));
				}
				slot.base_color_texture = get_gltf_texture_binding(gltf_model, path, material.pbrMetallicRoughness.baseColorTexture.index, slot);
				slot.normal_texture = get_gltf_texture_binding(gltf_model, path, material.normalTexture.index, slot);
				slot.metallic_roughness_texture = get_gltf_texture_binding(gltf_model, path, material.pbrMetallicRoughness.metallicRoughnessTexture.index, slot);
				slot.emissive_texture = get_gltf_texture_binding(gltf_model, path, material.emissiveTexture.index, slot);
				model.material_slots.push_back(std::move(slot));
			}

			std::vector<int32_t> mesh_index_map(gltf_model.meshes.size(), -1);
			for (size_t mesh_source_index = 0; mesh_source_index < gltf_model.meshes.size(); ++mesh_source_index)
			{
				const tinygltf::Mesh& source_mesh = gltf_model.meshes[mesh_source_index];
				Mesh mesh{};
				mesh.name = source_mesh.name.empty() ? (model.name + "_Mesh_" + std::to_string(mesh_source_index)) : source_mesh.name;
				mesh.source_path = path;

				for (size_t primitive_index = 0; primitive_index < source_mesh.primitives.size(); ++primitive_index)
				{
					const tinygltf::Primitive& primitive = source_mesh.primitives[primitive_index];
					const auto position_it = primitive.attributes.find("POSITION");
					if (position_it == primitive.attributes.end() || position_it->second < 0 || static_cast<size_t>(position_it->second) >= gltf_model.accessors.size())
					{
						continue;
					}

					if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != TINYGLTF_MODE_TRIANGLE_STRIP && primitive.mode != TINYGLTF_MODE_TRIANGLE_FAN)
					{
						continue;
					}

					const tinygltf::Accessor& position_accessor = gltf_model.accessors[static_cast<size_t>(position_it->second)];
					if (position_accessor.count == 0)
					{
						continue;
					}

					auto resolve_optional_accessor = [&](std::string_view semantic) -> const tinygltf::Accessor*
					{
						const auto it = primitive.attributes.find(std::string(semantic));
						if (it == primitive.attributes.end() || it->second < 0 || static_cast<size_t>(it->second) >= gltf_model.accessors.size())
						{
							return nullptr;
						}
						const tinygltf::Accessor& accessor = gltf_model.accessors[static_cast<size_t>(it->second)];
						return accessor.sparse.isSparse ? nullptr : &accessor;
					};

					const tinygltf::Accessor* normal_accessor = resolve_optional_accessor("NORMAL");
					const tinygltf::Accessor* tangent_accessor = resolve_optional_accessor("TANGENT");
					const tinygltf::Accessor* uv0_accessor = resolve_optional_accessor("TEXCOORD_0");
					const tinygltf::Accessor* uv1_accessor = resolve_optional_accessor("TEXCOORD_1");
					const tinygltf::Accessor* color_accessor = resolve_optional_accessor("COLOR_0");

					std::vector<uint32_t> primitive_indices{};
					if (primitive.indices >= 0)
					{
						if (static_cast<size_t>(primitive.indices) >= gltf_model.accessors.size())
						{
							continue;
						}

						const tinygltf::Accessor& index_accessor = gltf_model.accessors[static_cast<size_t>(primitive.indices)];
						primitive_indices.resize(index_accessor.count);
						bool indices_ok = true;
						for (size_t index = 0; index < index_accessor.count; ++index)
						{
							if (!read_gltf_index(gltf_model, index_accessor, index, primitive_indices[index]))
							{
								indices_ok = false;
								break;
							}
						}
						if (!indices_ok)
						{
							continue;
						}
					}
					else
					{
						primitive_indices.resize(position_accessor.count);
						for (size_t index = 0; index < position_accessor.count; ++index)
						{
							primitive_indices[index] = static_cast<uint32_t>(index);
						}
					}

					std::vector<uint32_t> triangle_indices{};
					if (!convert_gltf_primitive_to_triangles(primitive_indices, primitive.mode, triangle_indices))
					{
						continue;
					}

					MeshSection section{};
					section.name = mesh.name + "_Prim_" + std::to_string(primitive_index);
					section.vertex_offset = static_cast<uint32_t>(mesh.vertices.size());
					section.index_offset = static_cast<uint32_t>(mesh.indices.size());
					section.material_slot = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : k_invalid_material_slot;
					section.topology = MeshPrimitiveTopology::Triangles;

					bool primitive_ok = true;
					for (uint32_t vertex_index : triangle_indices)
					{
						std::array<float, 4> position_components{};
						if (!read_gltf_components(gltf_model, position_accessor, vertex_index, position_components))
						{
							primitive_ok = false;
							break;
						}

						MeshVertex vertex{};
						vertex.position = glm::vec3(position_components[0], position_components[1], position_components[2]);

						if (normal_accessor)
						{
							std::array<float, 4> normal_components{};
							if (read_gltf_components(gltf_model, *normal_accessor, vertex_index, normal_components))
							{
								vertex.normal = glm::vec3(normal_components[0], normal_components[1], normal_components[2]);
								mesh.has_normals = true;
							}
						}

						if (tangent_accessor)
						{
							std::array<float, 4> tangent_components{};
							if (read_gltf_components(gltf_model, *tangent_accessor, vertex_index, tangent_components))
							{
								vertex.tangent = glm::vec4(tangent_components[0], tangent_components[1], tangent_components[2], tangent_components[3]);
								mesh.has_tangents = true;
							}
						}

						if (uv0_accessor)
						{
							std::array<float, 4> uv_components{};
							if (read_gltf_components(gltf_model, *uv0_accessor, vertex_index, uv_components))
							{
								vertex.uv0 = glm::vec2(uv_components[0], uv_components[1]);
								mesh.has_uv0 = true;
							}
						}

						if (uv1_accessor)
						{
							std::array<float, 4> uv_components{};
							if (read_gltf_components(gltf_model, *uv1_accessor, vertex_index, uv_components))
							{
								vertex.uv1 = glm::vec2(uv_components[0], uv_components[1]);
								mesh.has_uv1 = true;
							}
						}

						if (color_accessor)
						{
							std::array<float, 4> color_components{};
							if (read_gltf_components(gltf_model, *color_accessor, vertex_index, color_components))
							{
								vertex.color = glm::vec4(
									color_components[0],
									color_components[1],
									color_components[2],
									color_components[3] == 0.0f && tinygltf::GetNumComponentsInType(color_accessor->type) < 4 ? 1.0f : color_components[3]);
								mesh.has_vertex_colors = true;
							}
						}

						mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
						mesh.vertices.push_back(vertex);
					}

					if (!primitive_ok)
					{
						mesh.vertices.resize(section.vertex_offset);
						mesh.indices.resize(section.index_offset);
						continue;
					}

					section.vertex_count = static_cast<uint32_t>(mesh.vertices.size()) - section.vertex_offset;
					section.index_count = static_cast<uint32_t>(mesh.indices.size()) - section.index_offset;
					mesh.sections.push_back(std::move(section));
				}

				if (!mesh.has_geometry())
				{
					continue;
				}

				if (!mesh.has_normals)
				{
					generate_missing_normals(mesh);
				}
				recalculate_mesh_bounds(mesh);
				ensure_default_mesh_section(mesh);

				mesh_index_map[mesh_source_index] = static_cast<int32_t>(model.meshes.size());
				model.meshes.push_back(std::move(mesh));
			}

			if (model.meshes.empty())
			{
				return make_error(out_error, "glTF file did not contain any supported triangle geometry.");
			}

			std::vector<uint8_t> is_child(gltf_model.nodes.size(), 0);
			for (const tinygltf::Node& node : gltf_model.nodes)
			{
				for (int child : node.children)
				{
					if (child >= 0 && static_cast<size_t>(child) < is_child.size())
					{
						is_child[static_cast<size_t>(child)] = 1;
					}
				}
			}

			std::vector<int32_t> node_map(gltf_model.nodes.size(), -1);
			std::function<void(int32_t, int32_t)> append_node = [&](int32_t source_node_index, int32_t parent_index) -> void
			{
				if (source_node_index < 0 || static_cast<size_t>(source_node_index) >= gltf_model.nodes.size())
				{
					return;
				}
				if (node_map[static_cast<size_t>(source_node_index)] >= 0)
				{
					return;
				}

				const tinygltf::Node& source_node = gltf_model.nodes[static_cast<size_t>(source_node_index)];
				ModelNode node{};
				node.name = source_node.name.empty() ? ("Node_" + std::to_string(source_node_index)) : source_node.name;
				node.parent_index = parent_index;
				node.mesh_index = (source_node.mesh >= 0 && static_cast<size_t>(source_node.mesh) < mesh_index_map.size())
					? mesh_index_map[static_cast<size_t>(source_node.mesh)]
					: -1;

				if (source_node.matrix.size() == 16)
				{
					glm::mat4 matrix(1.0f);
					for (int column = 0; column < 4; ++column)
					{
						for (int row = 0; row < 4; ++row)
						{
							matrix[column][row] = static_cast<float>(source_node.matrix[static_cast<size_t>(column * 4 + row)]);
						}
					}
					node.local_transform = matrix;
				}
				else
				{
					glm::vec3 translation(0.0f);
					glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
					glm::vec3 scale(1.0f);
					if (source_node.translation.size() == 3)
					{
						translation = glm::vec3(
							static_cast<float>(source_node.translation[0]),
							static_cast<float>(source_node.translation[1]),
							static_cast<float>(source_node.translation[2]));
					}
					if (source_node.rotation.size() == 4)
					{
						rotation = glm::quat(
							static_cast<float>(source_node.rotation[3]),
							static_cast<float>(source_node.rotation[0]),
							static_cast<float>(source_node.rotation[1]),
							static_cast<float>(source_node.rotation[2]));
					}
					if (source_node.scale.size() == 3)
					{
						scale = glm::vec3(
							static_cast<float>(source_node.scale[0]),
							static_cast<float>(source_node.scale[1]),
							static_cast<float>(source_node.scale[2]));
					}
					node.local_transform = compose_trs(translation, rotation, scale);
				}

				const uint32_t node_index = static_cast<uint32_t>(model.nodes.size());
				node_map[static_cast<size_t>(source_node_index)] = static_cast<int32_t>(node_index);
				model.nodes.push_back(std::move(node));
				if (parent_index >= 0)
				{
					model.nodes[static_cast<size_t>(parent_index)].children.push_back(node_index);
				}
				else
				{
					model.root_nodes.push_back(node_index);
				}

				for (int child : source_node.children)
				{
					append_node(child, static_cast<int32_t>(node_index));
				}
			};

			if (!gltf_model.scenes.empty())
			{
				int scene_index = gltf_model.defaultScene >= 0 ? gltf_model.defaultScene : 0;
				if (scene_index < 0 || static_cast<size_t>(scene_index) >= gltf_model.scenes.size())
				{
					scene_index = 0;
				}

				for (int root_node : gltf_model.scenes[static_cast<size_t>(scene_index)].nodes)
				{
					append_node(root_node, -1);
				}
			}
			else
			{
				for (size_t node_index = 0; node_index < gltf_model.nodes.size(); ++node_index)
				{
					if (!is_child[node_index])
					{
						append_node(static_cast<int32_t>(node_index), -1);
					}
				}
			}

			for (size_t node_index = 0; node_index < gltf_model.nodes.size(); ++node_index)
			{
				if (node_map[node_index] < 0 && gltf_model.nodes[node_index].mesh >= 0)
				{
					append_node(static_cast<int32_t>(node_index), -1);
				}
			}

			if (model.nodes.empty())
			{
				for (uint32_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
				{
					ModelNode node{};
					node.name = model.meshes[mesh_index].name;
					node.mesh_index = static_cast<int32_t>(mesh_index);
					model.root_nodes.push_back(static_cast<uint32_t>(model.nodes.size()));
					model.nodes.push_back(std::move(node));
				}
			}

			out_model = std::move(model);
			clear_error(out_error);
			return true;
		}

		static auto resolve_ofbx_texture_path(const std::filesystem::path& asset_path, const ofbx::Texture* texture) -> std::string
		{
			if (!texture)
			{
				return {};
			}

			std::string relative_filename = data_view_to_string(texture->getRelativeFileName());
			if (!relative_filename.empty())
			{
				return resolve_embedded_resource_path(asset_path, relative_filename);
			}

			std::string absolute_filename = data_view_to_string(texture->getFileName());
			if (!absolute_filename.empty())
			{
				return resolve_embedded_resource_path(asset_path, absolute_filename);
			}

			return {};
		}

		static auto import_fbx_model(const std::filesystem::path& path, Model& out_model, std::string* out_error) -> bool
		{
			std::ifstream input(path, std::ios::binary);
			if (!input.is_open())
			{
				return make_error(out_error, "Failed to open FBX file.");
			}

			std::vector<ofbx::u8> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (bytes.empty())
			{
				return make_error(out_error, "FBX file is empty.");
			}

			std::unique_ptr<ofbx::IScene, void(*)(ofbx::IScene*)> scene(
				ofbx::load(bytes.data(), static_cast<int>(bytes.size()), static_cast<ofbx::u16>(ofbx::LoadFlags::TRIANGULATE)),
				[](ofbx::IScene* value)
				{
					if (value)
					{
						value->destroy();
					}
				});
			if (!scene)
			{
				return make_error(out_error, "Failed to parse FBX file.");
			}

			Model model{};
			model.name = path.stem().string();
			model.source_path = path;

			std::unordered_map<const ofbx::Material*, uint32_t> material_indices{};
			auto acquire_material_slot = [&](const ofbx::Material* source_material) -> uint32_t
			{
				if (!source_material)
				{
					return k_invalid_material_slot;
				}

				if (const auto found = material_indices.find(source_material); found != material_indices.end())
				{
					return found->second;
				}

				MaterialSlot slot{};
				slot.name = source_material->name[0] != '\0'
					? std::string(source_material->name)
					: ("Material_" + std::to_string(model.material_slots.size()));

				const ofbx::Color diffuse = source_material->getDiffuseColor();
				const ofbx::Color emissive = source_material->getEmissiveColor();
				const float diffuse_factor = clamp01(static_cast<float>(source_material->getDiffuseFactor()));
				const float emissive_factor = clamp01(static_cast<float>(source_material->getEmissiveFactor()));
				const float shininess = std::max(0.0f, static_cast<float>(source_material->getShininess()));

				slot.base_color_factor = glm::vec4(
					clamp01(diffuse.r * diffuse_factor),
					clamp01(diffuse.g * diffuse_factor),
					clamp01(diffuse.b * diffuse_factor),
					1.0f);
				slot.emissive_factor = glm::vec3(
					clamp01(emissive.r * emissive_factor),
					clamp01(emissive.g * emissive_factor),
					clamp01(emissive.b * emissive_factor));
				slot.metallic_factor = 0.0f;
				slot.roughness_factor = 1.0f - clamp01(shininess / 100.0f);
				slot.base_color_texture = make_material_texture_binding(resolve_ofbx_texture_path(path, source_material->getTexture(ofbx::Texture::DIFFUSE)));
				slot.normal_texture = make_material_texture_binding(resolve_ofbx_texture_path(path, source_material->getTexture(ofbx::Texture::NORMAL)));
				slot.emissive_texture = make_material_texture_binding(resolve_ofbx_texture_path(path, source_material->getTexture(ofbx::Texture::EMISSIVE)));
				slot.metallic_roughness_texture = make_material_texture_binding(resolve_ofbx_texture_path(path, source_material->getTexture(ofbx::Texture::SHININESS)));

				const uint32_t slot_index = static_cast<uint32_t>(model.material_slots.size());
				model.material_slots.push_back(std::move(slot));
				material_indices.emplace(source_material, slot_index);
				return slot_index;
			};

			std::unordered_map<const ofbx::Mesh*, int32_t> mesh_object_to_model_mesh{};
			for (int mesh_object_index = 0; mesh_object_index < scene->getMeshCount(); ++mesh_object_index)
			{
				const ofbx::Mesh* source_mesh = scene->getMesh(mesh_object_index);
				const ofbx::Geometry* geometry = source_mesh ? source_mesh->getGeometry() : nullptr;
				if (!source_mesh || !geometry || geometry->getVertexCount() <= 0 || geometry->getIndexCount() <= 0)
				{
					continue;
				}

				Mesh mesh{};
				mesh.name = source_mesh->name[0] != '\0'
					? std::string(source_mesh->name)
					: (model.name + "_Mesh_" + std::to_string(mesh_object_index));
				mesh.source_path = path;

				const ofbx::Vec3* positions = geometry->getVertices();
				const ofbx::Vec3* normals = geometry->getNormals();
				const ofbx::Vec2* uv0 = geometry->getUVs(0);
				const ofbx::Vec2* uv1 = geometry->getUVs(1);
				const ofbx::Vec4* colors = geometry->getColors();
				const ofbx::Vec3* tangents = geometry->getTangents();
				const int* indices = geometry->getFaceIndices();
				const int* materials = geometry->getMaterials();

				auto emit_vertex = [&](int source_index) -> void
				{
					if (source_index < 0 || source_index >= geometry->getVertexCount())
					{
						return;
					}

					MeshVertex vertex{};
					vertex.position = glm::vec3(
						static_cast<float>(positions[source_index].x),
						static_cast<float>(positions[source_index].y),
						static_cast<float>(positions[source_index].z));

					if (normals)
					{
						vertex.normal = glm::vec3(
							static_cast<float>(normals[source_index].x),
							static_cast<float>(normals[source_index].y),
							static_cast<float>(normals[source_index].z));
						mesh.has_normals = true;
					}

					if (uv0)
					{
						vertex.uv0 = glm::vec2(
							static_cast<float>(uv0[source_index].x),
							static_cast<float>(uv0[source_index].y));
						mesh.has_uv0 = true;
					}

					if (uv1)
					{
						vertex.uv1 = glm::vec2(
							static_cast<float>(uv1[source_index].x),
							static_cast<float>(uv1[source_index].y));
						mesh.has_uv1 = true;
					}

					if (colors)
					{
						vertex.color = glm::vec4(
							static_cast<float>(colors[source_index].x),
							static_cast<float>(colors[source_index].y),
							static_cast<float>(colors[source_index].z),
							static_cast<float>(colors[source_index].w));
						mesh.has_vertex_colors = true;
					}

					if (tangents)
					{
						vertex.tangent = glm::vec4(
							static_cast<float>(tangents[source_index].x),
							static_cast<float>(tangents[source_index].y),
							static_cast<float>(tangents[source_index].z),
							1.0f);
						mesh.has_tangents = true;
					}

					mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
					mesh.vertices.push_back(vertex);
				};

				std::vector<int> face_vertices{};
				int current_material = std::numeric_limits<int>::min();
				int face_index = 0;
				MeshSection* current_section = nullptr;

				for (int index = 0; index < geometry->getIndexCount(); ++index)
				{
					const int raw_index = indices[index];
					const int source_index = raw_index < 0 ? (-raw_index - 1) : raw_index;
					face_vertices.push_back(source_index);

					if (raw_index >= 0)
					{
						continue;
					}

					if (face_vertices.size() >= 3)
					{
						const int material_index = materials ? materials[face_index] : -1;
						const ofbx::Material* source_material =
							(material_index >= 0 && material_index < source_mesh->getMaterialCount())
							? source_mesh->getMaterial(material_index)
							: nullptr;
						const uint32_t slot_index = acquire_material_slot(source_material);

						if (!current_section || current_material != material_index)
						{
							current_material = material_index;
							mesh.sections.push_back({});
							current_section = &mesh.sections.back();
							current_section->name = source_material && source_material->name[0] != '\0'
								? std::string(source_material->name)
								: mesh.name;
							current_section->material_slot = slot_index;
							current_section->vertex_offset = static_cast<uint32_t>(mesh.vertices.size());
							current_section->index_offset = static_cast<uint32_t>(mesh.indices.size());
							current_section->topology = MeshPrimitiveTopology::Triangles;
						}

						const int first = face_vertices[0];
						int previous = face_vertices[1];
						for (size_t face_vertex_index = 2; face_vertex_index < face_vertices.size(); ++face_vertex_index)
						{
							const int current = face_vertices[face_vertex_index];
							emit_vertex(first);
							emit_vertex(previous);
							emit_vertex(current);
							previous = current;
						}

						if (current_section)
						{
							current_section->vertex_count = static_cast<uint32_t>(mesh.vertices.size()) - current_section->vertex_offset;
							current_section->index_count = static_cast<uint32_t>(mesh.indices.size()) - current_section->index_offset;
						}
					}

					face_vertices.clear();
					++face_index;
				}

				if (!mesh.has_geometry())
				{
					continue;
				}

				if (!mesh.has_normals)
				{
					generate_missing_normals(mesh);
				}
				recalculate_mesh_bounds(mesh);
				ensure_default_mesh_section(mesh);

				mesh_object_to_model_mesh.emplace(source_mesh, static_cast<int32_t>(model.meshes.size()));
				model.meshes.push_back(std::move(mesh));
			}

			if (model.meshes.empty())
			{
				return make_error(out_error, "FBX file did not contain any mesh geometry.");
			}

			std::unordered_set<const ofbx::Object*> relevant_objects{};
			for (const auto& [mesh_object, mesh_index] : mesh_object_to_model_mesh)
			{
				(void)mesh_index;
				const ofbx::Object* current = mesh_object;
				while (current && current->getType() != ofbx::Object::Type::ROOT)
				{
					if (current->isNode())
					{
						relevant_objects.insert(current);
					}
					current = current->getParent();
				}
			}

			std::unordered_map<const ofbx::Object*, uint32_t> node_map{};
			std::function<uint32_t(const ofbx::Object*)> ensure_node = [&](const ofbx::Object* object) -> uint32_t
			{
				if (const auto found = node_map.find(object); found != node_map.end())
				{
					return found->second;
				}

				int32_t parent_index = -1;
				const ofbx::Object* parent_object = object ? object->getParent() : nullptr;
				if (parent_object && parent_object->getType() != ofbx::Object::Type::ROOT && relevant_objects.find(parent_object) != relevant_objects.end())
				{
					parent_index = static_cast<int32_t>(ensure_node(parent_object));
				}

				ModelNode node{};
				node.name = (object && object->name[0] != '\0')
					? std::string(object->name)
					: ("Node_" + std::to_string(model.nodes.size()));
				node.parent_index = parent_index;
				node.local_transform = object ? ofbx_matrix_to_glm(object->getLocalTransform()) : glm::mat4(1.0f);

				if (object && object->getType() == ofbx::Object::Type::MESH)
				{
					const ofbx::Mesh* mesh_object = static_cast<const ofbx::Mesh*>(object);
					if (const auto found_mesh = mesh_object_to_model_mesh.find(mesh_object); found_mesh != mesh_object_to_model_mesh.end())
					{
						node.mesh_index = found_mesh->second;
					}
					node.local_transform *= ofbx_matrix_to_glm(mesh_object->getGeometricMatrix());
				}

				const uint32_t node_index = static_cast<uint32_t>(model.nodes.size());
				node_map.emplace(object, node_index);
				model.nodes.push_back(std::move(node));
				if (parent_index >= 0)
				{
					model.nodes[static_cast<size_t>(parent_index)].children.push_back(node_index);
				}
				else
				{
					model.root_nodes.push_back(node_index);
				}

				return node_index;
			};

			for (const auto& [mesh_object, mesh_index] : mesh_object_to_model_mesh)
			{
				(void)mesh_index;
				ensure_node(mesh_object);
			}

			out_model = std::move(model);
			clear_error(out_error);
			return true;
		}

		static auto rebuild_ashasset_hierarchy(AshAsset& asset) -> void
		{
			asset.root_nodes.clear();
			for (AshAssetNode& node : asset.nodes)
			{
				node.children.clear();
			}

			for (uint32_t node_index = 0; node_index < asset.nodes.size(); ++node_index)
			{
				AshAssetNode& node = asset.nodes[node_index];
				if (node.parent_index >= 0 && static_cast<size_t>(node.parent_index) < asset.nodes.size() && static_cast<uint32_t>(node.parent_index) != node_index)
				{
					asset.nodes[static_cast<size_t>(node.parent_index)].children.push_back(node_index);
				}
				else
				{
					node.parent_index = -1;
					asset.root_nodes.push_back(node_index);
				}
			}
		}

	}

	bool Mesh::has_geometry() const
	{
		if (vertices.empty())
		{
			return false;
		}

		if (indices.empty())
		{
			return vertices.size() >= 3;
		}

		return indices.size() >= 3;
	}

	bool Model::is_valid() const
	{
		if (meshes.empty() && nodes.empty())
		{
			return false;
		}

		if (!nodes.empty() && root_nodes.empty())
		{
			return false;
		}

		for (uint32_t root_node : root_nodes)
		{
			if (root_node >= nodes.size())
			{
				return false;
			}
		}

		for (const ModelMaterialReference& material_reference : default_materials)
		{
			if (material_reference.material_slot == k_invalid_material_slot
				|| material_reference.material_slot >= material_slots.size())
			{
				return false;
			}
		}

		return true;
	}

	bool AshAsset::is_valid() const
	{
		if (nodes.empty() || root_nodes.empty())
		{
			return false;
		}

		for (uint32_t root_node : root_nodes)
		{
			if (root_node >= nodes.size())
			{
				return false;
			}
		}

		return true;
	}

	bool load_mesh_from_file(const std::filesystem::path& path, Mesh& out_mesh, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		Model model{};
		ASH_PROCESS_ERROR(load_model_from_file(path, model, out_error));

		out_mesh = merge_model_meshes(model);
		out_mesh.source_path = path;
		clear_error(out_error);
		bResult = out_mesh.has_geometry();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool load_model_from_file(const std::filesystem::path& path, Model& out_model, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, false, false);
		const std::string extension = to_lower_copy(path.extension().string());
		if (extension == ".obj")
		{
			bResult = import_obj_model(path, out_model, out_error);
		}
		else if (extension == ".gltf" || extension == ".glb")
		{
			bResult = import_gltf_model(path, out_model, out_error);
		}
		else if (extension == ".fbx")
		{
			bResult = import_fbx_model(path, out_model, out_error);
		}
		else
		{
			bResult = make_error(out_error, "Unsupported model format.");
		}
		if (bResult)
		{
			ensure_model_default_material_references(out_model);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool load_ashasset_from_file(const std::filesystem::path& path, AshAsset& out_asset, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		std::ifstream input(path);
		if (!input.is_open())
		{
			bResult = make_error(out_error, "Failed to open ashasset file.");
			break;
		}

		json root{};
		try
		{
			input >> root;
		}
		catch (const std::exception& exception)
		{
			bResult = make_error(out_error, exception.what());
			break;
		}

		const uint32_t version = root.value("version", k_ashasset_file_version);
		if (version > k_ashasset_file_version)
		{
			bResult = make_error(out_error, "AshAsset file version is newer than this runtime supports.");
			break;
		}

		const json nodes_json = root.value("nodes", json::array());
		if (!nodes_json.is_array())
		{
			bResult = make_error(out_error, "AshAsset nodes entry is invalid.");
			break;
		}

		AshAsset asset{};
		asset.name = root.value("name", path.stem().string());
		asset.source_path = path;
		asset.nodes.reserve(nodes_json.size());

		for (const json& node_json : nodes_json)
		{
			if (!node_json.is_object())
			{
				continue;
			}

			AshAssetNode node{};
			node.name = node_json.value("name", std::string("Entity"));
			node.parent_index = node_json.value("parent", -1);

			if (node_json.contains("transform"))
			{
				const json& transform_json = node_json["transform"];
				node.transform.position = from_json_vec3(transform_json.value("position", json::array()), node.transform.position);
				node.transform.rotation_euler_degrees = from_json_vec3(transform_json.value("rotation_euler_degrees", json::array()), node.transform.rotation_euler_degrees);
				node.transform.scale = from_json_vec3(transform_json.value("scale", json::array({ 1.0f, 1.0f, 1.0f })), node.transform.scale);
			}

			if (node_json.contains("camera"))
			{
				const json& camera_json = node_json["camera"];
				CameraComponent camera{};
				camera.primary = camera_json.value("primary", camera.primary);
				camera.projection = static_cast<CameraProjectionType>(camera_json.value("projection", static_cast<int32_t>(camera.projection)));
				camera.fov_y_degrees = camera_json.value("fov_y_degrees", camera.fov_y_degrees);
				camera.near_plane = camera_json.value("near_plane", camera.near_plane);
				camera.far_plane = camera_json.value("far_plane", camera.far_plane);
				camera.orthographic_height = camera_json.value("orthographic_height", camera.orthographic_height);
				node.camera = camera;
			}

			if (node_json.contains("light"))
			{
				const json& light_json = node_json["light"];
				LightComponent light{};
				light.type = static_cast<LightType>(light_json.value("type", static_cast<int32_t>(light.type)));
				light.color = from_json_vec3(light_json.value("color", json::array()), light.color);
				light.intensity = light_json.value("intensity", light.intensity);
				light.range = light_json.value("range", light.range);
				light.inner_cone_angle_degrees = light_json.value("inner_cone_angle_degrees", light.inner_cone_angle_degrees);
				light.outer_cone_angle_degrees = light_json.value("outer_cone_angle_degrees", light.outer_cone_angle_degrees);
				node.light = light;
			}

			if (node_json.contains("mesh"))
			{
				const json& mesh_json = node_json["mesh"];
				MeshComponent mesh{};
				mesh.asset_path = mesh_json.value("asset_path", std::string{});
				mesh.mesh_index = mesh_json.value("mesh_index", 0u);
				mesh.material_overrides = deserialize_material_overrides(mesh_json.value("material_overrides", json::array()));
				mesh.visible = mesh_json.value("visible", true);
				mesh.mobility = static_cast<SceneMobility>(mesh_json.value("mobility", static_cast<uint32_t>(mesh.mobility)));
				mesh.layer_mask = mesh_json.value("layer_mask", mesh.layer_mask);
				node.mesh = mesh;
			}

			asset.nodes.push_back(std::move(node));
		}

		rebuild_ashasset_hierarchy(asset);
		if (!asset.is_valid())
		{
			bResult = make_error(out_error, "AshAsset file does not contain a valid node hierarchy.");
			break;
		}

		out_asset = std::move(asset);
		clear_error(out_error);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool save_ashasset_to_file(const AshAsset& asset, const std::filesystem::path& path, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!asset.is_valid())
		{
			bResult = make_error(out_error, "AshAsset is invalid.");
			break;
		}

		std::error_code create_error{};
		if (!path.parent_path().empty())
		{
			std::filesystem::create_directories(path.parent_path(), create_error);
			if (create_error)
			{
				bResult = make_error(out_error, create_error.message());
				break;
			}
		}

		json root{};
		root["version"] = k_ashasset_file_version;
		root["name"] = asset.name;
		root["nodes"] = json::array();

		for (const AshAssetNode& node : asset.nodes)
		{
			json node_json{};
			node_json["name"] = node.name;
			node_json["parent"] = node.parent_index;
			node_json["transform"] =
			{
				{ "position", to_json_vec3(node.transform.position) },
				{ "rotation_euler_degrees", to_json_vec3(node.transform.rotation_euler_degrees) },
				{ "scale", to_json_vec3(node.transform.scale) },
			};

			if (node.camera.has_value())
			{
				const CameraComponent& camera = node.camera.value();
				node_json["camera"] =
				{
					{ "primary", camera.primary },
					{ "projection", static_cast<int32_t>(camera.projection) },
					{ "fov_y_degrees", camera.fov_y_degrees },
					{ "near_plane", camera.near_plane },
					{ "far_plane", camera.far_plane },
					{ "orthographic_height", camera.orthographic_height },
				};
			}

			if (node.light.has_value())
			{
				const LightComponent& light = node.light.value();
				node_json["light"] =
				{
					{ "type", static_cast<int32_t>(light.type) },
					{ "color", to_json_vec3(light.color) },
					{ "intensity", light.intensity },
					{ "range", light.range },
					{ "inner_cone_angle_degrees", light.inner_cone_angle_degrees },
					{ "outer_cone_angle_degrees", light.outer_cone_angle_degrees },
				};
			}

			if (node.mesh.has_value())
			{
				const MeshComponent& mesh = node.mesh.value();
				node_json["mesh"] =
				{
					{ "asset_path", mesh.asset_path },
					{ "mesh_index", mesh.mesh_index },
					{ "visible", mesh.visible },
					{ "mobility", static_cast<uint32_t>(mesh.mobility) },
					{ "layer_mask", mesh.layer_mask },
				};
				if (!mesh.material_overrides.empty())
				{
					node_json["mesh"]["material_overrides"] = serialize_material_overrides(mesh.material_overrides);
				}
			}

			root["nodes"].push_back(std::move(node_json));
		}

		std::ofstream output(path);
		if (!output.is_open())
		{
			bResult = make_error(out_error, "Failed to open ashasset output file.");
			break;
		}

		try
		{
			output << root.dump(2);
		}
		catch (const std::exception& exception)
		{
			bResult = make_error(out_error, exception.what());
			break;
		}

		clear_error(out_error);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	AshAsset make_ashasset_from_model(const Model& model, std::filesystem::path source_asset_path)
	{
		AshAsset asset{};
		asset.name = model.name.empty() ? "AshAsset" : model.name;
		asset.source_path = source_asset_path.empty() ? model.source_path : std::move(source_asset_path);

		const std::string mesh_asset_path = asset.source_path.generic_string();

		if (!model.nodes.empty())
		{
			asset.nodes.resize(model.nodes.size());
			for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index)
			{
				const ModelNode& source_node = model.nodes[node_index];
				AshAssetNode& target_node = asset.nodes[node_index];
				target_node.name = source_node.name;
				target_node.parent_index = source_node.parent_index;
				target_node.children = source_node.children;
				target_node.transform = matrix_to_transform_component(source_node.local_transform);
				if (source_node.mesh_index >= 0)
				{
					MeshComponent mesh{};
					mesh.asset_path = mesh_asset_path;
					mesh.mesh_index = static_cast<uint32_t>(source_node.mesh_index);
					mesh.visible = true;
					target_node.mesh = mesh;
				}
			}
			asset.root_nodes = model.root_nodes;
			return asset;
		}

		if (model.meshes.empty())
		{
			return asset;
		}

		if (model.meshes.size() == 1)
		{
			AshAssetNode node{};
			node.name = model.meshes[0].name.empty() ? asset.name : model.meshes[0].name;
			MeshComponent mesh{};
			mesh.asset_path = mesh_asset_path;
			mesh.mesh_index = 0;
			mesh.visible = true;
			node.mesh = mesh;
			asset.root_nodes.push_back(0);
			asset.nodes.push_back(std::move(node));
			return asset;
		}

		AshAssetNode root{};
		root.name = asset.name;
		asset.nodes.push_back(std::move(root));
		asset.root_nodes.push_back(0);

		for (uint32_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
		{
			AshAssetNode child{};
			child.name = model.meshes[mesh_index].name.empty()
				? ("Mesh_" + std::to_string(mesh_index))
				: model.meshes[mesh_index].name;
			child.parent_index = 0;

			MeshComponent mesh{};
			mesh.asset_path = mesh_asset_path;
			mesh.mesh_index = mesh_index;
			mesh.visible = true;
			child.mesh = mesh;

			const uint32_t child_index = static_cast<uint32_t>(asset.nodes.size());
			asset.nodes.push_back(std::move(child));
			asset.nodes[0].children.push_back(child_index);
		}

		return asset;
	}

	const Mesh* get_model_mesh_by_index(const Model& model, uint32_t mesh_index)
	{
		return mesh_index < model.meshes.size() ? &model.meshes[mesh_index] : nullptr;
	}

	bool try_get_model_mesh_bounds(const Model& model, uint32_t mesh_index, glm::vec3& out_bounds_min, glm::vec3& out_bounds_max)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const Mesh* mesh = get_model_mesh_by_index(model, mesh_index);
		ASH_PROCESS_ERROR(mesh);

		out_bounds_min = mesh->bounds_min;
		out_bounds_max = mesh->bounds_max;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	const MaterialSlot* get_model_material_slot_by_index(const Model& model, uint32_t material_slot)
	{
		return material_slot < model.material_slots.size() ? &model.material_slots[material_slot] : nullptr;
	}
}
