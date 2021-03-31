idf_component_register(SRCS "config_provider.c" "esp_http_server_ext.c" "http_server.c" "modbus_request_queue.c" "modbus_rtu.c" "modbus_rtu2tcp_main.c" "modbus_tcp_server.c" "modbus_utils.c" "ota.c"
                    EMBED_TXTFILES "index.html"
                    INCLUDE_DIRS "."
                    REQUIRES "tcpip_adapter")