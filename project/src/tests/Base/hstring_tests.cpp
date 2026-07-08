#include "doctest.h"
#include "Base/hstring.h"

#include <cstring>

using AshEngine::StringView;

TEST_CASE("StringView::equals compares length then content")
{
	char hello[] = "hello";
	char hello2[] = "hello";
	char other[] = "helpo";

	StringView a{ hello, 5 };
	StringView b{ hello2, 5 };
	StringView c{ other, 5 };
	StringView shorter{ hello, 4 };

	CHECK(StringView::equals(a, b));
	CHECK_FALSE(StringView::equals(a, c));
	CHECK_FALSE(StringView::equals(a, shorter));

	StringView emptyNull{ nullptr, 0 };
	StringView emptyText{ hello, 0 };
	CHECK(StringView::equals(emptyNull, emptyText));
}

TEST_CASE("StringView::copy_to truncates to buffer and null-terminates")
{
	char source[] = "abcdef";
	StringView view{ source, 6 };

	char exact[8] = {};
	StringView::copy_to(view, exact, sizeof(exact));
	CHECK(std::strcmp(exact, "abcdef") == 0);

	char tight[4] = { 'x', 'x', 'x', 'x' };
	StringView::copy_to(view, tight, sizeof(tight));
	CHECK(std::strcmp(tight, "abc") == 0);

	char one[1] = { 'x' };
	StringView::copy_to(view, one, sizeof(one));
	CHECK(one[0] == 0);
}
