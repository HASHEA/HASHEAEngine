/*
    filename:       mdoperationgpu_gpuutil.h
    author:         Ming Dong
    date:           2016-JUN-14
    description:    
*/
#pragma once

#include "../public/mdoperationgpu.h"

DOME_NAMESPACE_BEGIN

static const Int k_MaxTexture = 16;
static const Int k_MaxTexture3D = 4;
static const Int k_MaxTextureCube = 2;
static const Int k_MaxFloat4 = 1024;
namespace GpuUtil
{
    typedef TArray<const MDOperationGpu*, IDefaultMemManager, 16>       GpuOperationArray;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 16>       GpuTexArray;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 16>       GpuTex3DArray;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 16>       GpuTexCubeArray;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 16>       GpuMatrix44Array;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 16>       GpuMatrix33Array;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 16>       GpuMatrix22Array;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 64>       GpuFloat4Array;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 64>       GpuFloat3Array;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 128>      GpuFloat2Array;
    typedef TArray<MDOperandCPtr, IDefaultMemManager, 256>      GpuFloat1Array;


    struct GpuData
    {
        Int                             m_DataType;     // 0:Operand  1:Operation

        GpuData(Int i_DataType)
        {
            m_DataType = i_DataType;
        }

        Bool        isOperand() const
        {
            return m_DataType == 0;
        }

        Bool        isOperation() const
        {
            return m_DataType == 1;
        }
    };

    struct GpuOperand : public GpuData
    {
        const MDOperand*                m_pOperand;

        GpuOperand()
            :GpuData(0)
        {
            m_pOperand = DM_NULL;
        }
    };

    struct GpuResourceContext
    {
        GpuResourceContext()
        {
            m_NumTextureUsed = 0;
            m_NumTexture3DUsed = 0;
            m_NumTextureCubeUsed = 0;
            m_NumMatrix4x4Used = 0;
            m_NumMatrix3x3Used = 0;
            m_NumMatrix2x2Used = 0;
            m_NumFloat4Used = 0;
            m_NumFloat3Used = 0;
            m_NumFloat2Used = 0;
            m_NumFloat1Used = 0;
        }

        TArray<const MDOperationGpu*>   m_MergedOperationArray;
        TArray<const MDOperand*>        m_MergedTextureArray;
        Int                             m_NumTextureUsed;
        Int                             m_NumTexture3DUsed;
        Int                             m_NumTextureCubeUsed;
        Int                             m_NumMatrix4x4Used;
        Int                             m_NumMatrix3x3Used;
        Int                             m_NumMatrix2x2Used;
        Int                             m_NumFloat4Used;
        Int                             m_NumFloat3Used;
        Int                             m_NumFloat2Used;
        Int                             m_NumFloat1Used;
    };

    struct GpuCompileResult
    {
        DString                         m_ShaderSignature;
        GpuOperationArray               m_OperationArray;
        GpuTexArray                     m_TextureArray;
        GpuTex3DArray                   m_Texture3DArray;
        GpuTexCubeArray                 m_TextureCubeArray;
        GpuMatrix44Array                m_Matrix4x4Array;
        GpuMatrix33Array                m_Matrix3x3Array;
        GpuMatrix22Array                m_Matrix2x2Array;
        GpuFloat4Array                  m_Float4Array;
        GpuFloat3Array                  m_Float3Array;
        GpuFloat2Array                  m_Float2Array;
        GpuFloat1Array                  m_Float1Array;
    };

    struct GpuOperation : GpuData
    {
        GpuOperation()
            :GpuData(1)
        {
            m_pOperation = DM_NULL;
            m_Complexity = 0;
        }

        ~GpuOperation()
        {
            for (Int i = 0; i < m_OperandArray.size(); ++i)
            {
                if(m_OperandArray[i]->isOperand())
                {
                    DOME_Del(m_OperandArray[i]);
                }
            }
        }

