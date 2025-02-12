#include "network_utils.h"


// #include <format>
// #include <iostream>
// #include <string>
#include <exception>

#include "sys/param.h"

#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "mdns.h"

#include "ESPNtpClient/ESPNtpClient.h"
#include "ESPNtpClient/TZdef.h"
#include "esp_log.h"
#include "esp_mac.h"


/*DHCP server option*/
#define DHCPS_OFFER_DNS             0x02


NetworkUtils::NetworkUtils() {
    snprintf(mdns_service_type, 64, "_%s", MDNS_NAME);
}


int NetworkUtils::s_retry_num = 0;

EventGroupHandle_t NetworkUtils::s_wifi_event_group = xEventGroupCreate();


void NetworkUtils::setup_wifi(SPIFFSManager *assets_manager)
{
    wifi_counter_us = 0;
   
    

    //someone else is calling this it seems:
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "esp_netif_init passed.");

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &s_wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &s_wifi_event_handler,
                                                        sta_ipv4_addr,
                                                        &instance_got_ip));

    
    wifi_config_t wifi_config_sta = {
        .sta = {
            .ssid = "temp",
            .password = "temp",
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    bool sta_enabled = false;
         
    try {
        String config = assets_manager->read_file("/mox-config.json");
        DynamicJsonDocument doc(JSON_DOC_SIZE);
        deserializeJson(doc, config);
        String sta_ssid = doc["sta_ssid"].as<String>();
        String sta_pass = doc["sta_pass"].as<String>();  
        strncpy((char *)wifi_config_sta.sta.ssid, sta_ssid.c_str(), 32);
        strncpy((char *)wifi_config_sta.sta.password, sta_pass.c_str(), 64);
        sta_enabled = true;
     } catch (const std::exception& e) {
        ESP_LOGI(TAG, "sta credentials parse failed: %s\n", e.what());
    }


    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = MDNS_NAME,
            .password = "1234567890",
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 8,
            .beacon_interval = 5000, //1000=1s
        }
    };
    unsigned char mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf((char *)wifi_config_ap.ap.ssid, 32, "%s-%02x:%02x:%02x:%02x:%02x:%02x", MDNS_NAME, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap) );
    if (sta_enabled) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta) );
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init finished.");
    if (sta_enabled) {
        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
        * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                pdMS_TO_TICKS(WIFI_STA_CONNECT_STARTUP_WAIT_MS));

        
        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
        * happened. */
        if (bits & WIFI_CONNECTED_BIT) {
            esp_netif_set_default_netif(esp_netif_sta);
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                    CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
            softap_set_dns_addr(esp_netif_ap,esp_netif_sta);
        } else if (bits & WIFI_FAIL_BIT) {
            esp_netif_set_default_netif(esp_netif_ap);
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                    CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
        } else {
            esp_netif_set_default_netif(esp_netif_ap);
            ESP_LOGI(TAG, "Timed out connecting to SSID:%s, password:%s",
                    CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
        }
        
    }
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_ap,&ip_info);
    snprintf(ap_ipv4_addr, 32, IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "got AP ip:" IPSTR, IP2STR(&ip_info.ip));

    /* Enable napt on the AP netif */
    // if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
    //     ESP_LOGE(TAG, "NAPT not enabled on the netif: %p", esp_netif_ap);
    // }
    int ota_port = 2323;
    if (setup_ota_server(ota_port)) {
        ESP_LOGI(TAG, "Started ota update server on port: %i\n", ota_port);
    } else {
        ESP_LOGI(TAG, "Failed to start ota update server\n");
    }
}


