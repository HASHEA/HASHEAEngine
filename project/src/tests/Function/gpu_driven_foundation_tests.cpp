#include "Function/Render/RenderDevice.h"
#include "Function/Render/GPUDriven/GpuDrivenInstancePageStorage.h"
#include "Function/Render/GPUDriven/GpuDrivenPageAllocator.h"
#include "Function/Render/GPUDriven/GpuDrivenTypes.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <cstdint>
#include <limits>
#include <memory>

namespace
{
	AshEngine::GpuDrivenInstancePageDesc make_page_desc(
		AshEngine::GpuDrivenTransformEncoding encoding =
			AshEngine::GpuDrivenTransformEncoding::CompressedTRS)
	{
		AshEngine::GpuDrivenInstancePageDesc desc{};
		desc.version = AshEngine::kGpuDrivenInstancePageVersion;
		desc.encoding = encoding;
		desc.instance_stride = AshEngine::gpu_driven_instance_stride(encoding);
		desc.capacity = 64u;
		desc.count = 12u;
		return desc;
	}

	struct StorageFactoryRecorder
	{
		uint32_t call_count = 0u;
		AshEngine::StorageBufferDesc desc{};
		bool return_buffer = true;

		static std::shared_ptr<AshEngine::StorageBuffer> create(
			void* user_data,
			const AshEngine::StorageBufferDesc& desc)
		{
			StorageFactoryRecorder& recorder = *static_cast<StorageFactoryRecorder*>(user_data);
			++recorder.call_count;
			recorder.desc = desc;
			return recorder.return_buffer
				? std::make_shared<AshEngine::StorageBuffer>()
				: nullptr;
		}

		AshEngine::GpuDrivenStorageBufferFactory factory()
		{
			AshEngine::GpuDrivenStorageBufferFactory result{};
			result.user_data = this;
			result.create = &StorageFactoryRecorder::create;
			return result;
		}
	};
}

TEST_CASE("GPU-driven foundation prototype and page handles reserve invalid values")
{
	const AshEngine::GpuDrivenPrototypeId invalid_prototype{};
	CHECK_FALSE(invalid_prototype.is_valid());
	CHECK_FALSE(AshEngine::GpuDrivenPrototypeId{ 0u }.is_valid());
	CHECK(AshEngine::GpuDrivenPrototypeId{ 7u }.is_valid());

	const AshEngine::GpuDrivenPageHandle invalid_handle{};
	CHECK_FALSE(invalid_handle.is_valid());
	const AshEngine::GpuDrivenPageHandle first{ 3u, 9u };
	const AshEngine::GpuDrivenPageHandle same{ 3u, 9u };
	const AshEngine::GpuDrivenPageHandle newer{ 3u, 10u };
	CHECK(first.is_valid());
	CHECK(first == same);
	CHECK(first != newer);
}

TEST_CASE("GPU-driven foundation page retirement waits for canonical frame completion")
{
	AshEngine::GpuDrivenPageAllocator allocator{ 1u };
	const AshEngine::GpuDrivenPageHandle first = allocator.allocate();
	REQUIRE(first.is_valid());
	CHECK(first.slot == 0u);
	CHECK(first.generation == 1u);

	REQUIRE(allocator.retire(first, 12u));
	CHECK_FALSE(allocator.allocate().is_valid());
	CHECK(allocator.collect_completed(11u) == 0u);
	CHECK_FALSE(allocator.allocate().is_valid());
	CHECK(allocator.collect_completed(12u) == 1u);

	const AshEngine::GpuDrivenPageHandle reused = allocator.allocate();
	REQUIRE(reused.is_valid());
	CHECK(reused.slot == first.slot);
	CHECK(reused.generation == first.generation + 1u);
	CHECK_FALSE(allocator.retire(first, 13u));
}

TEST_CASE("GPU-driven foundation page allocator rejects stale double and foreign handles")
{
	AshEngine::GpuDrivenPageAllocator allocator{ 1u };
	const AshEngine::GpuDrivenPageHandle handle = allocator.allocate();
	REQUIRE(handle.is_valid());
	REQUIRE(allocator.retire(handle, 4u));
	CHECK_FALSE(allocator.retire(handle, 4u));
	CHECK_FALSE(allocator.retire(AshEngine::GpuDrivenPageHandle{ 1u, 1u }, 4u));

	AshEngine::GpuDrivenPageAllocator foreign_allocator{ 1u, 7u };
	const AshEngine::GpuDrivenPageHandle foreign = foreign_allocator.allocate();
	REQUIRE(foreign.is_valid());
	CHECK_FALSE(allocator.retire(foreign, 4u));

	REQUIRE(allocator.collect_completed(4u) == 1u);
	CHECK_FALSE(allocator.retire(handle, 5u));
}

