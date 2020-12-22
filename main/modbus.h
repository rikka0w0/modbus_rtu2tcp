#ifndef MAIN_MODBUS_H_
#define MAIN_MODBUS_H_

#include <stdint.h>

#define MODBUS_PORT 502

void modbus_tcp_server_create();

//__attribute__ ((packed))
typedef struct mbap_header {
    uint16_t transactionID;
    uint16_t protocolID;
    uint16_t length;
    uint8_t uid;
} mbap_header_t;

#endif /* MAIN_MODBUS_H_ */
