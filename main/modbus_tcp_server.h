#ifndef MAIN_MODBUS_TCP_SERVER_H_
#define MAIN_MODBUS_TCP_SERVER_H_

#include <stdint.h>
#include "lwip/sys.h"

#define TCP_SERVER_DEBUG
#define TCP_SERVER_RXBUF_FULL_RETRY_SEC     0
#define TCP_SERVER_RXBUF_FULL_RETRY_USEC    100000
#define TCP_SERVER_RXBUF_FULL_MAX_MSEC      10000
#define TCP_SERVER_FRAME_PENDING_MAX_MSEC   10000

#define TCP_SERVER_RXBUF_MAXLEN 32
typedef struct tcp_server_client_state {
    // Cyclic buffer
    uint8_t rx_buffer[TCP_SERVER_RXBUF_MAXLEN];
    size_t rx_buffer_head;  // Index of the first data byte
    size_t rx_buffer_tail;  // Index of the first free byte

    size_t frame_size;
    uint8_t frame_ready;
} tcp_server_client_state_t;

//////////////////////
/// Utils
//////////////////////
// Read a given number of bytes from the rx buffer.
// Returns the actual number of bytes read.
// If buf==NULL, return the number of bytes stored in the rx buffer, buflen is ignored in this case.
size_t tcp_server_client_buffer_query(tcp_server_client_state_t* client_state, void* buf, size_t buflen);
// Remove a given number of bytes from the rx buffer.
size_t tcp_server_client_buffer_pop(tcp_server_client_state_t* client_state, size_t len);

//////////////////////
/// Callbacks
//////////////////////
// Initialize the state of the client (rx buffer)
void tcp_server_client_state_init(tcp_server_client_state_t* client_state);

// Called before recv() to query the address and length of the buffer.
// Linear buffer: return the length of the buffer, *buf_addr is the address of the buffer.
// Cyclic buffer: return the length of the next available memory space in the buffer, *buf_addr is the starting address of that memory space.
// Return non-zero to indicate that the buffer can still receive data
// Return 0 to signal that the buffer is full
size_t tcp_server_client_get_recv_buffer_vacancy(tcp_server_client_state_t* client_state, void** buf_addr);

// Called if recv()>0.
// Linear buffer: copy the data from the buffer into other queue.
// Cyclic buffer: The data has been placed into the cyclic buffer, update head and tail pointers.
void tcp_server_recv_success(tcp_server_client_state_t* client_state, const void* buf, ssize_t len);

// Called when:
// 1. new data is added to the buffer
// 2. buffer is full, attempt to pop the current frame from the buffer
// 3. new frame is already prepared, but the next level buffer/queue is full
// Return 0 if:
// 1. frame is not ready
// 2. frame is ready and popped
// Return non-zero if:
// 1. frame is ready but the next level buffer/queue is full, attempt to pop next time
int tcp_server_framer_run(tcp_server_client_state_t* client_state, int client_socket);

#endif
