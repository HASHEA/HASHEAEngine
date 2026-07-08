#include "doctest.h"
#include "Base/EngineSelfTests.h"

TEST_CASE("engine-self-tests bridge: legacy in-DLL suite passes")
{
	CHECK(AshEngine::run_engine_base_self_tests() == 0);
}
