//
//  _avltree.h
//  engine
//
//  Created by Ming Dong on 12-03-19.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine__avltree_h
//#define engine__avltree_h
#include "../typedefs.h"
#include "../error.h"
#include "../math/mathutils.h"
#include "../imemory.h"
#include "array.h"

DOME_NAMESPACE_BEGIN

template<class TREE_VALUE_T, class TREE_KEY_T = TREE_VALUE_T, class TREE_COMP_T = Math::TCompare<TREE_KEY_T, TREE_VALUE_T>, class TREE_ALLOCATOR_T = IDefaultMemManager, Int PAGESIZE = 1>
class TAvlTree
{
public:
    struct AvlFreeNode
    {
        AvlFreeNode*        m_pNextFreeNode;
    };
    struct AvlNode
    {
        AvlNode*            m_pParent;
        AvlNode*            m_pLeft;
        AvlNode*            m_pRight;
        Int                 m_Height;
        TREE_VALUE_T        m_Value;

        AvlNode()
        : m_Value()
        {
            m_pParent = DM_NULL;
            m_pLeft = DM_NULL;
            m_pRight = DM_NULL;
            m_Height = 0;
        }
        AvlNode(const TREE_KEY_T& i_Key)
        : m_Value(i_Key)
        {
            m_pParent = DM_NULL;
            m_pLeft = DM_NULL;
            m_pRight = DM_NULL;
            m_Height = 0;
        }
        AvlNode(const AvlNode& i_Node)
        : m_Value(i_Node.m_Value)
        {
            m_pParent = i_Node.m_pParent;
            m_pLeft = i_Node.m_pLeft;
            m_pRight = i_Node.m_pRight;
            m_Height = i_Node.m_Height;
        }
    };



public:
   TAvlTree()
   {
       init();
   }

   TAvlTree(const TAvlTree& i_Tree)
   {
       init();
       copyFrom(i_Tree);
   }

   TAvlTree& operator= (const TAvlTree& i_Other)
   {
       reset();
       copyFrom(i_Other);
       return *this;
   }

   ~TAvlTree()
   {
       reset();
   }

    void clear()
    {
        reset();
    }

    void reset()
    {
        _removeAllNodes();
        m_pRoot = DM_NULL;
        m_NodeCount = 0;        
    }

    void copyFrom(const TAvlTree& i_Other)
    {
        reset();

        m_pRoot = _cloneNodeAndChild(i_Other.m_pRoot);
        if(m_pRoot)
            m_pRoot->m_pParent = DM_NULL;
        m_NodeCount = i_Other.m_NodeCount;
    }

    Int getNodeCount() const
    {
        return m_NodeCount;
    }

    AvlNode* getRootNode()
    {
        return m_pRoot;
    }

    const AvlNode* getRootNode() const
    {
        return m_pRoot;
    }

    AvlNode* getMostLeftNode(AvlNode* i_pNode)
    {
        AvlNode* l_pResult = i_pNode;
        while(l_pResult->m_pLeft)
        {
            l_pResult = l_pResult->m_pLeft;
        }
        return l_pResult;
    }

    const AvlNode* getMostLeftNode(const AvlNode* i_pNode) const
    {
        const AvlNode* l_pResult = i_pNode;
        while(l_pResult->m_pLeft)
        {
            l_pResult = l_pResult->m_pLeft;
        }
        return l_pResult;
    }

