
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "main.h"

#define MODBUS_PORT 502

typedef struct tcp_server_config tcp_server_config_t;
typedef void (*tcp_server_init_tm) (tcp_server_config_t*);
typedef void (*tcp_server_new_conn_tm)(const tcp_server_config_t*, int); // this, clientSocket
typedef void (*tcp_server_conn_down_tm)(const tcp_server_config_t*, int); // this, clientSocket
struct tcp_server_config {
    // Properties
    union {
        struct sockaddr_in v4;
        struct sockaddr_in6 v6;
        struct sockaddr sa;
    } addr;
    socklen_t addr_len;
    int protocol;

    // Callbacks
    tcp_server_new_conn_tm tcp_server_new_conn;
    tcp_server_conn_down_tm tcp_server_conn_down;
};

#define MODBUS_LOGE(...) ESP_LOGE(pcTaskGetName(NULL), ##__VA_ARGS__)
#define MODBUS_LOGI(...) ESP_LOGI(pcTaskGetName(NULL), ##__VA_ARGS__)

static void tcp_server_data_arrive(const tcp_server_config_t* cfg, int clientSocket, char* buf, ssize_t len) {
    // Echo back
    if(send(clientSocket, buf, len, 0) == -1)
        MODBUS_LOGE("send() error lol!");

    MODBUS_LOGI("Receive bytes (%d):", len);
    for (ssize_t i=0; i<len; i++)
        MODBUS_LOGI("0x%02X", buf[i]);
}

static void tcp_server_ip4_new_conn(const tcp_server_config_t* cfg, int clientSocket) {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(struct sockaddr_in);
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &addrLen);
    char addr_str[16];
    inet_ntoa_r(clientAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
    MODBUS_LOGI("New connection from %s on socket %d", addr_str, clientSocket);
}

static void tcp_server_ip6_new_conn(const tcp_server_config_t* cfg, int clientSocket) {
    struct sockaddr_in6 clientAddr;
    socklen_t addrLen = sizeof(struct sockaddr_in6);
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &addrLen);
    char addr_str[40];
    inet6_ntoa_r(clientAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
    MODBUS_LOGI("New connection from %s on socket %d", addr_str, clientSocket);
}

static void tcp_server_conn_down(const tcp_server_config_t* cfg, int clientSocket) {
    MODBUS_LOGI("Socket %d hung up\n", clientSocket);
}

static void tcp_server_ip4_init(tcp_server_config_t* cfg) {
    cfg->addr.v4.sin_addr.s_addr = htonl(INADDR_ANY);
    cfg->addr.v4.sin_family = AF_INET;
    cfg->addr.v4.sin_port = htons(MODBUS_PORT);
    cfg->addr_len = sizeof(struct sockaddr_in);
    cfg->protocol = IPPROTO_IP;

    // Callbacks
    cfg->tcp_server_new_conn = tcp_server_ip4_new_conn;
    cfg->tcp_server_conn_down = tcp_server_conn_down;
}

static void tcp_server_ip6_init(tcp_server_config_t* cfg) {
    bzero(&(cfg->addr.v6.sin6_addr.un), sizeof(cfg->addr.v6.sin6_addr.un));
    cfg->addr.v6.sin6_family = AF_INET6;
    cfg->addr.v6.sin6_port = htons(MODBUS_PORT);
    cfg->addr_len = sizeof(struct sockaddr_in6);
    cfg->protocol = IPPROTO_IPV6;

    // Callbacks
    cfg->tcp_server_new_conn = tcp_server_ip6_new_conn;
    cfg->tcp_server_conn_down = tcp_server_conn_down;
}

// For both IPv4 and IPv6
static void tcp_server_task(void *pvParameters) {
    tcp_server_config_t cfg;
    tcp_server_init_tm mtd_init = (tcp_server_init_tm) pvParameters;
    mtd_init(&cfg);

    int listener = socket(cfg.addr.sa.sa_family, SOCK_STREAM, cfg.protocol);
    if (listener < 0) {
        MODBUS_LOGE("Unable to create the socket");
        goto close_socket;
    }

    int enable = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        MODBUS_LOGE("setsockopt(SO_REUSEADDR) failed");
        goto close_socket;
    }

    //struct linger sl;
    //sl.l_onoff = 1;     // non-zero value enables linger option in kernel
    //sl.l_linger = 30;   // timeout interval in seconds
    //if (setsockopt(listen_sock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
    //    ESP_LOGE(TAG, "setsockopt(SO_LINGER) failed");
    //    goto close_socket;
    //}

    // tell socket we don't want blocking...
    /*int flags = fcntl(listener, F_GETFL);
    if (flags<0) {
        ESP_LOGE(TAG, "unable to retrieve descriptor status flags");
        goto close_socket;
    }

    flags |= O_NONBLOCK;

    if (fcntl(listener, F_SETFL, flags)<0) {
        ESP_LOGE(TAG, "unable to set descriptor status flags");
        goto close_socket;
    }*/

    if (bind(listener, &(cfg.addr.sa), cfg.addr_len) != 0) {
        MODBUS_LOGE("Unable to bind the socket");
        goto close_socket;
    }

    if (listen(listener, 5) != 0) {
        MODBUS_LOGE("Error occured during listen: errno %d", errno);
        goto close_socket;
    }
    MODBUS_LOGI("Socket listening");

    int fdmax = listener;
    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_SET(listener, &master);
    FD_ZERO(&read_fds);

    while (1) {
        read_fds = master;
        if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            MODBUS_LOGE("Server-select() error lol!");
            break;
        }
        // MODBUS_LOGD("Server-select() is OK...");

        // run through the existing connections looking for data to be read
        for(int i = 0; i <= fdmax; i++) {
            if(FD_ISSET(i, &read_fds)) {
                if(i == listener) {
                    // handle new connections
                    int newfd = accept(listener, NULL, 0);
                    if(newfd == -1) {
                        MODBUS_LOGE("Server-accept() error lol!");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if(newfd > fdmax) {
                            // keep track of the maximum
                            fdmax = newfd;
                        }
                        cfg.tcp_server_new_conn(&cfg, newfd);
                    }
                } else { // if(i == listener) {
                    char buf[16];
                    ssize_t nbytes = recv(i, buf, sizeof(buf), 0);
                    if (nbytes == 0) {
                        // Connection closed
                        cfg.tcp_server_conn_down(&cfg, i);
                        close(i);
                        FD_CLR(i, &master);
                    } else if (nbytes < 0) {
                        // Error
                        MODBUS_LOGE("recv() error lol [%d]!", nbytes);
                        close(i);
                        FD_CLR(i, &master);
                    } else {
                        // Data arrived
                        tcp_server_data_arrive(&cfg, i, buf, nbytes);
                    }
                }
            }
        }

        vTaskDelay(10);
    }

close_socket:
    if (listener >= 0)
        close(listener);
    MODBUS_LOGI("TCP server closed");
    vTaskDelete(NULL);
}

static void tcp_server_ip4_task(void* param) {
    tcp_server_task(tcp_server_ip4_init);
}

static void tcp_server_ip6_task(void* param) {
    tcp_server_task(tcp_server_ip6_init);
}

void modbus_tcp_server_create() {
    xTaskCreate(tcp_server_ip4_task, "tcp_server_ip4", 2048, NULL, 5, NULL);
    xTaskCreate(tcp_server_ip6_task, "tcp_server_ip6", 2048, NULL, 5, NULL);
}
