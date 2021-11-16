#ifndef MAIN_MODBUS_H_
#define MAIN_MODBUS_H_

#include <stdint.h>
#include <strings.h>

// GPIO ID of the DE pin
#define MODBUS_GPIO_DE_ID 0
// Invert the polarity of the DE pin
#define MODBUS_GPIO_DE_INV 1

#define MODBUS_TCP_PAYLOAD_OFFSET 6
#define MODBUS_RTU_PDU_MAXLEN       252
#define MODBUS_RTU_FRAME_MAXLEN     256
#define MODBUS_RTU_TX_FIFO_LEN      (MODBUS_RTU_FRAME_MAXLEN*2)
#define MODBUS_RTU_TX_DELAY_US_MAX  1024

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

void modbus_uart_init(uint32_t baudrate, uint8_t parity, uint32_t tx_delay);
// Attempt to queue a new request, block the caller task if the queue is full
void modbus_uart_queue_send(const void* buf, size_t len);
// Queue the response to the Tx FIFO of TCP, non-blocking
void tcp_server_send_response(const rtu_session_t* session_header, void* payload, size_t len);

void modbus_uart_set_baudrate(uint32_t baudrate);
void modbus_uart_set_parity(uint8_t parity);
void modbus_uart_set_tx_delay(uint32_t baudrate);

#ifdef MODBUS_DEBUG
void modbus_send_dummy(const rtu_session_t* session_header, uint8_t* rtu_request_payload);
void hexdump(const uint8_t* buf, size_t len);
#endif

#endif /* MAIN_MODBUS_H_ */
