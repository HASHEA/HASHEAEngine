/*
    filename:       rc.cpp
    author:         Ming Dong
    date:           2016-MAR-22
    description:    
*/
#include "../public/rc.h"
#include <domecore/public/typedvalue/simpletype_general.h>

RC_NAMESPACE_BEGIN

class RCOSTypes : public TSingleton<RCOSTypes>
{
public:
    RCOSTypes()
        : m_Type_OSTexture2D(DSimpleTypeName(RCGlobal::k_SimpleTypeName_OSTexture2D), DSimpleTypeName("rc"))
		, m_Type_OSTexture3D(DSimpleTypeName(RCGlobal::k_SimpleTypeName_OSTexture3D), DSimpleTypeName("rc"))
        , m_Type_OSTextureCube(DSimpleTypeName(RCGlobal::k_SimpleTypeName_OSTextureCube), DSimpleTypeName("rc"))
        , m_Type_OSVertexShader(DSimpleTypeName("OSVertexShader"), DSimpleTypeName("rc"))
        , m_Type_OSPixelShader(DSimpleTypeName("OSPixelShader"), DSimpleTypeName("rc"))
        , m_Type_OSVertexLayout(DSimpleTypeName("OSVertexLayout"), DSimpleTypeName("rc"))
        , m_Type_OSVertexBuffer(DSimpleTypeName("OSVertexBuffer"), DSimpleTypeName("rc"))
        , m_Type_OSIndexBuffer(DSimpleTypeName("OSIndexBuffer"), DSimpleTypeName("rc"))
        , m_Type_OSConstBuffer(DSimpleTypeName("OSConstBuffer"), DSimpleTypeName("rc"))
        , m_Type_OSRenderOperation(DSimpleTypeName("OSRenderOperation"), DSimpleTypeName("rc"))
    {

    }

    DResult init()
    {
        DSimpleTypeManager::Instance().registerType(&m_Type_OSTexture2D);
		DSimpleTypeManager::Instance().registerType(&m_Type_OSTexture3D);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSTextureCube);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSVertexShader);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSPixelShader);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSVertexLayout);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSVertexBuffer);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSIndexBuffer);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSConstBuffer);
        DSimpleTypeManager::Instance().registerType(&m_Type_OSRenderOperation);

        return R_SUCCESS;
    }

    DResult uninit()
    {
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSTexture2D);
		DSimpleTypeManager::Instance().unregisterType(&m_Type_OSTexture3D);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSTextureCube);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSVertexShader);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSPixelShader);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSVertexLayout);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSVertexBuffer);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSIndexBuffer);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSConstBuffer);
        DSimpleTypeManager::Instance().unregisterType(&m_Type_OSRenderOperation);

        return R_SUCCESS;
    }

    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSTexture2D>                        m_Type_OSTexture2D;
	TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSTexture3D>                        m_Type_OSTexture3D;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSTextureCube>                      m_Type_OSTextureCube;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSVertexShader>                     m_Type_OSVertexShader;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSPixelShader>                      m_Type_OSPixelShader;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSVertexLayout>                     m_Type_OSVertexLayout;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSVertexBuffer>                     m_Type_OSVertexBuffer;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSIndexBuffer>                      m_Type_OSIndexBuffer;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSConstBuffer>                      m_Type_OSConstBuffer;
    TSimpleType_FixedAlloc_CompareNo_SerializeNo<OSRenderOperation>                  m_Type_OSRenderOperation;
};

DResult RC_API RCInit(const Char* i_pRCDataRootPath)
{
    DResult l_Result = R_SUCCESS;
    l_Result = DomeCore_Init();
    DOME_ASSERT(DM_SUCC(l_Result));
    DString l_RCDataRootPath = i_pRCDataRootPath;

    // create the rc global structure
    DOME_New(RCGlobal);
    if (l_RCDataRootPath.size() > 0)
    {
        DString l_Path;
        if(l_RCDataRootPath.isEndWith("\\") || l_RCDataRootPath.isEndWith("/"))
            RCGlobal::Instance().m_RCDataRootPath = l_RCDataRootPath;
        else
            RCGlobal::Instance().m_RCDataRootPath = l_RCDataRootPath + "/";
    }
    else
        RCGlobal::Instance().m_RCDataRootPath = "./rcdata/";

    // create the rc global data structure
    DOME_New(RCOSTypes);
    RCOSTypes::Instance().init();


    // Do rc init work here
    DOME_New(RCManager);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_Result;
}

DResult RC_API RCUninit()
{
    DResult l_Result = R_SUCCESS;
    // Do rc uninit work here
    DOME_ASSERT(DM_SUCC(l_Result));
    DOME_Del(RCManager::InstancePtr());

    // destroy the rc global data structure
    RCOSTypes::Instance().uninit();
    DOME_Del(RCOSTypes::InstancePtr());

    // destroy the rc global structure
    DOME_Del(RCGlobal::InstancePtr());

    l_Result = DomeCore_Uninit();
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_Result;
}



RC_NAMESPACE_END