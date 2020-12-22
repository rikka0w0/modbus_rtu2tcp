
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

static const char *TAG = "MODBUS_TCP";
#define MODBUS_LOGE(...) ESP_LOGE(TAG, ##__VA_ARGS__)
#define MODBUS_LOGI(...) ESP_LOGI(TAG, ##__VA_ARGS__)

void tcp_server_ip4_task(void *pvParameters) {
    struct sockaddr_in destAddr;
    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(MODBUS_PORT);

    int listener = socket(destAddr.sin_family, SOCK_STREAM, IPPROTO_IP);
    if (listener < 0) {
        goto close_socket;
    }

    int enable = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        MODBUS_LOGE("setsockopt(SO_REUSEADDR) failed");
        goto close_socket;
    }

    struct linger sl;
    sl.l_onoff = 1;     /* non-zero value enables linger option in kernel */
    sl.l_linger = 30;    /* timeout interval in seconds */
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

    if (bind(listener, (struct sockaddr *)&destAddr, sizeof(destAddr)) != 0) {
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
            MODBUS_LOGE(TAG, "Server-select() error lol!");
            break;
        }
        MODBUS_LOGI("Server-select() is OK...");

        // run through the existing connections looking for data to be read
        for(int i = 0; i <= fdmax; i++) {
            if(FD_ISSET(i, &read_fds)) {
                if(i == listener) {
                    // handle new connections
                    struct sockaddr_in clientAddr;
                    socklen_t clientAddrLen = sizeof(struct sockaddr_in);
                    int newfd = accept(listener, (struct sockaddr *)&clientAddr, &clientAddrLen);
                    if(newfd == -1) {
                        MODBUS_LOGE("Server-accept() error lol!");
                    } else {
                        MODBUS_LOGI("Server-accept() is OK...");
                        FD_SET(newfd, &master); // add to master set
                        if(newfd > fdmax) {
                            // keep track of the maximum
                            fdmax = newfd;
                        }
                        char addr_str[32];
                        inet_ntoa_r(clientAddr.sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                        MODBUS_LOGI("New connection from %s %x on socket %d", addr_str, clientAddr.sin_addr.s_addr, newfd);
                    }
                } else { // if(i == listener) {
                    char buf[128];
                    ssize_t nbytes = recv(i, buf, sizeof(buf), 0);
                    if (nbytes == 0) {
                        // Connection closed
                        MODBUS_LOGI("Socket %d hung up\n", i);
                        close(i);
                        FD_CLR(i, &master);
                    } else if (nbytes < 0) {
                        // Error
                        MODBUS_LOGE("recv() error lol [%d]!", nbytes);
                        close(i);
                        FD_CLR(i, &master);
                    } else {
                        // Echo back
                        if(send(i, buf, nbytes, 0) == -1)
                            MODBUS_LOGE("send() error lol!");
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

void tcp_server_ip6_task(void *pvParameters) {
    struct sockaddr_in6 destAddr;
    bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
    destAddr.sin6_family = AF_INET6;
    destAddr.sin6_port = htons(MODBUS_PORT);

    int listen_sock = socket(destAddr.sin6_family, SOCK_STREAM, IPPROTO_IPV6);
    if (listen_sock < 0) {
        goto close_socket;
    }

    if (bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr)) != 0) {
        goto close_socket;
    }

close_socket:
    if (listen_sock >= 0)
        close(listen_sock);
    vTaskDelete(NULL);
}

/*static void tcp_server_task(void *pvParameters) {
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1) {
        int listen_sock = listen_ip4();
        ESP_LOGI(TAG, "Socket binded");

        int err = listen(listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");

#ifdef CONFIG_EXAMPLE_IPV6
        struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
#else
        struct sockaddr_in sourceAddr;
#endif
        uint addrLen = sizeof(sourceAddr);
        int sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket accepted");

        while (1) {
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occured during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Connection closed
            else if (len == 0) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
            // Data received
            else {
#ifdef CONFIG_EXAMPLE_IPV6
                // Get the sender's ip address as string
                if (sourceAddr.sin6_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                } else if (sourceAddr.sin6_family == PF_INET6) {
                    inet6_ntoa_r(sourceAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }
#else
                inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
#endif

                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);

                int err = send(sock, rx_buffer, len, 0);
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                    break;
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}*/

void modbus_tcp_server_create() {
    xTaskCreate(tcp_server_ip4_task, "tcp_server_ip4", 4096, NULL, 5, NULL);
}
