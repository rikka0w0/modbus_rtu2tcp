#include <sys/param.h>
#include <cJSON.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_http_server.h>
#include "esp_http_server_ext.h"

#include "main.h"
#include "ota.h"

#define HTTP_GET_ARG_MAXLEN 512
#define HTTP_PARAM_MAXLEN 128

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static const char *TAG="APP";

static httpd_handle_t server = NULL;

static const char* json_post_set_fields(cJSON* req_array);

esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, index_html_start, strlen(index_html_start));
    return ESP_OK;
}

httpd_uri_t index_get = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler
};

// "/restart?confirm=yes", restart the system
static void restart_task(void* param) {
    stop_webserver();
    ESP_LOGI(TAG, "Restarting...");
    esp_restart();
}

static esp_err_t restart_get_handler(httpd_req_t *req) {
    const char* resp_str = "?confirm=yes not found, restarting aborted.";
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[4];
        /* Get value of expected key from query string */
        if (httpd_query_key_value(buf, "confirm", param, sizeof(param)) == ESP_OK &&
                strcmp(param, "yes") == 0) {
            resp_str = (const char*) req->user_ctx;

            xTaskCreate(restart_task, "restart_task", 1024, NULL, 6, NULL);
        }
    }

    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

httpd_uri_t restart = {
    .uri       = "/restart",
    .method    = HTTP_GET,
    .handler   = restart_get_handler,
    .user_ctx  = "Restarting now..."
};

static esp_err_t config_get_handler(httpd_req_t *req) {
    char* resp = NULL;
    char param[128];
    char* buf = NULL;

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*) malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "method", param, sizeof(param)) == ESP_OK) {
                if (strcmp(param, "set") == 0) {
                    if (httpd_query_key_value(buf, "field", param, sizeof(param)) == ESP_OK) {
                        enum cfg_data_idt cfg_id = cp_id_from_name(param);
                        if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
                            if (cp_set_by_id_from_raw(cfg_id, param) == ESP_OK) {
                                ESP_LOGI(TAG, "Set %s to %s", cp_name_from_id(cfg_id), param);
                                resp = (char*)req->user_ctx;
                            }
                        }
                    }
                } else if (strcmp(param, "get") == 0) {
                    if (httpd_query_key_value(buf, "field", param, sizeof(param)) == ESP_OK) {
                        enum cfg_data_idt cfg_id = cp_id_from_name(param);
                        if (cp_get_by_id_to_readable(cfg_id, param, sizeof(param)) == ESP_OK) {
                            resp = param;
                        }
                    }
                }
            }
        }
    }

    if (resp == NULL) {
        httpd_resp_set_status(req, HTTPD_400);
        httpd_resp_send(req, HTTPD_400, strlen(HTTPD_400));
    } else {
        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_send(req, resp, strlen(resp));
    }

    if (buf != NULL)
        free(buf);
    return ESP_OK;
}

httpd_uri_t config_get = {
    .uri       = "/config",
    .method    = HTTP_GET,
    .handler   = config_get_handler,
    .user_ctx  = "Ok~~~"
};

static void json_get_get_fields(cJSON* resp_root, cJSON* req_array) {
    char param[HTTP_PARAM_MAXLEN];

    if (req_array == NULL)
        return;

    cJSON* req_iterator = NULL;
    cJSON_ArrayForEach(req_iterator, req_array) {
        char* field_name = cJSON_GetStringValue(req_iterator);
        enum cfg_data_idt cfg_id = cp_id_from_name(field_name);
        if (cp_get_by_id_to_readable(cfg_id, param, sizeof(param)) == ESP_OK) {
            cJSON_AddStringToObject(resp_root, field_name, param);
        }
    }
}

static const char* const wifi_sta_status_str[] = {"disconnected", "connecting", "connected"};

