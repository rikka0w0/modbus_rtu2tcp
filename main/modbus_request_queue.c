#include "esp_log.h"
#include <stdio.h>
#include <string.h> // memcpy

#include "lwip/sockets.h"

#include "modbus_tcp_server.h"
#include "modbus.h"

#define MODBUS_TCP_PAYLOAD_OFFSET 6

void tcp_server_send_response(const rtu_session_t* session_header, const void* payload, size_t len) {
#ifdef MODBUS_BRIDGE_COMPATIBILITY_MODE
    uint8_t buf[MODBUS_RTU_PDU_MAXLEN + sizeof(mbap_header_t)];
#else
    uint8_t buf[sizeof(mbap_header_t)];
#endif
    mbap_header_t* resp_header = (mbap_header_t*) buf;
    resp_header->transaction_id = session_header->transaction_id;
    resp_header->protocol_id = session_header->protocol_id;
    resp_header->length = len + MODBUS_TCP_PAYLOAD_OFFSET;
    resp_header->uid = session_header->uid;
    mbap_header_hton(resp_header);

#ifdef MODBUS_BRIDGE_COMPATIBILITY_MODE
    memcpy(buf + MODBUS_TCP_PAYLOAD_OFFSET, payload, len);
    send(session_header->socket, buf, len + MODBUS_TCP_PAYLOAD_OFFSET, 0);
#else
    send(session_header->socket, resp_header, MODBUS_TCP_PAYLOAD_OFFSET, MSG_MORE);
    send(session_header->socket, payload, len, 0);
#endif
}
//////////////////////
/// Callbacks
//////////////////////
size_t tcp_server_frame_length_from_header(const void* buf, size_t len) {
    mbap_header_t header;
    memcpy(&header, buf, len);
    mbap_header_ntoh(&header);
    return header.length > 0 && header.length < 255 ? header.length + TCP_SERVER_FRAME_HEADER_MIN_LEN() : 0;
}

void tcp_server_client_frame_ready(int client_socket, const void* buf, size_t len) {

}

int tcp_server_client_frame_pop(int client_socket, const void* buf, size_t len) {
    mbap_header_t header;
    memcpy(&header, buf, len);
    mbap_header_ntoh(&header);

    rtu_session_t session_header;
    session_header.socket = client_socket;
    session_header.transaction_id = header.transaction_id;
    session_header.protocol_id = header.protocol_id;
    session_header.uid = header.uid;

    return modbus_uart_fifo_push(&session_header, ((uint8_t*)buf) + MODBUS_TCP_PAYLOAD_OFFSET, len - MODBUS_TCP_PAYLOAD_OFFSET);
}
