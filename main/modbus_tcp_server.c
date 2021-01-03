#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "modbus_tcp_server.h"
#include "main.h"
#include "modbus.h"

// LWIP is missing this from netinet/tcp.h, but this code still works if we manually define it.
#ifndef SOL_TCP
#define SOL_TCP 6
#endif

#ifdef TCP_SERVER_DEBUG
#define TCPSVR_LOGE(...) ESP_LOGE(pcTaskGetName(NULL), ##__VA_ARGS__)
#define TCPSVR_LOGW(...) ESP_LOGW(pcTaskGetName(NULL), ##__VA_ARGS__)
#define TCPSVR_LOGI(...) ESP_LOGI(pcTaskGetName(NULL), ##__VA_ARGS__)
#else
#define TCPSVR_LOGE(...)
#define TCPSVR_LOGW(...)
#define TCPSVR_LOGI(...)
#endif

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

/////////////////////////////////////////////////////////////////////////////////////////////////
/// A double linked-list for client rx buffers, sorted by the socket number in ascending order.
/////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct tcp_server_client_info_node tcp_server_client_info_node_t;
struct tcp_server_client_info_node {
    tcp_server_client_info_node_t* prev;
    tcp_server_client_info_node_t* next;

    int socket;
    tcp_server_client_state_t state;
    uint8_t skip_next_select;
    TimerHandle_t  buf_full_expire_timer;
    TimerHandle_t  frame_pending_expire_timer;
};

typedef struct tcp_server_client_info_list {
    tcp_server_client_info_node_t* first;
    tcp_server_client_info_node_t* last;
    int count;
} tcp_server_client_info_list_t ;

typedef struct tcp_server_client_info_list_iterator {
    tcp_server_client_info_list_t* list;
    tcp_server_client_info_node_t* current;
    tcp_server_client_info_node_t* next;
    int cur_removed;
} tcp_server_client_info_list_iterator_t;

static void cil_iterator_init(tcp_server_client_info_list_t* list, tcp_server_client_info_list_iterator_t* iterator) {
    iterator->list = list;
    iterator->current = NULL;
    iterator->next = list->first;
    iterator->cur_removed = 0;
}

static int cil_iterator_step(tcp_server_client_info_list_iterator_t* iterator, tcp_server_client_info_node_t** node) {
    iterator->cur_removed = 0;
    if (iterator->next == NULL) {
        *node = NULL;
        return 0;
    } else {
        iterator->current = iterator->next;
        iterator->next = iterator->current->next;
        *node = iterator->current;
        return 1;
    }
}

static void cil_iterator_remove_current(tcp_server_client_info_list_iterator_t* iterator) {
    // iterator->next has been updated in cil_iterator_step(),
    // so we can safely delete the current node.

    if (iterator->cur_removed) {
        return;
    }

    tcp_server_client_info_node_t* removed = iterator->current;

    if (removed->prev == NULL) {
        // First node
        iterator->list->first = removed->next;
    } else {
        removed->prev->next = removed->next;
    }

    if (removed->next == NULL) {
        // Last node
        iterator->list->last = removed->prev;
    } else {
        removed->next->prev = removed->prev;
    }

    iterator->list->count--;

    // Release resources
    xTimerDelete(removed->buf_full_expire_timer, 0);
    xTimerDelete(removed->frame_pending_expire_timer, 0);
    free(removed);
    iterator->cur_removed = 1;
}

static void cil_init(tcp_server_client_info_list_t* list) {
    list->count = 0;
    list->first = NULL;
    list->last = NULL;
}

static void tcp_server_client_timer_expire(TimerHandle_t xTimer) {
    vTimerSetTimerID(xTimer, (void*)1);
    xTimerStop(xTimer, 0);
}

// Called when a client connection is established, store the socket file descriptor and allocate frame buffer.
static tcp_server_client_info_node_t* cil_register_client(tcp_server_client_info_list_t* list, int socket) {
    tcp_server_client_info_node_t* node = malloc(sizeof(tcp_server_client_info_node_t));
    node->socket = socket;
    tcp_server_client_state_init(&(node->state));
    node->skip_next_select = 0;
    node->buf_full_expire_timer =
            xTimerCreate("buf_exp", pdMS_TO_TICKS(TCP_SERVER_RXBUF_FULL_MAX_MSEC), pdFALSE, (void*)0, tcp_server_client_timer_expire);
    node->frame_pending_expire_timer =
            xTimerCreate("frm_exp", pdMS_TO_TICKS(TCP_SERVER_FRAME_PENDING_MAX_MSEC), pdFALSE, (void*)0, tcp_server_client_timer_expire);

    if (list->last == NULL) {
        // List is empty
        node->prev = NULL;
        node->next = NULL;
        list->first = node;
        list->last = node;
        list->count = 1;
        return node;
    } else {
        tcp_server_client_info_list_iterator_t iterator;
        tcp_server_client_info_node_t* cur;
        cil_iterator_init(list, &iterator);
        while (cil_iterator_step(&iterator, &cur)) {
            if (cur->socket > socket) {
                // cur should contain the first node with a greater socket number than the given
                node->prev = cur->prev;
                node->next = cur;
                if (cur->prev == NULL) {
                    // Insert at the beginning
                    list->first = node;
                } else {
                    cur->prev->next = node;
                }
                cur->prev = node;
                list->count ++;
                return node;
            }
        }

        // The given socket number is the largest, append to the end of the list
        node->prev = list->last;
        node->next = NULL;
        list->last->next = node;
        list->last = node;
        list->count ++;
        return node;
    }
}