static void json_get_wifi_sta_status(cJSON* resp_root) {
    char ssid[WIFI_SSID_MAXLEN];
    ip_info_t ip_info;

    cJSON_AddStringToObjectCS(resp_root, "wifi_sta_status", wifi_sta_status_str[wifi_sta_query_status()]);

    if (wifi_sta_query_ap(ssid, sizeof(ssid)) == ESP_OK) {
        cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ap_ssid", ssid);

        if (wifi_query_ip_info(0, &ip_info) == ESP_OK) {
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ip4_address", ip_info.ip4_addr);
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ip4_netmask", ip_info.ip4_netmask);
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ip4_gateway", ip_info.ip4_gateway);

            char* ipv6_addr[IPV6_ADDR_COUNT];
            for (size_t i=0; i<ip_info.ip6_count; i++) {
                ipv6_addr[i] = (char*)&ip_info.ip6_addr[i];
            }
            cJSON_AddItemToObjectCS(resp_root, "wifi_sta_ip6_address",
                                    cJSON_CreateStringArray((const char**)ipv6_addr, ip_info.ip6_count));
        }
    }
}

static void json_get_wifi_connect(cJSON* resp_root, cJSON* req) {
    cJSON* req_item_node;
    char sta_ssid_req[WIFI_SSID_MAXLEN];
    char sta_pass_req[WIFI_PASS_MAXLEN];
    int use_prev_cfg = 0;

    sta_ssid_req[0] = '\0';
    sta_pass_req[0] = '\0';

    req_item_node = cJSON_GetObjectItem(req, "wifi_sta_ssid");
    if (req_item_node != NULL) {
        use_prev_cfg = 1;

        strncpy(sta_ssid_req, cJSON_GetStringValue(req_item_node), WIFI_SSID_MAXLEN);
        // Trucate the string if it is greater than WIFI_SSID_MAXLEN-1
        sta_ssid_req[WIFI_SSID_MAXLEN-1] = '\0';
        cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ssid", sta_ssid_req);

        req_item_node = cJSON_GetObjectItem(req, "wifi_sta_pass");
        if (req_item_node != NULL) {
            strncpy(sta_pass_req, cJSON_GetStringValue(req_item_node), WIFI_PASS_MAXLEN);
            // Trucate the string if it is greater than WIFI_PASS_MAXLEN-1
            sta_pass_req[WIFI_PASS_MAXLEN-1] = '\0';
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_pass", sta_pass_req);
        }
    }

    cJSON_AddBoolToObject(resp_root, "wifi_sta_use_prev_cfg", use_prev_cfg);
    cJSON_AddBoolToObject(resp_root, "return_value",
                          wifi_sta_connect(sta_ssid_req, sta_pass_req));
}

static void json_get_wifi_ap_status(cJSON* resp_root) {
    char ssid[WIFI_SSID_MAXLEN];
    ip_info_t ip_info;
    esp_err_t ret = wifi_ap_query(ssid, WIFI_SSID_MAXLEN);

    cJSON_AddBoolToObject(resp_root, "wifi_ap_turned_on", ret == ESP_OK);

    if (ret == ESP_OK) {
        cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ssid", ssid);

        if (wifi_query_ip_info(1, &ip_info) == ESP_OK) {
            cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ip4_address", ip_info.ip4_addr);
            cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ip4_netmask", ip_info.ip4_netmask);
            cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ip4_gateway", ip_info.ip4_gateway);

            char* ipv6_addr[IPV6_ADDR_COUNT];
            for (size_t i=0; i<ip_info.ip6_count; i++) {
                ipv6_addr[i] = (char*)&ip_info.ip6_addr[i];
            }
            cJSON_AddItemToObjectCS(resp_root, "wifi_ap_ip6_address",
                                    cJSON_CreateStringArray((const char**)ipv6_addr, ip_info.ip6_count));
        }
    }
}

