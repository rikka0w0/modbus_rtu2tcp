#ifndef MAIN_MODBUS_TCP_SERVER_H_
#define MAIN_MODBUS_TCP_SERVER_H_

#include <stdint.h>
#include "lwip/sys.h"

#define TCP_SERVER_PORT 502
#define TCP_SERVER_CONN_MAX 5

#define TCP_SERVER_DEBUG
#define TCP_SERVER_BLOCKING_MAX_TICK        5000
#define TCP_SERVER_FRAME_PENDING_MAX_TICK   200
#define TCP_SERVER_FRAME_RETRY_MAX          4

// The length of header has to be received before determine the frame size.
// Can be less than the actual header.
#define TCP_SERVER_FRAME_HEADER_MIN_LEN() (6)
#define TCP_SERVER_RXBUF_MAXLEN 300

void modbus_tcp_server_create();
//////////////////////
/// Callbacks
//////////////////////
size_t tcp_server_frame_header_min_length();
// Should return the frame length parsed from the header.
size_t tcp_server_frame_length_from_header(const void* buf, size_t len);
// Called when the frame becomes ready (entirely placed in the buffer).
void tcp_server_client_frame_ready(int client_socket, const void* buf, size_t len);
// Return 1 if the frame is consumed, 0 if the frame is not consumed (e.g due to next level buffer is full)
int tcp_server_client_frame_pop(int client_socket, const void* buf, size_t len);

#endif
