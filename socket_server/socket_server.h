
#ifndef _SOCKET_SERVER_H_
#define _SOCKET_SERVER_H_

#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"

#define SOCKET_SERVER_PORT (8800)
#define SOCKET_SERVER_GREETING "Hello!\n"

extern int mp3_volume;
void start_socket_server(void* arg);

#endif
