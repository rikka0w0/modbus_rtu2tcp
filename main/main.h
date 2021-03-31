#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_

#include <stdint.h>
#include <strings.h>
#include "esp_err.h"

#define IPV4_ADDR_MAXLEN 16
#define IPV6_ADDR_MAXLEN 40
#define IPV6_ADDR_COUNT 3
#define CFG_STORAGE_NAMESPACE "app_cfg"
#define WIFI_SSID_MAXLEN 32
#define WIFI_PASS_MAXLEN 64
#define WIFI_AP_SSID_DEFAULT "Modbus RTU2TCP"
#define WIFI_AP_PASS_DEFAULT "password"
#define WIFI_AP_MAX_CONN_DEFAULT 3

#define UART_BAUD_DEFAULT 115200

enum cfg_data_type {
    CFG_DATA_UNKNOWN = 0,
    CFG_DATA_STR = 1,
    CFG_DATA_U8 = 2,
    CFG_DATA_U32 = 3
};

enum cfg_data_idt {
    CFG_WIFI_SSID,
    CFG_WIFI_PASS,
    CFG_WIFI_STA_MAX_RETRY,
    CFG_WIFI_SSID_AP,
    CFG_WIFI_PASS_AP,
    CFG_WIFI_AUTH_AP,
    CFG_WIFI_MAX_CONN_AP,
    CFG_WIFI_MODE,

    CFG_UART_BAUD,
    CFG_UART_PARITY,
    CFG_UART_TX_DELAY,

    CFG_IDT_MAX
};

typedef struct ip_info {
    char ip4_addr[IPV4_ADDR_MAXLEN];
    char ip4_netmask[IPV4_ADDR_MAXLEN];
    char ip4_gateway[IPV4_ADDR_MAXLEN];
    size_t ip6_count;
    char ip6_addr[IPV6_ADDR_COUNT][IPV6_ADDR_MAXLEN];
} ip_info_t;

// cJSON helpers
// This method assumes the name is a literal, immutable during the entire lifecycle.
#define cJSON_AddStringToObjectCS(object, name, value) (cJSON_AddItemToObjectCS(object, name, cJSON_CreateString(value)))

/**
 * If not set, ssid or pass will be a string with 0 length.
 */
#define cp_is_valid_id(id) (id >= 0 && id < CFG_IDT_MAX)
enum cfg_data_idt cp_id_from_name(const char* name);
char* cp_name_from_id(enum cfg_data_idt id);
esp_err_t cp_get_by_id(enum cfg_data_idt id, void* buf, size_t* maxlen);
esp_err_t cp_set_by_id(enum cfg_data_idt id, const void* buf);
esp_err_t cp_set_by_id_from_raw(enum cfg_data_idt id, const char* param);
esp_err_t cp_get_by_id_to_readable(enum cfg_data_idt id, char* buf, size_t maxlen);
#define cp_get_u8_by_id(id, out_addr) (cp_get_by_id(id, (void*)(out_addr), NULL))
#define cp_set_u8_by_id(id, out_addr) (cp_set_by_id(id, (void*)((uint8_t)out_addr)))
#define cp_get_u32_by_id(id, out_addr) (cp_get_by_id(id, (void*)(out_addr), NULL))
#define cp_set_u32_by_id(id, out_addr) (cp_set_by_id(id, (void*)(out_addr)))

esp_err_t start_webserver(void);
void stop_webserver(void);
esp_err_t wifi_sta_query_ap(char* ssid, size_t ssid_len);
esp_err_t wifi_query_ip_info(uint8_t sta_0_ap_1, ip_info_t* ip_info);
uint8_t wifi_sta_connect(char ssid[WIFI_SSID_MAXLEN], char password[WIFI_PASS_MAXLEN]);
uint8_t wifi_sta_query_status();
void wifi_sta_disconnect();
uint8_t  wifi_ap_turn_on();
uint8_t  wifi_ap_turn_off();
esp_err_t wifi_ap_query(char* ssid, size_t ssid_len);

// Config provider callbacks
esp_err_t cpcb_check_set_baudrate(uint32_t baudrate);
esp_err_t cpcb_check_set_parity(uint8_t parity);
esp_err_t cpcb_check_set_tx_delay(uint32_t tx_delay);
esp_err_t cpcb_check_ap_auth(uint8_t auth);

#endif /* MAIN_MAIN_H_ */
