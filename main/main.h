/*
 * main.h
 *
 *  Created on: Dec 21, 2020
 *      Author: Administrator
 */

#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_

#include "esp_err.h"

#define CFG_STORAGE_NAMESPACE "app_cfg"
#define WIFI_SSID_MAXLEN 32
#define WIFI_PASS_MAXLEN 64

/**
 * If not set, ssid or pass will be a string with 0 length.
 */
esp_err_t cp_get_wifi_params(char ssid[WIFI_SSID_MAXLEN], char pass[WIFI_PASS_MAXLEN]);
esp_err_t cp_set_wifi_params(const char* ssid, const char* pass);

void modbus_tcp_server_create();

esp_err_t start_webserver(void);
void stop_webserver(void);

#endif /* MAIN_MAIN_H_ */
