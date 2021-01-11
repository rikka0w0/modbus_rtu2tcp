#ifndef MAIN_MODBUS_H_
#define MAIN_MODBUS_H_

#include <stdint.h>
#include <strings.h>

#define MODBUS_TCP_PAYLOAD_OFFSET 6
#define MODBUS_RTU_PDU_MAXLEN       252
#define MODBUS_RTU_FRAME_MAXLEN     256
#define MODBUS_RTU_TX_FIFO_LEN      (MODBUS_RTU_FRAME_MAXLEN*2)
#define MODBUS_RTU_CHAR_US(baud)    (10*1000000/baud)

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

void modbus_uart_init(uint32_t baudrate, uint8_t parity);
// Attempt to queue a new request to the fifo, non-blocking
int modbus_uart_fifo_push(const rtu_session_t* session_header, const void* payload, size_t len);
// Queue the response to the Tx FIFO of TCP, non-blocking
void tcp_server_send_response(const rtu_session_t* session_header, void* payload, size_t len);

void modbus_uart_set_baudrate(int baudrate);
void modbus_uart_set_parity(uint8_t parity);

#endif /* MAIN_MODBUS_H_ */
