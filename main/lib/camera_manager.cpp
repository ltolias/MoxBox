#include "camera_manager.h"
#include "esp_log.h"
#include "Arduino.h"
#include "task_camburst.h"

CameraManager::CameraManager() {
  running = false;
}

bool CameraManager::is_running() {
    return running;
}

void CameraManager::initialize(int xclk_freq_hz, int jpeg_quality) {
    configure_pins();
    configure_camera(xclk_freq_hz, jpeg_quality);

    // Initialize the camera
    if (initialize_camera() != ESP_OK) {
        handle_camera_init_error();
        return;
    }

    configure_sensor_settings();
    
    ESP_LOGI(TAG, "Camera initialized successfully.\n\n");
    running = true;
}

void CameraManager::setup_camburst() {
    s_framebuffer_locked = xSemaphoreCreateBinary();
    
    size_t alloc_size = 128*1024;

    uint32_t alloc_caps = MALLOC_CAP_8BIT;
    alloc_caps |= MALLOC_CAP_SPIRAM;     // MALLOC_CAP_INTERNAL

    user_framebuffer = (frame_record_t *) malloc(sizeof(frame_record_t) * USER_FRAMEBUFFER_COUNT);
    for (int ii = 0; ii < USER_FRAMEBUFFER_COUNT; ii++) {
      user_framebuffer[ii].buf = (uint8_t *)heap_caps_malloc(alloc_size, alloc_caps);
      user_framebuffer[ii].len = 0;
      user_framebuffer[ii].time = 0;
      
      if (user_framebuffer[ii].buf  == NULL) {
        ESP_LOGI(TAG, "Frame buffer malloc failed on %d\n", ii);
      }
    }
    xSemaphoreGive(s_framebuffer_locked);

    std::tuple<SemaphoreHandle_t *, frame_record_t *> camburst_task_params = std::make_tuple(&s_framebuffer_locked, user_framebuffer);
    
    
    xTaskCreatePinnedToCore(
        CamBurstTask,   /* Task function. */
        "CamBurstTask",     /* name of task. */
        2048,       /* Stack size of task */
        &camburst_task_params,        /* parameter of the task */
        3,           /* priority of the task */
        &cam_burst_task_handle,      /* Task handle to keep track of created task */
        1);          /* pin task to core 1 */                  
    
    ESP_LOGI(TAG, "CamBurstTask launched\n");
    vTaskDelay(pdMS_TO_TICKS(500)); 
}