void NetworkUtils::s_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Station started");

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d", MAC2STR(event->mac), event->aid);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");


    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        snprintf((char *) arg, 32, IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "got Station ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


void NetworkUtils::softap_set_dns_addr(esp_netif_t *esp_netif_ap,esp_netif_t *esp_netif_sta)
{
    esp_netif_dns_info_t dns;
    esp_netif_get_dns_info(esp_netif_sta,ESP_NETIF_DNS_MAIN,&dns);
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
}




void NetworkUtils::setup_ntp(ControlQueue *ipc_control_queue, uint64_t latest_time_us) {
    NTP.setTimeZone(TZ_Etc_UTC);
    NTP.setNTPTimeout(2500);
    NTP.setMinSyncAccuracy(500);
    NTP.settimeSyncThreshold(500);

    NTP.onNTPSyncEvent([ipc_control_queue] (NTPEvent_t event) {
        if (event.event == NTPSyncEventType_t::timeSyncd) {
            ESP_LOGI(TAG, "\nNTP update received %s\n", NTP.ntpEvent2str(event));
            ipc_control_queue->send(NULL, NTP.micros(), TIME_SYNC);
        }
    });

    NTP.setInterval (60);
    NTP.begin ("pool.ntp.org", false); //start autosyncing ntp
    int i = 0;
    int ntp_startup_timeout = NTP_SYNC_TIMEOUT;
    while (NTP.syncStatus() != NTPStatus_t::syncd && i < ntp_startup_timeout) { //try to get an ntp fix at startup
        ESP_LOGI(TAG, "Waiting for ntp sync...\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
        i++;
    }
    if (i == ntp_startup_timeout) {  
        ipc_control_queue->send(NULL, latest_time_us, TIME_SYNC);
    }
}



esp_err_t NetworkUtils::s_ota_update_app_handler(httpd_req_t *req)
{
	char buf[1024];
	esp_ota_handle_t ota_handle;
	int remaining = req->content_len;

	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
	ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chunk
		if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			return ESP_FAIL;
		}

		remaining -= recv_len;
	}

	// Validate and switch to new OTA image and reboot
	if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
			return ESP_FAIL;
	}

	httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

	vTaskDelay(pdMS_TO_TICKS(500));
	esp_restart();

	return ESP_OK;
}
esp_err_t NetworkUtils::s_ota_update_assets_handler(httpd_req_t *req)
{
	char buf[1024];
	size_t write_offset = 0;
	int remaining = req->content_len;

	const esp_partition_t *spiffs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "assets");
    
    if (req->content_len > spiffs_partition->size) {
        sprintf(buf, "Spiffs partition size (%lu) is smaller than payload (%d)", spiffs_partition->size, req->content_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, buf);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_partition_erase_range(spiffs_partition, 0, spiffs_partition->size));

	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chunk
        if (esp_partition_write(spiffs_partition, write_offset, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			return ESP_FAIL;
		}
        write_offset += recv_len;
		remaining -= recv_len;
	}

	httpd_resp_sendstr(req, "assets update complete\n");
	return ESP_OK;
}
esp_err_t NetworkUtils::s_ota_update_certs_handler(httpd_req_t *req)
{
	char buf[1024];
	size_t write_offset = 0;
	int remaining = req->content_len;

	const esp_partition_t *spiffs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "certs");
    
    if (req->content_len > spiffs_partition->size) {
        sprintf(buf, "Spiffs partition size (%lu) is smaller than payload (%d)", spiffs_partition->size, req->content_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, buf);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_partition_erase_range(spiffs_partition, 0, spiffs_partition->size));

	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chunk
        if (esp_partition_write(spiffs_partition, write_offset, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			return ESP_FAIL;
		}
        write_offset += recv_len;
		remaining -= recv_len;
	}

	httpd_resp_sendstr(req, "certs update complete\n");
	return ESP_OK;
}

void NetworkUtils::setup_mdns() {
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGI(TAG, "MDNS Init failed: %d\n", err);
    }
    send_mdns();
}

