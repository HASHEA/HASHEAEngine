#pragma once
#define BIT(x) (1 << x)
#define NO_COPYABLE(TypeName) \
	TypeName(const TypeName &) = delete;   \
	TypeName(TypeName &&) = delete;	\
	TypeName& operator=(TypeName &&) = delete;	\
	TypeName& operator=(const TypeName &) = delete
#ifdef HASHEA_ENGINE
#define HASHEA_API __declspec(dllexport)
#else
#define HASHEA_API __declspec(dllimport)
#endif // HASHEA_ENGINE