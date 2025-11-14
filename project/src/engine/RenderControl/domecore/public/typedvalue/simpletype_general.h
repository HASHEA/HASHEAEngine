/*
    filename:       simpletype_general.h
    author:         Ming Dong
    date:           2016-MAR-27
    description:    
*/
#pragma once

#include "isimpletype.h"
#include "simpletypemanager.h"

DOME_NAMESPACE_BEGIN

#define SIMPLETYPE_GENERAL_BEGIN                                                    \
template<class TYPE, class ALLOCATOR_T = IDefaultMemManager>                        \
class SIMPLETYPE_GENERAL_CLASS_NAME : public ISimpleType                            \
{                                                                                   \
public:                                                                             \
    typedef TYPE        Type_t;                                                     \
    typedef ALLOCATOR_T Allocator_t;


#define SIMPLETYPE_GENERAL_END                                                      \
private:                                                                            \
    DSimpleTypeName       m_TypeName;                                               \
    DSimpleTypeName       m_GroupName;                                              \
};


// EMBED TYPE
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Embed_CompareYes_SerializeYes
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_embed.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_yes.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_embed.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Embed_CompareYes_SerializeNo
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_embed.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_no.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_embed.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Embed_CompareNo_SerializeYes
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_embed.inc"
#include "simpletype_general_compare_no.inc"
#include "simpletype_general_serialize_yes.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_embed.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Embed_CompareNo_SerializeNo
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_embed.inc"
#include "simpletype_general_compare_no.inc"
#include "simpletype_general_serialize_no.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_embed.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS



// ALLOCED TYPE
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Alloced_CompareYes_SerializeYes
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_alloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_yes.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_alloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Alloced_CompareYes_SerializeNo
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_alloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_no.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_alloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Alloced_CompareNo_SerializeYes
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_alloc.inc"
#include "simpletype_general_compare_no.inc"
#include "simpletype_general_serialize_yes.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_alloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_Alloced_CompareNo_SerializeNo
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_alloc.inc"
#include "simpletype_general_compare_no.inc"
#include "simpletype_general_serialize_no.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_alloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS



// FIXED ALLOC TYPE
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareYes_SerializeYes
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_yes.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareYes_SerializeNo
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_no.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareNo_SerializeYes
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_no.inc"
#include "simpletype_general_serialize_yes.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareNo_SerializeNo
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_no.inc"
#include "simpletype_general_serialize_no.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS



// STRING SPECIAL SERIALIZE TYPE
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareYes_SerializeDString
#define SIMPLETYPE_GENERAL_STRING_CLASS                 DString
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_string.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_STRING_CLASS
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareYes_SerializeDHashString
#define SIMPLETYPE_GENERAL_STRING_CLASS                 DHashString
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_string.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_STRING_CLASS
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareYes_SerializeDWString
#define SIMPLETYPE_GENERAL_STRING_CLASS                 DWString
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_string.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_STRING_CLASS
#undef SIMPLETYPE_GENERAL_INSIDECLASS


#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_CLASS_NAME                   TSimpleType_FixedAlloc_CompareYes_SerializeDWHashString
#define SIMPLETYPE_GENERAL_STRING_CLASS                 DWHashString
#define SIMPLETYPE_GENERAL_INSIDECLASS                  1
SIMPLETYPE_GENERAL_BEGIN
#include "simpletype_general_mem_fixedalloc.inc"
#include "simpletype_general_compare_yes.inc"
#include "simpletype_general_serialize_string.inc"
SIMPLETYPE_GENERAL_END
#undef SIMPLETYPE_GENERAL_INSIDECLASS
#define SIMPLETYPE_GENERAL_INSIDECLASS                  0
#include "simpletype_general_mem_fixedalloc.inc"
#undef SIMPLETYPE_GENERAL_CLASS_NAME
#undef SIMPLETYPE_GENERAL_STRING_CLASS
#undef SIMPLETYPE_GENERAL_INSIDECLASS


DOME_NAMESPACE_END