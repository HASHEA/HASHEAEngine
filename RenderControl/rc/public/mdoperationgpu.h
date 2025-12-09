/*
    filename:       mdoperationgpu.h
    author:         Ming Dong
    date:           2016-APR-22
    description:    
*/
#pragma once

#include "mdoperation.h"
#include "mdoperatorgpu.h"

DOME_NAMESPACE_BEGIN

class MDOperationGpu_MetaInfo
{
public:

};

struct MDOperationGpu_Parameter
{
    MDOperandPtr        m_pOperand;
    Bool                m_bOperation;
    Bool                m_MustBeMerged;
    Bool                m_CanBeMerged;
    Bool                m_bMerged;
};

namespace GpuUtil
{
    struct GpuOperation;
}

class RC_API MDOperationGpu : public MDOperation
{
public:
    MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator, 
                        Int i_ParamCount, const MDOperandPtr* i_ParamArray, 
                        const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator, 
                        Int i_ParamCount, const MDOperandPtr* i_ParamArray, 
                        Int i_Width, Int i_Height, 
                        const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator,
                        Int i_ParamCount, const MDOperandPtr* i_ParamArray,
                        RCGPUDATAFORMAT i_Format,
                        const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator,
                        Int i_ParamCount, const MDOperandPtr* i_ParamArray,
                        Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format,
                        const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    virtual ~MDOperationGpu();

    // functions from MDOperand class
    virtual Bool                        isOperation() const DOME_OVERRIDE;
    virtual Int                         getDataCount() const DOME_OVERRIDE;
    virtual DSimpleTypeID               getDataType(Int i_Index) const DOME_OVERRIDE;
    // if getDataCount() > 1, call the following functions to get data
    virtual const MDOperand*            getSubOperand(Int i_Index) const DOME_OVERRIDE;
    virtual MDOperand*                  getSubOperand(Int i_Index) DOME_OVERRIDE;
    // if getDataCount() == 1, call the following functions to get data
    virtual const DSimpleTypedValue*    getDataPtr() const DOME_OVERRIDE;
    virtual DSimpleTypedValue*          getDataPtr() DOME_OVERRIDE;
    // special functions for texture data
    virtual RCGPUDATAFORMAT             getTexDataFmt(Int i_Index) const DOME_OVERRIDE;
    virtual DVector2i                   getTexDataSize(Int i_Index) const DOME_OVERRIDE;

    // functions from MDOperation
    virtual Bool                        isGpu() const;

/******************************************************************************
    util functions for gpu operation compiling BEGIN
******************************************************************************/
private:
    struct GpuCompileData;
    GpuCompileData*                     m_pCompiledData;
    void                                initCompiledData();
    void                                uninitCompiledData();
    Bool                                mustBeMerged() const;
    Bool                                canMergeOperation(Int i_Index, const MDOperation* i_pOperation) const;
    Bool                                shouldMergeOperation(Int i_Index, const MDOperation* i_pOperation) const;
    DResult                             buildGpuOperation(GpuUtil::GpuOperation& o_GpuOperation) const;
    DResult                             tryMerge(Int& o_nTexture, Int& o_nTexture3D, Int& o_nTextureCube, Int & o_nFloat4) const;

    GpuCompileData*                     getCompileResult();
public:
    const MDOperatorGpu*                getOperator() const;
    virtual DResult                     compile();
    virtual DResult                     postCompile();
/******************************************************************************
    util functions for gpu operation compiling END
******************************************************************************/

    virtual DResult                     execute();
    virtual DResult                     finishCallback();

private:
    const MDOperatorGpu*                m_pOperator;
    Int                                 m_ParamCount;
    MDOperationGpu_Parameter*           m_ParamArray;
    MDOperandPtr*                       m_ParamOperandArray;
    const MDOperationGpu_MetaInfo*      m_pMetaInfo;

#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( push )
#  pragma warning( disable: 4251 )
#endif
    RCGPUDATAFORMAT                     m_TexResultFmt;
    DVector2i                           m_ResultSize;
#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( pop )
#endif

    MDOperand*                          m_pResult;
};



DOME_NAMESPACE_END