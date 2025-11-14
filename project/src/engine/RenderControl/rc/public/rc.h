/*
    filename:       rc.h
    author:         Ming Dong
    date:           2016-MAR-22
    description:    
*/
#pragma once

#include "rcdefines.h"
#include "rcconfigure.h"
#include "rcglobal.h"

#include "rcrenderer.h"
#include "rceffectmanager.h"
#include "rcmanager.h"

#include "mdeffect.h"
#include "mdoperandvalue.h"
#include "mdoperandvalueptr.h"
#include "mdoperandarray.h"
#include "mdoperatorcpu.h"
#include "mdoperatorgpu.h"

RC_NAMESPACE_BEGIN

DResult RC_API RCInit(const Char* i_pRCDataRootPath);

DResult RC_API RCUninit();

RC_NAMESPACE_END