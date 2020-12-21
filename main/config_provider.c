#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"
#include "nvs.h"

#include "main.h"

esp_err_t cp_get_wifi_params(char ssid[WIFI_SSID_MAXLEN], char pass[WIFI_PASS_MAXLEN]) {
    nvs_handle cfg_nvss_handle;
    size_t param_len;
    esp_err_t err = ESP_OK;

    // Open
    err = nvs_open(CFG_STORAGE_NAMESPACE, NVS_READWRITE, &cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

    // SSID
    if (ssid != NULL) {
        param_len = WIFI_SSID_MAXLEN;
        err = nvs_get_str(cfg_nvss_handle, "wifi_ssid", ssid, &param_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            memset(ssid, 0, WIFI_SSID_MAXLEN);
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
    }

    // Password
    if (pass != NULL) {
        param_len = WIFI_PASS_MAXLEN;
        err = nvs_get_str(cfg_nvss_handle, "wifi_pass", pass, &param_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            memset(pass, 0, WIFI_PASS_MAXLEN);
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
    }

err_ret:
    // Close
    nvs_close(cfg_nvss_handle);
    return err;
}

esp_err_t cp_set_wifi_params(const char* ssid, const char* pass) {
    nvs_handle cfg_nvss_handle;
    esp_err_t err = ESP_OK;

    // Open
    err = nvs_open(CFG_STORAGE_NAMESPACE, NVS_READWRITE, &cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

    // SSID
    if (ssid != NULL) {
        err = nvs_set_str(cfg_nvss_handle, "wifi_ssid", ssid);
        if (err != ESP_OK)
            goto err_ret;
    }

    // Password
    if (pass != NULL) {
        err = nvs_set_str(cfg_nvss_handle, "wifi_pass", pass);
        if (err != ESP_OK)
            goto err_ret;
    }

    // Commit written value.
    // After setting any values, nvs_commit() must be called to ensure changes are written
    // to flash storage. Implementations may write to storage at other times,
    // but this is not guaranteed.
    err = nvs_commit(cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

err_ret:
    // Close
    nvs_close(cfg_nvss_handle);
    return err;
}
