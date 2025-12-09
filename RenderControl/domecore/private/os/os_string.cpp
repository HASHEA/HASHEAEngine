#include "../../public/os/os_string.h"

#if DOME_IS_WINDOWS
#include "./windows/windt/os_string.inc"
#else
#error Your OS is not supported
#endif
