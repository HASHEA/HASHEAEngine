#include "pch.h"
#include "../../public/rcmod.h"
#include "rccolorshiftnode.h"
#include "KG3D_File.h"


RC_NAMESPACE_BEGIN

double CubicSplineIntl(double x[], double y[], int n, int k, double t, double s[5]);
void   SampleCurve(
    const std::vector<DVector2f>& vRed,
    const std::vector<DVector2f>& vGreen,
    const std::vector<DVector2f>& vBlue,
    std::vector<DVector3f>& vData
);

RCEffectNode*    RCColorShiftNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCColorShiftNode)(i_pEffect);
}

DResult          RCColorShiftNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCColorShiftNode::RCColorShiftNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_LutTexOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_CMYKOperand(RCGlobal::k_SimpleTypeID_DVector4f)
{
    m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
    m_CMYKOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_DVector4f, &DVector4f(0.0));
}

RCColorShiftNode::~RCColorShiftNode()
{
	OSTexture2D l_LutTex;
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    if (m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
    {
        l_LutTex = *m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
        if (l_LutTex.isValid())
        {
            l_pRenderer->destroyTexture2D(l_LutTex);
            m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
        }
    }
}

// Main Work is Done Here.
DResult         RCColorShiftNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    executePushInput(o_pStack, 0);

    o_pStack->pushOperand(&m_LutTexOperand);
    o_pStack->pushOperand(&m_CMYKOperand);

    updateLutTex();

    static const DStringHash k_MDOperator_MDLut("MDColorShift");
    const MDOperator* l_pMDLut = RCManager::Instance().getMDOperator(k_MDOperator_MDLut);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDLut);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCColorShiftNode::finishLoad()
{
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTexture2D l_LutTex;

    l_Result = l_pRenderer->createTexture2D(l_LutTex, 256, 1, 1, RGDF_RGBA8, RBU_DYNAMIC, DM_FALSE, NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    m_LutTexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_LutTex);

    LoadCurveStypeIni();
    updateLutTex();
}

void            RCColorShiftNode::updateLutTex()
{
    const DSimpleTypedValue* l_NewVersionValue = getParam(0);
    int l_NewVersion = int(l_NewVersionValue->getF32());

    if (l_NewVersion != m_LutVersion)
    {
        RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
        OSTexture2D l_LutTex;
        int nIndex = 0;

        m_LutVersion = l_NewVersion;
        nIndex = l_NewVersion;

        if (nIndex >= (int)m_svCurveStype.size() || nIndex < 0)
        {
            nIndex = 0;
        }

        std::vector<DVector3f> vData(256);
        SampleCurve(
            m_svCurveStype[nIndex].vRed,
            m_svCurveStype[nIndex].vGreen,
            m_svCurveStype[nIndex].vBlue,
            vData
        );

        DVector4f CMYKValue = DVector4f(m_svCurveStype[nIndex].fKC, m_svCurveStype[nIndex].fKM, m_svCurveStype[nIndex].fKY, 1.0f);
        m_CMYKOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_DVector4f, &CMYKValue);

        DOME_ASSERT(m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>());
        l_LutTex = *m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();

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
        for (int x = 0; x < 256; ++x)
        {
            DVector3f l_Value = vData[x];
            l_pBuf[x].r = U8(l_Value.x * 255);
            l_pBuf[x].g = U8(l_Value.y * 255);
            l_pBuf[x].b = U8(l_Value.z * 255);
            l_pBuf[x].a = U8(255);
        }

        l_pRenderer->unlockTexture2D(l_LutTex, 0);
    }
}

#define ProcessError(Condition) if(!(Condition)) goto Exit0;
    

