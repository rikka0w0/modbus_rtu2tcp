#ifndef MAIN_MODBUS_H_
#define MAIN_MODBUS_H_

#include <stdint.h>
#include <strings.h>

// If the compatibility mode is enabled, the response will be send
// to the TCP client via a single send(). Some MODBUS TCP client
// assumes recv() and send() are paired, this is a common mistake,
// TCP streams bytes, recv() and send() does not define packets.
// A correct TCP client implementation should allow a frame to be
// sent via multiple send().
// NOTE: Enable this option will increase the RAM usage (stack),
// the unnecessary memcpy() may compromise the performance.
#define MODBUS_BRIDGE_COMPATIBILITY_MODE

#define MODBUS_RTU_PDU_MAXLEN       252
#define MODBUS_RTU_FRAME_MAXLEN     256
#define MODBUS_RTU_TX_FIFO_LEN      (MODBUS_RTU_FRAME_MAXLEN*2)

//__attribute__ ((packed))
typedef struct mbap_header {
    uint16_t transaction_id;    // Big-endian
    uint16_t protocol_id;       // Big-endian
    uint16_t length;            // Big-endian
    uint8_t uid;
} mbap_header_t;

typedef struct rtu_session {
    int socket;
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint8_t uid;
} rtu_session_t;

uint16_t modbus_rtu_crc16(const uint8_t *data, size_t dat_len);
void mbap_header_ntoh(mbap_header_t* header);
void mbap_header_hton(mbap_header_t* header);

void modbus_uart_init();
// Attempt to queue a new request to the fifo, non-blocking
int modbus_uart_fifo_push(const rtu_session_t* session_header, const void* payload, size_t len);
// Queue the response to the Tx FIFO of TCP, non-blocking
void tcp_server_send_response(const rtu_session_t* session_header, const void* payload, size_t len);

#endif /* MAIN_MODBUS_H_ */