    AvlNode* findNode(const TREE_KEY_T& i_Key, Bool i_bCreate = DM_FALSE, Bool* o_bFind = DM_NULL)
    {
        // First, find the right node
        Int   l_FindResult = 0;     // 1: found the node, 2: Add to left 3: Add to right
        AvlNode* l_pNode = m_pRoot;
        AvlNode* l_pNewNode = DM_NULL;

        if(o_bFind)
            *o_bFind = DM_TRUE;

        if(!l_pNode)
        {
            if(o_bFind)
                *o_bFind = DM_FALSE;

            if(i_bCreate)
            {
                m_pRoot = _allocNode(AvlNode(i_Key));
                m_NodeCount = 1;
            }
            return m_pRoot;
        }

        while(DM_TRUE)
        {
            if(TREE_COMP_T::Less(i_Key, l_pNode->m_Value))
            {
                AvlNode* l_pLeft = l_pNode->m_pLeft;
                if(l_pLeft)
                    l_pNode = l_pLeft;
                else
                {
                    l_FindResult = 2;
                    break;
                }
            }
            else if(TREE_COMP_T::Greater(i_Key, l_pNode->m_Value))
            {
                AvlNode* l_pRight = l_pNode->m_pRight;
                if(l_pRight)
                    l_pNode = l_pRight;
                else
                {
                    l_FindResult = 3;
                    break;
                }
            }
            else
            {
                l_FindResult = 1;
                break;
            }
        }
        
        //
        if(l_FindResult != 1 && o_bFind)
        {
            *o_bFind = DM_FALSE;
        }

        if(l_FindResult == 1)
        {
            return l_pNode;
        }
        else if(!i_bCreate)
            return DM_NULL;
        else if(l_FindResult == 2)
        {
            l_pNewNode = _allocNode(AvlNode(i_Key));
            l_pNode->m_pLeft = l_pNewNode;
            l_pNewNode->m_pParent = l_pNode;
            m_NodeCount ++;
        }
        else
        {
            l_pNewNode = _allocNode(AvlNode(i_Key));
            l_pNode->m_pRight = l_pNewNode;
            l_pNewNode->m_pParent = l_pNode;
            m_NodeCount ++;
        }
        
        _updateFrom(l_pNode);
        return l_pNewNode;
    }

    const AvlNode* findNode(const TREE_KEY_T& i_Key) const
    {
        Bool l_bFind = DM_FALSE;
        const AvlNode* l_pNode = m_pRoot;
        
        while(l_pNode && !l_bFind)
        {
            if(TREE_COMP_T::Less(i_Key, l_pNode->m_Value))
                l_pNode = l_pNode->m_pLeft;
            else if(TREE_COMP_T::Greater(i_Key, l_pNode->m_Value))
                l_pNode = l_pNode->m_pRight;
            else
                l_bFind = DM_TRUE;
        }
        
        if(l_bFind)
        {
            return l_pNode;
        }
        else
            return DM_NULL;
    }

    Bool deleteNode(const TREE_KEY_T& i_Key)
    {
        AvlNode* l_pNode = findNode(i_Key, DM_FALSE);
        if(l_pNode)
            return _deleteNode(l_pNode);
        else
            return DM_FALSE;
    }

    Int validTree() const
    {
        Int l_Ret = _validTree(m_pRoot);
        DOME_ASSERT( l_Ret == m_NodeCount);
        return l_Ret;
    }

private:
    Int _validTree(const AvlNode* i_pNode) const
    {
        Int l_NodeCount = 0;

        if (i_pNode == DM_NULL)
        {
            return 0;
        }

        l_NodeCount ++;

        // if the node is root node, parent node must be NULL
        if (i_pNode == m_pRoot)
        {
            DOME_ASSERT(i_pNode->m_pParent == DM_NULL);
        }

        if (i_pNode->m_pLeft)
        {
            DOME_ASSERT(i_pNode->m_pLeft->m_pParent == i_pNode);
        }

        if (i_pNode->m_pRight)
        {
            DOME_ASSERT(i_pNode->m_pRight->m_pParent == i_pNode);
        }

        l_NodeCount += _validTree(i_pNode->m_pLeft);
        l_NodeCount += _validTree(i_pNode->m_pRight);

        Int l_LeftHeight = _getNodeHeight(i_pNode->m_pLeft);
        Int l_RightHeight = _getNodeHeight(i_pNode->m_pRight);
        Int l_NodeHeight = Math::Max(l_LeftHeight, l_RightHeight) + 1;
        DOME_ASSERT(l_NodeHeight == i_pNode->m_Height);
        Int l_Diff = l_LeftHeight - l_RightHeight;
        DOME_ASSERT(l_Diff >= -1 && l_Diff <= 1);

        return l_NodeCount;
    }

