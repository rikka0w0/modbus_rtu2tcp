/*
 * ota.h
 *
 *  Created on: Mar 31, 2021
 *      Author: Administrator
 */

#ifndef MAIN_OTA_H_
#define MAIN_OTA_H_

#include <strings.h>
#include "esp_err.h"

#ifdef CONFIG_PARTITION_TABLE_TWO_OTA
    #define OTA_ESP_ENABLED CONFIG_PARTITION_TABLE_TWO_OTA
#else
    #define OTA_ESP_ENABLED 0
#endif



#if OTA_ESP_ENABLED
esp_err_t ota_esp_begin(size_t blob_len);
esp_err_t ota_esp_buf_write(char* buf, size_t len);
esp_err_t ota_esp_end();
#endif

#endif /* MAIN_OTA_H_ */