static inline int cil_max_socket(tcp_server_client_info_list_t* list) {
    return list->last == NULL ? -1 : list->last->socket;
}

static void cil_free(tcp_server_client_info_list_t* list) {
    tcp_server_client_info_node_t* client_node;
    tcp_server_client_info_list_iterator_t iterator;
    while (cil_iterator_step(&iterator, &client_node)) {
        cil_iterator_remove_current(&iterator);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////
/// TCP Server
/////////////////////////////////////////////////////////////////////////////////////////////////
static void tcp_server_ip4_new_conn(const tcp_server_config_t* cfg, int clientSocket) {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(struct sockaddr_in);
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &addrLen);
    char addr_str[16];
    inet_ntoa_r(clientAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
    TCPSVR_LOGI("New connection from %s on socket %d", addr_str, clientSocket);
}

static void tcp_server_ip6_new_conn(const tcp_server_config_t* cfg, int clientSocket) {
    struct sockaddr_in6 clientAddr;
    socklen_t addrLen = sizeof(struct sockaddr_in6);
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &addrLen);
    char addr_str[40];
    inet6_ntoa_r(clientAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
    TCPSVR_LOGI("New connection from %s on socket %d", addr_str, clientSocket);
}

static void tcp_server_conn_down(const tcp_server_config_t* cfg, int clientSocket) {
    TCPSVR_LOGI("Socket %d hung up\n", clientSocket);
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

static int tcp_server_enable_keepalive(int socket) {
    int keepalive = 1;      // Enable KEEPALIVE
    int keepidle = 5;       // Start probing if being idle longer than "keepidle" seconds
    int keepinterval = 5;   // The interval between probing packets, in seconds
    int keepcount = 2;      // Number of retry before giving up

    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
        TCPSVR_LOGE("setsockopt(SO_KEEPALIVE) failed");
        return -1;
    }

    if (setsockopt(socket, SOL_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
        TCPSVR_LOGE("setsockopt(TCP_KEEPIDLE) failed");
        return -2;
    }

    if (setsockopt(socket, SOL_TCP, TCP_KEEPCNT, &keepcount , sizeof(keepcount)) < 0) {
        TCPSVR_LOGE("setsockopt(TCP_KEEPCNT) failed");
        return -3;
    }

    if (setsockopt(socket, SOL_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval)) < 0) {
        TCPSVR_LOGE("setsockopt(TCP_KEEPINTVL) failed");
        return -4;
    }

    return 0;
}

static void tcp_server_exec_framer(tcp_server_client_info_node_t* client_node) {
    if (tcp_server_framer_run(&(client_node->state), client_node->socket)) {
        if (!xTimerIsTimerActive(client_node->frame_pending_expire_timer)) {
            xTimerReset(client_node->frame_pending_expire_timer, 0);
        }
    } else {
        if (xTimerIsTimerActive(client_node->frame_pending_expire_timer)) {
            xTimerStop(client_node->frame_pending_expire_timer, 0);
        }
    }
}

static void tcp_server_check_client_rxbuf_vacancy(tcp_server_client_info_node_t* client_node) {
    if (tcp_server_client_get_recv_buffer_vacancy(&(client_node->state), NULL) == 0) {
        client_node->skip_next_select = 1;
        if (!xTimerIsTimerActive(client_node->buf_full_expire_timer)) {
            xTimerReset(client_node->buf_full_expire_timer, 0);
        }
    } else {
        client_node->skip_next_select = 0;
        if (xTimerIsTimerActive(client_node->buf_full_expire_timer)) {
            xTimerStop(client_node->buf_full_expire_timer, 0);
        }
    }
}

// For both IPv4 and IPv6
static void tcp_server_task(void *pvParameters) {
    tcp_server_config_t cfg;
    tcp_server_init_tm mtd_init = (tcp_server_init_tm) pvParameters;
    mtd_init(&cfg);

    tcp_server_client_info_list_t client_list;
    cil_init(&client_list);

    int listener = socket(cfg.addr.sa.sa_family, SOCK_STREAM, cfg.protocol);
    if (listener < 0) {
        TCPSVR_LOGE("Unable to create the socket");
        goto close_socket;
    }

    int enable = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        TCPSVR_LOGE("setsockopt(SO_REUSEADDR) failed");
        goto close_socket;
    }

    if (tcp_server_enable_keepalive(listener) < 0) {
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
        TCPSVR_LOGE("Unable to bind the socket");
        goto close_socket;
    }

    if (listen(listener, 5) != 0) {
        TCPSVR_LOGE("Error occured during listen: errno %d", errno);
        goto close_socket;
    }
    TCPSVR_LOGI("Socket listening");

    while (1) {
        tcp_server_client_info_node_t* client_node;
        tcp_server_client_info_list_iterator_t iterator;

        // Determine the set of sockets for monitoring
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listener, &read_fds);    // Always watch the listener socket
        cil_iterator_init(&client_list, &iterator);
        while (cil_iterator_step(&iterator, &client_node)) {
            if (client_node->skip_next_select) {
                client_node->skip_next_select = 0;
            } else {
                FD_SET(client_node->socket, &read_fds);
            }
        }

        struct timeval timeout_val;
        timeout_val.tv_sec = TCP_SERVER_RXBUF_FULL_RETRY_SEC;
        timeout_val.tv_usec = TCP_SERVER_RXBUF_FULL_RETRY_USEC;
        int fdmax = cil_max_socket(&client_list);
        fdmax = fdmax>listener ? fdmax : listener;
        // Re-use fdmax for the return value of select()
        fdmax = select(fdmax+1, &read_fds, NULL, NULL, &timeout_val);
        if (fdmax == -1) {
            TCPSVR_LOGE("Server-select() error lol!");
            break;
        }

        cil_iterator_init(&client_list, &iterator);
        while (cil_iterator_step(&iterator, &client_node)) {
            if (xTimerIsTimerActive(client_node->frame_pending_expire_timer) && !FD_ISSET(listener, &read_fds)) {
                tcp_server_exec_framer(client_node);
                tcp_server_check_client_rxbuf_vacancy(client_node);
                TCPSVR_LOGW("[%d] frame pending", client_node->socket);
            }

            if (    pvTimerGetTimerID(client_node->frame_pending_expire_timer) ||
                    pvTimerGetTimerID(client_node->buf_full_expire_timer)) {
                if (cfg.tcp_server_conn_down != NULL) {
                    cfg.tcp_server_conn_down(&cfg, client_node->socket);
                }
                cil_iterator_remove_current(&iterator);
                close(client_node->socket);

#ifdef TCP_SERVER_DEBUG
                if (pvTimerGetTimerID(client_node->frame_pending_expire_timer)) {
                    TCPSVR_LOGE("[%d] Socket close due to frame_pending", client_node->socket);
                }
                if (pvTimerGetTimerID(client_node->buf_full_expire_timer)) {
                    TCPSVR_LOGE("[%d] Socket close due to buf_full_expire_timer", client_node->socket);
                }
#endif
            }
        }

        if (fdmax == 0) {
            // Timeout, nothing happened
            continue;
        }

        // We have some events to process or something to read
        // Check if there are any new connections to accept (listener)
        if(FD_ISSET(listener, &read_fds)) {
            // handle new connections
            int newfd = accept(listener, NULL, 0);
            if(newfd == -1) {
                TCPSVR_LOGE("Server-accept() error lol!");
            } else {
                tcp_server_enable_keepalive(newfd);
                cil_register_client(&client_list, newfd);
                if (cfg.tcp_server_new_conn != NULL) {
                    cfg.tcp_server_new_conn(&cfg, newfd);
                }
            }
        }

        // Check if any data is available to read (clients)
        cil_iterator_init(&client_list, &iterator);
        while (cil_iterator_step(&iterator, &client_node)) {
            if(FD_ISSET(client_node->socket, &read_fds)) {
                void* buf;
                size_t max_len = tcp_server_client_get_recv_buffer_vacancy(&(client_node->state), &buf);
                if (max_len == 0) {
                    // Buffer is full
                    TCPSVR_LOGW("[%d] Rx buffer is full", client_node->socket);
                    tcp_server_exec_framer(client_node);
                    tcp_server_check_client_rxbuf_vacancy(client_node);
                } else {
                    ssize_t nbytes = recv(client_node->socket, buf, max_len, 0);
                    if (nbytes > 0) {
                        // Data arrived
                        tcp_server_recv_success(&(client_node->state), buf, nbytes);
                        tcp_server_exec_framer(client_node);
                    } else {
                        // Connection closed (==0) or on error (<0)
                        if (nbytes < 0) {
                            TCPSVR_LOGE("[%d] recv() error: %d", client_node->socket, nbytes);
                        }
                        if (cfg.tcp_server_conn_down != NULL) {
                            cfg.tcp_server_conn_down(&cfg, client_node->socket);
                        }
                        cil_iterator_remove_current(&iterator);
                        close(client_node->socket);
                    }
                } // if (max_len == 0) {
            } // if(FD_ISSET(client_node->socket, &read_fds)) {
        }
    }

close_socket:
    cil_free(&client_list);
    if (listener >= 0)
        close(listener);
    TCPSVR_LOGI("TCP server closed");
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
