

#include "SD_MMC.h"
#include "mdns.h"

#define KEY_SIZE 2048
#define EXPONENT 65537
#include "PsychicHttp/PsychicHttp.h"
#include "PsychicHttp/PsychicHttpsServer.h"
#include "PsychicHttp/PsychicStaticFileHandler.h"

#include "lib/structs.h"
#include "lib/ssl_keygen.h"
#include "lib/camera_manager.h"
#include "lib/data_logger.h"



const char *certificate_manager_html =
#include "generated/certificates.html" //todo: this is going into the assets partition anyways so might as well grab it from spiffs instead
;


void initialize_web_server(ControlQueue *ipc_control_queue, std::map<char *, double> *collected_data, SPIFFSManager *assets_manager, NetworkUtils *network_utils, DataLoggerDB *data_logger_db) {
    
    const char* TAG = "webserver_init";

    SSLCertGenerator *cert_manager = new SSLCertGenerator();
    
    // create a http redirect server that allows clients to create/get our certificate and add it to their truststore
    // but otherwise just redirects everything to https
    ESP_LOGI(TAG, "creating redirect server");
    PsychicHttpServer *redirectServer = new PsychicHttpServer();
    redirectServer->config.ctrl_port = 32771; 
    redirectServer->config.max_uri_handlers = 20; 
    redirectServer->config.max_open_sockets = 2;
    redirectServer->config.task_priority = 23; 
    redirectServer->config.stack_size = 8192;
    redirectServer->config.core_id = 0; 
    redirectServer->config.backlog_conn = 16; 
    redirectServer->config.lru_purge_enable = true; 
    redirectServer->config.recv_wait_timeout = 10;
    redirectServer->config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "redirect server listen");
    redirectServer->listen(80);

    redirectServer->on("/ca_cert", HTTP_GET, [cert_manager](PsychicRequest *request){
        esp_err_t err = request->reply_cors(200, "text/plain", cert_manager->rootca_cert());
        return err;
    });

    redirectServer->on("/wipe_certs", HTTP_GET, [cert_manager](PsychicRequest *request){
        cert_manager->wipe_certs();
        return request->reply_cors(200, "text/plain", "all certs deleted");
    });

    redirectServer->on("/certificates", HTTP_GET, [](PsychicRequest *request){
        esp_err_t err = request->reply_cors(200, "text/html", certificate_manager_html);
        return err;
    });
    redirectServer->on("/restart", HTTP_GET, [](PsychicRequest *request){
        esp_err_t err = request->reply_cors(200, "text/plain", "restarting esp32");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        return err;
    });


    redirectServer->on("/generate_rootca", HTTP_GET, [cert_manager](PsychicRequest *request){
        //todo: theres a timezone issue here which I've gotten around by asking the user to send 
        //yesterday's date from the client as the validity start date instead of todays.  
        //ideally a whole datetime would be passed through here
        int year = atoi(request->getParam("year")->value().c_str());
        int month = atoi(request->getParam("month")->value().c_str());
        int day = atoi(request->getParam("day")->value().c_str());
        Serial.printf("generate rootca with year=%d, month=%d, day=%d", year, month, day);
        mbedtls_x509_time valid_from = {
            .year=year, .mon=month, .day=day, .hour=0, .min=0, .sec=0
        };
        mbedtls_x509_time valid_to = {
            .year=year + 1, .mon=month, .day=day, .hour=0, .min=0, .sec=0
        };
        if (cert_manager->rootca_exists()) {
            return request->reply_cors(200, "text/plain", "rootca already exists");
        } else {
            cert_manager->generate_root_ca(valid_from, valid_to);
            return request->reply_cors(200, "text/plain", "rootca successfuly generated");
        }
    });
    
    //but otherwise redirect everything to https
    redirectServer->onNotFound([](PsychicRequest *request) {
      String url = "https://" + request->host() + request->url();
      return request->redirect(url.c_str());
    });

     
    //if we have a rootca, setup the https server
    if (cert_manager->rootca_exists()) {
        //create a ssl cert for the ip addresses of our sta and ap interfaces
        cert_manager->generate_server_key(network_utils->sta_ipv4_addr, network_utils->ap_ipv4_addr);
        PsychicHttpsServer *server = new PsychicHttpsServer();

        server->ssl_config.httpd.ctrl_port = 32770; 
        server->ssl_config.httpd.max_uri_handlers = 128; 
        server->ssl_config.httpd.max_open_sockets = 2; //tls handshakes are slow, but the server will reuse them accross multiple requests
        server->ssl_config.httpd.task_priority = 23; 
        server->ssl_config.httpd.stack_size = 8192;
        server->ssl_config.httpd.core_id = 0; 
        server->ssl_config.httpd.backlog_conn = 16; //3 
        server->ssl_config.httpd.lru_purge_enable = true; // making this true improves stability, but slows things down
        server->ssl_config.httpd.recv_wait_timeout = 10; //2
        server->ssl_config.httpd.send_wait_timeout = 10; //2

        
        ESP_LOGI(TAG, "https listen");
        server->listen(443, cert_manager->server_cert, cert_manager->server_key);


        ESP_LOGI(TAG, "setup cors headers\n");
        //safari wont accept * for any of these

        DefaultHeaders::Instance().addHeader("Access-Control-Expose-Headers", "Cache-Control,Content-Disposition,X-frame-time,X-Moxbox-Cache");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS,HEAD");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Accept,Origin,Referer,User-Agent,Cache-Control,Content-Type");
        


        ESP_LOGI(TAG, "serve spiffs");

        // attach spiffs filesystem root to serve the rest of the frontend
        server->serveStatic("/", *(assets_manager->get_fs()), "/", NULL)->setDefaultFile("index.html");

        //attach sd card filesystem to serve our data
        #ifdef FEATURE_MMC_LOGGING
        
        server->on("/sdcard/*", [data_logger_db](PsychicRequest *request) {
            String req_path = request->path();
            
            //parse out the various path fields
            int file_index_start = req_path.lastIndexOf('/') + 1;
            String file_index_s = req_path.substring(file_index_start);

            int mins_per_file_start = req_path.lastIndexOf('/', file_index_start - 2) + 1;
            String mins_per_file_s = req_path.substring(mins_per_file_start, file_index_start - 1);

            int data_name_start = req_path.lastIndexOf('/', mins_per_file_start - 2) + 1;
            String data_name = req_path.substring(data_name_start, mins_per_file_start - 1);

            int mins_per_file = atoi(mins_per_file_s.c_str());
            int file_index = atoi(file_index_s.c_str());

            char logger_key[128];
            snprintf(logger_key, 128, "%.32s/%.32s/%.32s", data_logger_db->db_dirname, data_name.c_str(), mins_per_file_s.c_str());
            
            auto it1 = data_logger_db->loggers.find(logger_key);
            if (it1 == data_logger_db->loggers.end()) {
                return request->reply_cors(404, "text/plain", "not found");
            }
            int fs_path_start = req_path.indexOf('/', 1);
            String fs_path = req_path.substring(fs_path_start);
            PsychicFileResponse response(request, SD_MMC, fs_path);

            DataLogger *logger = it1->second;
            //allow frontend to cache this if its not currently being written to
            if (file_index != logger->time_ms_to_index(logger->latest_time_stored_ms)) {
                response.addHeader("X-Moxbox-Cache", "yes");
            }
            //add in dynamic cors header
            response.addHeader("Access-Control-Allow-Origin", request->header("Origin").c_str());

            return response.send();
        });
        // server->serveStatic("/sdcard", SD_MMC, "/", NULL); 

        //other data api endpoints
        server->on("/latest_log_indexes", HTTP_GET, [data_logger_db](PsychicRequest *request){
            JsonDocument doc;
            for (auto [key, data_logger]: data_logger_db->loggers) {
                JsonDocument doc2;
                doc2["path"] = key;
                doc2["mins_per_file"] = data_logger->min_per_file;
                doc2["latest_time_stored_ms"] = data_logger->latest_time_stored_ms;
                doc2["latest_index"] = data_logger->time_ms_to_index(data_logger->latest_time_stored_ms);
                doc.add(doc2);
            }
            int len_final = measureJson(doc) + 1; 
            char *output = new char[len_final];
            serializeJson(doc, output, len_final);
            esp_err_t err = request->reply_cors(200, "application/json", output);
            delete[] output;
            return err;
        });

        server->on("/timings", HTTP_GET, [data_logger_db](PsychicRequest *request){
            JsonDocument doc;
            for (timings_t timing_detail: data_logger_db->timing_details) {
                JsonDocument doc2;
                doc2["ms_per_row"] = timing_detail.ms_per_row;
                doc2["mins_per_file"] = timing_detail.mins_per_file;
                doc2["discard_after_hrs"] = timing_detail.discard_after_hrs;
                doc.add(doc2);
            }
            int len_final = measureJson(doc) + 1; 
            char *output = new char[len_final];
            serializeJson(doc, output, len_final);
            esp_err_t err = request->reply_cors(200, "application/json", output);
            delete[] output;
            return err;
        });

        server->on("/delete_db", HTTP_GET, [data_logger_db](PsychicRequest *request){
            data_logger_db->delete_dir("/sd/dldb");
            esp_err_t err = request->reply_cors(200, "text/plain", "deleted dldb from sd card, restarting...");
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
            return err;
        });
        #endif

        server->on("/sensor_data", HTTP_GET, [collected_data](PsychicRequest *request){
            JsonDocument doc;
            for (auto const & [key, val]: *collected_data) {
                doc[key] = val;
            }
            int len_final = measureJson(doc) + 1; 
            char *output = new char[len_final];
            serializeJson(doc, output, len_final);
            
            esp_err_t err = request->reply_cors(200, "application/json", output);
            delete[] output;
            return err;
        });

        //setup camera and register endpoints:
        ESP_LOGI(TAG, "Setting up camera and endpoints");
        CameraManager *camera_manager = new CameraManager();
        camera_manager->setup_camburst();
        camera_manager->register_camera_endpoints(server, assets_manager);

        //setup utility endpoints:

        server->on("/peers", HTTP_GET, [network_utils](PsychicRequest *request){
            JsonDocument doc;

            if (!network_utils->mdns_gather_peers(doc)) {
                request->reply_cors(500, "application/json", "[]");
            }

            int len_final = measureJson(doc) + 1; 
            char *output = new char[len_final];
            serializeJson(doc, output, len_final);

            esp_err_t err = request->reply_cors(200, "application/json", output);
            delete[] output;
            return err;
        });
        
        server->on("/config", HTTP_POST, [assets_manager, ipc_control_queue](PsychicRequest *request){
            String body_in = request->body();
            assets_manager->apply_config(body_in, ipc_control_queue);
            assets_manager->write_file("/mox-config.json", body_in);

            return request->reply_cors(200, "application/json", "{\"result\": \"success\"}");

        });
        
        server->on("/config", HTTP_GET, [assets_manager](PsychicRequest *request){
            String content = assets_manager->read_file("/mox-config.json");
            return request->reply_cors(200, "application/json", content.c_str());
        });

        server->on("/restart", HTTP_GET, [](PsychicRequest *request){
            esp_err_t err = request->reply_cors(200, "text/plain", "restarting esp32");
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
            return err;
        });
        server->on("/ping", HTTP_GET, [](PsychicRequest *request){
            esp_err_t err = request->reply_cors(200, "text/plain", "pong2");
            return err;
        });

        server->on("/ca_cert", HTTP_GET, [cert_manager](PsychicRequest *request){
            esp_err_t err = request->reply_cors(200, "text/plain", cert_manager->rootca_cert());
            return err;
        });
        server->on("/server_cert", HTTP_GET, [cert_manager](PsychicRequest *request){
            esp_err_t err = request->reply_cors(200, "text/plain", cert_manager->server_cert);
            return err;
        });

        

        server->on("/mcuinfo", HTTP_GET, [assets_manager](PsychicRequest *request){
        char *combined = new char[4096];
        *combined = '\0';
        char line_break[2] = "\n";

        char *buffer = new char[1024];
        buffer[0] = '\0';

        sprintf(buffer, "stack_highwater=%d, cpu_freq=%lu, chip_cores=%d\n\
dram: free=%lu, largest=%lu low_water=%lu\n\
psram: free=%u, largest=%u, low_water=%u\n\
sd_mmc: total=%llu used=%llu\n",
            uxTaskGetStackHighWaterMark(NULL), ESP.getCpuFreqMHz(), ESP.getChipCores(), 
            ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), 
            heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), SD_MMC.totalBytes(), SD_MMC.usedBytes()
        ); 
        strcat(combined, buffer);
        strcat(combined, line_break);
        buffer[0] = '\0';
        

        vTaskGetRunTimeStats(buffer);
        strcat(combined, buffer);
        strcat(combined, line_break);
        buffer[0] = '\0';

        vTaskList(buffer);
        
        strcat(combined, buffer);
        buffer[0] = '\0';

        esp_err_t err = request->reply_cors(200, "text/plain", combined);
        delete[] combined;
        delete[] buffer;
        return err;
    });

        //some other logging and control endpoints
        

        server->on("/lamp", HTTP_GET, [](PsychicRequest *request){
            String value = request->getParam("value")->value();
            set_lamp(value.toInt());
            esp_err_t err = request->reply_cors(200, "text/plain", "lamp set");
            return err;
        });

        server->on("/relay", HTTP_GET, [](PsychicRequest *request){
            String value = request->getParam("value")->value();
            set_relay(value.toInt());
            esp_err_t err = request->reply_cors(200, "text/plain", "relay set");
            return err;
        });
    }
}




/*


*/