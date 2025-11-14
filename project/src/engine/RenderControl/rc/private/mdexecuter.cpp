/*
	filename:       mdexecuter.cpp
	author:         Ming Dong
	date:           2016-JUN-04
	description:
*/

#include "mdexecuter.h"
#include "../public/rceffectmanager.h"
#include "../../rcmod/public/rcmod.h"
#include "KGCommon/Publish/Include/KG_ProfileTool.h"
RC_NAMESPACE_BEGIN

MDExecuter::MDExecuter(RCEffectManager* i_pEffectManager, const DHashString& i_Signature)
{
	m_pEffectManager = i_pEffectManager;
	m_Signature = i_Signature;
	m_BlendMode = RBM_COPY;
	init();
}

MDExecuter::~MDExecuter()
{
	uninit();
}

Int     MDExecuter::getNumTextureParam() const
{
	return m_TextureParams.size();
}

Int     MDExecuter::getNumTexture3DParam() const
{
	return m_Texture3DParams.size();
}

Int     MDExecuter::getNumTextureCubeParam() const
{
	return m_TextureCubeParams.size();
}

Int     MDExecuter::getNumMatrix4x4Param() const
{
	return m_Matrix4x4Params.size();
}

Int     MDExecuter::getNumMatrix3x3Param() const
{
	return m_Matrix3x3Params.size();
}

Int     MDExecuter::getNumMatrix2x2Param() const
{
	return m_Matrix2x2Params.size();
}

Int     MDExecuter::getNumFloat4Param() const
{
	return m_Float4Params.size();
}

Int     MDExecuter::getNumFloat3Param() const
{
	return m_Float3Params.size();
}

Int     MDExecuter::getNumFloat2Param() const
{
	return m_Float2Params.size();
}

Int     MDExecuter::getNumFloat1Param() const
{
	return m_Float1Params.size();
}

DResult MDExecuter::setRenderTarget(OSTexture2D i_Rt)
{
	m_RenderTarget = i_Rt;
	return R_SUCCESS;
}

DResult MDExecuter::setRenderTargetViewport(Int x, Int y, Int width, Int height)
{
	m_Viewport2D.set(x, y, width, height);
	return R_SUCCESS;
}

DResult MDExecuter::setBlendMode(RCBLENDMODE i_Mode)
{
	m_BlendMode = i_Mode;
	return R_SUCCESS;
}

DResult MDExecuter::setUVCoef(const DVector4f& i_UVCoef)
{
	m_UVCoef = i_UVCoef;
	return R_SUCCESS;
}

DResult MDExecuter::setTextureParam(Int i_Index, OSTexture2D i_Texture)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumTextureParam());
	m_TextureParams[i_Index] = i_Texture;
	return R_SUCCESS;
}

DResult MDExecuter::setTexture3DParam(Int i_Index, OSTexture3D i_Texture)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumTexture3DParam());
	m_Texture3DParams[i_Index] = i_Texture;
	return R_SUCCESS;
}

DResult MDExecuter::setTextureCubeParam(Int i_Index, OSTextureCube i_Texture)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumTextureCubeParam());
	m_TextureCubeParams[i_Index] = i_Texture;
	return R_SUCCESS;
}

DResult MDExecuter::setMatrix4x4Param(Int i_Index, const DMatrix4x4f& i_Mat)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumMatrix4x4Param());
	m_Matrix4x4Params[i_Index] = i_Mat;
	return R_SUCCESS;
}

DResult MDExecuter::setMatrix3x3Param(Int i_Index, const DMatrix3x3f& i_Mat)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumMatrix3x3Param());
	m_Matrix3x3Params[i_Index] = i_Mat;
	return R_SUCCESS;
}

DResult MDExecuter::setMatrix2x2Param(Int i_Index, const DMatrix2x2f& i_Mat)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumMatrix2x2Param());
	m_Matrix2x2Params[i_Index] = i_Mat;
	return R_SUCCESS;
}

DResult MDExecuter::setFloat4Param(Int i_Index, const DVector4f& i_Val)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumFloat4Param());
	m_Float4Params[i_Index] = i_Val;
	return R_SUCCESS;
}

DResult MDExecuter::setFloat3Param(Int i_Index, const DVector3f& i_Val)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumFloat3Param());
	m_Float3Params[i_Index] = i_Val;
	return R_SUCCESS;
}

DResult MDExecuter::setFloat2Param(Int i_Index, const DVector2f& i_Val)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumFloat2Param());
	m_Float2Params[i_Index] = i_Val;
	return R_SUCCESS;
}

DResult MDExecuter::setFloat1Param(Int i_Index, F32 i_Val)
{
	DOME_ASSERT(i_Index >= 0 && i_Index < getNumFloat1Param());
	m_Float1Params[i_Index] = i_Val;
	return R_SUCCESS;
}

