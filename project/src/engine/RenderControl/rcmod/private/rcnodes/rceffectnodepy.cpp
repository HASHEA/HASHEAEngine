#include "pch.h"
/*
filename:       rceffectnodepy.cpp
author:         Ming Dong
date:           2016-SEP-22
description:
*/
#include "../../public/rcmod.h"
#include "rceffectnodepy.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


#ifdef DOME_USE_PYTHONSCRIPTNODE

RC_NAMESPACE_BEGIN

typedef struct
{
    PyObject_HEAD
    RCEffectNodePy*     m_pEffectNode;
}PyEffectNode;

static PyTypeObject PyEffectNode_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "rc.RCEffectNodePy",       /* tp_name */
    sizeof(PyEffectNode),       /* tp_basicsize */
    0,                         /* tp_itemsize */
    0,                         /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "RC Effect Node In Python",           /* tp_doc */
};

typedef struct
{
    PyObject_HEAD
    DSimpleTypedValue       m_Value;
}PySimpleTypedValue;

static PyObject* PySimpleTypedValue_New(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PySimpleTypedValue* self;
    const Char* l_pTypeName = DM_NULL;

    if (args)
    {
        if (!PyArg_ParseTuple(args, "|s", &l_pTypeName))
            return NULL;
    }

    self = (PySimpleTypedValue*)type->tp_alloc(type, 0);
    DOME_NewPlacement(PySimpleTypedValue, self);
    if (l_pTypeName)
    {
        DSimpleTypeID l_TypeID(l_pTypeName);
        self->m_Value.initType(l_TypeID);
    }

    return (PyObject*)self;
}

static void PySimpleTypedValue_Dealloc(PySimpleTypedValue* self)
{
    self->m_Value.~DSimpleTypedValue();
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject PySimpleTypedValue_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "rc.DSimpleTypedValue",             /* tp_name */
    sizeof(PySimpleTypedValue),             /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)PySimpleTypedValue_Dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,         /* tp_flags */
    "DSimpleTypedValue In Python",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                          /* tp_methods */
    0,                          /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                          /* tp_init */
    0,                         /* tp_alloc */
    PySimpleTypedValue_New,    /* tp_new */
};


static PyObject* rcpy_test(PyObject* self, PyObject* args)
{
    const char* command;
    if (!PyArg_ParseTuple(args, "s", &command))
        return NULL;
    printf(command);
    return PyLong_FromLong(0);
}