    Bool _deleteNode(AvlNode* i_pNode)
    {
        AvlNode* l_pNode = i_pNode;
        
        if(!l_pNode)
            return DM_FALSE;
        
        if(l_pNode->m_pLeft == DM_NULL || l_pNode->m_pRight == DM_NULL)
        {
            // Now the l_pNode can at most have 1 child
            AvlNode* l_pParent = l_pNode->m_pParent;
            AvlNode* l_pChild = l_pNode->m_pLeft;
            if(!l_pChild)
                l_pChild = l_pNode->m_pRight;
            
            if(l_pChild)
                l_pChild->m_pParent = l_pParent;
            if(l_pParent)
            {
                if(l_pParent->m_pLeft == l_pNode)
                    l_pParent->m_pLeft = l_pChild;
                else
                    l_pParent->m_pRight = l_pChild;
            }
            else
            {
                m_pRoot = l_pChild;
            }
            _freeNode(l_pNode);
            m_NodeCount --;
            _updateFrom(l_pParent);
            
        }
        else
        {
            AvlNode* l_pReplace = DM_NULL;
            Int l_HLeft = l_pNode->m_pLeft->m_Height;
            Int l_HRight = l_pNode->m_pRight->m_Height;
            if(l_HLeft > l_HRight)
            {
                // Find most right element in left tree
                l_pReplace = l_pNode->m_pLeft;
                while(l_pReplace->m_pRight)
                    l_pReplace = l_pReplace->m_pRight;
            }
            else
            {
                // Find most left element in right tree
                l_pReplace = l_pNode->m_pRight;
                while(l_pReplace->m_pLeft)
                    l_pReplace = l_pReplace->m_pLeft;
            }
            
            // Remove l_pReplace node
            AvlNode* l_pParent = l_pReplace->m_pParent;
            AvlNode* l_pChild = l_pReplace->m_pLeft;
            if(!l_pChild)
                l_pChild = l_pReplace->m_pRight;
            
            if(l_pChild)
                l_pChild->m_pParent = l_pParent;
            
            if(l_pParent->m_pLeft == l_pReplace)
                l_pParent->m_pLeft = l_pChild;
            else 
                l_pParent->m_pRight = l_pChild;
            
            m_NodeCount --;
            _updateFrom(l_pParent);
            
            // Replace l_pNode with l_pReplace
            l_pParent = l_pNode->m_pParent;
            if(l_pParent)
            {
                if(l_pParent->m_pLeft == l_pNode)
                    l_pParent->m_pLeft = l_pReplace;
                else
                    l_pParent->m_pRight = l_pReplace;
                
                l_pReplace->m_pParent = l_pParent;
            }
            else
            {
                m_pRoot = l_pReplace;
                l_pReplace->m_pParent = DM_NULL;
            }
            
            AvlNode* l_pLeft = l_pNode->m_pLeft;
            if(l_pLeft)
                l_pLeft->m_pParent = l_pReplace;
            l_pReplace->m_pLeft = l_pLeft;
            
            AvlNode* l_pRight = l_pNode->m_pRight;
            if(l_pRight)
                l_pRight->m_pParent = l_pReplace;
            l_pReplace->m_pRight = l_pRight;
            
            // Important, set the node height
            l_pReplace->m_Height = l_pNode->m_Height;
            
            // free the node
            _freeNode(l_pNode);
        }
        
        
        return DM_TRUE;
    }

protected:
    void init()
    {
       _initAllocation();
       m_pRoot = DM_NULL;
       m_NodeCount = 0;
    }



private:
    // Node allocation helper functions
    struct _MemPageHeader
    {
        _MemPageHeader*     m_pNextPage;
        AvlFreeNode*        m_pFreeNodeHead;
        Int                 m_FreeNodeCount;
        Int                 m_Padding;
    };
    _MemPageHeader*            m_pFirstMemPage;

