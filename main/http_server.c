/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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

static const char *TAG="APP";

static httpd_handle_t server = NULL;

/* An HTTP GET handler */
esp_err_t hello_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
            }
        }
        free(buf);
    }

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    //const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, index_html_start, strlen(index_html_start));

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

httpd_uri_t hello = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
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

// "/config?ssid=BASE64&wifipass=BASE64", set the configuration
static esp_err_t config_get_handler(httpd_req_t *req) {
    char* resp = (char*) req->user_ctx;
    char* buf = NULL;
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*) malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[128];
            if (httpd_query_key_value(buf, "method", param, sizeof(param)) == ESP_OK) {
                if (strcmp(param, "set") == 0) {
                    /* Get value of expected key from query string */
                    if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) {
                        ESP_LOGI(TAG, "Set SSID to %s", param);
                        cp_set_wifi_params(param, NULL);
                    }

                    if (httpd_query_key_value(buf, "wifipass", param, sizeof(param)) == ESP_OK) {
                        ESP_LOGI(TAG, "Set Wifi Password to %s", param);
                        cp_set_wifi_params(NULL, param);
                    }
                } else if (strcmp(param, "get") == 0
                        && httpd_query_key_value(buf, "field", param, sizeof(param)) == ESP_OK) {
                    if (strcmp(param, "ssid") == 0) {
                        cp_get_wifi_params(buf, NULL);
                    } else if (strcmp(param, "wifipass") == 0) {
                        cp_get_wifi_params(NULL, buf);
                    }
                    resp = buf;
                }
            }
        }
    }

    httpd_resp_send(req, resp, strlen(resp));

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
        httpd_register_uri_handler(server, &hello);
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
