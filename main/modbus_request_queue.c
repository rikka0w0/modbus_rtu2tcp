#include "esp_log.h"
#include <stdio.h>

#include "modbus_tcp_server.h"
#include "modbus.h"

//////////////////////
/// Utils
//////////////////////
size_t tcp_server_client_buffer_query(tcp_server_client_state_t* client_state, void* buf, size_t buflen) {
    size_t maxlen = 0;
    if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        maxlen = client_state->rx_buffer_tail - client_state->rx_buffer_head;
    } else {
        maxlen = TCP_SERVER_RXBUF_MAXLEN - client_state->rx_buffer_head;
        maxlen += client_state->rx_buffer_tail;
    }

    if (buf == NULL)
        return maxlen;

    size_t buf_idx = 0;
    if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        for (size_t i=client_state->rx_buffer_head; i<client_state->rx_buffer_tail && buf_idx < buflen; i++)
            ((char*)buf)[buf_idx++] = client_state->rx_buffer[i];
    } else {
        for (size_t i=client_state->rx_buffer_head; i<TCP_SERVER_RXBUF_MAXLEN && buf_idx < buflen; i++)
            ((char*)buf)[buf_idx++] = client_state->rx_buffer[i];
        // Wrap around
        for (size_t i=0; i<client_state->rx_buffer_tail && buf_idx < buflen; i++)
            ((char*)buf)[buf_idx++] = client_state->rx_buffer[i];
    }

    return buf_idx;
}

size_t tcp_server_client_buffer_pop(tcp_server_client_state_t* client_state, size_t len) {
    size_t maxlen = tcp_server_client_buffer_query(client_state, NULL, 0);
    len = len > maxlen ? maxlen : len;
    client_state->rx_buffer_head += len;
    if (client_state->rx_buffer_head >= TCP_SERVER_RXBUF_MAXLEN)
        client_state->rx_buffer_head -= TCP_SERVER_RXBUF_MAXLEN;
    return len;
}

//////////////////////
/// Callbacks
//////////////////////
void tcp_server_client_state_init(tcp_server_client_state_t* client_state) {
    client_state->rx_buffer_head = 0;
    client_state->rx_buffer_tail = 0;
    client_state->frame_size = 0;
    client_state->frame_ready = 0;
}

size_t tcp_server_client_get_recv_buffer_vacancy(tcp_server_client_state_t* client_state, void** buf_addr_o) {
    size_t vacancy_len = 0;
    void* buf_addr;
    if (client_state->rx_buffer_tail == TCP_SERVER_RXBUF_MAXLEN - 1) {
        if (client_state->rx_buffer_head == 0) {
            buf_addr = NULL;
            vacancy_len = 0;
        } else {
            buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            vacancy_len = 1;
        }
    } if (client_state->rx_buffer_tail >= client_state->rx_buffer_head) {
        if (client_state->rx_buffer_head == 0) {
            buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            vacancy_len = TCP_SERVER_RXBUF_MAXLEN - client_state->rx_buffer_tail - 1;
        } else {
            buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            vacancy_len = TCP_SERVER_RXBUF_MAXLEN - client_state->rx_buffer_tail;
        }
    } else { // Wrap around
        if (client_state->rx_buffer_tail == client_state->rx_buffer_head - 1) {
            buf_addr = NULL;
            vacancy_len = 0;
        } else {
            buf_addr = &(client_state->rx_buffer[client_state->rx_buffer_tail]);
            vacancy_len = client_state->rx_buffer_head - client_state->rx_buffer_tail - 1;
        }
    }

    if (buf_addr_o != NULL)
        *buf_addr_o = buf_addr;

    return vacancy_len;
}

void tcp_server_recv_success(tcp_server_client_state_t* client_state, const void* buf, ssize_t len) {
    // Since we supplied the next continuous memory region of the cyclic buffer in tcp_server_client_get_recv_buffer_vacancy(),
    // Some data are moved into the buffer, but we still need to update the tail pointer
    client_state->rx_buffer_tail += len;
    if (client_state->rx_buffer_tail >= TCP_SERVER_RXBUF_MAXLEN)
        client_state->rx_buffer_tail -= TCP_SERVER_RXBUF_MAXLEN;
}

// Read the header from the cyclic buffer and then convert it to local endian
static size_t read_mbap_header(tcp_server_client_state_t* client_state, mbap_header_t* header) {
    size_t len = tcp_server_client_buffer_query(client_state, header, sizeof(mbap_header_t));
    mbap_header_ntoh(header);
    return len;
}

static void on_frame_ready(tcp_server_client_state_t* client_state, int client_socket) {
    size_t buflen = tcp_server_client_buffer_query(client_state, NULL, 0);
    uint8_t mybuf[32];
    tcp_server_client_buffer_query(client_state, mybuf, buflen);
    char strbuf[128];
    int idx = 0;
    for (int i=0; i<buflen; i++) {
        idx += snprintf(&(strbuf[idx]), sizeof(strbuf), " %02X", mybuf[i]);
    }
    ESP_LOGI("MODBUS_REQ", "Rx[%d]:%s", client_socket, strbuf);

    // Remove the frame from the cyclic buffer and reset state
    //tcp_server_client_buffer_pop(client_state, buflen);
    //client_state->frame_ready = 0;
    //client_state->frame_size = 0;
}

int tcp_server_framer_run(tcp_server_client_state_t* client_state, int client_socket) {
    mbap_header_t header;
    size_t byte_read;


    if (client_state->frame_ready) {
        on_frame_ready(client_state, client_socket);
        return 1;
    } else {
        if (client_state->frame_size == 0) {
            byte_read = read_mbap_header(client_state, &header);
            if (byte_read >= sizeof(mbap_header_t)) {
                // All header bytes is in the buffer
                client_state->frame_size = header.length + 6;
            }
        }

        // The number of bytes in the buffer
        if (client_state->frame_size > 0) {
            if (tcp_server_client_buffer_query(client_state, NULL, 0) >= client_state->frame_size) {
                client_state->frame_ready = 1;
            }
        }

        if (client_state->frame_ready) {
            on_frame_ready(client_state, client_socket);
            return 1;
        }
    }

    return 0;
}
