#pragma once
#include "Pipeline.h"

namespace RHI
{

/// Interleaved vertex matching `sandbox/Shaders/SceneStaticMesh.hlsl` `VSInput` (locations 0..5).
inline VertexInputCreation make_vertex_input_scene_static_mesh_interleaved()
{
	VertexInputCreation v{};
	v.add_vertex_stream({ 0, 72, AshVertexInputRate::PerVertex });
	v.add_vertex_attribute({ 0, 0, 0, AshVertexComponentFormat::Float3, AshVertexSemantic::Position });
	v.add_vertex_attribute({ 1, 0, 12, AshVertexComponentFormat::Float3, AshVertexSemantic::Normal });
	v.add_vertex_attribute({ 2, 0, 24, AshVertexComponentFormat::Float4, AshVertexSemantic::Tangent });
	v.add_vertex_attribute({ 3, 0, 40, AshVertexComponentFormat::Float2, AshVertexSemantic::TexCoord0 });
	v.add_vertex_attribute({ 4, 0, 48, AshVertexComponentFormat::Float2, AshVertexSemantic::TexCoord1 });
	v.add_vertex_attribute({ 5, 0, 56, AshVertexComponentFormat::Float4, AshVertexSemantic::Color0 });
	return v;
}

} // namespace RHI
