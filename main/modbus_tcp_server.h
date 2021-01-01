#ifndef MAIN_MODBUS_TCP_SERVER_H_
#define MAIN_MODBUS_TCP_SERVER_H_

#include <stdint.h>
#include "lwip/sys.h"

#define TCP_SERVER_CLIENT_RX_BUF 512
typedef struct tcp_server_client_state {
    int socket;

    uint8_t rx_buffer[TCP_SERVER_CLIENT_RX_BUF];
    uint8_t rx_buffer_len;
    uint8_t data_ready;
} tcp_server_client_state_t;

void tcp_server_data_arrive(int client_socket, tcp_server_client_state_t* client_state, const char* buf, ssize_t len);

#endif
