#pragma once
#define BIT(x) (1 << x)
#define NO_COPYABLE(TypeName) \
	TypeName(const TypeName &) = delete;   \
	TypeName(TypeName &&) = delete;	\
	TypeName& operator=(TypeName &&) = delete;	\
	TypeName& operator=(const TypeName &) = delete
#ifdef ASH_ENGINE
#define ASH_API __declspec(dllexport)
#else
#define ASH_API __declspec(dllimport)
#endif // ASH_ENGINE

#define NO_COPYABLE(TypeName) \
	TypeName(const TypeName &) = delete;   \
	TypeName(TypeName &&) = delete;	\
	TypeName& operator=(TypeName &&) = delete;	\
	TypeName& operator=(const TypeName &) = delete


enum HS_Result
{
	HS_OK,
	HS_FAIL,
};

#define HS_CHECK_FAILED(cond)\
	(HS_Result)(cond) != HS_OK
		