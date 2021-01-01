#include "esp_log.h"

#include "modbus_tcp_server.h"

void tcp_server_data_arrive(int client_socket, tcp_server_client_state_t* client_state, const char* buf, ssize_t len) {
    // Echo back
    // if(send(clientSocket, buf, len, 0) == -1)
        // TCPSVR_LOGE("send() error lol!");

    ESP_LOGI("TCP_RX", "[%d] Receive bytes (%d):", client_socket, len);
    for (ssize_t i=0; i<len; i++)
        ESP_LOGI("TCP_RX", "[%d] 0x%02X", client_socket, buf[i]);
}
