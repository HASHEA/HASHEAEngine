// COMMON STRING CODE BEGIN

public:
    typedef CHAR_T      Char_t;
    typedef ALLOCATOR_T Allocator_t;
    typedef TStringHash<Char_t> StringHash_t;

    // constractors/destructor
    DOME_COMMONSTRING_CLASS()
    {
        _init();
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    DOME_COMMONSTRING_CLASS(const Char_t* i_pString)
    {
        _init();
        _copyFrom(i_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    DOME_COMMONSTRING_CLASS(const DOME_COMMONSTRING_CLASS& i_String)
    {
        _init();
        _copyFrom(i_String.m_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    ~DOME_COMMONSTRING_CLASS()
    {
        _reset();
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }


    // getter functions (const functions)
    Int getCharCount() const
    {
        if(!m_pString)
            return 0;
        return OS_String::TGetStrCharCount(m_pString);
    }

    Int getMemSize() const
    {
        if(!m_pString)
            return 0;
        return OS_String::TGetStrBufferSize(m_pString);
    }

    const Char_t* c_str() const
    {
        return m_pString;
    }

    Int size() const
    {
        return getCharCount();
    }

#ifdef DOME_COMMONSTRING_WITHHASH
    Bool isHashValid() const
    {
        return DM_BOOL(_computeHash(m_pString) == m_HashCode);
    }
    StringHash_t getHash() const
    {
        return m_HashCode;
    }
#endif

    Bool subString(Int i_Start, Int i_Length, DOME_COMMONSTRING_CLASS& o_Result) const
    {
        o_Result.copyFrom(m_pString, i_Start, i_Length);
        return o_Result.m_pString ? DM_TRUE : DM_FALSE;
    }

    DOME_COMMONSTRING_CLASS subString(Int i_Start, Int i_Length) const
    {
        DOME_COMMONSTRING_CLASS l_Result;
        subString(i_Start, i_Length, l_Result);
        return l_Result;
    }

    Int find(const Char_t* i_pSubStr, Int i_BeginPos) const
    {
        if(!m_pString) return -1;
        if(!i_pSubStr) return -1;
        Int l_StrLen = getCharCount();
        if(i_BeginPos >= l_StrLen) return -1;
        return OS_String::TFindStr(i_pSubStr, -1, m_pString + i_BeginPos, l_StrLen - i_BeginPos);
    }

    Int find(const DOME_COMMONSTRING_CLASS& i_SubStr, Int i_BeginPos) const
    {
        return find(i_SubStr.c_str(), i_BeginPos);
    }

    Int toInt(Int i_Default = 0, Bool* o_bSucc = DM_NULL, Int* o_Sign = DM_NULL) const
    {
        return OS_String::TStrToInteger<Char_t, Int>(m_pString, i_Default, o_bSucc, o_Sign);
    }

    Int hexToInt(Int i_Default = 0, Bool* o_bSucc = DM_NULL, Int* o_Sign = DM_NULL) const
    {
        return OS_String::THexStrToInteger<Char_t, Int>(m_pString, i_Default, o_bSucc, o_Sign);
    }

    F64 toFloat(F64 i_Default = 0.0, Bool* o_bSucc = DM_NULL, Int* o_Sign = DM_NULL) const
    {
        return OS_String::TStrToFloat<Char_t, F64>(m_pString, i_Default, o_bSucc, o_Sign);
    }



    // string operations
    void clear()
    {
        _reset();
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    void concat(const Char_t* i_pStr0, const Char_t* i_pStr1)
    {
        Int l_StrLen0 = OS_String::TGetStrCharCount(i_pStr0);
        Int l_StrLen1 = OS_String::TGetStrCharCount(i_pStr1);
        Int l_TotalLen = l_StrLen0 + l_StrLen1 + 1;
        _reserve(l_TotalLen);

        if(l_StrLen0 > 0)
        {
            OS_String::TStrCopy(m_pString, i_pStr0);
        }

        if (l_StrLen1 > 0)
        {
            OS_String::TStrCopy(m_pString + l_StrLen0, i_pStr1);
        }
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    void copyFrom(const Char_t* i_pString)
    {
        _copyFrom(i_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    void copyFrom(const Char_t* i_pString, Int i_StartPos, Int i_Len)
    {
        _copyFrom(i_pString, i_StartPos, i_Len);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
    }

    Bool isBeginWith(const Char_t* i_pSubString, Int i_SubStrLen) const
    {
        Int l_MySize = getCharCount();
        if(l_MySize < i_SubStrLen)
            return DM_FALSE;

        for (Int i = 0; i < i_SubStrLen; ++i)
        {
            if(m_pString[i] != i_pSubString[i])
                return DM_FALSE;
        }
        return DM_TRUE;
    }

    Bool isBeginWith(const Char_t* i_pSubString) const
    {
        return isBeginWith(i_pSubString, OS_String::TGetStrCharCount(i_pSubString));
    }

    Bool isBeginWith(const DOME_COMMONSTRING_CLASS& i_SubString) const
    {
        return isBeginWith(i_SubString.c_str(), i_SubString.getCharCount());
    }

    Bool isEndWith(const Char_t* i_pSubString, Int i_SubStrLen) const
    {
        Int l_MySize = getCharCount();
        if(l_MySize < i_SubStrLen)
            return DM_FALSE;

        Int l_BeginPos = l_MySize - i_SubStrLen;
        for (Int i = l_BeginPos; i < l_MySize; ++i)
        {
            if(m_pString[i] != i_pSubString[i - l_BeginPos])
                return DM_FALSE;
        }
        return DM_TRUE;
    }

    Bool isEndWith(const Char_t* i_pSubString) const
    {
        return isEndWith(i_pSubString, OS_String::TGetStrCharCount(i_pSubString));
    }

    Bool isEndWith(const DOME_COMMONSTRING_CLASS& i_SubString) const
    {
        return isEndWith(i_SubString.c_str(), i_SubString.getCharCount());
    }


    /*
        return the result string character count, without end 0
    */
    Int format(const Char_t* i_Format, ...)
    {
        Int l_StrLen = 0;
        va_list l_Args;
        va_start(l_Args, i_Format);
        l_StrLen = OS_String::StrVNSPrintf((Char_t*)DM_NULL, 0, i_Format, l_Args);
        if(l_StrLen < 0)
        {
            va_end(l_Args);
            return -1;
        }
        _reserve(l_StrLen + 1);
        OS_String::StrVNSPrintf(m_pString, l_StrLen + 1, i_Format, l_Args);
        va_end(l_Args);

#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
        return l_StrLen;
    }

    /*
        return how many args scaned
    */
    Int scan(const Char_t* i_Format, ...) const
    {
        Int l_NumArgScaned = -1;
        va_list l_Args;
        va_start(l_Args, i_Format);
        l_NumArgScaned = OS_String::StrVSScanf(m_pString, i_Format, l_Args);
        va_end(l_Args);

        return l_NumArgScaned;
    }

#ifdef DOME_COMMONSTRING_WITHHASH
    void validHash()
    {
        _updateHash();
    }
#endif



    // const c++ operator overload
    DOME_COMMONSTRING_CLASS operator+ (const Char_t* i_pString) const
    {
        DOME_COMMONSTRING_CLASS l_Result;
        l_Result.concat(m_pString, i_pString);
        return l_Result;
    }

    DOME_COMMONSTRING_CLASS operator+ (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return operator+(i_String.m_pString);
    }

    // compare functions and operators
    Int compare(const Char_t* i_pString) const
    {
#ifndef DOME_COMMONSTRING_WITHHASH
        return OS_String::TCompareStr(m_pString, i_pString);
#else
        StringHash_t l_Hash = _computeHash(i_pString);
        if(m_HashCode > l_Hash)
            return 1;
        else if(m_HashCode < l_Hash)
            return -1;
        else
        {
#ifdef DOME_HASHSTRING_FASTCOMPARE
            return 0;
#else
            return OS_String::TCompareStr(m_pString, i_pString);
#endif
        }
#endif
    }

    Int compare(const DOME_COMMONSTRING_CLASS& i_String) const
    {
#ifndef DOME_COMMONSTRING_WITHHASH
        return OS_String::TCompareStr(m_pString, i_String.m_pString);
#else
        if(m_HashCode > i_String.m_HashCode)
            return 1;
        else if(m_HashCode < i_String.m_HashCode)
            return -1;
        else
        {
#ifdef DOME_HASHSTRING_FASTCOMPARE
            return 0;
#else
            return OS_String::TCompareStr(m_pString, i_String.m_pString);
#endif
        }
#endif
    }

#ifdef DOME_COMMONSTRING_WITHHASH
    Int compare(StringHash_t i_Hash) const
    {
        if(m_HashCode > i_Hash)
            return 1;
        else if(m_HashCode < i_Hash)
            return -1;
        else
            return 0;
    }

    Bool operator== (StringHash_t i_Hash) const
    {
        return compare(i_Hash) == 0;
    }
#endif

    Bool operator== (const Char_t* i_pString) const
    {
        return compare(i_pString) == 0;
    }

    Bool operator== (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return compare(i_String) == 0;
    }

    Bool operator!= (const Char_t* i_pString) const
    {
        return compare(i_pString) != 0;
    }

    Bool operator!= (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return compare(i_String) != 0;
    }

    Bool operator> (const Char_t* i_pString) const
    {
        return compare(i_pString) > 0;
    }

    Bool operator> (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return compare(i_String) > 0;
    }

    Bool operator>= (const Char_t* i_pString) const
    {
        return compare(i_pString) >= 0;
    }

    Bool operator>= (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return compare(i_String) >= 0;
    }

    Bool operator< (const Char_t* i_pString) const
    {
        return compare(i_pString) < 0;
    }

    Bool operator< (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return compare(i_String) < 0;
    }

    Bool operator<= (const Char_t* i_pString) const
    {
        return compare(i_pString) <= 0;
    }

    Bool operator<= (const DOME_COMMONSTRING_CLASS& i_String) const
    {
        return compare(i_String) <= 0;
    }

    const Char_t& operator[] (Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < getCharCount());
        return m_pString[i_Index];
    }


    // c++ operator overload
    DOME_COMMONSTRING_CLASS& operator= (const Char_t* i_pString)
    {
        _copyFrom(i_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
        return *this;
    }

    DOME_COMMONSTRING_CLASS& operator= (const DOME_COMMONSTRING_CLASS& i_String)
    {
        _copyFrom(i_String.m_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
        return *this;
    }

    DOME_COMMONSTRING_CLASS& operator+= (const Char_t* i_pString)
    {
        DOME_COMMONSTRING_CLASS l_Me = m_pString;
        concat(l_Me.m_pString, i_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
        return *this;
    }

    DOME_COMMONSTRING_CLASS& operator+= (const DOME_COMMONSTRING_CLASS& i_String)
    {
        DOME_COMMONSTRING_CLASS l_Me = m_pString;
        concat(l_Me.m_pString, i_String.m_pString);
#ifdef DOME_COMMONSTRING_WITHHASH
        _updateHash();
#endif
        return *this;
    }

#ifndef DOME_COMMONSTRING_WITHHASH
    Char_t& operator[] (Int i_Index)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < getCharCount());
        return m_pString[i_Index];
    }
#endif



protected:
    void _init()
    {
        m_pString = DM_NULL;
    }

    void _reset()
    {
        if(m_pString)
        {
            DOME_FreeEx(m_pString, ALLOCATOR_T);
            m_pString = DM_NULL;
        }
    }

    void _copyFrom(const Char_t* i_pString)
    {
        _reset();

        Int l_CharCount = OS_String::TGetStrCharCount(i_pString);

        if(l_CharCount <= 0)
            return ;

        m_pString = (Char_t*)DOME_AllocEx((l_CharCount + 1) * sizeof(Char_t), ALLOCATOR_T);
        OS_String::TSafeStrCopy(m_pString, l_CharCount + 1, i_pString);
    }

    void _copyFrom(const Char_t* i_pString, Int i_StartPos, Int i_Len)
    {
        _reset();

        if(!i_pString)
            return ;

        Int l_CharCount = OS_String::TGetStrCharCount(i_pString);

        if(i_StartPos < 0 || i_StartPos >= l_CharCount)
            return ;
        if(i_Len <= 0 || (i_StartPos + i_Len) > l_CharCount)
            return ;

        m_pString = (Char_t*)DOME_AllocEx(i_Len + 1, ALLOCATOR_T);
        for(Int i = 0; i < i_Len; ++i)
        {
            m_pString[i] = i_pString[i_StartPos + i];
        }
        m_pString[i_Len] = 0;
    }

    void _reserve(Int i_ReserveSize)
    {
        DOME_ASSERT(i_ReserveSize > 0);
        Int l_MemSize = getMemSize();
        Int l_ReserveMemSize = i_ReserveSize * sizeof(Char_t);
        if (l_MemSize != l_ReserveMemSize)
        {
            _reset();
            m_pString = (Char_t*)DOME_AllocEx(l_ReserveMemSize, ALLOCATOR_T);
        }
    }

protected:
    Char_t*         m_pString;
#ifdef DOME_COMMONSTRING_WITHHASH
    StringHash_t    m_HashCode;
    StringHash_t _computeHash(const Char_t* i_pString) const
    {
        return StringHash_t(i_pString);
    }
    void _updateHash()
    {
        m_HashCode = _computeHash(m_pString);
    }
#endif

// COMMON STRING CODE END