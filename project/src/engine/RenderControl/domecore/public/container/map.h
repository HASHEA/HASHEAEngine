//
//  map.h
//  engine
//
//  Created by Ming Dong on 12-03-06.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_map_h
//#define engine_map_h
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../math/mathutils.h"
#include "../imemory.h"
#include "_avltree.h"

DOME_NAMESPACE_BEGIN
    
template<class MAP_KEY_T, class MAP_VALUE_T, class MAP_COMP_T = Math::TCompare<MAP_KEY_T, MAP_KEY_T>, class MAP_ALLOCATOR_T = IDefaultMemManager, Int PAGESIZE = 1>
class TMap
{
public:
    // compare type for map
    typedef Math::TPair<const MAP_KEY_T, MAP_VALUE_T>     TREE_VALUE_T;
    struct TREE_COMP_T
    {
        static Bool Less(const MAP_KEY_T& i_Key, const TREE_VALUE_T& i_Value)
        {
            return MAP_COMP_T::Less(i_Key, i_Value.first);
        }

        static Bool Greater(const MAP_KEY_T& i_Key, const TREE_VALUE_T& i_Value)
        {
            return MAP_COMP_T::Greater(i_Key, i_Value.first);
        }

        static Bool Equal(const MAP_KEY_T& i_Key, const TREE_VALUE_T& i_Value)
        {
            return MAP_COMP_T::Equal(i_Key, i_Value.first);
        }

        static Int Compare(const MAP_KEY_T& i_Key, const TREE_VALUE_T& i_Value)
        {
            return MAP_COMP_T::Compare(i_Key, i_Value.first);
        }
    };
    typedef TAvlTree<TREE_VALUE_T, MAP_KEY_T, TREE_COMP_T, MAP_ALLOCATOR_T, PAGESIZE>   MAP_TREE_T;

public:
    // iterator
    class iterator
    {
    public:
        iterator()
        {
            m_pNode = DM_NULL;
        }
        iterator(typename MAP_TREE_T::AvlNode* i_pNode)
        {
            m_pNode = i_pNode;
        }
        iterator(const iterator& i_It)
        {
            m_pNode = i_It.m_pNode;
        }
        iterator& operator=(const iterator& i_It)
        {
            m_pNode = i_It.m_pNode;
            return *this;
        }
        Bool operator==(const iterator& i_It) const
        {
            return m_pNode == i_It.m_pNode;
        }
        Bool operator!=(const iterator& i_It) const
        {
            return m_pNode != i_It.m_pNode;
        }
        Bool isNull() const
        {
            return m_pNode == DM_NULL;
        }
        iterator& operator++()
        {
            DOME_ASSERT(m_pNode);
            typename MAP_TREE_T::AvlNode* l_pParent = DM_NULL;
            if(m_pNode->m_pRight)
            {
                m_pNode = m_pNode->m_pRight;
                while(m_pNode->m_pLeft)
                    m_pNode = m_pNode->m_pLeft;
                return *this;
            }
            if(m_pNode->m_pParent == DM_NULL)
            {
                m_pNode = DM_NULL;
                return *this;
            }
            l_pParent = m_pNode->m_pParent;
            while(l_pParent)
            {
                if(l_pParent->m_pLeft == m_pNode)
                {
                    m_pNode = l_pParent;
                    return *this;
                }
                m_pNode = l_pParent;
                l_pParent = m_pNode->m_pParent;
            }
            m_pNode = DM_NULL;
            return *this;
        }
        iterator operator++(int)
        {
            iterator l_pOldIt(*this);
            operator++();
            return l_pOldIt;
        }
        iterator& operator--()
        {
            DOME_ASSERT(m_pNode);
            typename MAP_TREE_T::AvlNode* l_pParent = DM_NULL;
            if(m_pNode->m_pLeft)
            {
                m_pNode = m_pNode->m_pLeft;
                while(m_pNode->m_pRight)
                    m_pNode = m_pNode->m_pRight;
                return *this;
            }
            if(m_pNode->m_pParent == DM_NULL)
            {
                m_pNode = DM_NULL;
                return *this;
            }
            l_pParent = m_pNode->m_pParent;
            while(l_pParent)
            {
                if(l_pParent->m_pRight == m_pNode)
                {
                    m_pNode = l_pParent;
                    return *this;
                }
                m_pNode = l_pParent;
                l_pParent = m_pNode->m_pParent;
            }
            m_pNode = DM_NULL;
            return *this;
        }
        iterator operator--(int)
        {
            iterator l_pOldIt(*this);
            operator--();
            return l_pOldIt;
        }

