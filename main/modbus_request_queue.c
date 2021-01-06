#include "esp_log.h"
#include <stdio.h>
#include <string.h> // memcpy

#include "modbus_tcp_server.h"
#include "modbus.h"

//////////////////////
/// Callbacks
//////////////////////
size_t tcp_server_frame_length_from_header(const void* buf, size_t len) {
    mbap_header_t header;
    memcpy(&header, buf, len);
    mbap_header_ntoh(&header);
    return header.length > 0 && header.length < 255 ? header.length + TCP_SERVER_FRAME_HEADER_MIN_LEN() : 0;
}

int tcp_server_client_frame_ready(int client_socket, const void* buf, size_t len) {
    char* mybuf = (char*) buf;
    char strbuf[128];
    int idx = 0;
    for (int i=0; i<len; i++) {
        idx += snprintf(&(strbuf[idx]), sizeof(strbuf), " %02X", mybuf[i]);
    }
    ESP_LOGI("MODBUS_REQ", "Rx[%d]:%s", client_socket, strbuf);

    return 1;
}
