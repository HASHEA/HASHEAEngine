#include "doctest.h"
#include "Base/hfile.h"
#include "Base/hmemory.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>

namespace
{
	static_assert(std::is_standard_layout_v<AshEngine::Directory>);
#if defined(_WIN64)
	static_assert(sizeof(AshEngine::Directory) == AshEngine::k_max_path + sizeof(void*));
#endif

	auto tests_temp_dir() -> std::filesystem::path
	{
		const std::filesystem::path dir = "Intermediate/test-temp/tests";
		std::filesystem::create_directories(dir);
		return dir;
	}

	struct TemporaryDirectoryTree
	{
		explicit TemporaryDirectoryTree(const char* name)
			: root(tests_temp_dir() / name)
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(root / "child");
		}

		~TemporaryDirectoryTree()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		std::filesystem::path root;
	};

	struct GuardedDirectory
	{
		AshEngine::Directory directory{};
		std::array<unsigned char, 64> canary{};
	};

	auto close_saved_directory_handle(void* handle) -> void
	{
		if (!handle)
		{
			return;
		}

		AshEngine::Directory cleanup{};
		cleanup.os_handle = handle;
		CHECK(AshEngine::file_close_directory(&cleanup));
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

TEST_CASE("Directory failed child navigation preserves open state")
{
	TemporaryDirectoryTree tree("directory_missing_child");
	AshEngine::Directory directory{};
	REQUIRE(AshEngine::file_open_directory(tree.root.string().c_str(), &directory));

	const std::array<char, AshEngine::k_max_path> original_path = [&directory]()
	{
		std::array<char, AshEngine::k_max_path> path{};
		std::memcpy(path.data(), directory.path, path.size());
		return path;
	}();
	void* const original_handle = directory.os_handle;

	CHECK_FALSE(AshEngine::file_sub_directory(&directory, "missing-child"));
	CHECK(std::memcmp(directory.path, original_path.data(), original_path.size()) == 0);
	CHECK(directory.os_handle == original_handle);

	if (directory.os_handle == original_handle)
	{
		CHECK(AshEngine::file_close_directory(&directory));
	}
	else
	{
		close_saved_directory_handle(original_handle);
	}
}

TEST_CASE("Directory failed open preserves existing state")
{
	TemporaryDirectoryTree tree("directory_failed_open");
	AshEngine::Directory directory{};
	REQUIRE(AshEngine::file_open_directory(tree.root.string().c_str(), &directory));

	std::array<char, AshEngine::k_max_path> original_path{};
	std::memcpy(original_path.data(), directory.path, original_path.size());
	void* const original_handle = directory.os_handle;
	const std::filesystem::path missing = tree.root / "missing";

	CHECK_FALSE(AshEngine::file_open_directory(missing.string().c_str(), &directory));
	CHECK(std::memcmp(directory.path, original_path.data(), original_path.size()) == 0);
	CHECK(directory.os_handle == original_handle);
	CHECK(AshEngine::file_close_directory(&directory));
}

TEST_CASE("Directory APIs reject null and empty inputs")
{
	AshEngine::Directory directory{};
	const AshEngine::Directory original = directory;

	CHECK_FALSE(AshEngine::file_open_directory(nullptr, &directory));
	CHECK_FALSE(AshEngine::file_open_directory("", &directory));
	CHECK_FALSE(AshEngine::file_open_directory(".", nullptr));
	CHECK_FALSE(AshEngine::file_close_directory(nullptr));
	CHECK_FALSE(AshEngine::file_sub_directory(nullptr, "child"));
	CHECK_FALSE(AshEngine::file_sub_directory(&directory, nullptr));
	AshEngine::directory_current(nullptr);
	AshEngine::file_parent_directory(nullptr);

	CHECK(std::memcmp(&directory, &original, sizeof(directory)) == 0);
}

TEST_CASE("Directory oversized resolved child navigation preserves state and canary")
{
	GuardedDirectory guarded{};
	guarded.canary.fill(0xA5);
	const auto original_canary = guarded.canary;

	constexpr size_t prefix_length = AshEngine::k_max_path - 6;
	std::memset(guarded.directory.path, 'x', prefix_length);
	guarded.directory.path[prefix_length] = '*';
	guarded.directory.path[prefix_length + 1] = '\0';

	std::array<char, AshEngine::k_max_path> original_path{};
	std::memcpy(original_path.data(), guarded.directory.path, original_path.size());

	CHECK_FALSE(AshEngine::file_sub_directory(&guarded.directory, "ab"));
	CHECK(std::memcmp(guarded.directory.path, original_path.data(), original_path.size()) == 0);
	CHECK(guarded.directory.os_handle == nullptr);
	CHECK(guarded.canary == original_canary);

	const std::string oversized_child(AshEngine::k_max_path, 'z');
	CHECK_FALSE(AshEngine::file_sub_directory(&guarded.directory, oversized_child.c_str()));
	CHECK(std::memcmp(guarded.directory.path, original_path.data(), original_path.size()) == 0);
	CHECK(guarded.canary == original_canary);
}

TEST_CASE("Directory close is idempotent")
{
	TemporaryDirectoryTree tree("directory_close_idempotent");
	AshEngine::Directory directory{};
	REQUIRE(AshEngine::file_open_directory(tree.root.string().c_str(), &directory));

	CHECK(AshEngine::file_close_directory(&directory));
	CHECK(directory.os_handle == nullptr);
	CHECK(AshEngine::file_close_directory(&directory));
}

TEST_CASE("Directory child and parent navigation commit valid state")
{
	TemporaryDirectoryTree tree("directory_navigation");
	AshEngine::Directory directory{};
	REQUIRE(AshEngine::file_open_directory(tree.root.string().c_str(), &directory));

	REQUIRE(AshEngine::file_sub_directory(&directory, "child"));
	CHECK(std::string(directory.path).find("child") != std::string::npos);
	CHECK(directory.os_handle != nullptr);
	REQUIRE(AshEngine::file_sub_directory(&directory, ".."));
	CHECK(std::string(directory.path).find("child") == std::string::npos);
	REQUIRE(AshEngine::file_sub_directory(&directory, "child\\..\\child"));
	CHECK(std::string(directory.path).find("child") != std::string::npos);

	AshEngine::file_parent_directory(&directory);
	CHECK(std::string(directory.path).find("child") == std::string::npos);
	CHECK(directory.os_handle != nullptr);
	CHECK(AshEngine::file_close_directory(&directory));
}

TEST_CASE("Directory current path writes a terminated bounded result")
{
	GuardedDirectory guarded{};
	guarded.canary.fill(0x5A);
	const auto original_canary = guarded.canary;

	AshEngine::directory_current(&guarded.directory);

	CHECK(guarded.directory.path[0] != '\0');
	CHECK(std::memchr(guarded.directory.path, '\0', AshEngine::k_max_path) != nullptr);
	CHECK(guarded.canary == original_canary);
}

TEST_CASE("Directory current path releases a prior search handle")
{
	TemporaryDirectoryTree tree("directory_current_replaces_open_state");
	AshEngine::Directory directory{};
	REQUIRE(AshEngine::file_open_directory(tree.root.string().c_str(), &directory));
	REQUIRE(directory.os_handle != nullptr);

	AshEngine::directory_current(&directory);

	CHECK(directory.os_handle == nullptr);
	std::error_code error;
	CHECK(std::filesystem::equivalent(std::filesystem::path(directory.path), std::filesystem::current_path(), error));
	CHECK_FALSE(error);
	CHECK(AshEngine::file_close_directory(&directory));
}
