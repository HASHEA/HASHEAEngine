/*
    filename:       typedvalue_private.cpp
    author:         Ming Dong
    date:           2016-Mar-16
    description:    
*/
#include "simpletypedvalue_private.h"
#include "../../public/typedvalue/simpletype_embed.h"
#include "../../public/typedvalue/simpletype_embed_nocompare.h"
#include "../../public/typedvalue/simpletype_alloced.h"
#include "../../public/typedvalue/simpletype_fixedalloced.h"

#include "../../public/typedvalue/simpletype_general.h"

DOME_NAMESPACE_BEGIN

struct DOME_InternalTypeSet
{
    DOME_InternalTypeSet()
        : m_Type_Int(DSimpleTypeName("Int"), DSimpleTypeName("Internal"))
        , m_Type_F32(DSimpleTypeName("F32"), DSimpleTypeName("Internal"))
        , m_Type_F64(DSimpleTypeName("F64"), DSimpleTypeName("Internal"))
        , m_Type_DStringHash(DSimpleTypeName("DStringHash"), DSimpleTypeName("Internal"))
        , m_Type_DVector2f(DSimpleTypeName("DVector2f"), DSimpleTypeName("Internal"))
        , m_Type_DVector3f(DSimpleTypeName("DVector3f"), DSimpleTypeName("Internal"))
        , m_Type_DVector4f(DSimpleTypeName("DVector4f"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix2x2f(DSimpleTypeName("DMatrix2x2f"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix3x3f(DSimpleTypeName("DMatrix3x3f"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix4x4f(DSimpleTypeName("DMatrix4x4f"), DSimpleTypeName("Internal"))
        , m_Type_DQuaternionf(DSimpleTypeName("DQuaternionf"), DSimpleTypeName("Internal"))
        , m_Type_DLutf(DSimpleTypeName("DLutf"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut1f(DSimpleTypeName("DVectorLut1f"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut2f(DSimpleTypeName("DVectorLut2f"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut3f(DSimpleTypeName("DVectorLut3f"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut4f(DSimpleTypeName("DVectorLut4f"), DSimpleTypeName("Internal"))
        , m_Type_DVector2d(DSimpleTypeName("DVector2d"), DSimpleTypeName("Internal"))
        , m_Type_DVector3d(DSimpleTypeName("DVector3d"), DSimpleTypeName("Internal"))
        , m_Type_DVector4d(DSimpleTypeName("DVector4d"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix2x2d(DSimpleTypeName("DMatrix2x2d"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix3x3d(DSimpleTypeName("DMatrix3x3d"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix4x4d(DSimpleTypeName("DMatrix4x4d"), DSimpleTypeName("Internal"))
        , m_Type_DQuaterniond(DSimpleTypeName("DQuaterniond"), DSimpleTypeName("Internal"))
        , m_Type_DLutd(DSimpleTypeName("DLutd"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut1d(DSimpleTypeName("DVectorLut1d"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut2d(DSimpleTypeName("DVectorLut2d"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut3d(DSimpleTypeName("DVectorLut3d"), DSimpleTypeName("Internal"))
        , m_Type_DVectorLut4d(DSimpleTypeName("DVectorLut4d"), DSimpleTypeName("Internal"))
        , m_Type_DVector2i(DSimpleTypeName("DVector2i"), DSimpleTypeName("Internal"))
        , m_Type_DVector3i(DSimpleTypeName("DVector3i"), DSimpleTypeName("Internal"))
        , m_Type_DVector4i(DSimpleTypeName("DVector4i"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix2x2i(DSimpleTypeName("DMatrix2x2i"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix3x3i(DSimpleTypeName("DMatrix3x3i"), DSimpleTypeName("Internal"))
        , m_Type_DMatrix4x4i(DSimpleTypeName("DMatrix4x4i"), DSimpleTypeName("Internal"))
        , m_Type_DString(DSimpleTypeName("DString"), DSimpleTypeName("Internal"))
        , m_Type_DHashString(DSimpleTypeName("DHashString"), DSimpleTypeName("Internal"))
        , m_Type_DWString(DSimpleTypeName("DWString"), DSimpleTypeName("Internal"))
        , m_Type_DWHashString(DSimpleTypeName("DWHashString"), DSimpleTypeName("Internal"))
        , m_Type_OSHandle(DSimpleTypeName("OSHandle"), DSimpleTypeName("Internal"))
    {

    }

    // EMBED COMPARE SERIALIZE
    TSimpleType_Embed_CompareYes_SerializeYes<Int>                      m_Type_Int;
    TSimpleType_Embed_CompareYes_SerializeYes<F32>                      m_Type_F32;
    TSimpleType_Embed_CompareYes_SerializeYes<F64>                      m_Type_F64;
    TSimpleType_Embed_CompareYes_SerializeYes<DStringHash>              m_Type_DStringHash;

    // EMBED NOCOMPARE SERIALIZE
    TSimpleType_Embed_CompareNo_SerializeYes<DVector2f>                 m_Type_DVector2f;

    // FIXEDALLOC NOCOMPARE SERIALIZE
    TSimpleType_Alloced_CompareNo_SerializeYes<DVector3f>               m_Type_DVector3f;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DVector4f>            m_Type_DVector4f;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DMatrix2x2f>          m_Type_DMatrix2x2f;
    TSimpleType_Alloced_CompareNo_SerializeYes<DMatrix3x3f>             m_Type_DMatrix3x3f;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DMatrix4x4f>          m_Type_DMatrix4x4f;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DQuaternionf>         m_Type_DQuaternionf;

    // ALLOCED NOCOMPARE NOSERIALIZE        TODO: implement a special serialize function for these types
    TSimpleType_Alloced_CompareNo_SerializeNo<DLutf>                    m_Type_DLutf;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut1f>             m_Type_DVectorLut1f;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut2f>             m_Type_DVectorLut2f;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut3f>             m_Type_DVectorLut3f;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut4f>             m_Type_DVectorLut4f;


    // FIXEDALLOC NOCOMPARE SERIALIZE
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DVector2d>            m_Type_DVector2d;
    TSimpleType_Alloced_CompareNo_SerializeYes<DVector3d>               m_Type_DVector3d;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DVector4d>            m_Type_DVector4d;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DMatrix2x2d>          m_Type_DMatrix2x2d;
    TSimpleType_Alloced_CompareNo_SerializeYes<DMatrix3x3d>             m_Type_DMatrix3x3d;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DMatrix4x4d>          m_Type_DMatrix4x4d;
    TSimpleType_FixedAlloc_CompareNo_SerializeYes<DQuaterniond>         m_Type_DQuaterniond;


    // ALLOCED NOCOMPARE NOSERIALIZE        TODO: implement a special serialize function for these types
    TSimpleType_Alloced_CompareNo_SerializeNo<DLutd>                    m_Type_DLutd;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut1d>             m_Type_DVectorLut1d;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut2d>             m_Type_DVectorLut2d;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut3d>             m_Type_DVectorLut3d;
    TSimpleType_Alloced_CompareNo_SerializeNo<DVectorLut4d>             m_Type_DVectorLut4d;

    // ALLOCED NOCOMPARE SERIALIZE
    TSimpleType_Alloced_CompareNo_SerializeYes<DVector2i>               m_Type_DVector2i;
    TSimpleType_Alloced_CompareNo_SerializeYes<DVector3i>               m_Type_DVector3i;
    TSimpleType_Alloced_CompareNo_SerializeYes<DVector4i>               m_Type_DVector4i;
    TSimpleType_Alloced_CompareNo_SerializeYes<DMatrix2x2i>             m_Type_DMatrix2x2i;
    TSimpleType_Alloced_CompareNo_SerializeYes<DMatrix3x3i>             m_Type_DMatrix3x3i;
    TSimpleType_Alloced_CompareNo_SerializeYes<DMatrix4x4i>             m_Type_DMatrix4x4i;

    // FIXEDALLOC COMPARE SERIALIZE STRING
    TSimpleType_FixedAlloc_CompareYes_SerializeDString<DString>             m_Type_DString;
    TSimpleType_FixedAlloc_CompareYes_SerializeDHashString<DHashString>     m_Type_DHashString;
    TSimpleType_FixedAlloc_CompareYes_SerializeDWString<DWString>           m_Type_DWString;
    TSimpleType_FixedAlloc_CompareYes_SerializeDWHashString<DWHashString>   m_Type_DWHashString;

    // EMBED NOCOMPARE NOSERIALIZE
    TSimpleType_Embed_CompareNo_SerializeNo<OSHandle>                   m_Type_OSHandle;


} *g_Dome_InternalTypeSet;


DResult DomeCore_Init_SimpleTypedValue()
{
    DOME_New(DSimpleTypeManager);

    g_Dome_InternalTypeSet = DOME_New(DOME_InternalTypeSet);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_Int);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_F32);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_F64);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DStringHash);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector2f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector3f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector4f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix2x2f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix3x3f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix4x4f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DQuaternionf);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DLutf);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut1f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut2f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut3f);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut4f);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector2d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector3d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector4d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix2x2d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix3x3d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix4x4d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DQuaterniond);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DLutd);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut1d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut2d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut3d);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVectorLut4d);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector2i);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector3i);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DVector4i);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix2x2i);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix3x3i);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DMatrix4x4i);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DString);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DHashString);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DWString);
    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_DWHashString);

    DSimpleTypeManager::Instance().registerType(&g_Dome_InternalTypeSet->m_Type_OSHandle);

    return R_SUCCESS;
}

DResult DomeCore_Uninit_SimpleTypedValue()
{
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_Int);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_F32);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_F64);

    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DStringHash);

    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector2f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector3f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector4f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix2x2f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix3x3f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix4x4f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DQuaternionf);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DLutf);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut1f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut2f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut3f);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut4f);

    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector2d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector3d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector4d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix2x2d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix3x3d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix4x4d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DQuaterniond);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DLutd);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut1d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut2d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut3d);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVectorLut4d);

    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector2i);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector3i);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DVector4i);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix2x2i);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix3x3i);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DMatrix4x4i);

    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DString);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DHashString);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DWString);
    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_DWHashString);

    DSimpleTypeManager::Instance().unregisterType(&g_Dome_InternalTypeSet->m_Type_OSHandle);

    DOME_Del(g_Dome_InternalTypeSet);

    DOME_Del(DSimpleTypeManager::InstancePtr());

    return R_SUCCESS;
}

DOME_NAMESPACE_END