// Original INI loading logic reference:
// Sword3\\Source\\KG3DEngine\\KG3DEngine\\KG3DPostRenderEffectManager.cpp
const static char* s_strColorCurveConfig = "data\\public\\color_curve.ini";
HRESULT RCColorShiftNode::LoadCurveStypeIni()
{
    HRESULT hResult = E_FAIL;
    HRESULT hrRetCode = E_FAIL;
    _ColorCurve curve;
    int cnt = 0;
    size_t nSectionID = 0;
    m_svCurveStype.clear();

    IKG3D_IniFile* piIniFile = KG3D_LoadIniFile(s_strColorCurveConfig);
    ProcessError(piIniFile!=NULL);

    hrRetCode = piIniFile->GetSectionID("Head", &nSectionID);
    ProcessError(hrRetCode == S_OK);

    hrRetCode = piIniFile->GetInteger(nSectionID, "Count", &cnt, "0");
    ProcessError(hrRetCode == S_OK);
    ProcessError(cnt != 0);

    m_svCurveStype.resize(cnt);

    for (int i = 0; i < cnt; ++i)
    {
        char strSection[MAX_PATH];

        _ColorCurve& curve = m_svCurveStype[i];
        int ptNum = 0;

        sprintf_s(strSection, MAX_PATH, "Curve%d", i);
        hrRetCode = piIniFile->GetSectionID(strSection, &nSectionID);
        ProcessError(hrRetCode == S_OK);

        hrRetCode = piIniFile->GetInteger(nSectionID, "RedCount", &ptNum, "0");
        ProcessError(hrRetCode == S_OK);

        for (int v = 0; v < ptNum; ++v)
        {
            char strKey[MAX_PATH];
//            char temp[MAX_PATH];
            float fData[2];
            sprintf_s(strKey, MAX_PATH, "Red%d", v);
            hrRetCode = piIniFile->GetMultiFloat(nSectionID, strKey, fData, 2, "0,0");
            ProcessError(hrRetCode == S_OK);

            DVector2f pt;
            pt.x = fData[0];
            pt.y = fData[1];
            curve.vRed.push_back(pt);
        }

        hrRetCode = piIniFile->GetInteger(nSectionID, "GreenCount", &ptNum, "0");
        ProcessError(hrRetCode == S_OK);
        for (int v = 0; v < ptNum; ++v)
        {
//            char temp[MAX_PATH];
            char strKey[MAX_PATH];
            float fData[2];
            sprintf_s(strKey, MAX_PATH, "Green%d", v);
            hrRetCode = piIniFile->GetMultiFloat(nSectionID, strKey, fData, 2, "0,0");
            ProcessError(hrRetCode == S_OK);

            DVector2f pt;
            pt.x = fData[0];
            pt.y = fData[1];
            curve.vGreen.push_back(pt);
        }

        hrRetCode = piIniFile->GetInteger(nSectionID, "BlueCount", &ptNum, "0");
        ProcessError(hrRetCode == S_OK);
        for (int v = 0; v < ptNum; ++v)
        {
//            char temp[MAX_PATH];
            char strKey[MAX_PATH];
            float fData[2];
            sprintf_s(strKey, MAX_PATH, "Blue%d", v);
            hrRetCode = piIniFile->GetMultiFloat(nSectionID, strKey, fData, 2, "0,0");
            ProcessError(hrRetCode == S_OK);

            DVector2f pt;
            pt.x = fData[0];
            pt.y = fData[1];
            curve.vBlue.push_back(pt);
        }

        {
            float fData[3];
            hrRetCode = piIniFile->GetMultiFloat(nSectionID, "CMY", fData, 3, "0,0,0");
            ProcessError(hrRetCode == S_OK);

            curve.fKC = fData[0];
            curve.fKM = fData[1];
            curve.fKY = fData[2];
            
        }
        
    }

    hResult = S_OK;
Exit0:
    if (FAILED(hResult))
    {
        m_svCurveStype.clear();
        curve.vRed.push_back(DVector2f(0.f, 0.f));
        curve.vRed.push_back(DVector2f(1.f, 1.f));
        curve.vGreen.push_back(DVector2f(0.f, 0.f));
        curve.vGreen.push_back(DVector2f(1.f, 1.f));
        curve.vBlue.push_back(DVector2f(0.f, 0.f));
        curve.vBlue.push_back(DVector2f(1.f, 1.f));

        curve.fKC = 0.f;
        curve.fKM = 0.f;
        curve.fKY = 0.f;

        m_svCurveStype.push_back(curve);
    }
    
    if (piIniFile)
    {
        piIniFile->Release();
        piIniFile = nullptr;
    }

    return hResult;
}

void   SampleCurve(
    const std::vector<DVector2f>& vRed,
    const std::vector<DVector2f>& vGreen,
    const std::vector<DVector2f>& vBlue,
    std::vector<DVector3f>& vData
)
{
    const int n = 256;
    const std::vector<DVector2f>* vpPts[] = {
        &vRed,
        &vGreen,
        &vBlue
    };

    for (int i = 0; i < 3; ++i)
    {
        double step = 1.0 / (double)n;
        int size = (int)vpPts[i]->size();

        double* x = new double[size];
        double* y = new double[size];

        int j = 0;
        double s[5];

        for (int s = 0; s < size; ++s)
        {
            x[s] = (*vpPts[i])[s].x;
            y[s] = (*vpPts[i])[s].y;
        }

        for (double curr = 0.0; curr <= 1.0 && j < n - 1; curr += step)
        {
            if (i == 0)
            {
                vData[j++].x = max(0.0, min(1.0, CubicSplineIntl(x, y, size, -1, curr, s)));
            }
            else if (i == 1)
            {
                vData[j++].y = max(0.0, min(1.0, CubicSplineIntl(x, y, size, -1, curr, s)));
            }
            else
            {
                vData[j++].z = max(0.0, min(1.0, CubicSplineIntl(x, y, size, -1, curr, s)));
            }
        }

        delete[] x;
        delete[] y;
    }
}

