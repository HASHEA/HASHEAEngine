/*
    filename:       os_common.cpp
    author:         Ming Dong
    date:           2016-MAR-22
    description:    
*/
#include "../../public/os/os_common.h"
#include "../../public/os/os_thread.h"
#include "../../public/os/os_filesys.h"

DOME_NAMESPACE_BEGIN

DResult OS_Manager::Init()
{
    return R_SUCCESS;
}

DResult OS_Manager::Uninit()
{
    return R_SUCCESS;
}



DResult OS_Init()
{
    OS_Manager::Init();

    OS_Thread::Init();

    OS_FileSys::Init();

    return R_SUCCESS;
}

DResult OS_Uninit()
{
    OS_FileSys::Uninit();

    OS_Thread::Uninit();

    OS_Manager::Uninit();

    return R_SUCCESS;
}


DOME_NAMESPACE_END