void CameraManager::register_camera_endpoints(PsychicHttpsServer *server, SPIFFSManager *assets_manager) {

    CameraManager *cam_manager = this;

    server->on("/startcam", HTTP_GET, [assets_manager, cam_manager](PsychicRequest *request){
      String content = assets_manager->read_file("/mox-config.json");
      JsonDocument doc;
      deserializeJson(doc, content);

      int xclk_freq_mhz = doc["xclk_freq_mhz"].as<int>();
      int jpeg_quality = doc["jpeg_quality"].as<int>();

      if (!cam_manager->is_running()) {
        ESP_LOGI(TAG, "Starting camera initialization\n");
        cam_manager->initialize(xclk_freq_mhz * 1000000, jpeg_quality);
        vTaskDelay(pdMS_TO_TICKS(500));
        return request->reply_cors(200, "text/plain", "cam started");
      } else {
        return request->reply_cors(200, "text/plain", "cam already started");
      }
    });

    server->on("/frame", HTTP_GET, [cam_manager](PsychicRequest *request){
      if (cam_manager->is_running()) {
        camera_fb_t * fb = NULL;
        esp_err_t res = ESP_OK;
        int64_t fr_start = 0, fr_end = 0;

        if (request->hasParam("drop_frames")) {
          String value = request->getParam("drop_frames")->value();
          for (int ii = 0; ii < value.toInt(); ii++) {
            fr_start = esp_timer_get_time();

            fb = esp_camera_fb_get();
            esp_camera_fb_return(fb);
            fb = NULL; 

            fr_end = esp_timer_get_time();
            Serial.printf("%dnd %llu us\n", ii, fr_end - fr_start);
          }
        }
        
        fr_start = esp_timer_get_time();
        fb = esp_camera_fb_get(); // get fresh image   
        if (!fb) {
            Serial.println("Camera capture failed");
            return request->reply_cors(500, "text/plain", "camera capture failed");
        }
        Serial.printf("JPG: capture %ld us\n", (long)(esp_timer_get_time() - fr_start));
        fr_start = esp_timer_get_time();
        
        if(fb->format == PIXFORMAT_JPEG){
          PsychicResponse response(request);

          response.setCode(200);
          response.setContentType( "image/jpeg");

          // response.setContent((const char *)buf);
          
          response.setContent(fb->buf, fb->len);
          response.addHeader("Content-Disposition", "inline; filename=capture.jpg");
          response.addHeader("Access-Control-Allow-Origin", request->header("Origin").c_str());

          res = response.send();
          int64_t fr_end = esp_timer_get_time();
          Serial.printf("JPG: %u bytes transfer %lums\n", fb->len, (uint32_t)((fr_end - fr_start)/1000));
          esp_camera_fb_return(fb);

        } else {
          res = request->reply_cors(500, "text/plain", "camera format settings issue (PIXFORMAT)");
        }

        return res;
      } else {
        return request->reply_cors(500, "text/plain", "camera has not been started yet");
      }
    });




    server->on("/camera_burst_trigger", HTTP_GET, [cam_manager](PsychicRequest *request){
      if (cam_manager->is_running()) {
        if (request->hasParam("count")) {
          int count = request->getParam("count")->value().toInt();
          if (count <= USER_FRAMEBUFFER_COUNT) {
            if (xSemaphoreTake(cam_manager->s_framebuffer_locked, pdMS_TO_TICKS(100)) == pdTRUE) {
              xTaskNotifyIndexed(cam_manager->cam_burst_task_handle, 0, count, eSetValueWithOverwrite );
              return request->reply_cors(200, "text/plain", "cameraburst started");
            } else {
              return request->reply_cors(500, "text/plain", "frame buffer busy");
            }
          } else {
            return request->reply_cors(500, "text/plain", "too many frames requested");
          }
        }
        return request->reply_cors(500, "text/plain", "missing count parameter");
      }
      return request->reply_cors(500, "text/plain", "camera has not been started yet");
    });


    server->on("/camera_burst_retrieve", HTTP_GET, [cam_manager](PsychicRequest *request) {
      esp_err_t res = ESP_OK;
      if (request->hasParam("frame_index")) {
        int frame_index = request->getParam("frame_index")->value().toInt();
        if (frame_index <= USER_FRAMEBUFFER_COUNT) {
          if (xSemaphoreTake(cam_manager->s_framebuffer_locked, ( TickType_t ) 0 ) == pdTRUE) {
            PsychicResponse response(request);

            auto const & frame_record = cam_manager->user_framebuffer[frame_index];

            response.setCode(200);
            response.setContentType("image/jpeg");
            response.addHeader("Content-Disposition", "inline; filename=capture.jpg");
            response.addHeader("X-frame-time", String(frame_record.time).c_str());
            response.addHeader("Access-Control-Allow-Origin", request->header("Origin").c_str());

            /* Retrieve the pointer to scratch buffer for temporary storage */
            char *chunk = (char *)malloc(BURST_CHUNK_SIZE);
            if (chunk == NULL) {
              return request->reply_cors(500, "text/plain", "Unable to allocate memory");
            }
            response.sendHeaders();
            size_t buffer_index = 0;
            size_t bytes_remaining = frame_record.len;
            size_t chunksize = 0;
            do {
                if (bytes_remaining >= BURST_CHUNK_SIZE) {
                  chunksize = BURST_CHUNK_SIZE;
                } else {
                  chunksize = bytes_remaining;
                }
                memcpy(chunk, frame_record.buf + buffer_index, chunksize);
                bytes_remaining -= chunksize;
                buffer_index += chunksize;
                if (chunksize > 0)
                {
                  res = response.sendChunk((uint8_t *)chunk, chunksize);
                  if (res != ESP_OK)
                    break;
                }

                /* Keep looping till the whole file is sent */
            } while (chunksize != 0);
            //keep track of our memory
            free(chunk);
            if (res == ESP_OK) {
              response.finishChunking();
            }
          
          } else {
            res = request->reply_cors(500, "text/plain", "cam burst not ready");
          }
        } else {
          res = request->reply_cors(500, "text/plain", "invalid frame index");
        }
      } else {
        res = request->reply_cors(500, "text/plain", "frame index not provided");
      }
      xSemaphoreGive(cam_manager->s_framebuffer_locked);
      return res;
    });


    server->on("/frame_bmp", HTTP_GET, [](PsychicRequest *request){
      camera_fb_t * fb = NULL;
      esp_err_t res = ESP_OK;
      int64_t fr_start = esp_timer_get_time();

      fb = esp_camera_fb_get();
      if (!fb) {
          Serial.println("Camera capture failed");
          return request->reply_cors(500, "text/plain", "camera capture failed");
      }
      Serial.printf("BMP: capture %ld us\n", (long)(esp_timer_get_time() - fr_start));
      fr_start = esp_timer_get_time();
      uint8_t * buf = NULL;
      size_t buf_len = 0;
      bool converted = frame2bmp(fb, &buf, &buf_len);
      esp_camera_fb_return(fb);
      if(!converted){
          Serial.println("BMP conversion failed");
          return request->reply_cors(500, "text/plain", "BMP conversion failed");
      }
      Serial.printf("BMP: convert %lums\n", (uint32_t)((esp_timer_get_time() - fr_start)/1000));
      fr_start = esp_timer_get_time();
      PsychicResponse response(request);

      response.setCode(200);
      response.setContentType("image/x-windows-bmp");

      // response.setContent((const char *)buf);
      response.setContent((const uint8_t *)buf, buf_len);
      response.addHeader("Content-Disposition", "inline; filename=capture.bmp");
      response.addHeader("Access-Control-Allow-Origin", request->header("Origin").c_str());

      res = response.send();

      free(buf);
      int64_t fr_end = esp_timer_get_time();
      Serial.printf("BMP: %luKB transfer %lums\n", (uint32_t)(buf_len/1024), (uint32_t)((fr_end - fr_start)/1000));

      return res;
    });
}
void CameraManager::configure_pins() {
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    // GPIO pin assignments
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    // Special pin configurations for ESP32-S3 Eye
    #if defined(CAMERA_MODEL_ESP32S3_EYE)
        pinMode(13, INPUT_PULLUP);
        pinMode(14, INPUT_PULLUP);
    #endif
}

