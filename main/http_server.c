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

#define HTTP_GET_ARG_MAXLEN 512
#define HTTP_PARAM_MAXLEN 128

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static const char *TAG="APP";

static httpd_handle_t server = NULL;

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

static cJSON* json_get_get_fields(cJSON* req_array) {
    char param[HTTP_PARAM_MAXLEN];

    if (req_array == NULL)
        return NULL;

    cJSON* resp_root = cJSON_CreateObject();
    cJSON* req_iterator = NULL;
    cJSON_ArrayForEach(req_iterator, req_array) {
        char* field_name = cJSON_GetStringValue(req_iterator);
        enum cfg_data_idt cfg_id = cp_id_from_name(field_name);
        if (cp_get_by_id_to_readable(cfg_id, param, sizeof(param)) == ESP_OK) {
            cJSON_AddStringToObject(resp_root, field_name, param);
        }
        free(field_name);
    }
    return resp_root;
}

static cJSON* json_get_parser(const cJSON* req) {
    cJSON* resp_root = NULL;

    cJSON* req_method_node = cJSON_GetObjectItem(req, "method");
    char* req_method = cJSON_GetStringValue(req_method_node);

    if (strcmp(req_method, "get") == 0) {
        resp_root = json_get_get_fields(cJSON_GetObjectItem(req, "fields"));
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
        buf = cJSON_Print(json_resp);
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
        free(field_name);
        free(field_value);
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

    ESP_LOGI("json","len=%d",remaining);
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
