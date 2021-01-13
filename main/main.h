/*
 * main.h
 *
 *  Created on: Dec 21, 2020
 *      Author: Administrator
 */

#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_

#include <stdint.h>
#include <strings.h>
#include "esp_err.h"

#define CFG_STORAGE_NAMESPACE "app_cfg"
#define WIFI_SSID_MAXLEN 32
#define WIFI_PASS_MAXLEN 64
#define WIFI_AP_SSID_DEFAULT "Modbus RTU2TCP"
#define WIFI_AP_PASS_DEFAULT "password"

enum cfg_data_type {
    CFG_DATA_UNKNOWN = 0,
    CFG_DATA_STR = 1,
    CFG_DATA_U8 = 2,
    CFG_DATA_U32 = 3
};

enum cfg_data_idt {
    CFG_WIFI_SSID,
    CFG_WIFI_PASS,
    CFG_UART_BAUD,
    CFG_UART_PARITY,
    CFG_UART_TX_DELAY,

    CFG_WIFI_MODE,
    CFG_WIFI_SSID_AP,
    CFG_WIFI_PASS_AP,

    CFG_IDT_MAX
};

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

// Config provider callbacks
esp_err_t cpcb_check_set_baudrate(uint32_t baudrate);
esp_err_t cpcb_check_set_parity(uint8_t parity);
esp_err_t cpcb_check_set_tx_delay(uint32_t tx_delay);

#endif /* MAIN_MAIN_H_ */