        TREE_VALUE_T& operator*()
        {
            DOME_ASSERT(m_pNode);
            return m_pNode->m_Value;
        }

        TREE_VALUE_T* operator->()
        {
            DOME_ASSERT(m_pNode);
            return &m_pNode->m_Value;
        }

        operator TREE_VALUE_T* ()
        {
            if(m_pNode)
            {
                return &m_pNode->m_Value;
            }
            else
            {
                return DM_NULL;
            }
        }

        TREE_VALUE_T* getPtr()
        {
            return operator TREE_VALUE_T*();
        }

    protected:
        friend class TMap;
        typename MAP_TREE_T::AvlNode*        m_pNode;
    };

    class const_iterator
    {
    public:
        const_iterator(const iterator& i_It)
        {
            m_pNode = i_It.m_pNode;
        }
        const_iterator& operator=(const iterator& i_It)
        {
            m_pNode = i_It.m_pNode;
            return *this;
        }


        const_iterator()
        {
            m_pNode = DM_NULL;
        }
        const_iterator(const typename MAP_TREE_T::AvlNode* i_pNode)
        {
            m_pNode = i_pNode;
        }
        const_iterator(const const_iterator& i_pIt)
        {
            m_pNode = i_pIt.m_pNode;
        }
        const_iterator& operator=(const const_iterator& i_It)
        {
            m_pNode = i_It.m_pNode;
            return *this;
        }
        Bool operator==(const const_iterator& i_It) const
        {
            return m_pNode == i_It.m_pNode;
        }
        Bool operator!=(const const_iterator& i_It) const
        {
            return m_pNode != i_It.m_pNode;
        }
        Bool isNull() const
        {
            return m_pNode == DM_NULL;
        }
        const_iterator& operator++()
        {
            DOME_ASSERT(m_pNode);
            const typename MAP_TREE_T::AvlNode* l_pParent = DM_NULL;
            if(m_pNode->m_pRight)
            {
                m_pNode = m_pNode->m_pRight;
                while(m_pNode->m_pLeft)
                    m_pNode = m_pNode->m_pLeft;
                return *this;
            }
            if(m_pNode->m_pParent == DM_NULL)
            {
                m_pNode = DM_NULL;
                return *this;
            }
            l_pParent = m_pNode->m_pParent;
            while(l_pParent)
            {
                if(l_pParent->m_pLeft == m_pNode)
                {
                    m_pNode = l_pParent;
                    return *this;
                }
                m_pNode = l_pParent;
                l_pParent = m_pNode->m_pParent;
            }
            m_pNode = DM_NULL;
            return *this;
        }
        const_iterator operator++(int)
        {
            const_iterator l_pOldIt(*this);
            operator++();
            return l_pOldIt;
        }
        const_iterator& operator--()
        {
            DOME_ASSERT(m_pNode);
            const typename MAP_TREE_T::AvlNode* l_pParent = DM_NULL;
            if(m_pNode->m_pLeft)
            {
                m_pNode = m_pNode->m_pLeft;
                while(m_pNode->m_pRight)
                    m_pNode = m_pNode->m_pRight;
                return *this;
            }
            if(m_pNode->m_pParent == DM_NULL)
            {
                m_pNode = DM_NULL;
                return *this;
            }
            l_pParent = m_pNode->m_pParent;
            while(l_pParent)
            {
                if(l_pParent->m_pRight == m_pNode)
                {
                    m_pNode = l_pParent;
                    return *this;
                }
                m_pNode = l_pParent;
                l_pParent = m_pNode->m_pParent;
            }
            m_pNode = DM_NULL;
            return *this;
        }
        const_iterator operator--(int)
        {
            const_iterator l_pOldIt(*this);
            operator--();
            return l_pOldIt;
        }

        const TREE_VALUE_T& operator*() const
        {
            DOME_ASSERT(m_pNode);
            return m_pNode->m_Value;
        }