static PyObject* RCPY_AddOperandDString(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDString();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandInt(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandInt();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDVector2i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDVector2i();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDVector3i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDVector3i();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDVector4i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDVector4i();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandF32(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandF32();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDVector2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDVector2f();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDVector3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDVector3f();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDVector4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDVector4f();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDMatrix2x2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDMatrix2x2f();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDMatrix3x3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDMatrix3x3f();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandDMatrix4x4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = PyTuple_GetItem(args, 0);
    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandDMatrix4x4f();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_AddOperandConstTexture(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    const Char* l_pTexPath = DM_NULL;
    if (!PyArg_ParseTuple(args, "Os", &l_pObj, &l_pTexPath))
        return DM_NULL;

    PyEffectNode* l_pNode = (PyEffectNode*)l_pObj;
    l_pNode->m_pEffectNode->addOperandConstTexture(DString(l_pTexPath));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperand(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    PySimpleTypedValue* l_pValue = 0;
    if (!PyArg_ParseTuple(args, "OiO", &l_pNode, &l_Index, &l_pValue))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperand(l_Index, l_pValue->m_Value);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDString(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    const char* l_pStrVal = DM_NULL;
    if (!PyArg_ParseTuple(args, "Ois", &l_pNode, &l_Index, &l_pStrVal))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDString(l_Index, l_pStrVal);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandInt(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    Int l_Value = 0;
    if (!PyArg_ParseTuple(args, "Oii", &l_pNode, &l_Index, &l_Value))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandInt(l_Index, l_Value);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDVector2i(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    Int v0 = 0, v1 = 0;
    if (!PyArg_ParseTuple(args, "Oi(ii)", &l_pNode, &l_Index, &v0, &v1))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDVector2i(l_Index, DVector2i(v0, v1));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDVector3i(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    Int v0 = 0, v1 = 0, v2 = 0;
    if (!PyArg_ParseTuple(args, "Oi(iii)", &l_pNode, &l_Index, &v0, &v1, &v2))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDVector3i(l_Index, DVector3i(v0, v1, v2));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDVector4i(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    Int v0 = 0, v1 = 0, v2 = 0, v3 = 0;
    if (!PyArg_ParseTuple(args, "Oi(iiii)", &l_pNode, &l_Index, &v0, &v1, &v2, &v3))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDVector4i(l_Index, DVector4i(v0, v1, v2, v3));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandF32(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    F32 l_Value = 0;
    if (!PyArg_ParseTuple(args, "Oif", &l_pNode, &l_Index, &l_Value))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandF32(l_Index, l_Value);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDVector2f(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    F32 v0 = 0, v1 = 0;
    if (!PyArg_ParseTuple(args, "Oi(ff)", &l_pNode, &l_Index, &v0, &v1))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDVector2f(l_Index, DVector2f(v0, v1));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDVector3f(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    F32 v0 = 0.0f, v1 = 0.0f, v2 = 0.0f;
    if (!PyArg_ParseTuple(args, "Oi(fff)", &l_pNode, &l_Index, &v0, &v1, &v2))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDVector3f(l_Index, DVector3f(v0, v1, v2));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDVector4f(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    F32 v0 = 0.0f, v1 = 0.0f, v2 = 0.0f, v3 = 0.0f;
    if (!PyArg_ParseTuple(args, "Oi(ffff)", &l_pNode, &l_Index, &v0, &v1, &v2, &v3))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDVector4f(l_Index, DVector4f(v0, v1, v2, v3));
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDMatrix2x2f(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    DMatrix2x2f m;
    if (!PyArg_ParseTuple(args, "Oi((ff)(ff))", &l_pNode, &l_Index, &m.M(0), &m.M(1), &m.M(2), &m.M(3)))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDMatrix2x2f(l_Index, m);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDMatrix3x3f(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    DMatrix3x3f m;
    if (!PyArg_ParseTuple(args, "Oi((fff)(fff)(fff))", &l_pNode, &l_Index, 
        &m.M(0), &m.M(1), &m.M(2), 
        &m.M(3), &m.M(4), &m.M(5),
        &m.M(6), &m.M(7), &m.M(8)))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDMatrix3x3f(l_Index, m);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_SetOperandDMatrix4x4f(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    DMatrix4x4f m;
    if (!PyArg_ParseTuple(args, "Oi((ffff)(ffff)(ffff)(ffff))", &l_pNode, &l_Index,
        &m.M(0), &m.M(1), &m.M(2), &m.M(3),
        &m.M(4), &m.M(5), &m.M(6), &m.M(7),
        &m.M(8), &m.M(9), &m.M(10), &m.M(11),
        &m.M(12), &m.M(13), &m.M(14), &m.M(15)))
        return DM_NULL;
    l_pNode->m_pEffectNode->setOperandDMatrix4x4f(l_Index, m);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_GetParameter(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pNode, &l_Index))
        return DM_NULL;

    DSimpleTypedValue* l_pSrcValue = l_pNode->m_pEffectNode->getParam(l_Index);
    DOME_ASSERT2(l_pSrcValue, "RCPY_GetParameter(%lld) in RCEffectNodePy::%s", l_Index, l_pNode->m_pEffectNode->getSubType().c_str());

    PySimpleTypedValue* l_pResult = (PySimpleTypedValue*)PySimpleTypedValue_New(&PySimpleTypedValue_Type, DM_NULL, DM_NULL);
    l_pResult->m_Value = *l_pSrcValue;
    return (PyObject*)l_pResult;
}

static PyObject* RCPY_IsInputConnected(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pNode, &l_Index))
        return DM_NULL;

    Bool l_bConnected = l_pNode->m_pEffectNode->isInputConnected(l_Index);
    return PyBool_FromLong(l_bConnected ? 1 : 0);
}

static PyObject* RCPY_ExecutePushInput(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pNode, &l_Index))
        return DM_NULL;

    l_pNode->m_pEffectNode->executePushInputPy(l_Index);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_PushOperand(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Index = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pNode, &l_Index))
        return DM_NULL;

    l_pNode->m_pEffectNode->pushOperandPy(l_Index);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_PushCpuOperator(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    const Char* l_pOperatorName = 0;
    if (!PyArg_ParseTuple(args, "Os", &l_pNode, &l_pOperatorName))
        return DM_NULL;

    l_pNode->m_pEffectNode->pushCpuOperatorPy(l_pOperatorName);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_PushGpuOperator(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    const Char* l_pOperatorName = 0;
    const Char* l_pFormat = 0;
    Int l_Width = 0;
    Int l_Height = 0;
    if (PyArg_ParseTuple(args, "Os", &l_pNode, &l_pOperatorName))
    {
        PyErr_Clear();
        l_pNode->m_pEffectNode->pushGpuOperatorPy(l_pOperatorName);
        Py_INCREF(Py_None);
        return Py_None;
    }
    else if (PyArg_ParseTuple(args, "Osii", &l_pNode, &l_pOperatorName, &l_Width, &l_Height))
    {
        PyErr_Clear();
        l_pNode->m_pEffectNode->pushGpuOperatorPy(l_pOperatorName, l_Width, l_Height);
        Py_INCREF(Py_None);
        return Py_None;
    }
    else if (PyArg_ParseTuple(args, "Oss", &l_pNode, &l_pOperatorName, &l_pFormat))
    {
        PyErr_Clear();
        l_pNode->m_pEffectNode->pushGpuOperatorPy(l_pOperatorName, l_pFormat);
        Py_INCREF(Py_None);
        return Py_None;
    }
    else if (PyArg_ParseTuple(args, "Osiis", &l_pNode, &l_pOperatorName, &l_Width, &l_Height, &l_pFormat))
    {
        PyErr_Clear();
        l_pNode->m_pEffectNode->pushGpuOperatorPy(l_pOperatorName, l_Width, l_Height, l_pFormat);
        Py_INCREF(Py_None);
        return Py_None;
    }
    return DM_NULL;
}

static PyObject* RCPY_MarkTopOperand(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pNode))
        return DM_NULL;

    Int l_Marker = l_pNode->m_pEffectNode->markTopOperandPy();
    return PyLong_FromLong(l_Marker);
}

static PyObject* RCPY_PopTopOperand(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pNode))
        return DM_NULL;

    l_pNode->m_pEffectNode->popTopOperandPy();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_PushMarker(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_Marker = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pNode, &l_Marker))
        return DM_NULL;

    l_pNode->m_pEffectNode->pushMarkerPy(l_Marker);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_CacheResult(PyObject* self, PyObject* args)
{
    PyEffectNode* l_pNode = DM_NULL;
    Int l_OutputSelector = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pNode, &l_OutputSelector))
        return DM_NULL;

    l_pNode->m_pEffectNode->cacheResultPy(l_OutputSelector);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_IsDSimpleTypedValue(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;

    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
        return PyBool_FromLong(1);
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_IsDString(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDString())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDString(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDString())
        {
            return PyUnicode_FromString(l_pValue->m_Value.getDString().c_str());
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDString(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    const Char* l_pString = DM_NULL;
    if (!PyArg_ParseTuple(args, "Os", &l_pObj, &l_pString))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDString())
        {
            l_pValue->m_Value.setDString(DString(l_pString));
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsInt(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isInt())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeInt(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isInt())
        {
            return PyLong_FromLong(l_pValue->m_Value.getInt());
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeInt(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    Int l_Value = 0;
    if (!PyArg_ParseTuple(args, "Oi", &l_pObj, &l_Value))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isInt())
        {
            l_pValue->m_Value.setInt(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDVector2i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector2i())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDVector2i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector2i())
        {
            DVector2i l_Value = l_pValue->m_Value.getDVector2i();
            PyObject* l_pTuple = PyTuple_New(2);
            PyTuple_SetItem(l_pTuple, 0, PyLong_FromLong(l_Value.x));
            PyTuple_SetItem(l_pTuple, 1, PyLong_FromLong(l_Value.y));
            return l_pTuple;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDVector2i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DVector2i l_Value(0);
    if (!PyArg_ParseTuple(args, "O(ii)", &l_pObj, &l_Value.x, &l_Value.y))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector2i())
        {
            l_pValue->m_Value.setDVector2i(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDVector3i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector3i())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDVector3i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector3i())
        {
            DVector3i l_Value = l_pValue->m_Value.getDVector3i();
            PyObject* l_pTuple = PyTuple_New(3);
            PyTuple_SetItem(l_pTuple, 0, PyLong_FromLong(l_Value.x));
            PyTuple_SetItem(l_pTuple, 1, PyLong_FromLong(l_Value.y));
            PyTuple_SetItem(l_pTuple, 2, PyLong_FromLong(l_Value.z));
            return l_pTuple;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDVector3i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DVector3i l_Value(0);
    if (!PyArg_ParseTuple(args, "O(iii)", &l_pObj, &l_Value.x, &l_Value.y, &l_Value.z))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector3i())
        {
            l_pValue->m_Value.setDVector3i(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDVector4i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector4i())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDVector4i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector4i())
        {
            DVector4i l_Value = l_pValue->m_Value.getDVector4i();
            PyObject* l_pTuple = PyTuple_New(4);
            PyTuple_SetItem(l_pTuple, 0, PyLong_FromLong(l_Value.x));
            PyTuple_SetItem(l_pTuple, 1, PyLong_FromLong(l_Value.y));
            PyTuple_SetItem(l_pTuple, 2, PyLong_FromLong(l_Value.z));
            PyTuple_SetItem(l_pTuple, 3, PyLong_FromLong(l_Value.w));
            return l_pTuple;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDVector4i(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DVector4i l_Value(0);
    if (!PyArg_ParseTuple(args, "O(iiii)", &l_pObj, &l_Value.x, &l_Value.y, &l_Value.z, &l_Value.w))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector4i())
        {
            l_pValue->m_Value.setDVector4i(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsF32(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isF32())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeF32(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isF32())
        {
            return PyFloat_FromDouble(l_pValue->m_Value.getF32());
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeF32(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    F32 l_Value = 0.0f;
    if (!PyArg_ParseTuple(args, "Of", &l_pObj, &l_Value))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isF32())
        {
            l_pValue->m_Value.setF32(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDVector2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector2f())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDVector2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector2f())
        {
            DVector2f l_Value = l_pValue->m_Value.getDVector2f();
            PyObject* l_pTuple = PyTuple_New(2);
            PyTuple_SetItem(l_pTuple, 0, PyFloat_FromDouble(l_Value.x));
            PyTuple_SetItem(l_pTuple, 1, PyFloat_FromDouble(l_Value.y));
            return l_pTuple;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDVector2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DVector2f l_Value(0.0f);
    if (!PyArg_ParseTuple(args, "O(ff)", &l_pObj, &l_Value.x, &l_Value.y))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector2f())
        {
            l_pValue->m_Value.setDVector2f(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDVector3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector3f())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDVector3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector3f())
        {
            DVector3f l_Value = l_pValue->m_Value.getDVector3f();
            PyObject* l_pTuple = PyTuple_New(3);
            PyTuple_SetItem(l_pTuple, 0, PyFloat_FromDouble(l_Value.x));
            PyTuple_SetItem(l_pTuple, 1, PyFloat_FromDouble(l_Value.y));
            PyTuple_SetItem(l_pTuple, 2, PyFloat_FromDouble(l_Value.z));
            return l_pTuple;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDVector3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DVector3f l_Value(0.0f);
    if (!PyArg_ParseTuple(args, "O(fff)", &l_pObj, &l_Value.x, &l_Value.y, &l_Value.z))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector3f())
        {
            l_pValue->m_Value.setDVector3f(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDVector4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector4f())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDVector4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector4f())
        {
            DVector4f l_Value = l_pValue->m_Value.getDVector4f();
            PyObject* l_pTuple = PyTuple_New(4);
            PyTuple_SetItem(l_pTuple, 0, PyFloat_FromDouble(l_Value.x));
            PyTuple_SetItem(l_pTuple, 1, PyFloat_FromDouble(l_Value.y));
            PyTuple_SetItem(l_pTuple, 2, PyFloat_FromDouble(l_Value.z));
            PyTuple_SetItem(l_pTuple, 3, PyFloat_FromDouble(l_Value.w));
            return l_pTuple;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDVector4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DVector4f l_Value(0.0f);
    if (!PyArg_ParseTuple(args, "O(ffff)", &l_pObj, &l_Value.x, &l_Value.y, &l_Value.z, &l_Value.w))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDVector4f())
        {
            l_pValue->m_Value.setDVector4f(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDMatrix2x2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix2x2f())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDMatrix2x2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix2x2f())
        {
            DMatrix2x2f l_Value = l_pValue->m_Value.getDMatrix2x2f();
            PyObject* l_pRow = DM_NULL;
            PyObject* l_pMat = PyTuple_New(2);
            l_pRow = PyTuple_New(2);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(0)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(1)));
            PyTuple_SetItem(l_pMat, 0, l_pRow);
            l_pRow = PyTuple_New(2);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(2)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(3)));
            PyTuple_SetItem(l_pMat, 1, l_pRow);
            return l_pMat;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDMatrix2x2f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DMatrix2x2f l_Value;
    if (!PyArg_ParseTuple(args, "O((ff)(ff))", &l_pObj, 
        &l_Value.M(0), &l_Value.M(1),
        &l_Value.M(2), &l_Value.M(3)
    ))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix2x2f())
        {
            l_pValue->m_Value.setDMatrix2x2f(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDMatrix3x3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix3x3f())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDMatrix3x3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix3x3f())
        {
            DMatrix3x3f l_Value = l_pValue->m_Value.getDMatrix3x3f();
            PyObject* l_pRow = DM_NULL;
            PyObject* l_pMat = PyTuple_New(3);
            l_pRow = PyTuple_New(3);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(0)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(1)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(2)));
            PyTuple_SetItem(l_pMat, 0, l_pRow);
            l_pRow = PyTuple_New(3);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(3)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(4)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(5)));
            PyTuple_SetItem(l_pMat, 1, l_pRow);
            l_pRow = PyTuple_New(3);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(6)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(7)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(8)));
            PyTuple_SetItem(l_pMat, 2, l_pRow);
            return l_pMat;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDMatrix3x3f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DMatrix3x3f l_Value;
    if (!PyArg_ParseTuple(args, "O((fff)(fff)(fff))", &l_pObj,
        &l_Value.M(0), &l_Value.M(1), &l_Value.M(2),
        &l_Value.M(3), &l_Value.M(4), &l_Value.M(5),
        &l_Value.M(6), &l_Value.M(7), &l_Value.M(8)
    ))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix3x3f())
        {
            l_pValue->m_Value.setDMatrix3x3f(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}

static PyObject* RCPY_IsDMatrix4x4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix4x4f())
            return PyBool_FromLong(1);
        else
            return PyBool_FromLong(0);
    }
    else
        return PyBool_FromLong(0);
}

static PyObject* RCPY_DecodeDMatrix4x4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    if (!PyArg_ParseTuple(args, "O", &l_pObj))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix4x4f())
        {
            DMatrix4x4f l_Value = l_pValue->m_Value.getDMatrix4x4f();
            PyObject* l_pRow = DM_NULL;
            PyObject* l_pMat = PyTuple_New(4);
            l_pRow = PyTuple_New(4);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(0)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(1)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(2)));
            PyTuple_SetItem(l_pRow, 3, PyFloat_FromDouble(l_Value.M(3)));
            PyTuple_SetItem(l_pMat, 0, l_pRow);
            l_pRow = PyTuple_New(4);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(4)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(5)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(6)));
            PyTuple_SetItem(l_pRow, 3, PyFloat_FromDouble(l_Value.M(7)));
            PyTuple_SetItem(l_pMat, 1, l_pRow);
            l_pRow = PyTuple_New(4);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(8)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(9)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(10)));
            PyTuple_SetItem(l_pRow, 3, PyFloat_FromDouble(l_Value.M(11)));
            PyTuple_SetItem(l_pMat, 2, l_pRow);
            l_pRow = PyTuple_New(4);
            PyTuple_SetItem(l_pRow, 0, PyFloat_FromDouble(l_Value.M(12)));
            PyTuple_SetItem(l_pRow, 1, PyFloat_FromDouble(l_Value.M(13)));
            PyTuple_SetItem(l_pRow, 2, PyFloat_FromDouble(l_Value.M(14)));
            PyTuple_SetItem(l_pRow, 3, PyFloat_FromDouble(l_Value.M(15)));
            PyTuple_SetItem(l_pMat, 3, l_pRow);
            return l_pMat;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* RCPY_EncodeDMatrix4x4f(PyObject* self, PyObject* args)
{
    PyObject* l_pObj = DM_NULL;
    DMatrix4x4f l_Value;
    if (!PyArg_ParseTuple(args, "O((ffff)(ffff)(ffff)(ffff))", &l_pObj,
        &l_Value.M(0), &l_Value.M(1), &l_Value.M(2), &l_Value.M(3),
        &l_Value.M(4), &l_Value.M(5), &l_Value.M(6), &l_Value.M(7),
        &l_Value.M(8), &l_Value.M(9), &l_Value.M(10), &l_Value.M(11),
        &l_Value.M(12), &l_Value.M(13), &l_Value.M(14), &l_Value.M(15)
    ))
        return DM_NULL;
    if (Py_TYPE(l_pObj) == &PySimpleTypedValue_Type)
    {
        PySimpleTypedValue* l_pValue = (PySimpleTypedValue*)l_pObj;
        if (l_pValue->m_Value.isDMatrix4x4f())
        {
            l_pValue->m_Value.setDMatrix4x4f(l_Value);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return DM_NULL;
}


static PyMethodDef RCMODMethods[] = {
    { "rcpy_test",                      rcpy_test,                          METH_VARARGS, "haha" },

    { "RCPY_AddOperandDString",         RCPY_AddOperandDString,             METH_VARARGS, "haha" },
    { "RCPY_AddOperandInt",             RCPY_AddOperandInt,                 METH_VARARGS, "haha" },
    { "RCPY_AddOperandDVector2i",       RCPY_AddOperandDVector2i,           METH_VARARGS, "haha" },
    { "RCPY_AddOperandDVector3i",       RCPY_AddOperandDVector3i,           METH_VARARGS, "haha" },
    { "RCPY_AddOperandDVector4i",       RCPY_AddOperandDVector4i,           METH_VARARGS, "haha" },
    { "RCPY_AddOperandF32",             RCPY_AddOperandF32,                 METH_VARARGS, "haha" },
    { "RCPY_AddOperandDVector2f",       RCPY_AddOperandDVector2f,           METH_VARARGS, "haha" },
    { "RCPY_AddOperandDVector3f",       RCPY_AddOperandDVector3f,           METH_VARARGS, "haha" },
    { "RCPY_AddOperandDVector4f",       RCPY_AddOperandDVector4f,           METH_VARARGS, "haha" },
    { "RCPY_AddOperandDMatrix2x2f",     RCPY_AddOperandDMatrix2x2f,         METH_VARARGS, "haha" },
    { "RCPY_AddOperandDMatrix3x3f",     RCPY_AddOperandDMatrix3x3f,         METH_VARARGS, "haha" },
    { "RCPY_AddOperandDMatrix4x4f",     RCPY_AddOperandDMatrix4x4f,         METH_VARARGS, "haha" },
    { "RCPY_AddOperandConstTexture",    RCPY_AddOperandConstTexture,        METH_VARARGS, "haha" },

    { "RCPY_SetOperand",                RCPY_SetOperand,                    METH_VARARGS, "haha" },
    { "RCPY_SetOperandDString",         RCPY_SetOperandDString,             METH_VARARGS, "haha" },
    { "RCPY_SetOperandInt",             RCPY_SetOperandInt,                 METH_VARARGS, "haha" },
    { "RCPY_SetOperandDVector2i",       RCPY_SetOperandDVector2i,           METH_VARARGS, "haha" },
    { "RCPY_SetOperandDVector3i",       RCPY_SetOperandDVector3i,           METH_VARARGS, "haha" },
    { "RCPY_SetOperandDVector4i",       RCPY_SetOperandDVector4i,           METH_VARARGS, "haha" },
    { "RCPY_SetOperandF32",             RCPY_SetOperandF32,                 METH_VARARGS, "haha" },
    { "RCPY_SetOperandDVector2f",       RCPY_SetOperandDVector2f,           METH_VARARGS, "haha" },
    { "RCPY_SetOperandDVector3f",       RCPY_SetOperandDVector3f,           METH_VARARGS, "haha" },
    { "RCPY_SetOperandDVector4f",       RCPY_SetOperandDVector4f,           METH_VARARGS, "haha" },
    { "RCPY_SetOperandDMatrix2x2f",     RCPY_SetOperandDMatrix2x2f,         METH_VARARGS, "haha" },
    { "RCPY_SetOperandDMatrix3x3f",     RCPY_SetOperandDMatrix3x3f,         METH_VARARGS, "haha" },
    { "RCPY_SetOperandDMatrix4x4f",     RCPY_SetOperandDMatrix4x4f,         METH_VARARGS, "haha" },

    { "RCPY_GetParameter",              RCPY_GetParameter,                  METH_VARARGS, "haha" },

    
    { "RCPY_IsInputConnected",          RCPY_IsInputConnected,              METH_VARARGS, "haha" },
    { "RCPY_ExecutePushInput",          RCPY_ExecutePushInput,              METH_VARARGS, "haha" },
    { "RCPY_PushOperand",               RCPY_PushOperand,                   METH_VARARGS, "haha" },
    { "RCPY_PushCpuOperator",           RCPY_PushCpuOperator,               METH_VARARGS, "haha" },
    { "RCPY_PushGpuOperator",           RCPY_PushGpuOperator,               METH_VARARGS, "haha" },
    { "RCPY_MarkTopOperand",            RCPY_MarkTopOperand,                METH_VARARGS, "haha" },
    { "RCPY_PopTopOperand",             RCPY_PopTopOperand,                 METH_VARARGS, "haha" },
    { "RCPY_PushMarker",                RCPY_PushMarker,                    METH_VARARGS, "haha" },
    { "RCPY_CacheResult",               RCPY_CacheResult,                   METH_VARARGS, "haha" },

    { "RCPY_IsDSimpleTypedValue",       RCPY_IsDSimpleTypedValue,           METH_VARARGS, "haha" },

    { "RCPY_IsDString",                 RCPY_IsDString,                     METH_VARARGS, "haha" },
    { "RCPY_DecodeDString",             RCPY_DecodeDString,                 METH_VARARGS, "haha" },
    { "RCPY_EncodeDString",             RCPY_EncodeDString,                 METH_VARARGS, "haha" },

    { "RCPY_IsInt",                     RCPY_IsInt,                         METH_VARARGS, "haha" },
    { "RCPY_DecodeInt",                 RCPY_DecodeInt,                     METH_VARARGS, "haha" },
    { "RCPY_EncodeInt",                 RCPY_EncodeInt,                     METH_VARARGS, "haha" },

    { "RCPY_IsDVector2i",               RCPY_IsDVector2i,                   METH_VARARGS, "haha" },
    { "RCPY_DecodeDVector2i",           RCPY_DecodeDVector2i,               METH_VARARGS, "haha" },
    { "RCPY_EncodeDVector2i",           RCPY_EncodeDVector2i,               METH_VARARGS, "haha" },

    { "RCPY_IsDVector3i",               RCPY_IsDVector3i,                   METH_VARARGS, "haha" },
    { "RCPY_DecodeDVector3i",           RCPY_DecodeDVector3i,               METH_VARARGS, "haha" },
    { "RCPY_EncodeDVector3i",           RCPY_EncodeDVector3i,               METH_VARARGS, "haha" },

    { "RCPY_IsDVector4i",               RCPY_IsDVector4i,                   METH_VARARGS, "haha" },
    { "RCPY_DecodeDVector4i",           RCPY_DecodeDVector4i,               METH_VARARGS, "haha" },
    { "RCPY_EncodeDVector4i",           RCPY_EncodeDVector4i,               METH_VARARGS, "haha" },

    { "RCPY_IsF32",                     RCPY_IsF32,                         METH_VARARGS, "haha" },
    { "RCPY_DecodeF32",                 RCPY_DecodeF32,                     METH_VARARGS, "haha" },
    { "RCPY_EncodeF32",                 RCPY_EncodeF32,                     METH_VARARGS, "haha" },

    { "RCPY_IsDVector2f",               RCPY_IsDVector2f,                   METH_VARARGS, "haha" },
    { "RCPY_DecodeDVector2f",           RCPY_DecodeDVector2f,               METH_VARARGS, "haha" },
    { "RCPY_EncodeDVector2f",           RCPY_EncodeDVector2f,               METH_VARARGS, "haha" },

    { "RCPY_IsDVector3f",               RCPY_IsDVector3f,                   METH_VARARGS, "haha" },
    { "RCPY_DecodeDVector3f",           RCPY_DecodeDVector3f,               METH_VARARGS, "haha" },
    { "RCPY_EncodeDVector3f",           RCPY_EncodeDVector3f,               METH_VARARGS, "haha" },

    { "RCPY_IsDVector4f",               RCPY_IsDVector4f,                   METH_VARARGS, "haha" },
    { "RCPY_DecodeDVector4f",           RCPY_DecodeDVector4f,               METH_VARARGS, "haha" },
    { "RCPY_EncodeDVector4f",           RCPY_EncodeDVector4f,               METH_VARARGS, "haha" },

    { "RCPY_IsDMatrix2x2f",             RCPY_IsDMatrix2x2f,                 METH_VARARGS, "haha" },
    { "RCPY_DecodeDMatrix2x2f",         RCPY_DecodeDMatrix2x2f,             METH_VARARGS, "haha" },
    { "RCPY_EncodeDMatrix2x2f",         RCPY_EncodeDMatrix2x2f,             METH_VARARGS, "haha" },

    { "RCPY_IsDMatrix3x3f",             RCPY_IsDMatrix3x3f,                 METH_VARARGS, "haha" },
    { "RCPY_DecodeDMatrix3x3f",         RCPY_DecodeDMatrix3x3f,             METH_VARARGS, "haha" },
    { "RCPY_EncodeDMatrix3x3f",         RCPY_EncodeDMatrix3x3f,             METH_VARARGS, "haha" },

    { "RCPY_IsDMatrix4x4f",             RCPY_IsDMatrix4x4f,                 METH_VARARGS, "haha" },
    { "RCPY_DecodeDMatrix4x4f",         RCPY_DecodeDMatrix4x4f,             METH_VARARGS, "haha" },
    { "RCPY_EncodeDMatrix4x4f",         RCPY_EncodeDMatrix4x4f,             METH_VARARGS, "haha" },

    {NULL, NULL, 0, NULL}
};

static PyModuleDef RCMOD = {
    PyModuleDef_HEAD_INIT,
    "rc",
    "rc module",
    -1,
    RCMODMethods
};

PyObject* PyInit_RC()
{
    if (PyType_Ready(&PyEffectNode_Type) < 0)
        return NULL;

    if (PyType_Ready(&PySimpleTypedValue_Type) < 0)
        return NULL;

    PyObject* l_pRCModule = PyModule_Create(&RCMOD);
    if (!l_pRCModule)
        return NULL;

    Py_INCREF(&PyEffectNode_Type);
    PyModule_AddObject(l_pRCModule, "RCEffectNodePy", (PyObject*)&PyEffectNode_Type);

    Py_INCREF(&PySimpleTypedValue_Type);
    PyModule_AddObject(l_pRCModule, "DSimpleTypedValue", (PyObject*)&PySimpleTypedValue_Type);

    return l_pRCModule;
}

RCEffectNodePy::PythonNodeModuleMap* RCEffectNodePy::s_pPythonNodeModuleMap = DM_NULL;
DResult          RCEffectNodePy::PythonInit()
{
    DString l_RCDataPath = RCGlobal::Instance().m_RCDataRootPath.c_str();

	wchar_t strTexPath[4096];
	UINT nType = CP_ACP;
	int wchars_num = MultiByteToWideChar(nType, 0, l_RCDataPath.c_str(), -1, NULL, 0);
	MultiByteToWideChar(nType, 0, l_RCDataPath.c_str(), -1, strTexPath, wchars_num);
	nType = CP_UTF8;
#ifdef _DEBUG
	int wchars_num2 = MultiByteToWideChar(nType, 0, "PythonLib\\", -1, NULL, 0);
	MultiByteToWideChar(nType, 0, "PythonLib\\", -1, strTexPath + wchars_num - 1, wchars_num2);
	Py_SetPath((const wchar_t*)strTexPath);
#else
//	int wchars_num2 = MultiByteToWideChar(nType, 0, "PythonLib.zip", -1, NULL, 0);
//	MultiByteToWideChar(nType, 0, "PythonLib.zip", -1, strTexPath + wchars_num - 1, wchars_num2);
	Py_SetPath(L"PythonLib.zip;data\\rcdata\\PythonLib.zip;data\\rcdata\\PythonLib\\");
#endif

    //DWString l_PythonPath((l_RCDataPath + "PythonLib\\").c_str());

    PyImport_AppendInittab("rc", PyInit_RC);
    Py_Initialize();

    PyImport_ImportModule("rc");

    s_pPythonNodeModuleMap = DOME_New(PythonNodeModuleMap);

    return R_SUCCESS;
}

DResult          RCEffectNodePy::PythonDeinit()
{
    for (PythonNodeModuleMap::iterator it = s_pPythonNodeModuleMap->begin(); it != s_pPythonNodeModuleMap->end(); ++it)
    {
        if (it->second.m_pScriptModule)
        {
            Py_DECREF(it->second.m_pScriptModule);
            it->second.m_pScriptModule = DM_NULL;
            it->second.m_pFinishLoadFunc = DM_NULL;
            it->second.m_pBuildMDStackFunc = DM_NULL;
        }
    }
    DOME_Del(s_pPythonNodeModuleMap);

    Py_Finalize();

    return R_SUCCESS;
}

void             RCEffectNodePy::CreatePythonNodeModule(const DHashString& i_ModuleName, const DHashString& i_ScriptCode, Bool i_bForceCreate)
{
    PythonNodeModuleMap::iterator it = s_pPythonNodeModuleMap->find(i_ModuleName);
    if (it == s_pPythonNodeModuleMap->end())
    {
        PythonNodeModule l_Module;
        l_Module.m_pScriptModule = DM_NULL;
        l_Module.m_pFinishLoadFunc = DM_NULL;
        l_Module.m_pBuildMDStackFunc = DM_NULL;
        (*s_pPythonNodeModuleMap)[i_ModuleName] = l_Module;
        it = s_pPythonNodeModuleMap->find(i_ModuleName);
        DOME_ASSERT(it != s_pPythonNodeModuleMap->end());
    }
    PythonNodeModule* l_pModule = &it->second;

    if (i_bForceCreate || (l_pModule->m_ScriptCode != i_ScriptCode))
    {
        if (l_pModule->m_pScriptModule)
        {
            Py_DECREF(l_pModule->m_pScriptModule);
            l_pModule->m_pScriptModule = DM_NULL;
            l_pModule->m_pFinishLoadFunc = DM_NULL;
            l_pModule->m_pBuildMDStackFunc = DM_NULL;
        }
        l_pModule->m_ScriptCode = i_ScriptCode;

        PyObject* l_pByteCode = Py_CompileString(l_pModule->m_ScriptCode.c_str(), i_ModuleName.c_str(), Py_file_input);
        DOME_ASSERT(l_pByteCode);
        l_pModule->m_pScriptModule = PyImport_ExecCodeModule(i_ModuleName.c_str(), l_pByteCode);
        DOME_ASSERT(l_pModule->m_pScriptModule);
        PyObject* l_pModuleDict = PyModule_GetDict(l_pModule->m_pScriptModule);
        DOME_ASSERT(l_pModuleDict);
        l_pModule->m_pFinishLoadFunc = PyDict_GetItemString(l_pModuleDict, "FinishLoad");
        DOME_ASSERT(l_pModule->m_pFinishLoadFunc);
        DOME_ASSERT(PyCallable_Check(l_pModule->m_pFinishLoadFunc));
        l_pModule->m_pBuildMDStackFunc = PyDict_GetItemString(l_pModuleDict, "BuildMDStack");
        DOME_ASSERT(l_pModule->m_pBuildMDStackFunc);
        DOME_ASSERT(PyCallable_Check(l_pModule->m_pBuildMDStackFunc));
        Py_DECREF(l_pByteCode);
    }
}

RCEffectNodePy::PythonNodeModule* RCEffectNodePy::GetPythonNodeModule(const DHashString& i_ModuleName)
{
    PythonNodeModuleMap::iterator it = s_pPythonNodeModuleMap->find(i_ModuleName);
    if (it == s_pPythonNodeModuleMap->end())
        return DM_NULL;
    else
        return &it->second;
}


RCEffectNode*    RCEffectNodePy::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCEffectNodePy)(i_pEffect);
}