DResult MDExecuter::execute()
{
	MICROPROFILE_SCOPE_CPU_AND_GPU("GPU OPERATOR");
	RCRenderer* l_pRenderer = m_pEffectManager->getRenderer();
	DOME_ASSERT(l_pRenderer);
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)m_pEffectManager->getPlugin(k_KEY_ScenePlugin);
    if(l_pScenePlugin)
    {
        l_pScenePlugin->SetRCCommonParams();
    }

	l_pRenderer->ro_SetViewport(m_RO, m_Viewport2D.x, m_Viewport2D.y, m_Viewport2D.z, m_Viewport2D.w, 0.0f, 1.0f);
	l_pRenderer->ro_SetRS_EnableDepthTest(m_RO);
	l_pRenderer->ro_SetRS_EnableDepthWrite(m_RO);

	l_pRenderer->ro_SetRenderTarget(m_RO, 0, m_RenderTarget);
	l_pRenderer->ro_SetVertexShader(m_RO, l_pRenderer->getFullscreenVS());
	l_pRenderer->ro_SetPixelShader(m_RO, m_PixelShader);
	l_pRenderer->ro_SetVertexBuffer(m_RO, l_pRenderer->getFullscreenVB());

	DVector4f* l_pVSParams = (DVector4f*)l_pRenderer->lockConstBuffer(m_VSParams, RTLS_WRITEONLY);
	*l_pVSParams = m_UVCoef;
	l_pRenderer->unlockConstBuffer(m_VSParams);
	l_pRenderer->ro_SetVS_ConstBuffer(m_RO, 0, m_VSParams);

	// set blend mode
	l_pRenderer->ro_SetRS_BlendMode(m_RO, m_BlendMode);

	// cull mode
	l_pRenderer->ro_SetRS_CullMode(m_RO, RCM_CULL_NONE, DM_FALSE);

	if (m_PSParams.isValid())
	{
		DVector4f* l_pPSParams = (DVector4f*)l_pRenderer->lockConstBuffer(m_PSParams, RTLS_WRITEONLY);
		// for matrix4x4
		for (Int i = 0; i < m_Matrix4x4Params.size(); ++i)
		{
			(*l_pPSParams++) = m_Matrix4x4Params[i].axisX4();
			(*l_pPSParams++) = m_Matrix4x4Params[i].axisY4();
			(*l_pPSParams++) = m_Matrix4x4Params[i].axisZ4();
			(*l_pPSParams++) = m_Matrix4x4Params[i].translation4();
		}

		// for matrix3x3
		for (Int i = 0; i < m_Matrix3x3Params.size(); ++i)
		{
			(*l_pPSParams++) = m_Matrix3x3Params[i].axisX();
			(*l_pPSParams++) = m_Matrix3x3Params[i].axisY();
			(*l_pPSParams++) = m_Matrix3x3Params[i].axisZ();
		}

		// for matrix2x2
		for (Int i = 0; i < m_Matrix2x2Params.size(); ++i)
		{
			(*l_pPSParams++) = m_Matrix2x2Params[i].axisX();
			(*l_pPSParams++) = m_Matrix2x2Params[i].axisY();
		}

		// for float4
		for (Int i = 0; i < m_Float4Params.size(); ++i)
		{
			(*l_pPSParams++) = m_Float4Params[i];
		}

		// for float3
		for (Int i = 0; i < m_Float3Params.size(); ++i)
		{
			(*l_pPSParams++) = m_Float3Params[i];
		}

		// for float2
		for (Int i = 0; i < m_Float2Params.size(); i += 2)
		{
			DVector4f l_Value;
			l_Value.x = m_Float2Params[i].x;
			l_Value.y = m_Float2Params[i].y;
			if ((i + 1) < m_Float2Params.size())
			{
				l_Value.z = m_Float2Params[i + 1].x;
				l_Value.w = m_Float2Params[i + 1].y;
			}
			else
			{
				l_Value.z = 0.0f;
				l_Value.w = 0.0f;
			}
			(*l_pPSParams++) = l_Value;
		}

		// for float
		for (Int i = 0; i < m_Float1Params.size(); i += 4)
		{
			DVector4f l_Value;
			l_Value.x = m_Float1Params[i];
			if ((i + 1) < m_Float1Params.size())
			{
				l_Value.y = m_Float1Params[i + 1];
				if ((i + 2) < m_Float1Params.size())
				{
					l_Value.z = m_Float1Params[i + 2];
					if ((i + 3) < m_Float1Params.size())
						l_Value.w = m_Float1Params[i + 3];
				}
			}
			(*l_pPSParams++) = l_Value;
		}

		l_pRenderer->unlockConstBuffer(m_PSParams);
		l_pRenderer->ro_SetPS_ConstBuffer(m_RO, 0, m_PSParams);
	}

	for (Int i = 0; i < m_TextureParams.size(); ++i)
	{
		l_pRenderer->ro_SetPS_Texture(m_RO, i, m_TextureParams[i]);
	}

	for (Int i = 0; i < m_Texture3DParams.size(); ++i)
	{
		l_pRenderer->ro_SetPS_Texture3D(m_RO, i, m_Texture3DParams[i]);
	}

	for (Int i = 0; i < m_TextureCubeParams.size(); ++i)
	{
		l_pRenderer->ro_SetPS_TextureCube(m_RO, i, m_TextureCubeParams[i]);
	}

	return l_pRenderer->ro_Execute(m_RO);
}



RC_NAMESPACE_END