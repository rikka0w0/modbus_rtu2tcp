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

#define EXAMPLE_MAX_STA_CONN       4
static const char *TAG = "wifi softAP";

static const char* hostname = "modbus_rtu2tcp";

#define GOT_IPV4_BIT BIT(0)
#define GOT_IPV6_BIT BIT(1)

#define CONNECTED_BITS (GOT_IPV4_BIT | GOT_IPV6_BIT)

static EventGroupHandle_t s_connect_event_group;
static ip4_addr_t s_ipv4_addr;
static ip6_addr_t s_ipv6_addr;

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
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
    }
}

static void wifi_ap_set_default_ssid(char* ssid) {
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    snprintf(ssid, WIFI_SSID_MAXLEN, "%s %02X%02X%02X%02X%02X%02X", WIFI_AP_SSID_DEFAULT,
                                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void wifi_init_softap() {
    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    size_t ssid_len = WIFI_SSID_MAXLEN;
    size_t pass_len = WIFI_PASS_MAXLEN;
    ESP_ERROR_CHECK(cp_get_by_id(CFG_WIFI_SSID_AP, wifi_config.ap.ssid, &ssid_len));
    ESP_ERROR_CHECK(cp_get_by_id(CFG_WIFI_PASS_AP, wifi_config.ap.password, &pass_len));
    if (strlen((char*) wifi_config.ap.ssid) == 0) {
        wifi_ap_set_default_ssid((char*) wifi_config.ap.ssid);
    }
    wifi_config.ap.ssid_len = strlen((char*)wifi_config.ap.ssid);
    if (strlen((char*) wifi_config.ap.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));


    //strncpy((char*) wifi_config.ap.password, EXAMPLE_ESP_WIFI_PASS, 60);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
             (char*) wifi_config.ap.ssid, (char*) wifi_config.ap.password);
}

void wifi_init_sta() {
    wifi_config_t wifi_config = { 0 };
    size_t ssid_len = WIFI_SSID_MAXLEN;
    size_t pass_len = WIFI_PASS_MAXLEN;
    ESP_ERROR_CHECK(cp_get_by_id(CFG_WIFI_SSID, (char*)wifi_config.sta.ssid, &ssid_len));
    ESP_ERROR_CHECK(cp_get_by_id(CFG_WIFI_PASS, (char*)wifi_config.sta.password, &pass_len));
    if (strlen((char*)wifi_config.sta.ssid) == 0 || strlen((char*)wifi_config.sta.password) == 0) {
        ESP_LOGW(TAG, "Invalid SSID or password for STA mode, switching to AP mode.");
        wifi_init_softap();
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void rtu_init() {
    uint32_t baudrate = 9600;
    uint8_t parity = 0;
    uint32_t tx_delay = 1;
    ESP_ERROR_CHECK(cp_get_u32_by_id(CFG_UART_BAUD, &baudrate));
    ESP_ERROR_CHECK(cp_get_u8_by_id(CFG_UART_PARITY, &parity));
    ESP_ERROR_CHECK(cp_get_u8_by_id(CFG_UART_TX_DELAY, &tx_delay));
    modbus_uart_init(baudrate, parity, tx_delay);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init()); // mDNS Implies tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_connect_event_group = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    rtu_init();
    // Webserver
    ESP_ERROR_CHECK(start_webserver());
    modbus_tcp_server_create();

    uint8_t wifi_mode = 0;
    ESP_ERROR_CHECK(cp_get_u8_by_id(CFG_WIFI_MODE, &wifi_mode));
    if (wifi_mode == 0) {
        // AP mode only
        wifi_init_softap();
        initialise_mdns();
    } else {
        // STA mode
        wifi_init_sta();
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

esp_err_t cpcb_check_set_baudrate(uint32_t baudrate) {
    if (baudrate >= 1200 && baudrate <= 921600) {
        modbus_uart_set_baudrate(baudrate);
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t cpcb_check_set_parity(uint8_t parity) {
    if (parity < 3) {
        modbus_uart_set_parity(parity);
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t cpcb_check_set_tx_delay(uint32_t tx_delay) {
    if (tx_delay <= MODBUS_RTU_TX_DELAY_US_MAX) {
        modbus_uart_set_tx_delay(tx_delay);
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}
