#include"pch.h"

#include"mdoperatorfsrupsample.h"
#include<rc/public/iexecuter.h>

#ifdef RC_PERF
#include"KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static const int s_InputCount = 2;

MDOperatorFSRUpsample::MDOperatorFSRUpsample()
    :m_OperatorName("MDFSR")
{
}

MDOperatorFSRUpsample::~MDOperatorFSRUpsample()
{

}


const DString& MDOperatorFSRUpsample::getOperatorName()const
{
    return m_OperatorName;
}

Bool MDOperatorFSRUpsample::isGpuOperator()const
{
    return DM_FALSE;
}

Int MDOperatorFSRUpsample::getInputCount()const
{
    return s_InputCount;
}

Int MDOperatorFSRUpsample::getOutputCount()const
{
    return 1;
}

DSimpleTypeID MDOperatorFSRUpsample::getInputTypeID(Int i_Index)const
{
    DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
    DSimpleTypeID InputTypes[s_InputCount] =
    {
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_F32
    };
    return InputTypes[i_Index];
}

DSimpleTypeID MDOperatorFSRUpsample::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorFSRUpsample::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
    return RGDF_RGBA8;
}

DResult MDOperatorFSRUpsample::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == s_InputCount);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::OutputSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

MDOperandPtr MDOperatorFSRUpsample::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint)const
{
    PERF_COUNTER_EX(0);
   
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer*l_pRenderer=l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pTextureValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pQualityValue = i_pParamList[1]->getDataPtr();
   
   

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA32F || l_Format==RGDF_RGBA8);

    float l_QualityValue;
    l_QualityValue = l_pQualityValue->getF32();

    OSTexture2D l_UsTex;
   

    l_Result = l_pRenderer->createTexture2D(l_UsTex, l_RtSize.x,l_RtSize.y,1,l_Format,RBU_DEFAULT,DM_TRUE,DM_NULL,1);
    DOME_ASSERT(DM_SUCC(l_Result));

   

    RCMOD_Texture l_UpsampleTexture;
    *((OSTexture2D*)l_UpsampleTexture.getPtr()) = l_UsTex;

  

    RCMOD_Texture l_InputTexture;
    *((OSTexture2D*)l_InputTexture.getPtr()) = l_pTextureValue->getValue<OSTexture2D>();
    
    


    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
        
    l_pScenePlugin->RenderFSRUpsample((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_UpsampleTexture, &l_InputTexture,l_QualityValue);

    MDOperandValue*l_pRtOperand=DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
   
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_UsTex);
    DOME_ASSERT(DM_SUCC(l_Result));
   
    return l_pRtOperand;

}

DResult  MDOperatorFSRUpsample::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult)const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(i_pResult);



    return R_SUCCESS;
}

RC_NAMESPACE_END