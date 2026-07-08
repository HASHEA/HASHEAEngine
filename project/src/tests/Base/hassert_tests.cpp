#include "doctest.h"
#include "Base/hassert.h"

TEST_CASE("H_ASSERT is statement-safe in unbraced if/else")
{
	bool branch_executed = false;
	if (true)
		H_ASSERT(true);
	else
		branch_executed = true;

	CHECK_FALSE(branch_executed);
}
