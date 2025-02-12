#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H


#define CAMERA_MODEL_ESP32S3_EYE
#define MIN_FRAME_TIME 250
#define BURST_CHUNK_SIZE 2048
#define USER_FRAMEBUFFER_COUNT 1 //384KB per frame


#include "esp_camera.h"
#include "PsychicHttp/PsychicHttpsServer.h"

#include "spiffs_manager.h"
#include "camera_pins.h"
#include "structs.h"
#include "defines.h"




class CameraManager {
public:
    CameraManager();
    void initialize(int xclk_freq_hz, int jpeg_quality);
    bool is_running();
    void setup_camburst();
    void register_camera_endpoints(PsychicHttpsServer *server, SPIFFSManager *spiffs_manager);
    SemaphoreHandle_t s_framebuffer_locked;
    frame_record_t *user_framebuffer;
    TaskHandle_t cam_burst_task_handle;
private:
    void configure_pins();
    void configure_camera(int xclk_freq_hz, int jpeg_quality);
    esp_err_t initialize_camera();
    void handle_camera_init_error();
    void configure_sensor_settings();
    // Camera config structure
    camera_config_t config;
    bool running;
    int sensorPID;
    static constexpr const char * const TAG = "camera_mgr";
};


#endif // CAMERA_MANAGER_H