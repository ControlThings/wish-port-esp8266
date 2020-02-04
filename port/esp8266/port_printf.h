#pragma once

/* A useful printf macro to which can be easily disabled for production environments */

#define WITH_PORT_PRINTF_OUTPUT 0

#if WITH_PORT_PRINTF_OUTPUT
#define PORT_PRINTF(...) os_printf( __VA_ARGS__)
#else
#define PORT_PRINTF(...)
#endif

