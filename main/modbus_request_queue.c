#include "esp_log.h"

#include "modbus_tcp_server.h"

size_t tcp_server_client_buffer_query(tcp_server_client_state_t* client_state, uint8_t* buf, size_t buflen) {
    size_t maxlen = 0;
    if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        maxlen = client_state->rx_buffer_tail - client_state->rx_buffer_head;
    } else {
        maxlen = TCP_SERVER_CLIENT_RX_BUF - client_state->rx_buffer_head;
        maxlen += client_state->rx_buffer_tail;
    }

    if (buf == NULL)
        return maxlen;

    size_t buf_idx = 0;
    if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        for (size_t i=client_state->rx_buffer_head; i<client_state->rx_buffer_tail && buf_idx < buflen; i++)
            buf[buf_idx++] = client_state->rx_buffer[i];
    } else {
        for (size_t i=client_state->rx_buffer_head; i<TCP_SERVER_CLIENT_RX_BUF && buf_idx < buflen; i++)
            buf[buf_idx++] = client_state->rx_buffer[i];
        // Wrap around
        for (size_t i=0; i<client_state->rx_buffer_tail && buf_idx < buflen; i++)
            buf[buf_idx++] = client_state->rx_buffer[i];
    }

    return buf_idx;
}

size_t tcp_server_client_buffer_pop(tcp_server_client_state_t* client_state, size_t len) {
    size_t maxlen = tcp_server_client_buffer_query(client_state, NULL, 0);
    len = len > maxlen ? maxlen : len;
    client_state->rx_buffer_head += len;
    if (client_state->rx_buffer_head >= TCP_SERVER_CLIENT_RX_BUF)
        client_state->rx_buffer_head -= TCP_SERVER_CLIENT_RX_BUF;
    return len;
}

void tcp_server_client_state_init(tcp_server_client_state_t* client_state) {
    client_state->rx_buffer_head = 0;
    client_state->rx_buffer_tail = 0;
}

size_t tcp_server_client_get_recv_buffer_vacancy(tcp_server_client_state_t* client_state, void** buf_addr) {
    if (client_state->rx_buffer_tail == TCP_SERVER_CLIENT_RX_BUF - 1) {
        if (client_state->rx_buffer_head == 0) {
            *buf_addr = NULL;
            return 0;
        } else {
            *buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            return 1;
        }
    } if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        if (client_state->rx_buffer_head == 0) {
            *buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            return TCP_SERVER_CLIENT_RX_BUF - client_state->rx_buffer_tail - 1;
        } else {
            *buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            return TCP_SERVER_CLIENT_RX_BUF - client_state->rx_buffer_tail;
        }
    } else { // Wrap around
        if (client_state->rx_buffer_tail == client_state->rx_buffer_head - 1) {
            *buf_addr = NULL;
            return 0;
        } else {
            *buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            return client_state->rx_buffer_head - client_state->rx_buffer_tail - 1;
        }
    }
}

void tcp_server_data_arrive(int client_socket, tcp_server_client_state_t* client_state, const void* buf, ssize_t len) {
    // Since we supplied the next continuance memory region of the cyclic buffer in tcp_server_client_get_recv_buffer_vacancy(),
    // Some data are moved into the buffer, but we still need to update the tail pointer
    client_state->rx_buffer_tail += len;
    if (client_state->rx_buffer_tail >= TCP_SERVER_CLIENT_RX_BUF)
        client_state->rx_buffer_tail -= TCP_SERVER_CLIENT_RX_BUF;

    if (tcp_server_client_buffer_query(client_state, NULL, 0) < 5)
        return;

    // Consume and print all data in the buffer
    ESP_LOGI("TCP_RX", "SOP [%d - %d]", client_state->rx_buffer_head, client_state->rx_buffer_tail);
    if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        for (size_t i=client_state->rx_buffer_head; i<client_state->rx_buffer_tail; i++)
            ESP_LOGI("TCP_RX", "[%d] %c", client_socket, client_state->rx_buffer[i]);
    } else {
        for (size_t i=client_state->rx_buffer_head; i<TCP_SERVER_CLIENT_RX_BUF; i++)
            ESP_LOGI("TCP_RX", "[%d] %c", client_socket, client_state->rx_buffer[i]);
        ESP_LOGI("TCP_RX", "Wrap around");
        for (size_t i=0; i<client_state->rx_buffer_tail; i++)
            ESP_LOGI("TCP_RX", "[%d] %c", client_socket, client_state->rx_buffer[i]);
    }
    client_state->rx_buffer_head = client_state->rx_buffer_tail;
    ESP_LOGI("TCP_RX", "EOP [%d - %d]", client_state->rx_buffer_head, client_state->rx_buffer_tail);


    // Echo back
    // if(send(clientSocket, buf, len, 0) == -1)
        // TCPSVR_LOGE("send() error lol!");

    //ESP_LOGI("TCP_RX", "[%d] Receive bytes (%d):", client_socket, len);
    //for (ssize_t i=0; i<len; i++)
        //ESP_LOGI("TCP_RX", "[%d] 0x%02X", client_socket, ((char*)buf)[i]);
}
