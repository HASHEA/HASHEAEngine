#include "Function/Render/Renderer.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace
{
	using AshEngine::GraphicsIndirectKind;
	using AshEngine::GraphicsIndirectValidationError;
	using AshEngine::GraphicsIndirectValidationFacts;
	using AshEngine::GraphicsIndirectValidationResult;

	GraphicsIndirectValidationFacts make_indirect_facts(GraphicsIndirectKind kind)
	{
		GraphicsIndirectValidationFacts facts{};
		facts.kind = kind;
		facts.args_resource_present = true;
		facts.args_buffer_size = 256u;
		facts.args_buffer_indirect_usage = true;
		facts.index_buffer_present = kind == GraphicsIndirectKind::Indexed;
		facts.args_offset = 0u;
		facts.draw_count = 1u;
		facts.stride = 0u;
		facts.instance_count = 1u;
		return facts;
	}

	enum class RecordedIndirectOperation : uint8_t
	{
		BindIndex,
		DrawNonIndexed,
		DrawIndexed
	};

	struct IndirectOperationRecorder
	{
		std::vector<RecordedIndirectOperation> operations{};

		static bool bind_index(
			void* user_data,
			const std::shared_ptr<AshEngine::IndexBuffer>&,
			uint64_t)
		{
			static_cast<IndirectOperationRecorder*>(user_data)->operations.push_back(
				RecordedIndirectOperation::BindIndex);
			return true;
		}

		static bool draw_non_indexed(
			void* user_data,
			const std::shared_ptr<AshEngine::StorageBuffer>&,
			const GraphicsIndirectValidationResult&)
		{
			static_cast<IndirectOperationRecorder*>(user_data)->operations.push_back(
				RecordedIndirectOperation::DrawNonIndexed);
			return true;
		}

		static bool draw_indexed(
			void* user_data,
			const std::shared_ptr<AshEngine::IndexBuffer>&,
			const std::shared_ptr<AshEngine::StorageBuffer>&,
			const GraphicsIndirectValidationResult&)
		{
			static_cast<IndirectOperationRecorder*>(user_data)->operations.push_back(
				RecordedIndirectOperation::DrawIndexed);
			return true;
		}

		AshEngine::GraphicsIndirectDrawOperations make_operations()
		{
			AshEngine::GraphicsIndirectDrawOperations result{};
			result.user_data = this;
			result.bind_index_buffer = &IndirectOperationRecorder::bind_index;
			result.draw_non_indexed = &IndirectOperationRecorder::draw_non_indexed;
			result.draw_indexed = &IndirectOperationRecorder::draw_indexed;
			return result;
		}
	};
}

TEST_CASE("Renderer indirect None and native stride kinds resolve explicitly")
{
	SUBCASE("None accepts direct draw facts only when args are absent")
	{
		GraphicsIndirectValidationFacts facts{};
		facts.kind = GraphicsIndirectKind::None;
		facts.instance_count = 1u;
		const GraphicsIndirectValidationResult direct = AshEngine::validate_graphics_indirect(facts);
		CHECK(direct.valid);
		CHECK(direct.kind == GraphicsIndirectKind::None);

		facts.args_resource_present = true;
		const GraphicsIndirectValidationResult conflict = AshEngine::validate_graphics_indirect(facts);
		CHECK_FALSE(conflict.valid);
		CHECK(conflict.error == GraphicsIndirectValidationError::UnexpectedArgsBuffer);
	}

	SUBCASE("NonIndexed stride zero resolves to the native command size")
	{
		GraphicsIndirectValidationFacts facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
		facts.args_offset = 16u;
		facts.draw_count = 3u;
		const GraphicsIndirectValidationResult result = AshEngine::validate_graphics_indirect(facts);
		REQUIRE(result.valid);
		CHECK(result.stride == sizeof(RHI::AshDrawIndirectArgs));
		CHECK(result.args_range_end == 64u);
	}

	SUBCASE("Indexed stride zero resolves to the indexed native command size")
	{
		GraphicsIndirectValidationFacts facts = make_indirect_facts(GraphicsIndirectKind::Indexed);
		facts.args_offset = 20u;
		facts.draw_count = 2u;
		const GraphicsIndirectValidationResult result = AshEngine::validate_graphics_indirect(facts);
		REQUIRE(result.valid);
		CHECK(result.stride == sizeof(RHI::AshDrawIndexedIndirectArgs));
		CHECK(result.args_range_end == 60u);
	}
}

TEST_CASE("Renderer indirect validation rejects missing resource usage and index facts")
{
	GraphicsIndirectValidationFacts facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);

	facts.args_resource_present = false;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::MissingArgsBuffer);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.args_buffer_indirect_usage = false;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::MissingIndirectUsage);

	facts = make_indirect_facts(GraphicsIndirectKind::Indexed);
	facts.index_buffer_present = false;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::MissingIndexBuffer);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.index_buffer_present = true;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::UnexpectedIndexBuffer);
}

