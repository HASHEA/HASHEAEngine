/*
    filename:       strongtypedef.h
    author:         Ming Dong
    date:           2016-Mar-16
    description:    
*/
#pragma once

#define DOME_STRONGTYPEDEF(SOURCETYPE, DESTTYPE, DLL_API)                                   \
struct DLL_API DESTTYPE                                                                     \
{                                                                                           \
    SOURCETYPE      m_Value;                                                                \
    explicit DESTTYPE(const SOURCETYPE& i_Value):m_Value(i_Value){}                         \
    DESTTYPE() {}                                                                           \
    DESTTYPE(const DESTTYPE& i_Other):m_Value(i_Other.m_Value){}                            \
    DESTTYPE& operator=(const DESTTYPE& i_Other) {m_Value = i_Other.m_Value;return *this;}  \
    DESTTYPE& operator=(const SOURCETYPE& i_Value) {m_Value = i_Value; return *this;}       \
    operator const SOURCETYPE& () const {return m_Value;}                                   \
    operator SOURCETYPE& () {return m_Value;}                                               \
    const SOURCETYPE& get() const {return m_Value;}                                         \
    SOURCETYPE& get() {return m_Value;}                                                     \
    Bool operator==(const DESTTYPE& i_Other) const {return m_Value == i_Other.m_Value;}     \
    Bool operator!=(const DESTTYPE& i_Other) const {return m_Value != i_Other.m_Value;}     \
    Bool operator>(const DESTTYPE& i_Other) const {return m_Value > i_Other.m_Value;}       \
    Bool operator<(const DESTTYPE& i_Other) const {return m_Value < i_Other.m_Value;}       \
    Bool operator>=(const DESTTYPE& i_Other) const {return m_Value >= i_Other.m_Value;}     \
    Bool operator<=(const DESTTYPE& i_Other) const {return m_Value <= i_Other.m_Value;}     \
};