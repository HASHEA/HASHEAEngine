/*
    filename:       operators.h
    author:         Ming Dong
    date:           2016-Feb-28
    description:    operators 
*/
#pragma once
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "matrix2x2.h"
#include "matrix3x3.h"
#include "matrix4x4.h"
#include "matrixsrt.h"
#include "matrixsqt.h"
#include "quaternion.h"
#include "vectorlut.h"
#include "../container.h"

DOME_NAMESPACE_BEGIN

namespace Math
{
    DOME_CORE_API DResult MathToDString(F32 i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVector2f& i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVector3f& i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVector4f& i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVectorLut1f& i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVectorLut2f& i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVectorLut3f& i_Value, DString& o_Str);
    DOME_CORE_API DResult MathToDString(const DVectorLut4f& i_Value, DString& o_Str);

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, F32& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVector2f& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVector3f& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVector4f& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut1f& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut2f& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut3f& o_Value);
    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut4f& o_Value);
}
DOME_NAMESPACE_END
