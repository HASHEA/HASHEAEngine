#include "../../public/os/os_debug.h"

#if DOME_IS_WINDESKTOP
#include "windows/windt/os_debug.inc"
#else
#error Your OS is not supported
#endif