static cJSON* json_get_parser(cJSON* req) {
    // Duplicate "method" field to the response
    cJSON* req_item_node = cJSON_GetObjectItem(req, "method");
    if (req_item_node == NULL) {
        return NULL;
    }
    cJSON* resp_root = cJSON_CreateObject();
    cJSON_DetachItemViaPointer(req, req_item_node);
    cJSON_AddItemToObject(resp_root, "method", req_item_node);
    char* req_method = cJSON_GetStringValue(req_item_node);

    // Copy trans_id
    req_item_node = cJSON_GetObjectItem(req, "trans_id");
    if (req_item_node != NULL) {
        cJSON_DetachItemViaPointer(req, req_item_node);
        cJSON_AddItemToObject(resp_root, "trans_id", req_item_node);
    }

    if (strcmp(req_method, "get") == 0) {
        json_get_get_fields(resp_root, cJSON_GetObjectItem(req, "fields"));
    } else if (strcmp(req_method, "set") == 0) {
        cJSON_AddStringToObjectCS(resp_root, "return_value",
                                json_post_set_fields(cJSON_GetObjectItem(req, "fields")));
    } else if (strcmp(req_method, "wifi_sta_status") == 0) {
        json_get_wifi_sta_status(resp_root);
    } else if (strcmp(req_method, "wifi_sta_connect") == 0) {
        json_get_wifi_connect(resp_root, req);
    } else if (strcmp(req_method, "wifi_sta_disconnect") == 0) {
        wifi_sta_disconnect();
    } else if (strcmp(req_method, "wifi_ap_on") == 0) {
        cJSON_AddBoolToObject(resp_root, "return_value", wifi_ap_turn_on());
    } else if (strcmp(req_method, "wifi_ap_off") == 0) {
        cJSON_AddBoolToObject(resp_root, "return_value", wifi_ap_turn_off());
    } else if (strcmp(req_method, "wifi_ap_status") == 0) {
        json_get_wifi_ap_status(resp_root);
    }

    return resp_root;
}

static esp_err_t json_get_handler(httpd_req_t *req) {
    cJSON* json_req = NULL;
    esp_err_t ret = ESP_OK;
    char* buf = NULL;
    const char* req_str = NULL;
    size_t buf_len = 0;

    req_str = httpd_req_get_url_query_str_byref(req, &buf_len);
    if (req_str == NULL) {
        goto func_ret;
    }

    // Get HTTP request key-value pair
    req_str = httpd_query_key_value_byref(req_str, "json", &buf_len);
    if (req_str == NULL) {
        goto func_ret;
    }

    // HTTP request value decode
    if (buf_len > HTTP_GET_ARG_MAXLEN) {
        ret = ESP_FAIL;
        goto func_ret;
    }
    buf = malloc(buf_len + 1);
    buf_len = httpd_query_value_decode(req_str, buf_len, buf);

    // Json Parse
    json_req = cJSON_Parse(buf);
    free(buf);
    buf = NULL;
    if (!json_req) {
        ESP_LOGE("json", "Error before: [%s]", cJSON_GetErrorPtr());
        ret = ESP_ERR_INVALID_STATE;
        goto func_ret;
    }

    // Generate and send the response
    cJSON* json_resp = json_get_parser(json_req);
    if (json_resp) {
        buf = cJSON_PrintUnformatted(json_resp);
        cJSON_Delete(json_resp);
        ret = httpd_resp_send(req, buf, strlen(buf));
        free(buf);
        buf = NULL;
    } else {
        ret = httpd_resp_send(req, HTTPD_400, strlen(HTTPD_400));
    }

func_ret:
    if (json_req)
        cJSON_Delete(json_req);
    if (buf)
        free(buf);
    return ret;
}

httpd_uri_t json_get = {
    .uri       = "/json_get",
    .method    = HTTP_GET,
    .handler   = json_get_handler
};

