#ifndef CONFIG_H
#define CONFIG_H

#define MDNS_NAME "moxbox"

// #define BOARD_ESP_THINKER
#define BOARD_ESP_32S3_FREENOVE


#define FEATURE_CAMERA
// #define FEATURE_LAMP
// #define FEATURE_USER_RELAY

#define FEATURE_REALTIME_TASK //data logging task
#define COLLECTED_DATA_NAMES {"cpu0", "cpu1", "dram_free", "dram_max", "dram_min", "spiram_free", "spiram_max", "spiram_min"}
#define COLLECTED_DATA_COUNT 8
#define OVERSAMPLE_FACTOR 1
#define SAMPLE_INTERVAL_MS 1000
#define DATA_IPC_INTERVAL_MS 1000

#define FEATURE_MMC_LOGGING

#define FEATURE_NTP_SYNC
#define NTP_SYNC_TIMEOUT 2

#define WIFI_STA_CONNECT_STARTUP_WAIT_MS 10000

#ifdef BOARD_ESP_32S3_FREENOVE

    #define SD_MMC_CLK GPIO_NUM_39
    #define SD_MMC_CMD GPIO_NUM_38
    #define SD_MMC_D0 GPIO_NUM_40

    #define TOUCH_PAD_SENSE TOUCH_PAD_NUM1
    #define TOUCH_PAD_CAL TOUCH_PAD_NUM14

    #define LED_PIN  GPIO_NUM_2
    #ifdef FEATURE_USER_RELAY
        #define USER_RELAY_PIN GPIO_NUM_47
    #endif
    #ifdef FEATURE_REALTIME_TASK
        #define RELAY_PIN GPIO_NUM_21
    #endif
    #ifdef FEATURE_LAMP
        #define LAMP_PIN GPIO_NUM_48
    #endif
#elif defined(BOARD_ESP_THINKER)
    #define TOUCH_PAD_SENSE TOUCH_PAD_NUM0 //4
    #define TOUCH_PAD_CAL TOUCH_PAD_NUM5 //12
    #define LED_PIN  33
    #ifdef FEATURE_USER_RELAY
        #define USER_RELAY_PIN 2
    #endif
    #ifdef FEATURE_REALTIME_TASK
        // #define RELAY_PIN 16 //EDIT
    #endif
    #ifdef FEATURE_LAMP
        #define LAMP_PIN 4
    #endif
#endif

#define MAX_FILE_SIZE 50000


#endif