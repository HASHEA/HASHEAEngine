/*
    filename:       externalfs.cpp
    author:         Ming Dong
    date:           2017-Jun-13
    description:    
*/

#include "../../public/filesys/externalfs.h"

DOME_NAMESPACE_BEGIN

IExternalFS*        g_pExternalFS = DM_NULL;

DOME_CORE_API IExternalFS* DOME_GetExternalFS()
{
    return g_pExternalFS;
}

DOME_CORE_API DResult DOME_SetExternalFS(IExternalFS* i_pFS)
{
    g_pExternalFS = i_pFS;
    return R_SUCCESS;
}



DOME_NAMESPACE_END