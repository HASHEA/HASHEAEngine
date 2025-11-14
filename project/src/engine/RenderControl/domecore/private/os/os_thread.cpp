/*
    filename:       os_thread.cpp
    author:         Ming Dong
    date:           2016-MAR-10
    description:    
*/
#include "../../public/os/os_thread.h"

#ifdef DOME_IS_WINDOWS
#include "./windows/windt/os_thread.inc"
#else
#error Your OS is not supported
#endif