    void _initAllocation()
    {
        m_pFirstMemPage = DM_NULL;
    }
    void _deinitAllocation()
    {
        _MemPageHeader* l_pPage = m_pFirstMemPage;
        while(l_pPage)
        {
            DOME_ASSERT(l_pPage->m_FreeNodeCount == PAGESIZE);
            _MemPageHeader* l_pNextPage = l_pPage->m_pNextPage;
            DOME_FreeEx(l_pPage, TREE_ALLOCATOR_T);
            l_pPage = l_pNextPage;
        }
        m_pFirstMemPage = DM_NULL;
    }
    void _constructNode(AvlNode* i_pNode)
    {
        DOME_NewPlacement(AvlNode, i_pNode);
    }
    void _copyConstructNode(AvlNode* i_pNode, const AvlNode& i_Value)
    {
        DOME_NewPlacement(AvlNode, i_pNode)(i_Value);
    }
    void _destructNode(AvlNode* i_pNode)
    {
        i_pNode->~AvlNode();
    }
    AvlNode* _allocNode(const AvlNode& i_Node = AvlNode())
    {
        if(PAGESIZE == 1)
        {
            AvlNode* l_pNode = (AvlNode*)DOME_AllocEx(sizeof(AvlNode), TREE_ALLOCATOR_T);
            _copyConstructNode(l_pNode, i_Node);
            return l_pNode;
        }

        // first, try to allocate a node from existing pages
        _MemPageHeader* l_pPage = m_pFirstMemPage;
        while(l_pPage)
        {
            if(l_pPage->m_FreeNodeCount > 0)
            {
                DOME_ASSERT(l_pPage->m_pFreeNodeHead);

                AvlNode* l_pNode = (AvlNode*)l_pPage->m_pFreeNodeHead;

                l_pPage->m_pFreeNodeHead = l_pPage->m_pFreeNodeHead->m_pNextFreeNode;
                l_pPage->m_FreeNodeCount --;

                _copyConstructNode(l_pNode, i_Node);
                return l_pNode;
            }

            l_pPage = l_pPage->m_pNextPage;
        }

        // there is no free node in existing pages, alloacate a new page
        _MemPageHeader* l_pNewPage = (_MemPageHeader*)DOME_AllocEx(sizeof(_MemPageHeader) + sizeof(AvlNode) * PAGESIZE, TREE_ALLOCATOR_T);
        AvlNode* l_NodeArray = (AvlNode*)(l_pNewPage + 1);
        l_pNewPage->m_FreeNodeCount = PAGESIZE - 1;
        l_pNewPage->m_pFreeNodeHead = DM_NULL;

        for(Int i = 1; i < PAGESIZE; ++i)
        {
            AvlFreeNode* l_pFreeNode = (AvlFreeNode*)(l_NodeArray + i);
            l_pFreeNode->m_pNextFreeNode = l_pNewPage->m_pFreeNodeHead;
            l_pNewPage->m_pFreeNodeHead = l_pFreeNode;
        }

        l_pNewPage->m_pNextPage = m_pFirstMemPage;
        m_pFirstMemPage = l_pNewPage;
        
        AvlNode* l_pResult = l_NodeArray;
        _copyConstructNode(l_pResult, i_Node);
        return l_pResult;
    }

    void _freeNode(AvlNode* i_pNode)
    {
        if(PAGESIZE == 1)
        {
            _destructNode(i_pNode);
            DOME_FreeEx(i_pNode, TREE_ALLOCATOR_T);
            return ;
        }

        // find which page this node is belong to
        _MemPageHeader* l_pPrevPage = DM_NULL;
        _MemPageHeader* l_pPage = m_pFirstMemPage;
        while(l_pPage)
        {
            AvlNode* l_pFirstNode = (AvlNode*)(l_pPage + 1);
            Int l_Offset = i_pNode - l_pFirstNode;
            if(l_Offset >= 0 && l_Offset < PAGESIZE)
            {
                // first destruct the object
                _destructNode(i_pNode);

                // add the free node to free node list
                AvlFreeNode* l_pFreeNode = (AvlFreeNode*)i_pNode;
                l_pFreeNode->m_pNextFreeNode = l_pPage->m_pFreeNodeHead;
                l_pPage->m_pFreeNodeHead = l_pFreeNode;
                l_pPage->m_FreeNodeCount ++;

                // if all the nodes in this page are free node, free this page
                if(l_pPage->m_FreeNodeCount == PAGESIZE)
                {
                    if(l_pPrevPage)
                        l_pPrevPage->m_pNextPage = l_pPage->m_pNextPage;
                    else
                        m_pFirstMemPage->m_pNextPage = l_pPage->m_pNextPage;

                    DOME_FreeEx(l_pPage, TREE_ALLOCATOR_T);
                }
                return;
            }
            else
            {
                l_pPrevPage = l_pPage;
                l_pPage = l_pPage->m_pNextPage;
            }
        }

        // the code shouldn't execute here.
        DOME_ASSERT(0);
    }

private:// node helper functions
    // remove all nodes
    void _removeAllNodes()
    {
        AvlNode* l_pCurNode = m_pRoot;
        while(l_pCurNode)
        {
            if(l_pCurNode->m_pLeft)
                l_pCurNode = l_pCurNode->m_pLeft;
            else if(l_pCurNode->m_pRight)
                l_pCurNode = l_pCurNode->m_pRight;
            else
            {
                AvlNode* l_pParent = l_pCurNode->m_pParent;
                if(l_pParent)
                {
                    if(l_pParent->m_pLeft == l_pCurNode)
                        l_pParent->m_pLeft = DM_NULL;
                    else
                        l_pParent->m_pRight = DM_NULL;
                }
                _freeNode(l_pCurNode);
                l_pCurNode = l_pParent;
            }
        }
        m_pRoot = DM_NULL;
        m_NodeCount = 0;
    }

