/*
    filename:       mdoperationcpu.h
    author:         Ming Dong
    date:           2016-APR-22
    description:    
*/
#pragma once

#include "mdoperation.h"
#include "mdoperator.h"

DOME_NAMESPACE_BEGIN

class MDOperatorCpu;
class RC_API MDOperationCpu : public MDOperation
{
public:
    MDOperationCpu(MDEffect* i_pMDEffect, const MDOperatorCpu* i_pOperator, Int i_ParamCount, const MDOperandPtr* i_ParamArray);
    virtual ~MDOperationCpu();

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

    virtual DResult                     compile();
    virtual DResult                     postCompile();

    virtual DResult                     execute();
    virtual DResult                     finishCallback();

private:
    const MDOperatorCpu*                m_pOperator;
    Int                                 m_ParamCount;
    MDOperandPtr*                       m_ParamArray;
    MDOperator::IRP*                    m_ParamIRPArray;


#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( push )
#  pragma warning( disable: 4251 )
#endif
    struct _OutputInfo
    {
        Bool                            m_bTextureResult;
        RCGPUDATAFORMAT                 m_TexResultFmt;
        DVector2i                       m_TexResultSize;
    };
    Int                                 m_OutputCount;
    _OutputInfo*                        m_OutputInfoArray;
#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( pop )
#endif

    MDOperand*                          m_pResult;
};



DOME_NAMESPACE_END