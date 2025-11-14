#include "pch.h"
#include "../../public/rcmod.h"
#include "rccolorcurvesnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCColorCurvesNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCColorCurvesNode)(i_pEffect);
}

DResult          RCColorCurvesNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCColorCurvesNode::RCColorCurvesNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_LutTexOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_LutVersion(-1)
{

}

RCColorCurvesNode::~RCColorCurvesNode()
{
    OSTexture2D l_LutTex;
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    if (m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
    {
        l_LutTex = *m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
        l_pRenderer->destroyTexture2D(l_LutTex);
    }
}

DResult         RCColorCurvesNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    executePushInput(o_pStack, 0);

    o_pStack->pushOperand(&m_LutTexOperand);

    updateLutTex();

    static const DStringHash k_MDOperator_MDLut("MDLut");
    const MDOperator* l_pMDLut = RCManager::Instance().getMDOperator(k_MDOperator_MDLut);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDLut);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCColorCurvesNode::finishLoad()
{
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTexture2D l_LutTex;

    l_Result = l_pRenderer->createTexture2D(l_LutTex, 256, 1, 1, RGDF_RGBA8, RBU_DYNAMIC, DM_FALSE, NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    m_LutTexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_LutTex);

    updateLutTex();
}

void            RCColorCurvesNode::updateLutTex()
{
    U64 l_NewVersion = getParamVersion(0);
    if (l_NewVersion != m_LutVersion)
    {
        RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
        OSTexture2D l_LutTex;

        m_LutVersion = l_NewVersion;

        DOME_ASSERT(m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>());
        l_LutTex = *m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();

        const DSimpleTypedValue* l_ColorCurvesValue = getParam(0);
        DOME_ASSERT(l_ColorCurvesValue && l_ColorCurvesValue->getTypeID() == RCGlobal::k_SimpleTypeID_DVectorLut4f);
        DVectorLut4f l_ColorCurves = l_ColorCurvesValue->getDVectorLut4f();

        struct PixelRGBA8
        {
            U8      r;
            U8      g;
            U8      b;
            U8      a;
        };
        PixelRGBA8* l_pBuf = DM_NULL;

        RCTexLockedRect l_LockRectData;

        l_pRenderer->lockTexture2D(l_LutTex, 0, RTLS_WRITEONLY, l_LockRectData);

        l_pBuf = (PixelRGBA8*)l_LockRectData.m_pBits;
        for(int x = 0; x < 256; ++x)
        {
            DVector4f l_Value = l_ColorCurves.lookupVector4(x / 255.0);
            l_pBuf[x].r = U8(l_Value.x * 255);
            l_pBuf[x].g = U8(l_Value.y * 255);
            l_pBuf[x].b = U8(l_Value.z * 255);
            l_pBuf[x].a = U8(l_Value.w * 255);
        }

        l_pRenderer->unlockTexture2D(l_LutTex, 0);
    }
}


RC_NAMESPACE_END