    // after you get the cloned node, you should set its parent node property
    AvlNode* _cloneNodeAndChild(const AvlNode* i_pNode)
    {
        if(i_pNode)
        {
            AvlNode* l_pCloneNode = _allocNode(*i_pNode);
            const AvlNode* l_pLeft = i_pNode->m_pLeft;
            const AvlNode* l_pRight = i_pNode->m_pRight;
            
            if(l_pLeft)
            {
                AvlNode* l_pCloneLeft = _cloneNodeAndChild(l_pLeft);
                l_pCloneLeft->m_pParent = l_pCloneNode;
                l_pCloneNode->m_pLeft = l_pCloneLeft;
            }
            
            if(l_pRight)
            {
                AvlNode* l_pCloneRight = _cloneNodeAndChild(l_pRight);
                l_pCloneRight->m_pParent = l_pCloneNode;
                l_pCloneNode->m_pRight = l_pCloneRight;
            }
            
            return l_pCloneNode;
        }
        return DM_NULL;
    }

    // leaf node's height is ZERO
    Int _getNodeHeight(AvlNode* i_pNode) const
    {
        if(i_pNode)
            return i_pNode->m_Height;
        else
            return -1;
    }
    
    void _computeNodeHeight(AvlNode* i_pNode)
    {
        if(!i_pNode)
            return ;
        
        i_pNode->m_Height = Math::Max( _getNodeHeight(i_pNode->m_pLeft), _getNodeHeight(i_pNode->m_pRight) ) + 1;
    }
    
    // When this function is called
    // 1) The left tree should be a balance tree
    // 2) The right tree should be a balance tree
    // 3) The height difference between left and right tree shouldn't more than 2
    // 4) the i_pNode's height is the value which keep the original tree balance, but now may contain wrong information because the modification of its child
    void _updateFrom(AvlNode* i_pNode)
    {
        AvlNode* l_pCurNode = i_pNode;
        while(l_pCurNode)
        {
            Int l_HeightLeft = _getNodeHeight(l_pCurNode->m_pLeft);
            Int l_HeightRight = _getNodeHeight(l_pCurNode->m_pRight);
            
            Int l_OrigHeight = l_pCurNode->m_Height;
            // Set the correct new height anyway, before rotating.
            l_pCurNode->m_Height = Math::Max(l_HeightLeft, l_HeightRight) + 1;
            Int l_Diff = l_HeightLeft - l_HeightRight;
            DOME_ASSERT(l_Diff >= -2 && l_Diff <= 2);
            if(l_Diff == -2)
            {
                AvlNode* l_pRight = l_pCurNode->m_pRight;
                DOME_ASSERT(l_pRight);
                Int l_HRL = _getNodeHeight(l_pRight->m_pLeft);
                Int l_HRR = _getNodeHeight(l_pRight->m_pRight);
                Int l_RDiff = l_HRL - l_HRR;
                DOME_ASSERT(l_RDiff >= -1 && l_RDiff <= 1);
                if(l_RDiff == 1)
                    l_pRight = _rotate(l_pRight, DM_FALSE);
                
                l_pCurNode = _rotate(l_pCurNode, DM_TRUE);
            }
            else if(l_Diff == 2)
            {
                AvlNode* l_pLeft = l_pCurNode->m_pLeft;
                DOME_ASSERT(l_pLeft);
                Int l_HLL = _getNodeHeight(l_pLeft->m_pLeft);
                Int l_HLR = _getNodeHeight(l_pLeft->m_pRight);
                Int l_LDiff = l_HLL - l_HLR;
                DOME_ASSERT(l_LDiff >= -1 && l_LDiff <= 1);
                if(l_LDiff == -1)
                    l_pLeft = _rotate(l_pLeft, DM_TRUE);
                
                l_pCurNode = _rotate(l_pCurNode, DM_FALSE);
            }
            
            if(l_OrigHeight == l_pCurNode->m_Height)
                break;
            
            l_pCurNode = l_pCurNode->m_pParent;
        }
    }
    
