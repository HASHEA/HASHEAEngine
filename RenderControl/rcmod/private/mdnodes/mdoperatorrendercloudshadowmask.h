#pragma once
#include<rc/public/rc.h>
#include"../../public/rcmod.h"

RC_NAMESPACE_BEGIN

class MDOperatorRenderCloudShadowMask :public MDOperatorCpu
{
public:
	MDOperatorRenderCloudShadowMask();
	virtual ~MDOperatorRenderCloudShadowMask();

	virtual const DString& getOperatorName()const DOME_OVERRIDE;
	virtual Bool isGpuOperator()const DOME_OVERRIDE;
	virtual Int getInputCount()const DOME_OVERRIDE;
	virtual DSimpleTypeID getInputTypeID(Int i_Index)const DOME_OVERRIDE;

	virtual Int getOutputCount()const DOME_OVERRIDE;
	virtual DSimpleTypeID getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const DOME_OVERRIDE;
	virtual RCGPUDATAFORMAT getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const DOME_OVERRIDE;
	virtual DResult calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const DOME_OVERRIDE;

	virtual MDOperandPtr execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP*o_pInputReleasePoint)const DOME_OVERRIDE;
	virtual DResult destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult)const DOME_OVERRIDE;

private:
	mutable DString m_OutputTypeName;
	DString m_OperatorName;

};

RC_NAMESPACE_END