        Int         isTextureInArray(MDOperandCPtr i_pTexOperand, const MDOperandCPtr* i_pTexOperandArray, Int i_ArraySize) const
        {
            for (Int i = 0; i < i_ArraySize; ++i)
            {
                if (i_pTexOperand->isTexture() && i_pTexOperandArray[i]->isTexture())
                {
                    if (i_pTexOperand == i_pTexOperandArray[i])
                        return i;

                    const OSTexture2D* l_pTexture0 = i_pTexOperand->getTexturePtr();
                    const OSTexture2D* l_pTexture1 = i_pTexOperandArray[i]->getTexturePtr();
                    if (l_pTexture0 && l_pTexture1)
                    {
                        if (*l_pTexture0 == *l_pTexture1)
                            return i;
                    }

                }
            }
            return -1;
        }

        DResult     calcResourceNeeded(GpuResourceContext* o_pContext) const
        {
            DResult     l_Result = R_SUCCESS;
            // 1) check if this operation is already in the list
            //    if it is already in the list, it will cost nothing
            for (Int i = 0; i < o_pContext->m_MergedOperationArray.size(); ++i)
            {
                if(m_pOperation == o_pContext->m_MergedOperationArray[i])
                    return R_SUCCESS;
            }

            // 2) calculate resource cost for each operand
            for (Int i = 0; i < m_OperandArray.size(); ++i)
            {
                GpuData* l_pGpuData = m_OperandArray[i];
                if (l_pGpuData->isOperand())
                {
                    // if the parameter is a operand
                    const MDOperand* l_pOperand = ((GpuOperand*)l_pGpuData)->m_pOperand;
                    if (l_pOperand->isTexture())
                    {
                        Int l_FoundTex = -1;
                        if (o_pContext->m_MergedTextureArray.size() > 0)
                            l_FoundTex = isTextureInArray(l_pOperand, &o_pContext->m_MergedTextureArray[0], o_pContext->m_MergedTextureArray.size());

                        if (l_FoundTex < 0)
                        {
                            o_pContext->m_MergedTextureArray.push_back(l_pOperand);
                            o_pContext->m_NumTextureUsed++;
                        }
                    }
                    else if (l_pOperand->isTexture3D())
                        o_pContext->m_NumTexture3DUsed++;
                    else if (l_pOperand->isTextureCube())
                        o_pContext->m_NumTextureCubeUsed++;
                    else if(l_pOperand->isMatrix4x4())
                        o_pContext->m_NumMatrix4x4Used ++;
                    else if(l_pOperand->isMatrix3x3())
                        o_pContext->m_NumMatrix3x3Used ++;
                    else if(l_pOperand->isMatrix2x2())
                        o_pContext->m_NumMatrix2x2Used ++;
                    else if(l_pOperand->isFloat4())
                        o_pContext->m_NumFloat4Used ++;
                    else if(l_pOperand->isFloat3())
                        o_pContext->m_NumFloat3Used ++;
                    else if(l_pOperand->isFloat2())
                        o_pContext->m_NumFloat2Used ++;
                    else if(l_pOperand->isFloat())
                        o_pContext->m_NumFloat1Used ++;
                    else
                    {
                        DOME_ASSERT(0);
                    }
                }
                else if (l_pGpuData->isOperation())
                {
                    GpuOperation* l_pGpuOperation = ((GpuOperation*)l_pGpuData);
                    l_Result = l_pGpuOperation->calcResourceNeeded(o_pContext);
                    if(DM_FAIL(l_Result))
                        return l_Result;
                }
                else
                {
                    DOME_ASSERT(0);
                }
            }

            // 3) push this operation to the merged operation list
            o_pContext->m_MergedOperationArray.push_back(m_pOperation);

            return R_SUCCESS;
        }

