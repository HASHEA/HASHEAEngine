#pragma once
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../math/mathutils.h"
#include "../imemory.h"
#include "../os.h"
#include "stringhash.h"


DOME_NAMESPACE_BEGIN

template<class CHAR_T, class ALLOCATOR_T = IDefaultMemManager>
class TCommonString
{
#ifdef DOME_COMMONSTRING_WITHHASH
#undef DOME_COMMONSTRING_WITHHASH
#endif
#define DOME_COMMONSTRING_CLASS     TCommonString

#include "commonstring.inc"

#ifdef DOME_COMMONSTRING_WITHHASH
#undef DOME_COMMONSTRING_WITHHASH
#endif
#undef DOME_COMMONSTRING_CLASS
};

template<class CHAR_T, class ALLOCATOR_T = IDefaultMemManager>
class THashCommonString
{
#ifndef DOME_COMMONSTRING_WITHHASH
#define DOME_COMMONSTRING_WITHHASH
#endif
#define DOME_COMMONSTRING_CLASS     THashCommonString

#include "commonstring.inc"

#ifdef DOME_COMMONSTRING_WITHHASH
#undef DOME_COMMONSTRING_WITHHASH
#endif
#undef DOME_COMMONSTRING_CLASS
};

DOME_NAMESPACE_END