#include "../../public/os/os_guid.h"

#if DOME_IS_WINDOWS
#include "./windows/windt/os_guid.inc"
#else
#error Your OS is not supported
#endif