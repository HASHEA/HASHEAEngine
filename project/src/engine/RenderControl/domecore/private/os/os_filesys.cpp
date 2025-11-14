/*
    filename:       os_filesys.cpp
    author:         Ming Dong
    date:           2016-MAR-26
    description:    
*/

#include "../../public/os/os_filesys.h"
#include "../../public/container/staticpool.h"

#ifdef DOME_IS_WINDOWS
#include "./windows/windt/os_filesys.inc"
#else
#error Your OS is not supported
#endif
