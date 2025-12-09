//
//  dome.h
//  engine
//
//  Created by Ming Dong on 12-03-05.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_dome_h
//#define engine_dome_h

#include "configure.h"
#include "defines.h"
#include "typedefs.h"
#include "strongtypedef.h"
#include "error.h"
#include "singleton.h"
#include "endian.h"
#include "unicode.h"
#include "os.h"
#include "imemory.h"
#include "./math/mathutils.h"
#include "container.h"
#include "math.h"
#include "threadhub.h"
#include "typedvaluehub.h"
#include "filesys.h"
#include "networkhub.h"

#include "external.h"



DOME_NAMESPACE_BEGIN

DOME_CORE_API DResult DomeCore_Init();
DOME_CORE_API DResult DomeCore_Uninit();

DOME_NAMESPACE_END

//#endif
