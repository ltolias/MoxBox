


#include "lib/network_utils.h"

void PreemptibleTask( void * pvParameters ){
    Serial.printf("PreemptibleTask started on core: %d\n", xPortGetCoreID());
    
    auto const & params = *((std::tuple<NetworkUtils *, DataLoggerDB *, std::map<char *, double> *, QueueHandle_t, int> *) pvParameters);

    auto const & network_utils = std::get<0>(params);
    auto const & data_logger_db = std::get<1>(params);
    auto const & collected_data = std::get<2>(params);
    auto const & ipc_data_queue = std::get<3>(params);
    auto const & logging_enabled = std::get<4>(params);

    ipc_event_t evt_ipc;

    int report_interval_ms = 3000;
    int report_time_last_ms = millis();
    int received_count = 0;
    while (true) {

        while (xQueueReceive(ipc_data_queue, &evt_ipc, pdMS_TO_TICKS(100))) 
        {   
            int64_t evt_time_ms = evt_ipc.time_us / 1000;
            if (strncmp(evt_ipc.name, "d_", 2) != 0) {
                received_count += 1;
                #if defined(FEATURE_MMC_LOGGING)
                if (logging_enabled == 1) {
                    data_logger_db->write(evt_ipc.name, evt_time_ms, evt_ipc.value, evt_ipc.data_ipc_interval_ms);  //EDIT
                }
                #endif
            }
            (*collected_data)[evt_ipc.name] = evt_ipc.value;
        }

        if (millis() - report_time_last_ms >= report_interval_ms) {
            flashLED(5);
            vTaskDelay(pdMS_TO_TICKS(200));
            for (int jj = 0; jj <= received_count / 10; jj++) {
                flashLED(5);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            report_time_last_ms = millis();
            Serial.printf("\ndram: f=%lu, ma=%lu lw=%lu psram: f=%u, ma=%u, lw=%u sd_mmc_used=%llu\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), 
                heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), SD_MMC.usedBytes()
            ); 
            #ifdef FEATURE_MMC_LOGGING
                Serial.printf("Latest recorded time: %lld.%lld\n", data_logger_db->latest_time_stored_ms / 1000, data_logger_db->latest_time_stored_ms % 1000);
            #endif
            received_count = 0;
        }
        
        /*
        1s for 1h by 5min = 12ind, 300rpf, 3600rt 
        1min for 3 days by 4 hours = 18ind, 240rpf, 4320rt  
        15min for 900 days by 72 hours = 300ind, 288rpf, 86400rt for 2.5yrs,            
        */

        handleSerial();
        network_utils->handle_wifi();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

}

