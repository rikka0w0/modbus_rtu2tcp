#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "mdns.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/ip6.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "main.h"
#include "modbus.h"
#include "modbus_tcp_server.h"

#define EXAMPLE_ESP_WIFI_SSID      "Modbus RTU2TCP Init Setup"
#define EXAMPLE_ESP_WIFI_PASS      "mypassword"
#define EXAMPLE_MAX_STA_CONN       4
static const char *TAG = "wifi softAP";

static const char* hostname = "modbus_rtu2tcp";

#define GOT_IPV4_BIT BIT(0)
#define GOT_IPV6_BIT BIT(1)

#define CONNECTED_BITS (GOT_IPV4_BIT | GOT_IPV6_BIT)

static EventGroupHandle_t s_connect_event_group;
static ip4_addr_t s_ipv4_addr;
static ip6_addr_t s_ipv6_addr;

static void network_ready(void) {
    // Webserver
    ESP_ERROR_CHECK(start_webserver());
    modbus_tcp_server_create();
}

static void initialise_mdns(void) {
    //initialize mDNS
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(hostname) );
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set("ESP8266 with mDNS") ); // EXAMPLE_MDNS_INSTANCE

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[3] = {
        {"board","esp8266"},
        {"u","user"},
        {"p","password"}
    };

    //initialize service
    ESP_ERROR_CHECK( mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData, 3) );
    //add another TXT item
    //ESP_ERROR_CHECK( mdns_service_txt_item_set("_http", "_tcp", "path", "/foobar") );
    //change TXT item value
    //ESP_ERROR_CHECK( mdns_service_txt_item_set("_http", "_tcp", "u", "admin") );
    //free(hostname);
}

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    memcpy(&s_ipv4_addr, &event->ip_info.ip, sizeof(s_ipv4_addr));
    xEventGroupSetBits(s_connect_event_group, GOT_IPV4_BIT);
}

static void on_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    memcpy(&s_ipv6_addr, &event->ip6_info.ip, sizeof(s_ipv6_addr));
    xEventGroupSetBits(s_connect_event_group, GOT_IPV6_BIT);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_START) {
        // IPv6 - FE80::1
        ESP_ERROR_CHECK(tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_AP));
        struct netif * netif = NULL;
        ESP_ERROR_CHECK(tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_AP, (void**) &netif));
        ip6_addr_t ip6ll;
        ESP_ERROR_CHECK(ip6addr_aton("FE80::1", &ip6ll) == 1 ? 0 : 1);
        netif_ip6_addr_set(netif, 0, &ip6ll);

        // IPv4 - 10.1.10.1
        tcpip_adapter_ip_info_t info = { 0, };
		IP4_ADDR(&info.ip, 10, 1, 10, 1);
		IP4_ADDR(&info.gw, 10, 1, 10, 1);
		IP4_ADDR(&info.netmask, 255, 255, 255, 0);
		ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
		ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
		ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));

		memcpy(&s_ipv4_addr, &info.ip, sizeof(s_ipv4_addr));
		xEventGroupSetBits(s_connect_event_group, GOT_IPV4_BIT);

		network_ready();
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);

        network_ready();
    }
}

void wifi_init_softap() {
    // tcpip_adapter_init();
    /**/

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));


    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init()); // mDNS Implies tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_connect_event_group = xEventGroupCreate();

    modbus_uart_init();

    char ssid[WIFI_SSID_MAXLEN], pass[WIFI_PASS_MAXLEN];
    ESP_ERROR_CHECK(cp_get_wifi_params(ssid, pass));
    if (strlen(ssid) == 0) {
        ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
        wifi_init_softap();
        initialise_mdns();
    } else {
        ESP_LOGI(TAG, "ESP_WIFI_MODE_STA: [%s] = [%s]", ssid, pass);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        wifi_config_t wifi_config = { 0 };

        strcpy((char *)&wifi_config.sta.ssid, ssid);
        strcpy((char *)&wifi_config.sta.password, pass);

        ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    printf("Hello world!\n");
    xEventGroupWaitBits(s_connect_event_group, CONNECTED_BITS, pdTRUE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "IPv4 address: " IPSTR, IP2STR(&s_ipv4_addr));
    ESP_LOGI(TAG, "IPv6 address: " IPV6STR, IPV62STR(s_ipv6_addr));

    /* Print chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP8266 chip with %d CPU cores, WiFi, ",
            chip_info.cores);

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    */
}