TEST_CASE("Renderer indirect validation enforces count stride alignment and full checked range")
{
	GraphicsIndirectValidationFacts facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);

	facts.draw_count = 0u;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::InvalidDrawCount);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.stride = sizeof(RHI::AshDrawIndirectArgs) + 4u;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::InvalidStride);

	facts = make_indirect_facts(GraphicsIndirectKind::Indexed);
	facts.stride = sizeof(RHI::AshDrawIndexedIndirectArgs) + 4u;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::InvalidStride);

	facts = make_indirect_facts(GraphicsIndirectKind::Indexed);
	facts.stride = sizeof(RHI::AshDrawIndexedIndirectArgs);
	CHECK(AshEngine::validate_graphics_indirect(facts).valid);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.args_offset = 2u;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::MisalignedOffset);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.args_buffer_size = 47u;
	facts.args_offset = 16u;
	facts.draw_count = 2u;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::RangeOutOfBounds);

	facts.args_buffer_size = 48u;
	const GraphicsIndirectValidationResult exact = AshEngine::validate_graphics_indirect(facts);
	REQUIRE(exact.valid);
	CHECK(exact.args_range_end == 48u);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.args_buffer_size = std::numeric_limits<uint64_t>::max();
	facts.args_offset = std::numeric_limits<uint64_t>::max() - 15u;
	facts.draw_count = 2u;
	CHECK(AshEngine::validate_graphics_indirect(facts).error == GraphicsIndirectValidationError::RangeOverflow);
}

TEST_CASE("Renderer indirect validation rejects every non-neutral direct scalar")
{
	auto expect_conflict = [](const GraphicsIndirectValidationFacts& facts)
	{
		const GraphicsIndirectValidationResult result = AshEngine::validate_graphics_indirect(facts);
		CHECK_FALSE(result.valid);
		CHECK(result.error == GraphicsIndirectValidationError::ConflictingDirectArguments);
	};

	GraphicsIndirectValidationFacts facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.vertex_count = 1u;
	expect_conflict(facts);
	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.index_count = 1u;
	expect_conflict(facts);
	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.first_vertex = 1u;
	expect_conflict(facts);
	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.first_index = 1u;
	expect_conflict(facts);
	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.first_instance = 1u;
	expect_conflict(facts);
	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	facts.vertex_offset = 1;
	expect_conflict(facts);

	facts = make_indirect_facts(GraphicsIndirectKind::NonIndexed);
	CHECK(AshEngine::validate_graphics_indirect(facts).valid);
	facts.instance_count = 2u;
	expect_conflict(facts);
}

TEST_CASE("Renderer indirect indexed routing binds index before indexed command only")
{
	AshEngine::GraphicsDrawDesc desc{};
	desc.indirect_kind = GraphicsIndirectKind::Indexed;
	desc.index_buffer = std::make_shared<AshEngine::IndexBuffer>();
	desc.indirect_args_buffer = std::make_shared<AshEngine::StorageBuffer>();
	const GraphicsIndirectValidationResult result = AshEngine::validate_graphics_indirect(
		make_indirect_facts(GraphicsIndirectKind::Indexed));
	REQUIRE(result.valid);

	IndirectOperationRecorder recorder{};
	REQUIRE(AshEngine::route_graphics_indirect_draw(desc, result, recorder.make_operations()));
	REQUIRE(recorder.operations.size() == 2u);
	CHECK(recorder.operations[0] == RecordedIndirectOperation::BindIndex);
	CHECK(recorder.operations[1] == RecordedIndirectOperation::DrawIndexed);
	CHECK(std::find(
		recorder.operations.begin(),
		recorder.operations.end(),
		RecordedIndirectOperation::DrawNonIndexed) == recorder.operations.end());
}

TEST_CASE("Renderer indirect non-indexed routing never binds an index buffer")
{
	AshEngine::GraphicsDrawDesc desc{};
	desc.indirect_kind = GraphicsIndirectKind::NonIndexed;
	desc.indirect_args_buffer = std::make_shared<AshEngine::StorageBuffer>();
	const GraphicsIndirectValidationResult result = AshEngine::validate_graphics_indirect(
		make_indirect_facts(GraphicsIndirectKind::NonIndexed));
	REQUIRE(result.valid);

	IndirectOperationRecorder recorder{};
	REQUIRE(AshEngine::route_graphics_indirect_draw(desc, result, recorder.make_operations()));
	REQUIRE(recorder.operations.size() == 1u);
	CHECK(recorder.operations[0] == RecordedIndirectOperation::DrawNonIndexed);
}