bool NetworkUtils::mdns_gather_peers(JsonDocument &doc) {
    ESP_LOGI(TAG, "Query mdns: %s.%s.local\n", "_tcp", mdns_service_type);

    mdns_result_t * results = NULL;
    esp_err_t err = mdns_query_ptr(mdns_service_type, "_tcp", 3000, 64,  &results);
    if(err){
        return false;
    }
    mdns_ip_addr_t * a = NULL;
    int i = 1, t;

    doc.to<JsonArray>();
    mdns_result_t * current_result = results;

    while(current_result){
        JsonDocument doc2;
        if(current_result->instance_name){
            doc2["name"] = current_result->instance_name;
        }
        if(current_result->hostname){
            doc2["host"] = current_result->hostname;
            doc2["port"] = current_result->port;
        }
        if(current_result->txt_count){
            doc2["txt_count"] = current_result->txt_count;
            JsonDocument doc3;
            for(t=0; t<current_result->txt_count; t++){
                doc3[current_result->txt[t].key] = current_result->txt[t].value;
            }
            doc2["txt"] = doc3;
        }
        a = current_result->addr;

        doc2["addr"].to<JsonArray>();
        while(a){
            JsonDocument doc3;
            if(a->addr.type == IPADDR_TYPE_V6){
                const uint8_t* ip_pt = (const uint8_t*)&a->addr.u_addr.ip6.addr;
                char ip_buf[40];
                snprintf(ip_buf, 40, IPV6STR,  (ip_pt[0]>> 16) & 0xffff, ip_pt[0] & 0xffff, \
                    (ip_pt[1]>> 16) & 0xffff, ip_pt[1] & 0xffff, \
                    (ip_pt[2]>> 16) & 0xffff, ip_pt[2] & 0xffff, \
                    (ip_pt[3]>> 16) & 0xffff, ip_pt[3] & 0xffff);

                doc3["ip"] = ip_buf;
                doc3["type"] = "ipv6";
            } else {
                const uint8_t* ip_pt = (const uint8_t*)&a->addr.u_addr.ip4.addr;
                char ip_buf[16];
                snprintf(ip_buf, 16, IPSTR,  ip_pt[0], ip_pt[1], ip_pt[2], ip_pt[3]);
                doc3["ip"] = ip_buf;
                doc3["type"] = "ipv4";
            }
            doc2["addr"].add(doc3);
            a = a->next;
        }
        doc.add(doc2);
        current_result = current_result->next;
    }
    JsonDocument doc4;
    doc4["name"] = "self";
    doc4["port"] = 80;

    JsonDocument doc5;
    doc5["ip"] = this->sta_ipv4_addr;
    doc5["type"] = "ipv4";

    doc4["addr"].to<JsonArray>();
    doc4["addr"].add(doc5);

    doc.add(doc4);

    mdns_query_results_free(results);
    return true;
}
esp_err_t NetworkUtils::setup_ota_server(int port)
{

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;                     
    config.core_id = 0;       
    config.server_port = port;   
    config.ctrl_port = 32769; //32768 is default    

    
    httpd_uri_t ota_app = {
        .uri	  = "/ota_app",
        .method   = HTTP_POST,
        .handler  = s_ota_update_app_handler,
        .user_ctx = NULL
    };
    httpd_uri_t ota_assets = {
        .uri	  = "/ota_assets",
        .method   = HTTP_POST,
        .handler  = s_ota_update_assets_handler,
        .user_ctx = NULL
    };  
    httpd_uri_t ota_certs = {
        .uri	  = "/ota_certs",
        .method   = HTTP_POST,
        .handler  = s_ota_update_certs_handler,
        .user_ctx = NULL
    };      

	if (httpd_start(&ota_server, &config) == ESP_OK) {
		httpd_register_uri_handler(ota_server, &ota_app);
        httpd_register_uri_handler(ota_server, &ota_assets);
        httpd_register_uri_handler(ota_server, &ota_certs);
	}

	return ota_server == NULL ? ESP_FAIL : ESP_OK;
}




void NetworkUtils::send_mdns() {
    mdns_hostname_set(MDNS_NAME);
    mdns_service_add(MDNS_NAME, mdns_service_type, "_tcp", 80, NULL, 0);
}




void NetworkUtils::handle_wifi() {
    if (esp_timer_get_time() - wifi_counter_us >= (int64_t)30000000 ) {
        ESP_LOGI(TAG, "Resending mdns");
        send_mdns();
        wifi_counter_us = esp_timer_get_time();
    }
   
}

// assert failed: pbuf_free /IDF/components/lwip/lwip/src/core/pbuf.c:753 (pbuf_free: p->ref > 0)