#include "wish_port_config.h"

void user_start_server(void);

/** This defines the maximum amount of TCP clients that can be connected to the TCP server at any given time */
#define USER_MAX_SERVER_TCP_CONNECTIONS WISH_PORT_CONTEXT_POOL_SZ
