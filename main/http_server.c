#include <sys/param.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_http_server.h>

#include "main.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static const char* fail_str = "Failed.";
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
        httpd_resp_send(req, fail_str, strlen(fail_str));
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
