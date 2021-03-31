/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"
#include "tcpip_adapter.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"

#include "ota.h"

// OTA states, protected by ota_mutex
static SemaphoreHandle_t ota_mutex;
static size_t blob_len = 0;
static size_t written = 0;
static const esp_partition_t *update_partition = NULL;
/* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
static esp_ota_handle_t update_handle = 0 ;

static const char *TAG = "OTA";

esp_err_t ota_esp_begin(size_t blob_len_param) {
    if (ota_mutex == NULL) {
        ota_mutex = xSemaphoreCreateMutex();
        xSemaphoreGive(ota_mutex);
    }

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    blob_len = blob_len_param;
    written = 0;

    ESP_LOGI(TAG, "Starting OTA, flash size: %s", CONFIG_ESPTOOLPY_FLASHSIZE);

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
        goto func_ret;
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");

func_ret:
    return err;
}

esp_err_t ota_esp_buf_write(char* buf, size_t len) {
    esp_err_t err = esp_ota_write( update_handle, (const void *)buf, len);

    if (err == ESP_OK) {
        written += len;
        ESP_LOGI(TAG, "Progress + %d => %d", len, written);
    } else {
        // Fatal error
        ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
    }

    return err;
}

esp_err_t ota_esp_end() {
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Total Write binary data length : %d", blob_len);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
        goto ret;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%x", err);
        goto ret;
    }

    ESP_LOGI(TAG, "Prepare to restart system!");

ret:
    xSemaphoreGive(ota_mutex);
    return err;
}