DResult          RCEffectNodePy::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCEffectNodePy::RCEffectNodePy(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_pStack(DM_NULL)
{

}

RCEffectNodePy::~RCEffectNodePy()
{
    for (Int i = 0; i < m_OperandArray.size(); ++i)
        DOME_Del(m_OperandArray[i]);
    m_OperandArray.clear();
}

void            RCEffectNodePy::addOperandDString()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DString)));
}

void            RCEffectNodePy::addOperandInt()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_Int)));
}

void            RCEffectNodePy::addOperandDVector2i()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DVector2i)));
}

void            RCEffectNodePy::addOperandDVector3i()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DVector3i)));
}

void            RCEffectNodePy::addOperandDVector4i()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DVector4i)));
}

void            RCEffectNodePy::addOperandF32()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_F32)));
}

void            RCEffectNodePy::addOperandDVector2f()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DVector2f)));
}

void            RCEffectNodePy::addOperandDVector3f()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DVector3f)));
}

void            RCEffectNodePy::addOperandDVector4f()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DVector4f)));
}

void            RCEffectNodePy::addOperandDMatrix2x2f()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DMatrix2x2f)));
}

void            RCEffectNodePy::addOperandDMatrix3x3f()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DMatrix3x3f)));
}

void            RCEffectNodePy::addOperandDMatrix4x4f()
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_DMatrix4x4f)));
}

