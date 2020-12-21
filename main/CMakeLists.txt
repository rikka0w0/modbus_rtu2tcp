idf_component_register(SRCS "modbus_rtu2tcp_main.c" "http_server.c" "config_provider.c"
                    EMBED_TXTFILES "index.html"
                    INCLUDE_DIRS "."
                    REQUIRES "tcpip_adapter")