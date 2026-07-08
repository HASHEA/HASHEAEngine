#include "doctest.h"
#include "Base/hfile.h"
#include "Base/hmemory.h"

#include <cstring>
#include <filesystem>

namespace
{
	auto tests_temp_dir() -> std::filesystem::path
	{
		const std::filesystem::path dir = "Intermediate/test-temp/tests";
		std::filesystem::create_directories(dir);
		return dir;
	}
}

TEST_CASE("file_delete returns true on successful delete")
{
	const std::filesystem::path file = tests_temp_dir() / "file_delete_test.tmp";
	AshEngine::file_write_binary(file.string().c_str(), const_cast<char*>("x"), 1);

	CHECK(AshEngine::file_delete(file.string().c_str()));
	CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("file_read_text and file_extension_from_path handle plain inputs")
{
	const std::filesystem::path file = tests_temp_dir() / "file_read_text_test.txt";
	const char text[] = "AshEngineText";
	AshEngine::file_write_binary(file.string().c_str(), const_cast<char*>(text), sizeof(text) - 1);

	size_t text_size = 0;
	char* text_data = AshEngine::file_read_text(file.string().c_str(), text_size, nullptr);
	REQUIRE(text_data != nullptr);
	CHECK(text_size == sizeof(text) - 1);
	CHECK(std::memcmp(text_data, text, sizeof(text) - 1) == 0);
	AshEngine::MemoryService::instance()->get_system_allocator()->deallocate(text_data);

	char no_extension_path[] = "Intermediate/test-temp/tests/no_extension";
	CHECK(AshEngine::file_extension_from_path(no_extension_path) == nullptr);
}