TEST_CASE("GPU-driven foundation page allocator seals generation wrap and reports exhaustion")
{
	AshEngine::GpuDrivenPageAllocator wrapping{
		1u,
		std::numeric_limits<uint32_t>::max() };
	const AshEngine::GpuDrivenPageHandle last = wrapping.allocate();
	REQUIRE(last.is_valid());
	REQUIRE(wrapping.retire(last, 0u));
	CHECK(wrapping.collect_completed(0u) == 0u);
	CHECK(wrapping.sealed_count() == 1u);
	CHECK_FALSE(wrapping.allocate().is_valid());

	AshEngine::GpuDrivenPageAllocator finite{ 2u };
	CHECK(finite.allocate().is_valid());
	CHECK(finite.allocate().is_valid());
	CHECK_FALSE(finite.allocate().is_valid());
}

TEST_CASE("GPU-driven foundation page desc validates version encoding stride count and byte size")
{
	const AshEngine::GpuDrivenInstancePageDesc valid = make_page_desc();
	const AshEngine::GpuDrivenInstancePageValidationResult valid_result =
		AshEngine::validate_gpu_driven_instance_page_desc(valid);
	REQUIRE(valid_result.valid);
	CHECK(valid_result.payload_byte_size == valid.instance_stride * valid.capacity);

	auto invalid = valid;
	invalid.version = 0u;
	CHECK_FALSE(AshEngine::validate_gpu_driven_instance_page_desc(invalid).valid);
	invalid = valid;
	invalid.encoding = static_cast<AshEngine::GpuDrivenTransformEncoding>(255u);
	CHECK_FALSE(AshEngine::validate_gpu_driven_instance_page_desc(invalid).valid);
	invalid = valid;
	invalid.instance_stride = 0u;
	CHECK_FALSE(AshEngine::validate_gpu_driven_instance_page_desc(invalid).valid);
	invalid = valid;
	invalid.capacity = 0u;
	CHECK_FALSE(AshEngine::validate_gpu_driven_instance_page_desc(invalid).valid);
	invalid = valid;
	invalid.count = invalid.capacity + 1u;
	CHECK_FALSE(AshEngine::validate_gpu_driven_instance_page_desc(invalid).valid);
	invalid = valid;
	invalid.capacity = std::numeric_limits<uint32_t>::max();
	CHECK_FALSE(AshEngine::validate_gpu_driven_instance_page_desc(invalid).valid);
}

TEST_CASE("GPU-driven foundation transform encodings share the versioned stride contract")
{
	const AshEngine::GpuDrivenInstancePageDesc compressed = make_page_desc(
		AshEngine::GpuDrivenTransformEncoding::CompressedTRS);
	const AshEngine::GpuDrivenInstancePageDesc affine = make_page_desc(
		AshEngine::GpuDrivenTransformEncoding::Affine3x4F32);

	CHECK(compressed.encoding != affine.encoding);
	CHECK(compressed.version == affine.version);
	CHECK(compressed.instance_stride != affine.instance_stride);
	CHECK(compressed.instance_stride ==
		AshEngine::gpu_driven_instance_stride(compressed.encoding));
	CHECK(affine.instance_stride == 48u);
	CHECK(AshEngine::validate_gpu_driven_instance_page_desc(compressed).valid);
	CHECK(AshEngine::validate_gpu_driven_instance_page_desc(affine).valid);
}

TEST_CASE("GPU-driven foundation page storage creates checked shader storage payloads")
{
	StorageFactoryRecorder recorder{};
	AshEngine::GpuDrivenInstancePageStorage storage{};
	const AshEngine::GpuDrivenInstancePageDesc desc = make_page_desc();
	const AshEngine::GpuDrivenPageHandle handle{ 2u, 5u };

	REQUIRE(AshEngine::GpuDrivenInstancePageStorage::create(
		desc,
		handle,
		recorder.factory(),
		storage,
		"GpuDrivenPagePayload"));
	CHECK(recorder.call_count == 1u);
	CHECK(recorder.desc.size == desc.instance_stride * desc.capacity);
	CHECK(recorder.desc.stride == desc.instance_stride);
	CHECK_FALSE(recorder.desc.cpu_write);
	CHECK_FALSE(recorder.desc.indirect_args);
	CHECK(recorder.desc.initial_data == nullptr);
	CHECK(storage.is_valid());
	CHECK(storage.handle() == handle);
	CHECK(storage.desc().count == desc.count);
	CHECK(storage.buffer() != nullptr);

	AshEngine::GpuDrivenInstancePageDesc invalid = desc;
	invalid.capacity = 0u;
	CHECK_FALSE(AshEngine::GpuDrivenInstancePageStorage::create(
		invalid,
		handle,
		recorder.factory(),
		storage));
	CHECK(recorder.call_count == 1u);

	recorder.return_buffer = false;
	CHECK_FALSE(AshEngine::GpuDrivenInstancePageStorage::create(
		desc,
		handle,
		recorder.factory(),
		storage));
	CHECK(recorder.call_count == 2u);
}
