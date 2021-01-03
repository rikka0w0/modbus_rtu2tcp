#ifndef MAIN_MODBUS_H_
#define MAIN_MODBUS_H_

#include <stdint.h>

#include "lwip/inet.h"

#define MODBUS_PORT 502

void modbus_tcp_server_create();

//__attribute__ ((packed))
typedef struct mbap_header {
    uint16_t transaction_id;    // Big-endian
    uint16_t protocol_id;       // Big-endian
    uint16_t length;            // Big-endian
    uint8_t uid;
} mbap_header_t;

static inline void mbap_header_ntoh(mbap_header_t* header) {
    header->transaction_id = ntohs(header->transaction_id);
    header->protocol_id = ntohs(header->protocol_id);
    header->length = ntohs(header->length);
};

static inline void mbap_header_hton(mbap_header_t* header) {
    header->transaction_id = htons(header->transaction_id);
    header->protocol_id = htons(header->protocol_id);
    header->length = htons(header->length);
};

void modbus_uart_init();

#endif /* MAIN_MODBUS_H_ */
