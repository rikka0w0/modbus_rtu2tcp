#ifndef MAIN_MODBUS_TCP_SERVER_H_
#define MAIN_MODBUS_TCP_SERVER_H_

#include <stdint.h>
#include <strings.h>
#include "lwip/sys.h"

#define TCP_SERVER_CLIENT_RX_BUF 16
typedef struct tcp_server_client_state {
    // Cyclic buffer
    uint8_t rx_buffer[TCP_SERVER_CLIENT_RX_BUF];
    size_t rx_buffer_head;  // Index of the first data byte
    size_t rx_buffer_tail;  // Index of the first free byte
} tcp_server_client_state_t;

//////////////////////
/// Utils
//////////////////////
// Read a given number of bytes from the rx buffer.
// Returns the actual number of bytes read.
// If buf==NULL, return the number of bytes stored in the rx buffer, buflen is ignored in this case.
size_t tcp_server_client_buffer_query(tcp_server_client_state_t* client_state, uint8_t* buf, size_t buflen);
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
size_t tcp_server_client_get_recv_buffer_vacancy(tcp_server_client_state_t* client_state, void** buf_addr);

// Called if recv()>0, process framing here.
// Linear buffer: copy the data from the buffer into other queue.
// Cyclic buffer: The data has been placed into the cyclic buffer, update head and tail pointers.
void tcp_server_data_arrive(int client_socket, tcp_server_client_state_t* client_state, const void* buf, ssize_t len);

#endif
