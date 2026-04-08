#pragma once

#include <rc/public/rc.h>

//////////////////////////////////////////////////////////////////////////
///// Screen color-shift post effect
// 1. Shader: client\data\public\shader\PostRenderFinalPass.fx
// technique FinalPassCurveCMYK
// 
// 2. Preset index range: 0 - 8, configured in: client\data\public\color_curve.ini
// Reference loader: Sword3\Source\KG3DEngine\KG3DEngine\KG3DPostRenderEffectManager.cpp
// HRESULT KG3DPostRenderEffectManager::LoadCurveStype()
// 
// 3. Runtime color-switch entry:
// HRESULT KG3DPostRenderEffectManager::SetActiveStype(int index, int bFlush)

RC_NAMESPACE_BEGIN

#define Curve_Stype_Name_Size 64

class RCColorShiftNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCColorShiftNode(RCEffect* i_pEffect);
    virtual ~RCColorShiftNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    HRESULT LoadCurveStypeIni();
    void    updateLutTex();

private:
    MDOperandValue          m_LutTexOperand;
    MDOperandValue          m_CMYKOperand;

    struct _ColorCurve
    {
        std::vector<DVector2f> vRed;
        std::vector<DVector2f> vGreen;
        std::vector<DVector2f> vBlue;

        float  fKC;
        float  fKM;
        float  fKY;
    };

    std::vector<_ColorCurve> m_svCurveStype;
    Int                     m_LutVersion;
};

RC_NAMESPACE_END

