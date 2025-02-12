#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H



#include "esp_http_server.h"
#include "ArduinoJson.h"
#include "esp_netif.h"

#include "defines.h"
#include "structs.h"
#include "spiffs_manager.h"


class NetworkUtils {
public:
    NetworkUtils();
    ~NetworkUtils();
    void setup_wifi(SPIFFSManager *assets_manager);
    void handle_wifi();
    void send_mdns();
    void setup_mdns();
    void setup_ntp(ControlQueue *ipc_control_queue, uint64_t latest_time_us);
    esp_err_t setup_ota_server(int port);
    bool mdns_gather_peers(JsonDocument &doc);

    char sta_ipv4_addr[32] = "";
    char ap_ipv4_addr[32] = "";
private:
    httpd_handle_t ota_server;
    int64_t wifi_counter_us;

    uint8_t sta_ssid[64];
    uint8_t sta_pass[64];

    char mdns_service_type[64];

    void softap_set_dns_addr(esp_netif_t *esp_netif_ap, esp_netif_t *esp_netif_sta);

    static esp_err_t s_ota_update_assets_handler(httpd_req_t *req);
    static esp_err_t s_ota_update_app_handler(httpd_req_t *req);
    static esp_err_t s_ota_update_certs_handler(httpd_req_t *req);
    static void s_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
    
    static int s_retry_num;
    static EventGroupHandle_t s_wifi_event_group;
    /* The event group allows multiple bits for each event, but we only care about two events:
    * - we are connected to the AP with an IP
    * - we failed to connect after the maximum amount of retries */
    static const int WIFI_CONNECTED_BIT = BIT0;
    static const int WIFI_FAIL_BIT = BIT1;
    static constexpr const char * const TAG = "network_utl";
    unsigned long wifi_counter_start;
};


#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


#endif


