/*
    filename:       mdoperation.h
    author:         Ming Dong
    date:           2016-APR-07
    description:    
*/
#pragma once

#include "mdoperand.h"
#include "rcrenderer.h"

DOME_NAMESPACE_BEGIN

class MDOperator;
class MDEffect;
class RC_API MDOperation : public MDOperand
{
public:
    MDOperation(MDEffect* i_pMDEffect)
        :m_pMDEffect(i_pMDEffect) 
        ,m_bStandalone(DM_FALSE)
        ,m_bCompiled(DM_FALSE)
        ,m_RefCount(0)
        ,m_UsedCount(0)
    {}
    virtual ~MDOperation(){}

    virtual Bool                        isOperation() const{return DM_TRUE;}

    virtual Bool                        isGpu() const = 0;

    virtual DResult                     compile() = 0;
    virtual DResult                     postCompile() = 0;

    virtual DResult                     execute() = 0;
    virtual DResult                     finishCallback() = 0;

public:
    Int                                 addRefCount(){m_RefCount++; return m_RefCount;}
    Int                                 getRefCount() const {return m_RefCount;}

    void                                setStandalone() {m_bStandalone = DM_TRUE;}
    Bool                                isStandalone() const {return m_bStandalone;}

    Int                                 addUsedCount(){m_UsedCount++; return m_UsedCount;}
    Int                                 subUsedCount(){m_UsedCount--; return m_UsedCount;}
    Int                                 getUsedCount() const {return m_UsedCount;}

protected:
    MDEffect*                           m_pMDEffect;
    Bool                                m_bStandalone;
    Bool                                m_bCompiled;
    Int                                 m_RefCount;
    Int                                 m_UsedCount;
};


DOME_NAMESPACE_END