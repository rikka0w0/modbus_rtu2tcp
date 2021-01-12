idf_component_register(SRCS "modbus_rtu2tcp_main.c" "http_server.c" "config_provider.c" "modbus_tcp_server.c" "modbus_rtu.c" "modbus_request_queue.c" "modbus_utils.c" "esp_http_server_ext.c"
                    EMBED_TXTFILES "index.html"
                    INCLUDE_DIRS "."
                    REQUIRES "tcpip_adapter")