void CameraManager::configure_camera(int xclk_freq_hz, int jpeg_quality) {
    config.xclk_freq_hz = xclk_freq_hz;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_SXGA;//FRAMESIZE_UXGA;
    config.jpeg_quality = jpeg_quality;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
}

esp_err_t CameraManager::initialize_camera() {
    esp_err_t err = esp_camera_init(&config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Camera initialization succeeded.\n");
    }
    return err;
}

void CameraManager::handle_camera_init_error() {
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "\n\nCRITICAL FAILURE: Camera sensor failed to initialize.\n");
    ESP_LOGI(TAG, "Reboot the device to recover. This unit will reboot in 1 minute.\n");

    // Optional: Reset I2C bus (uncomment if necessary)
    // periph_module_disable(PERIPH_I2C0_MODULE);
    // periph_module_disable(PERIPH_I2C1_MODULE);
    // periph_module_reset(PERIPH_I2C0_MODULE);
    // periph_module_reset(PERIPH_I2C1_MODULE);

    // Optionally: Implement a watchdog timer for periodic reboot
    // esp_task_wdt_init(60, true);
    // esp_task_wdt_add(NULL);
}

void CameraManager::configure_sensor_settings() {
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor) {
        ESP_LOGI(TAG, "Failed to retrieve sensor settings.\n");
        return;
    }

    // Detect camera module and apply specific settings
    sensorPID = sensor->id.PID;
    switch (sensorPID) {
        case OV9650_PID:
            ESP_LOGI(TAG, "WARNING: OV9650 module is unsupported. Falling back to OV2640 settings.\n");
            break;
        case OV7725_PID:
            ESP_LOGI(TAG, "WARNING: OV7725 module is unsupported. Falling back to OV2640 settings.\n");
            break;
        case OV2640_PID:
            ESP_LOGI(TAG, "OV2640 camera module detected.\n");
            break;
        case OV3660_PID:
            ESP_LOGI(TAG, "OV3660 camera module detected.\n");
            sensor->set_vflip(sensor, 1);
            sensor->set_brightness(sensor, 1);
            sensor->set_saturation(sensor, -2);
            break;
        default:
            ESP_LOGI(TAG, "WARNING: Unknown camera module. Falling back to OV2640 settings.\n");
    }

    // Additional configurations for specific camera models
    #if defined(CAMERA_MODEL_M5STACK_WIDE)
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 1);
    #endif

    // Apply user-defined flip and mirror settings
    #if defined(H_MIRROR)
    sensor->set_hmirror(sensor, H_MIRROR);
    #endif
    #if defined(V_FLIP)
    sensor->set_vflip(sensor, V_FLIP);
    #endif

    // Set default resolution
    #if defined(DEFAULT_RESOLUTION)
    sensor->set_framesize(sensor, DEFAULT_RESOLUTION);
    #endif
}