#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"
#include "nvs.h"

#include "main.h"

typedef esp_err_t (*validater_str_t)(const char*);
typedef esp_err_t (*validater_u8_t)(uint8_t);
typedef esp_err_t (*validater_u32_t)(uint32_t);

typedef struct config_def {
    char* name;
    enum cfg_data_type type;
    union {
        char* str;
        uint8_t u8;
        uint32_t u32;
    } default_val;
    union {
        validater_str_t str;
        validater_u8_t u8;
        validater_u32_t u32;
    } validate;
} config_def_t;

static config_def_t config_defs[CFG_IDT_MAX] = {
    {.name = "wifi_ssid",       .type = CFG_DATA_STR,   .default_val.str = NULL,    .validate.str = NULL},
    {.name = "wifi_pass",       .type = CFG_DATA_STR,   .default_val.str = NULL,    .validate.str = NULL},
    {.name = "uart_baud_rate",  .type = CFG_DATA_U32,   .default_val.u32 = 9600,    .validate.u32 = cpcb_check_set_baudrate},
    {.name = "uart_parity",     .type = CFG_DATA_U8,    .default_val.u8 = 0,        .validate.u8 = cpcb_check_set_parity},
    {.name = "uart_tx_delay",   .type = CFG_DATA_U32,   .default_val.u32 = 1,       .validate.u32 = cpcb_check_set_tx_delay}
};

enum cfg_data_idt cp_id_from_name(const char* name) {
    for (enum cfg_data_idt i = 0; i<CFG_IDT_MAX; i++) {
        if (strcmp(name, config_defs[i].name) == 0) {
            return i;
        }
    }

    return CFG_IDT_MAX;
}

char* cp_name_from_id(enum cfg_data_idt id) {
    return cp_is_valid_id(id) ? config_defs[id].name : NULL;
}

esp_err_t cp_get_by_id(enum cfg_data_idt id, void* buf, size_t* maxlen) {
    if (!cp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    esp_err_t err = ESP_OK;
    nvs_handle cfg_nvss_handle;
    size_t param_len;

    // Open
    err = nvs_open(CFG_STORAGE_NAMESPACE, NVS_READWRITE, &cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

    switch (cfg->type) {
    case CFG_DATA_STR:
        param_len = *maxlen;
        err = nvs_get_str(cfg_nvss_handle, cfg->name, (char*)buf, &param_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (cfg->default_val.str != NULL) {
                strncpy(buf, cfg->default_val.str, *maxlen);
            } else {
                *((char*)buf) = '\0';
            }
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
        *maxlen = param_len;
        break;

    case CFG_DATA_U8:
        err = nvs_get_u8(cfg_nvss_handle, cfg->name, (uint8_t*) buf);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *((uint8_t*)buf) = cfg->default_val.u8;
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
        break;

    case CFG_DATA_U32:
        err = nvs_get_u32(cfg_nvss_handle, cfg->name, (uint32_t*) buf);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *((uint32_t*)buf) = cfg->default_val.u32;
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
        break;

    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

err_ret:
    // Close
    nvs_close(cfg_nvss_handle);
    return err;
}

esp_err_t cp_set_by_id(enum cfg_data_idt id, const void* buf) {
    if (!cp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    nvs_handle cfg_nvss_handle;
    esp_err_t err = ESP_OK;

    // Open
    err = nvs_open(CFG_STORAGE_NAMESPACE, NVS_READWRITE, &cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

    switch (cfg->type) {
    case CFG_DATA_STR:
        if (cfg->validate.str != NULL)
            err = cfg->validate.str((const char*)buf);
        if (err == ESP_OK)
            err = nvs_set_str(cfg_nvss_handle, cfg->name, (const char*)buf);
        break;
    case CFG_DATA_U8:
        if (cfg->validate.u8 != NULL)
            err = cfg->validate.u8((uint8_t) ((uint32_t)buf));
        if (err == ESP_OK)
            err = nvs_set_u8(cfg_nvss_handle, cfg->name, (uint8_t) ((uint32_t)buf));
        break;
    case CFG_DATA_U32:
        if (cfg->validate.u32 != NULL)
            err = cfg->validate.u32((uint32_t)buf);
        if (err == ESP_OK)
            err = nvs_set_u32(cfg_nvss_handle, cfg->name, (uint32_t)buf);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    if (err != ESP_OK)
        goto err_ret;

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

esp_err_t cp_set_by_id_from_raw(enum cfg_data_idt id, const char* param) {
    if (!cp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    esp_err_t err = ESP_OK;
    uint32_t u32_var;

    switch (cfg->type) {
    case CFG_DATA_STR:
        err = cp_set_by_id(id, param);
        break;
    case CFG_DATA_U8:
    case CFG_DATA_U32:
        u32_var = atoi(param);
        err = cp_set_by_id(id, (void*) u32_var);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    return err;
}

esp_err_t cp_get_by_id_to_readable(enum cfg_data_idt id, char* buf, size_t maxlen) {
    if (!cp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    esp_err_t err = ESP_OK;
    uint32_t u32_var = 0;

    switch (cfg->type) {
    case CFG_DATA_STR:
        err = cp_get_by_id(id, buf, &maxlen);
        break;
    case CFG_DATA_U8:
    case CFG_DATA_U32:
        err = cp_get_by_id(id, (void*) &u32_var, NULL);
        snprintf(buf, maxlen, "%d", u32_var);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    return err;
}
