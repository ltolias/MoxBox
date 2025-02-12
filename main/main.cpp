
#include <stdio.h>
#include <tuple>
#include "time.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"




#include "defines.h"
#include "lib/mcu.h"
#include "lib/structs.h"
#include "./version.h"
#include "lib/network_utils.h"
#include "web_server.h"
#include "data_logger.h"


#include "esp_log.h"

#ifdef FEATURE_REALTIME_TASK
#include "lib/task_realtime.h"
#endif

#include "lib/task_preemptible.h"


extern "C" void app_main(void)
{
    const char *TAG = "app_main";
    flashLED(100);
    vTaskDelay(pdMS_TO_TICKS(100));
    flashLED(100);
    vTaskDelay(pdMS_TO_TICKS(100));
    flashLED(100);

    // vTaskPrioritySet(NULL, 23);
    
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    esp_err_t error = heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);

    // wifi will only work if non volatile storage is setup
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS complete\n");
    vTaskDelay(pdMS_TO_TICKS(100));


    SPIFFSManager *assets_manager = new SPIFFSManager("assets");
    ESP_LOGI(TAG, "SPIFFS complete\n");
    
    NetworkUtils *network_utils = new NetworkUtils();
    ESP_LOGI(TAG, "Starting WiFi\n");
    
    
    network_utils->setup_wifi(assets_manager);
    
    network_utils->setup_mdns();
    
    ESP_LOGI(TAG, "Network complete\n");
    ESP_LOGI(TAG, "stack= %d, free_heap=%lu, free_psram=%u\n", uxTaskGetStackHighWaterMark(NULL), ESP.getFreeHeap(), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    
    // initialize_io();
    ESP_LOGI(TAG, "IO complete\n");
    ESP_LOGI(TAG, "stack= %d, free_heap=%lu, free_psram=%u\n", uxTaskGetStackHighWaterMark(NULL), ESP.getFreeHeap(), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    
    vTaskDelay(pdMS_TO_TICKS(100));

    QueueHandle_t ipc_data_queue = xQueueCreate(512, sizeof(ipc_event_t));
    ControlQueue *ipc_control_queue = new ControlQueue();

    xQueueReset(ipc_data_queue);    
    xQueueReset(ipc_control_queue->get_queue());   
    
    #ifdef FEATURE_MMC_LOGGING
        DataLoggerDB *data_logger_db = new DataLoggerDB("/dldb");
        ipc_control_queue->send(NULL, data_logger_db->latest_time_stored_ms * 1000, TIME_SYNC);
        ESP_LOGI(TAG, "SD_MMC complete\n");
    #else
        DataLoggerDB *data_logger_db = nullptr;
    #endif
        

    char * collected_data_names[] = COLLECTED_DATA_NAMES;
    std::map<char *, double> collected_data;
    for (auto const & name: collected_data_names) {
        collected_data[name] = 0;
    }


    initialize_web_server(ipc_control_queue, &collected_data, assets_manager, network_utils, data_logger_db);
    ESP_LOGI(TAG, "Web Server complete\n");
    ESP_LOGI(TAG, "stack= %d, free_heap=%lu, free_psram=%u\n", uxTaskGetStackHighWaterMark(NULL), ESP.getFreeHeap(), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    
    while (Serial.available()) Serial.read();

    
    String existing_config = assets_manager->read_file("/mox-config.json");
    int logging_enabled = assets_manager->apply_config(existing_config, ipc_control_queue);
    
    #ifdef FEATURE_NTP_SYNC
        #ifdef FEATURE_MMC_LOGGING
            network_utils->setup_ntp(ipc_control_queue, data_logger_db->latest_time_stored_ms * 1000);
        #else
            network_utils->setup_ntp(ipc_control_queue, 0);
        #endif
    #endif

  
    auto preemptible_task_params = std::make_tuple(network_utils, data_logger_db, &collected_data, ipc_data_queue, logging_enabled);

    TaskHandle_t PreemptibleTaskHandle;
    xTaskCreatePinnedToCore(
        PreemptibleTask,   /* Task function. */
        "PreemptibleTask",     /* name of task. */
        8192,       /* Stack size of task */
        &preemptible_task_params,        /* parameter of the task */
        3,           /* priority of the task */
        &PreemptibleTaskHandle,      /* Task handle to keep track of created task */
        0);          /* pin task to core 0 */                  
    
    ESP_LOGI(TAG, "PreemtibleTask launched\n");
    vTaskDelay(pdMS_TO_TICKS(500)); 


    #ifdef FEATURE_REALTIME_TASK

    auto realtime_task_params = std::make_tuple(ipc_data_queue, ipc_control_queue);
    TaskHandle_t RealTimeTaskHandle;

    xTaskCreatePinnedToCore(
        RealTimeTask,   /* Task function. */
        "RealTimeTask",     /* name of task. */
        8192,       /* Stack size of task */
        &realtime_task_params,        /* parameter of the task */
        3,           /* priority of the task */
        &RealTimeTaskHandle,      /* Task handle to keep track of created task */
        1);          /* pin task to core 1 */     

    ESP_LOGI(TAG, "RealTimeTask  launched\n");
    vTaskDelay(pdMS_TO_TICKS(100)); 

    #endif
    
   

    /* If we make it through setup, mark current ota app as valid, otherwise allow for a rollback on restart*/
	const esp_partition_t *partition = esp_ota_get_running_partition();
	ESP_LOGI(TAG, "Currently running partition: %s\n", partition->label);
	esp_ota_img_states_t ota_state;
	if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK) {
		if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
			esp_ota_mark_app_valid_cancel_rollback();
		}
	}

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