        const TREE_VALUE_T* operator->() const
        {
            DOME_ASSERT(m_pNode);
            return &m_pNode->m_Value;
        }

        operator const TREE_VALUE_T* () const
        {
            if(m_pNode)
            {
                return &m_pNode->m_Value;
            }
            else
            {
                return DM_NULL;
            }
        }

        const TREE_VALUE_T* getPtr() const
        {
            return operator const TREE_VALUE_T*();
        }

    protected:
        friend class TMap;
        const typename MAP_TREE_T::AvlNode*      m_pNode;
    };

public:
    TMap()
    {
        init();
    }

    TMap(const TMap& i_Other)
    {
        init();
        copyFrom(i_Other);
    }

    TMap& operator= (const TMap& i_Other)
    {
        copyFrom(i_Other);
        return *this;
    }

    Int validMap() const
    {
        return m_AvlTree.validTree();
    }

    Int size() const
    {
        return m_AvlTree.getNodeCount();
    }

    Bool empty() const
    {
        return size() == 0;
    }

    void clear()
    {
        reset();
    }

    iterator begin()
    {
        if(empty())
            return end();
        return iterator(m_AvlTree.getMostLeftNode(m_AvlTree.getRootNode()));
    }

    const_iterator begin() const
    {
        if(empty())
            return end();
        return const_iterator(m_AvlTree.getMostLeftNode(m_AvlTree.getRootNode()));
    }

    const_iterator cbegin() const
    {
        if(empty())
            return cend();
        return const_iterator(m_AvlTree.getMostLeftNode(m_AvlTree.getRootNode()));
    }

    iterator end()
    {
        return iterator(DM_NULL);
    }

    const_iterator end() const
    {
        return const_iterator(DM_NULL);
    }

    const_iterator cend() const
    {
        return const_iterator(DM_NULL);
    }

    iterator find(const MAP_KEY_T& i_Key)
    {
        typename MAP_TREE_T::AvlNode* l_pNode = m_AvlTree.findNode(i_Key, DM_FALSE, DM_NULL);
        return iterator(l_pNode);
    }

    const_iterator find(const MAP_KEY_T& i_Key) const
    {
        const typename MAP_TREE_T::AvlNode* l_pNode = m_AvlTree.findNode(i_Key);
        return const_iterator(l_pNode);
    }

    iterator get(const MAP_KEY_T& i_Key, Bool* o_bFound = DM_NULL)
    {
        typename MAP_TREE_T::AvlNode* l_pNode = m_AvlTree.findNode(i_Key, DM_TRUE, o_bFound);
        DOME_ASSERT(l_pNode);
        return iterator(l_pNode);
    }

    iterator insert(const MAP_KEY_T& i_Key, const MAP_VALUE_T& i_Value)
    {
        Bool l_bFind = DM_FALSE;
        typename MAP_TREE_T::AvlNode* l_pNode = m_AvlTree.findNode(i_Key, DM_TRUE, &l_bFind);
        DOME_ASSERT(l_pNode);
        l_pNode->m_Value.second = i_Value;
        return iterator(l_pNode);
    }

    void erase(const MAP_KEY_T& i_Key)
    {
        m_AvlTree.deleteNode(i_Key);
    }

    void erase(iterator i_It)
    {
        m_AvlTree.deleteNode(i_It->first);
    }

    MAP_VALUE_T& operator[] (const MAP_KEY_T& i_Key)
    {
        typename MAP_TREE_T::AvlNode* l_pNode = m_AvlTree.findNode(i_Key, DM_TRUE);
        DOME_ASSERT(l_pNode);
        return l_pNode->m_Value.second;
    }

    const MAP_VALUE_T& operator[] (const MAP_KEY_T& i_Key) const
    {
        typename const MAP_TREE_T::AvlNode* l_pNode = m_AvlTree.findNode(i_Key);
        DOME_ERROR(l_pNode);
        return l_pNode->m_Value.second;
    }


private:
    void init()
    {
    }

    void reset()
    {
        m_AvlTree.clear();
    }

    void copyFrom(const TMap& i_Other)
    {
        m_AvlTree.copyFrom(i_Other.m_AvlTree);
    }

private:
    MAP_TREE_T              m_AvlTree;
};


DOME_NAMESPACE_END


//#endif