double CubicSplineIntl(double x[], double y[], int n, int k, double t, double s[5])
{
    int kk, m, l;
    double u[5], p, q;
    s[4] = 0.0; s[0] = 0.0; s[1] = 0.0; s[2] = 0.0; s[3] = 0.0;
    if (n < 1) return s[4];
    if (n == 1) { s[0] = y[0]; s[4] = y[0]; return s[4]; }
    if (n == 2)
    {
        s[0] = y[0]; s[1] = (y[1] - y[0]) / (x[1] - x[0]);
        if (k < 0)
            s[4] = (y[0] * (t - x[1]) - y[1] * (t - x[0])) / (x[0] - x[1]);
        return s[4];
    }
    if (k < 0)
    {
        if (t <= x[1]) kk = 0;
        else if (t >= x[n - 1]) kk = n - 2;
        else
        {
            kk = 1; m = n;
            while (((kk - m) != 1) && ((kk - m) != -1))
            {
                l = (kk + m) / 2;
                if (t < x[l - 1]) m = l;
                else kk = l;
            }
            kk = kk - 1;
        }
    }
    else kk = k;
    if (kk >= n - 1) kk = n - 2;
    u[2] = (y[kk + 1] - y[kk]) / (x[kk + 1] - x[kk]);
    if (n == 3)
    {
        if (kk == 0)
        {
            u[3] = (y[2] - y[1]) / (x[2] - x[1]);
            u[4] = 2.0*u[3] - u[2];
            u[1] = 2.0*u[2] - u[3];
            u[0] = 2.0*u[1] - u[2];
        }
        else
        {
            u[1] = (y[1] - y[0]) / (x[1] - x[0]);
            u[0] = 2.0*u[1] - u[2];
            u[3] = 2.0*u[2] - u[1];
            u[4] = 2.0*u[3] - u[2];
        }
    }
    else
    {
        if (kk <= 1)
        {
            u[3] = (y[kk + 2] - y[kk + 1]) / (x[kk + 2] - x[kk + 1]);
            if (kk == 1)
            {
                u[1] = (y[1] - y[0]) / (x[1] - x[0]);
                u[0] = 2.0*u[1] - u[2];
                if (n == 4) u[4] = 2.0*u[3] - u[2];
                else u[4] = (y[4] - y[3]) / (x[4] - x[3]);
            }
            else
            {
                u[1] = 2.0*u[2] - u[3];
                u[0] = 2.0*u[1] - u[2];
                u[4] = (y[3] - y[2]) / (x[3] - x[2]);
            }
        }
        else if (kk >= (n - 3))
        {
            u[1] = (y[kk] - y[kk - 1]) / (x[kk] - x[kk - 1]);
            if (kk == (n - 3))
            {
                u[3] = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);
                u[4] = 2.0*u[3] - u[2];
                if (n == 4) u[0] = 2.0*u[1] - u[2];
                else u[0] = (y[kk - 1] - y[kk - 2]) / (x[kk - 1] - x[kk - 2]);
            }
            else
            {
                u[3] = 2.0*u[2] - u[1];
                u[4] = 2.0*u[3] - u[2];
                u[0] = (y[kk - 1] - y[kk - 2]) / (x[kk - 1] - x[kk - 2]);
            }
        }
        else
        {
            u[1] = (y[kk] - y[kk - 1]) / (x[kk] - x[kk - 1]);
            u[0] = (y[kk - 1] - y[kk - 2]) / (x[kk - 1] - x[kk - 2]);
            u[3] = (y[kk + 2] - y[kk + 1]) / (x[kk + 2] - x[kk + 1]);
            u[4] = (y[kk + 3] - y[kk + 2]) / (x[kk + 3] - x[kk + 2]);
        }
    }

    s[0] = fabs(u[3] - u[2]);
    s[1] = fabs(u[0] - u[1]);

    if ((s[0] + 1.0 == 1.0) && (s[1] + 1.0 == 1.0))
        p = (u[1] + u[2]) / 2.0;
    else p = (s[0] * u[1] + s[1] * u[2]) / (s[0] + s[1]);

    s[0] = fabs(u[3] - u[4]);
    s[1] = fabs(u[2] - u[1]);

    if ((s[0] + 1.0 == 1.0) && (s[1] + 1.0 == 1.0))
        q = (u[2] + u[3]) / 2.0;
    else q = (s[0] * u[2] + s[1] * u[3]) / (s[0] + s[1]);

    s[0] = y[kk];
    s[1] = p;
    s[3] = x[kk + 1] - x[kk];
    s[2] = (3.0*u[2] - 2.0*p - q) / s[3];
    s[3] = (q + p - 2.0*u[2]) / (s[3] * s[3]);

    if (k < 0)
    {
        p = t - x[kk];
        s[4] = s[0] + s[1] * p + s[2] * p*p + s[3] * p*p*p;
    }

    return s[4];
}

RC_NAMESPACE_END