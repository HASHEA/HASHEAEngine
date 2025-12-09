// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")


#pragma intrinsic(_InterlockedAnd)
#pragma intrinsic(_InterlockedOr)

DOME_NAMESPACE_BEGIN
	// The return value is the U32 value before ANDing with mask
	// This operation is considered a full memory barrier
	inline U32 OS_Atomic::Sync_FetchAnd(volatile U32* io_pU32, U32 i_Mask)
	{
		return (U32)_InterlockedAnd((LONG*)io_pU32, i_Mask);
	}

	// The return value is the U32 value before ORing with mask
	// This operation is considered a full memory barrier
	inline U32 OS_Atomic::Sync_FetchOr(volatile U32* io_pU32, U32 i_Mask)
	{
		return (U32)_InterlockedOr((LONG*)io_pU32, i_Mask);
	}

	// Full memory Barrier
	inline void OS_Atomic::MemBarrier()
	{
		MemoryBarrier();
	}

	// System sleep function
	inline void OS_Atomic::Sleep(U32 i_Milliseconds)
	{
		::Sleep(i_Milliseconds);
	}
DOME_NAMESPACE_END
