#ifndef MAIN_MODBUS_TCP_SERVER_H_
#define MAIN_MODBUS_TCP_SERVER_H_

#include <stdint.h>
#include "lwip/sys.h"

#define TCP_SERVER_DEBUG
#define TCP_SERVER_BLOCKING_MAX_TICK        5000
#define TCP_SERVER_FRAME_PENDING_MAX_TICK   200

#define TCP_SERVER_FRAME_HEADER_MIN_LEN() (6)
#define TCP_SERVER_RXBUF_MAXLEN 32

//////////////////////
/// Callbacks
//////////////////////
size_t tcp_server_frame_length_from_header(const void* buf, size_t len);
// Return 1 if the frame is consumed, 0 if the frame is not consumed (e.g due to next level buffer is full)
int tcp_server_client_frame_ready(int client_socket, const void* buf, size_t len);

#endif
