#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "Base/hlog.h"
#include "Base/hmemory.h"

#include <filesystem>
#include <iostream>

namespace
{
	namespace fs = std::filesystem;

	bool is_engine_root(const fs::path& path)
	{
		return fs::exists(path / "AshEngine.sln") &&
			fs::exists(path / "project") &&
			fs::exists(path / "product");
	}

	// 与 EntryPoint.h 的 init_dir 语义一致：文件类测试与引擎运行时共用仓库根作路径基准
	bool enter_repo_root()
	{
		fs::path current = fs::current_path();
		for (int depth = 0; depth < 16; ++depth)
		{
			if (is_engine_root(current))
			{
				fs::current_path(current);
				return true;
			}
			if (current == current.parent_path())
			{
				break;
			}
			current = current.parent_path();
		}
		return false;
	}
}

int main(int argc, char** argv)
{
	if (!enter_repo_root())
	{
		std::cerr << "Tests: failed to locate engine repository root from current directory." << std::endl;
		return 1;
	}
	// 进程唯一一次服务初始化：doctest 不保证用例顺序，被测代码依赖的服务必须在这里就绪；
	// EngineSelfTestsBridge 侧经 MemoryService::is_initialized() 防二次 init
	AshEngine::LogService::instance()->init(nullptr);
	AshEngine::MemoryService::instance()->init(nullptr);
	doctest::Context context(argc, argv);
	const int result = context.run();
	AshEngine::MemoryService::instance()->shutdown();
	AshEngine::LogService::instance()->shutdown();
	return result;
}