static const char* json_post_set_fields(cJSON* req_array) {
    const char* ret = HTTPD_200;

    if (req_array == NULL)
        return NULL;

    cJSON* req_iterator = NULL;
    cJSON_ArrayForEach(req_iterator, req_array) {
        char* field_name = req_iterator->string;
        char* field_value = cJSON_GetStringValue(req_iterator);
        enum cfg_data_idt cfg_id = cp_id_from_name(field_name);
        if (cp_set_by_id_from_raw(cfg_id, field_value) != ESP_OK) {
            ret = HTTPD_404;
        }
    }
    return ret;
}

static const char* json_post_parser(const cJSON* req) {
    cJSON* req_method_node = cJSON_GetObjectItem(req, "method");
    char* req_method = cJSON_GetStringValue(req_method_node);

    if (strcmp(req_method, "set") == 0) {
        return json_post_set_fields(cJSON_GetObjectItem(req, "fields"));
    }

    return HTTPD_404;
}

static esp_err_t json_post_handler(httpd_req_t *req) {
    cJSON* json_req = NULL;
    esp_err_t ret = ESP_OK;
    char* buf = NULL;
    size_t remaining = req->content_len;
    if (remaining <= 1 || remaining >= 1024) {
        ret = ESP_FAIL;
        goto func_ret;
    }

    buf = (char*) malloc(remaining);
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, remaining);
        if (ret > 0) {
            remaining -= ret;
        } else if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* Retry receiving if timeout occurred */
            continue;
        } else {
            ret = ESP_FAIL;
            goto func_ret;
        }
    }

    json_req = cJSON_Parse(buf);
    free(buf);
    buf = NULL;
    ret = ESP_OK;
    if (!json_req) {
        ESP_LOGE("json", "Error before: [%s]", cJSON_GetErrorPtr());
        ret = ESP_ERR_INVALID_STATE;
        goto func_ret;
    }

    // Json Parse
    httpd_resp_set_status(req, json_post_parser(json_req));

func_ret:
    if (json_req)
        cJSON_Delete(json_req);
    if (buf)
        free(buf);
    return ret;
}

httpd_uri_t json_post = {
    .uri       = "/json_post",
    .method    = HTTP_POST,
    .handler   = json_post_handler
};

#if OTA_ESP_ENABLED
static esp_err_t ota_post_handler(httpd_req_t *req) {
    esp_err_t ret = ESP_OK;
    char* status_code = HTTPD_200;
    char buf[255];

    size_t remaining = req->content_len;

    ESP_LOGI("OTA", "Blob Size: %d bytes", remaining);
    ret = ota_esp_begin(remaining);
    if (ret != ESP_OK) {
        status_code = HTTPD_500;
        goto func_ret;
    }

    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, sizeof(buf));

        if (ret > 0) {
            remaining -= ret;

            ota_esp_buf_write(buf, ret);

            ret = ESP_OK;
        } else if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* Retry receiving if timeout occurred */
            continue;
        } else {
            ret = ESP_FAIL;
            goto func_ret;
        }
    }

    ret = ota_esp_end();
    if (ret != ESP_OK) {
        status_code = HTTPD_500;
        goto func_ret;
    }

func_ret:
    httpd_resp_set_status(req, status_code);

    return ret;
}

httpd_uri_t ota_post = {
    .uri       = "/ota_post",
    .method    = HTTP_POST,
    .handler   = ota_post_handler
};
#endif // OTA_ESP_ENABLED

esp_err_t start_webserver(void) {
    if (server != NULL)
        return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_get);
        httpd_register_uri_handler(server, &config_get);
        httpd_register_uri_handler(server, &restart);
        httpd_register_uri_handler(server, &json_get);
        httpd_register_uri_handler(server, &json_post);
#if OTA_ESP_ENABLED
        httpd_register_uri_handler(server, &ota_post);
#endif
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

void stop_webserver(void) {
    if (server == NULL) {
        ESP_LOGI(TAG, "Web server is not running.");
        return;
    }

    ESP_LOGI(TAG, "Stopping web server.");
    httpd_stop(server);
    ESP_LOGI(TAG, "Web server stopped.");
}
