/*
filename:       rceffectnodepy.h
author:         Ming Dong
date:           2016-SEP-22
description:
*/
#pragma once

#include <rc/public/rc.h>

#define DOME_USE_PYTHONSCRIPTNODE

#ifdef DOME_USE_PYTHONSCRIPTNODE

#include <python/Python.h>

RC_NAMESPACE_BEGIN

class RCEffectNodePy : public RCEffectNode
{
public:
    struct PythonNodeModule
    {
        DHashString         m_ScriptCode;
        PyObject*           m_pScriptModule;
        PyObject*           m_pFinishLoadFunc;
        PyObject*           m_pBuildMDStackFunc;
    };
    typedef TMap<DHashString, PythonNodeModule>     PythonNodeModuleMap;
    static PythonNodeModuleMap*                     s_pPythonNodeModuleMap;
    static DResult          PythonInit();
    static DResult          PythonDeinit();
    static void             CreatePythonNodeModule(const DHashString& i_ModuleName, const DHashString& i_ScriptCode, Bool i_bForceCreate = DM_FALSE);
    static PythonNodeModule* GetPythonNodeModule(const DHashString& i_ModuleName);

    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCEffectNodePy(RCEffect* i_pEffect);
    virtual ~RCEffectNodePy();

    // functions should be called in finishLoad
    void                    addOperandDString();
    void                    addOperandInt();
    void                    addOperandDVector2i();
    void                    addOperandDVector3i();
    void                    addOperandDVector4i();
    void                    addOperandF32();
    void                    addOperandDVector2f();
    void                    addOperandDVector3f();
    void                    addOperandDVector4f();
    void                    addOperandDMatrix2x2f();
    void                    addOperandDMatrix3x3f();
    void                    addOperandDMatrix4x4f();
    void                    addOperandConstTexture(const DString& i_TexPath);

    // functions can be called in finishLoad and buildMDStack
    void                    setOperand              (Int i_Index, const DSimpleTypedValue& i_Value);
    void                    setOperandDString       (Int i_Index, const Char* i_pString);
    void                    setOperandInt           (Int i_Index, Int i_Value);
    void                    setOperandDVector2i     (Int i_Index, const DVector2i& i_Value);
    void                    setOperandDVector3i     (Int i_Index, const DVector3i& i_Value);
    void                    setOperandDVector4i     (Int i_Index, const DVector4i& i_Value);
    void                    setOperandF32           (Int i_Index, F32 i_Value);
    void                    setOperandDVector2f     (Int i_Index, const DVector2f& i_Value);
    void                    setOperandDVector3f     (Int i_Index, const DVector3f& i_Value);
    void                    setOperandDVector4f     (Int i_Index, const DVector4f& i_Value);
    void                    setOperandDMatrix2x2f   (Int i_Index, const DMatrix2x2f& i_Value);
    void                    setOperandDMatrix3x3f   (Int i_Index, const DMatrix3x3f& i_Value);
    void                    setOperandDMatrix4x4f   (Int i_Index, const DMatrix4x4f& i_Value);

    // functions should be called in buildMDStack
    void                    executePushInputPy      (Int i_Index);
    void                    pushOperandPy           (Int i_Index);
    void                    pushCpuOperatorPy       (const Char* i_pOperatorName);
    void                    pushGpuOperatorPy       (const Char* i_pOperatorName);
    void                    pushGpuOperatorPy       (const Char* i_pOperatorName, Int i_Width, Int i_Height);
    void                    pushGpuOperatorPy       (const Char* i_pOperatorName, const Char* i_pFormat);
    void                    pushGpuOperatorPy       (const Char* i_pOperatorName, Int i_Width, Int i_Height, const Char* i_pFormat);
    Int                     markTopOperandPy        ();
    void                    popTopOperandPy         ();
    void                    pushMarkerPy            (Int i_Marker);
    void                    cacheResultPy           (Int i_Index);


public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector) DOME_OVERRIDE;
    virtual void            customizeLoad(const rapidxml::xml_node<>* i_pXmlNode) DOME_OVERRIDE;
    virtual void            finishLoad() DOME_OVERRIDE;

private:
    DHashString             m_ScriptCode;

    TArray<MDOperand*>      m_OperandArray;

    MDEffect*               m_pStack;
    TArray<MDOperand*>      m_MarkerArray;
};

RC_NAMESPACE_END

#endif