        DResult     compile(GpuCompileResult& o_Result, Bool i_bFirst = DM_TRUE)
        {
            DResult l_Result = R_SUCCESS;

            // first, check if this operation already merged
            for (Int iOperation = 0; iOperation < o_Result.m_OperationArray.size(); ++iOperation)
            {
                if(m_pOperation == o_Result.m_OperationArray[iOperation])
                {
                    Char l_Buff[32];
                    if (i_bFirst)
                    {
                        OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$REF[%d]", iOperation);
                    }
                    else
                    {
                        OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$REF[%d]", iOperation);
                    }
                    o_Result.m_ShaderSignature += l_Buff;
                    return l_Result;
                }
            }

            if(!i_bFirst)
                o_Result.m_ShaderSignature += ",";
            o_Result.m_ShaderSignature += m_pOperation->getOperator()->getOperatorName();
            o_Result.m_ShaderSignature += "(";

            for (Int iop = 0; iop < m_OperandArray.size(); ++iop)
            {
                GpuData* l_pGpuData = m_OperandArray[iop];
                if (l_pGpuData->isOperand())
                {
                    const MDOperand* l_pOperand = ((GpuOperand*)l_pGpuData)->m_pOperand;
                    if (l_pOperand->isTexture())
                    {
                        Int l_FoundTex = -1;
                        if (o_Result.m_TextureArray.size() > 0)
                            l_FoundTex = isTextureInArray(l_pOperand, &o_Result.m_TextureArray[0], o_Result.m_TextureArray.size());

                        if (l_FoundTex < 0)
                        {
                            o_Result.m_TextureArray.push_back(l_pOperand);
                            l_FoundTex = o_Result.m_TextureArray.size() - 1;
                        }

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$TEX[%d]", l_FoundTex);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$TEX[%d]", l_FoundTex);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isTexture3D())
                    {
                        o_Result.m_Texture3DArray.push_back(l_pOperand);

                        Char l_Buff[32];
                        if (iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$T3D[%d]", o_Result.m_Texture3DArray.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$T3D[%d]", o_Result.m_Texture3DArray.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isTextureCube())
                    {
                        o_Result.m_TextureCubeArray.push_back(l_pOperand);

                        Char l_Buff[32];
                        if (iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$TCB[%d]", o_Result.m_TextureCubeArray.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$TCB[%d]", o_Result.m_TextureCubeArray.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isMatrix4x4())
                    {
                        o_Result.m_Matrix4x4Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$M44[%d]", o_Result.m_Matrix4x4Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$M44[%d]", o_Result.m_Matrix4x4Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isMatrix3x3())
                    {
                        o_Result.m_Matrix3x3Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$M33[%d]", o_Result.m_Matrix3x3Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$M33[%d]", o_Result.m_Matrix3x3Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isMatrix2x2())
                    {
                        o_Result.m_Matrix2x2Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$M22[%d]", o_Result.m_Matrix2x2Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$M22[%d]", o_Result.m_Matrix2x2Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isFloat4())
                    {
                        o_Result.m_Float4Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$F4[%d]", o_Result.m_Float4Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$F4[%d]", o_Result.m_Float4Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isFloat3())
                    {
                        o_Result.m_Float3Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$F3[%d]", o_Result.m_Float3Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$F3[%d]", o_Result.m_Float3Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isFloat2())
                    {
                        o_Result.m_Float2Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$F2[%d]", o_Result.m_Float2Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$F2[%d]", o_Result.m_Float2Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else if (l_pOperand->isFloat())
                    {
                        o_Result.m_Float1Array.push_back(l_pOperand);

                        Char l_Buff[32];
                        if(iop != 0)
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), ",$F1[%d]", o_Result.m_Float1Array.size() - 1);
                        else
                            OS_String::StrFormat(l_Buff, sizeof(l_Buff), "$F1[%d]", o_Result.m_Float1Array.size() - 1);
                        o_Result.m_ShaderSignature += l_Buff;
                    }
                    else
                    {
                        // shouldn't be here.
                        DOME_ASSERT(0);
                    }
                }
                else if (l_pGpuData->isOperation())
                {
                    l_Result = ((GpuOperation*)l_pGpuData)->compile(o_Result, iop == 0);
                    DOME_ASSERT(DM_SUCC(l_Result));
                }
                else
                {
                    DOME_ASSERT(0);
                }
            }

            o_Result.m_ShaderSignature += ")";
            o_Result.m_OperationArray.push_back(m_pOperation);

            return R_SUCCESS;
        }

        const MDOperationGpu*           m_pOperation;
        Int                             m_Complexity;
        TArray<GpuData*>                m_OperandArray;
    };
}

struct MDOperationGpu::GpuCompileData
{
    GpuUtil::GpuOperation           m_GpuOperation;
    GpuUtil::GpuCompileResult       m_CompiledResult;
};


DOME_NAMESPACE_END