    // The height of i_pNode is not necessary correct when this function is called
    // But the left and right sub tree's height value should be correct
    // And when this function returned, everything should be fixed except i_pNode's parent height value.
    /*
              A
             / \
            B   C
           / \ / \
         B1 B2 C1 C2

         When rotate to left
         1) A C C1  node's parent/child relationship are changed
         2) A C     node's height are changed
                C
               / \
              A   C2
             / \
            B   C1
           / \
          B1  B2

         When rotate to right
         1) A B B2  node's parent/child relationship are changed
         2) A B     node's height are changed
                B
               / \
              B1  A
                 / \
               B2   C
                   / \
                  C1  C2
    */
    AvlNode* _rotate(AvlNode* i_pNode, bool i_bToLeft)
    {
        if(!i_pNode)
            return DM_NULL;
        
        AvlNode* l_pParent = i_pNode->m_pParent;
        Bool  l_bLeft;
        AvlNode* l_pA = i_pNode;
        AvlNode* l_pB = l_pA->m_pLeft;
        AvlNode* l_pC = l_pA->m_pRight;
        AvlNode* l_pB2 = l_pB ? l_pB->m_pRight : DM_NULL;
        AvlNode* l_pC1 = l_pC ? l_pC->m_pLeft : DM_NULL;
        AvlNode* l_pTopNode = DM_NULL;
        
        if(l_pParent)
        {
            if(l_pParent->m_pLeft == l_pA)
                l_bLeft = DM_TRUE;
            else
                l_bLeft = DM_FALSE;
        }
        
        if(i_bToLeft)
        {
            if(!l_pC)
            {
                // the right child tree is NULL, it can't be rotate to left
                // maybe something is wrong here
                // fix l_pA's height value
                DOME_WARNING(0);
                _computeNodeHeight(l_pA);
                return l_pA;
            }
            
            l_pA->m_pRight = l_pC1;
            if(l_pC1)
                l_pC1->m_pParent = l_pA;
            l_pC->m_pLeft = l_pA;
            l_pA->m_pParent = l_pC;
            _computeNodeHeight(l_pA);
            _computeNodeHeight(l_pC);
            
            l_pTopNode = l_pC;
        }
        else
        {
            if(!l_pB)
            {
                // the left child tree is NULL, it can't be rotate to right
                // maybe something is wrong here
                // fix l_pA's height value
                DOME_WARNING(0);
                _computeNodeHeight(l_pA);
                return l_pA;
            }
            
            l_pA->m_pLeft = l_pB2;
            if(l_pB2)
                l_pB2->m_pParent = l_pA;
            l_pB->m_pRight = l_pA;
            l_pA->m_pParent = l_pB;
            _computeNodeHeight(l_pA);
            _computeNodeHeight(l_pB);
            
            l_pTopNode = l_pB;
        }
        
        if(l_pParent)
        {
            if(l_bLeft)
                l_pParent->m_pLeft = l_pTopNode;
            else
                l_pParent->m_pRight = l_pTopNode;
            l_pTopNode->m_pParent = l_pParent;
        }
        else
        {
            m_pRoot = l_pTopNode;
            l_pTopNode->m_pParent = DM_NULL;
        }
        
        return l_pTopNode;
    }


private:
    AvlNode*            m_pRoot;
    Int                 m_NodeCount;

public:
};


DOME_NAMESPACE_END

//#endif
