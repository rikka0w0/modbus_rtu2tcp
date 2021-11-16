#include "esp_log.h"
#include <stdio.h>
#include <string.h> // memcpy

#include "lwip/sockets.h"

#include "modbus_tcp_server.h"
#include "modbus.h"

// To maintain compatibility, the response will be send to the
// TCP client via a single send(). Some MODBUS TCP client assumes
// recv() and send() are paired, this is a common mistake,
// TCP streams bytes, recv() and send() does not define packets.
// A correct TCP client implementation should allow a frame to be
// sent via multiple send().
void tcp_server_send_response(const rtu_session_t* session_header, void* payload, size_t len) {
    mbap_header_t* resp_header = (mbap_header_t*) payload;
    resp_header->transaction_id = session_header->transaction_id;
    resp_header->protocol_id = session_header->protocol_id;
    resp_header->length = len + MODBUS_TCP_PAYLOAD_OFFSET;
    resp_header->uid = session_header->uid;
    mbap_header_hton(resp_header);
    send(session_header->socket, payload, len, 0);
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
    mbap_header_t header;
    memcpy(&header, buf, len);
    mbap_header_ntoh(&header);

    rtu_session_t session_header;
    session_header.socket = client_socket;
    session_header.transaction_id = header.transaction_id;
    session_header.protocol_id = header.protocol_id;
    session_header.uid = header.uid;

    size_t payload_len = sizeof(rtu_session_t) + len - MODBUS_TCP_PAYLOAD_OFFSET;
    uint8_t payload[sizeof(rtu_session_t) + MODBUS_RTU_PDU_MAXLEN];
    memcpy(payload, &session_header, sizeof(rtu_session_t));
    memcpy(payload+sizeof(rtu_session_t), ((uint8_t*)buf) + MODBUS_TCP_PAYLOAD_OFFSET, len - MODBUS_TCP_PAYLOAD_OFFSET);
    modbus_uart_queue_send(payload, payload_len);
}