void            RCEffectNodePy::addOperandConstTexture(const DString& i_TexPath)
{
    m_OperandArray.push_back(DOME_New(MDOperandValue(RCGlobal::k_SimpleTypeID_OSTexture2D)));
    Int l_Index = m_OperandArray.size() - 1;

    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
    DResult l_Result;
    OSTexture2D l_Tex;
    DString l_FullFilePath;
    if (i_TexPath.isBeginWith("data"))
        l_FullFilePath = i_TexPath;
    else
    {
        l_pRenderer->getDataPath(l_FullFilePath);
        l_FullFilePath += i_TexPath;
    }


    l_Result = l_pRenderer->createTexture2DFromFile(l_Tex, l_FullFilePath);
    DOME_ASSERT2(DM_SUCC(l_Result), "Texture %s create failed in RCEffectNodePy::%s", l_FullFilePath.c_str(), getSubType().c_str());

    m_OperandArray[l_Index]->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Tex);
}

void            RCEffectNodePy::setOperand(Int i_Index, const DSimpleTypedValue& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperand(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == i_Value.getTypeID(), "setOperand(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    *m_OperandArray[i_Index]->getDataPtr() = i_Value;
}

void            RCEffectNodePy::setOperandDString(Int i_Index, const Char* i_pString)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDString(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DString, "setOperandDString(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDString(DString(i_pString));
}

void            RCEffectNodePy::setOperandInt(Int i_Index, Int i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandInt(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_Int, "setOperandInt(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setInt(i_Value);
}

void            RCEffectNodePy::setOperandDVector2i(Int i_Index, const DVector2i& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDVector2i(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DVector2i, "setOperandDVector2i(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDVector2i(i_Value);
}

void            RCEffectNodePy::setOperandDVector3i(Int i_Index, const DVector3i& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDVector3i(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DVector3i, "setOperandDVector3i(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDVector3i(i_Value);
}

void            RCEffectNodePy::setOperandDVector4i(Int i_Index, const DVector4i& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDVector4i(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DVector4i, "setOperandDVector4i(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDVector4i(i_Value);
}

void            RCEffectNodePy::setOperandF32(Int i_Index, F32 i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandF32(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_F32, "setOperandF32(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setF32(i_Value);
}

void            RCEffectNodePy::setOperandDVector2f(Int i_Index, const DVector2f& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDVector2f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DVector2f, "setOperandDVector2f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDVector2f(i_Value);
}

void            RCEffectNodePy::setOperandDVector3f(Int i_Index, const DVector3f& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDVector3f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DVector3f, "setOperandDVector3f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDVector3f(i_Value);
}

void            RCEffectNodePy::setOperandDVector4f(Int i_Index, const DVector4f& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDVector4f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DVector4f, "setOperandDVector4f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDVector4f(i_Value);
}

void            RCEffectNodePy::setOperandDMatrix2x2f(Int i_Index, const DMatrix2x2f& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDMatrix2x2f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DMatrix2x2f, "setOperandDMatrix2x2f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDMatrix2x2f(i_Value);
}

void            RCEffectNodePy::setOperandDMatrix3x3f(Int i_Index, const DMatrix3x3f& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDMatrix3x3f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DMatrix3x3f, "setOperandDMatrix3x3f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDMatrix3x3f(i_Value);
}

void            RCEffectNodePy::setOperandDMatrix4x4f(Int i_Index, const DMatrix4x4f& i_Value)
{
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "setOperandDMatrix4x4f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(m_OperandArray[i_Index]->getDataPtr()->getTypeID() == RCGlobal::k_SimpleTypeID_DMatrix4x4f, "setOperandDMatrix4x4f(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());

    m_OperandArray[i_Index]->getDataPtr()->setDMatrix4x4f(i_Value);
}

void            RCEffectNodePy::executePushInputPy(Int i_Index)
{
    DOME_ASSERT2(m_pStack, "executePushInputPy(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(i_Index >= 0 && i_Index < getInputCount(), "executePushInputPy(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    executePushInput(m_pStack, i_Index);
}

void            RCEffectNodePy::pushOperandPy(Int i_Index)
{
    DOME_ASSERT2(m_pStack, "pushOperandPy(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    DOME_ASSERT2(i_Index >= 0 && i_Index < m_OperandArray.size(), "pushOperandPy(%lld) failed in RCEffectNodePy::%s", i_Index, getSubType().c_str());
    m_pStack->pushOperand(m_OperandArray[i_Index]);
}

void            RCEffectNodePy::pushCpuOperatorPy(const Char* i_pOperatorName)
{
    DStringHash l_Hash(i_pOperatorName);
    const MDOperator* l_pOperator = RCManager::Instance().getMDOperator(l_Hash);
    DOME_ASSERT2(l_pOperator, "pushCpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
    DResult l_Result = m_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pOperator);
    DOME_ASSERT2(DM_SUCC(l_Result), "pushCpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
}

void            RCEffectNodePy::pushGpuOperatorPy(const Char* i_pOperatorName)
{
    DStringHash l_Hash(i_pOperatorName);
    const MDOperator* l_pOperator = RCManager::Instance().getMDOperator(l_Hash);
    DOME_ASSERT2(l_pOperator, "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
    DResult l_Result = m_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pOperator);
    DOME_ASSERT2(DM_SUCC(l_Result), "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
}

void            RCEffectNodePy::pushGpuOperatorPy(const Char* i_pOperatorName, Int i_Width, Int i_Height)
{
    DStringHash l_Hash(i_pOperatorName);
    const MDOperator* l_pOperator = RCManager::Instance().getMDOperator(l_Hash);
    DOME_ASSERT2(l_pOperator, "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
    DResult l_Result = m_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pOperator, i_Width, i_Height);
    DOME_ASSERT2(DM_SUCC(l_Result), "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
}

void            RCEffectNodePy::pushGpuOperatorPy(const Char* i_pOperatorName, const Char* i_pFormat)
{
    DStringHash l_Hash(i_pOperatorName);
    RCGPUDATAFORMAT l_Format = RGDF_RGBA8;
    if (DString("RGDF_RGBA16F") == i_pFormat)
        l_Format = RGDF_RGBA16F;
    const MDOperator* l_pOperator = RCManager::Instance().getMDOperator(l_Hash);
    DOME_ASSERT2(l_pOperator, "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
    DResult l_Result = m_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pOperator, l_Format);
    DOME_ASSERT2(DM_SUCC(l_Result), "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
}

void            RCEffectNodePy::pushGpuOperatorPy(const Char* i_pOperatorName, Int i_Width, Int i_Height, const Char* i_pFormat)
{
    DStringHash l_Hash(i_pOperatorName);
    RCGPUDATAFORMAT l_Format = RGDF_RGBA8;
    if (DString("RGDF_RGBA16F") == i_pFormat)
        l_Format = RGDF_RGBA16F;
    const MDOperator* l_pOperator = RCManager::Instance().getMDOperator(l_Hash);
    DOME_ASSERT2(l_pOperator, "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
    DResult l_Result = m_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pOperator, i_Width, i_Height, l_Format);
    DOME_ASSERT2(DM_SUCC(l_Result), "pushGpuOperatorPy(%s) failed in RCEffectNodePy::%s", i_pOperatorName, getSubType().c_str());
}

Int             RCEffectNodePy::markTopOperandPy()
{
    DOME_ASSERT2(m_pStack, "markTopOperandPy failed in RCEffectNodePy::%s", getSubType().c_str());
    m_MarkerArray.push_back(m_pStack->getTopOperand());
    return m_MarkerArray.size() - 1;
}

void            RCEffectNodePy::popTopOperandPy()
{
    DOME_ASSERT2(m_pStack, "popTopOperandPy failed in RCEffectNodePy::%s", getSubType().c_str());
    m_pStack->popOperand();
}

void            RCEffectNodePy::pushMarkerPy(Int i_Marker)
{
    DOME_ASSERT2(m_pStack, "pushMarkerPy failed in RCEffectNodePy::%s", getSubType().c_str());
    DOME_ASSERT2(i_Marker >= 0 && i_Marker < m_MarkerArray.size(), "pushMarkerPy failed in RCEffectNodePy::%s", getSubType().c_str());
    m_pStack->pushOperand(m_MarkerArray[i_Marker]);
}

void            RCEffectNodePy::cacheResultPy(Int i_Index)
{
    DOME_ASSERT2(m_pStack, "cacheResultPy failed in RCEffectNodePy::%s", getSubType().c_str());
    DOME_ASSERT2(i_Index >= 0 && i_Index < getOutputCount(), "pushMarkerPy failed in RCEffectNodePy::%s", getSubType().c_str());
    cacheResult(m_pStack, i_Index);
}



// Main Work is Done Here.
DResult         RCEffectNodePy::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    m_pStack = o_pStack;
    m_MarkerArray.clear();

	FRAMETIMER_BEGIN(FTT_RC_CAL_PY_PROCESS1, FTT_RC_CAL_EXECUTE_REAL);
    PythonNodeModule* l_pModule = GetPythonNodeModule(getSubType());
    DOME_ASSERT(l_pModule);
	FRAMETIMER_END(FTT_RC_CAL_PY_PROCESS1);

	FRAMETIMER_BEGIN(FTT_RC_CAL_PY_PROCESS2, FTT_RC_CAL_EXECUTE_REAL);
    PyObject* l_pArgs = PyTuple_New(2);
    PyEffectNode* l_pNode = (PyEffectNode*)PyEffectNode_Type.tp_alloc(&PyEffectNode_Type, 0);
    l_pNode->m_pEffectNode = this;
    PyTuple_SetItem(l_pArgs, 0, (PyObject*)l_pNode);
    PyTuple_SetItem(l_pArgs, 1, PyLong_FromLong(i_OutputSelector));
	FRAMETIMER_END(FTT_RC_CAL_PY_PROCESS2);

	FRAMETIMER_BEGIN(FTT_RC_CAL_PY_PROCESS3, FTT_RC_CAL_EXECUTE_REAL);
    PyObject* l_pResult = PyObject_CallObject(l_pModule->m_pBuildMDStackFunc, l_pArgs);
    if (PyErr_Occurred())
        PyErr_Print();
	
	Py_DECREF(l_pArgs);
    if (l_pResult)
    {
        Py_DECREF(l_pResult);
    }
    m_pStack = DM_NULL;
    m_MarkerArray.clear();
	FRAMETIMER_END(FTT_RC_CAL_PY_PROCESS3);

    return R_SUCCESS;
}

void            RCEffectNodePy::customizeLoad(const rapidxml::xml_node<>* i_pXmlNode)
{
    rapidxml::xml_node<>* l_pXmlChild = i_pXmlNode->first_node("Script");
    DOME_ASSERT(l_pXmlChild);

    m_ScriptCode = l_pXmlChild->first_node()->value();
}

void            RCEffectNodePy::finishLoad()
{
    CreatePythonNodeModule(getSubType(), m_ScriptCode, DM_FALSE);
    PythonNodeModule* l_pModule = GetPythonNodeModule(getSubType());
    DOME_ASSERT(l_pModule);

    PyObject* l_pArgs = PyTuple_New(1);
    PyEffectNode* l_pNode = (PyEffectNode*)PyEffectNode_Type.tp_alloc(&PyEffectNode_Type, 0);
    l_pNode->m_pEffectNode = this;
    PyTuple_SetItem(l_pArgs, 0, (PyObject*)l_pNode);

    PyObject* l_pResult = PyObject_CallObject(l_pModule->m_pFinishLoadFunc, l_pArgs);
    if (PyErr_Occurred())
        PyErr_Print();

    Py_DECREF(l_pArgs);

    if (l_pResult)
    {
        Py_DECREF(l_pResult);
    }
}


RC_NAMESPACE_END

#endif