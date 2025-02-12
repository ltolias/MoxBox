

#include "esp_private/esp_int_wdt.h"
#include "esp_task_wdt.h"
#include "esp_camera.h"
#include <cmath>
#include "Arduino.h"
#include "freertos/FreeRTOS.h"

#include "defines.h"

#include "camera_pins.h"
#include "structs.h"


void CamBurstTask( void * pvParameters ){
  Serial.printf("CamBurstTask started on core: %d\n", xPortGetCoreID());
  
  auto const & params = *((std::tuple<SemaphoreHandle_t *, frame_record_t *> *) pvParameters);

  auto const & s_framebuffer_locked_task = std::get<0>(params);
  auto const & user_framebuffer_task = std::get<1>(params);
  
  TickType_t xLastWakeTime;
  BaseType_t xWasDelayed;

  const TickType_t xFrequency = pdMS_TO_TICKS(333);

  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  int64_t fr_start = 0, fr_end = 0;
  int discard = 2;

  while (true) {
    int count = ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY );
    if (count > 0) {
      Serial.printf("burst capture started %d frames\n", count);
      xLastWakeTime = xTaskGetTickCount ();
      fr_start = esp_timer_get_time();

      for (int ii = 0; ii < count + discard; ii++) {
        xWasDelayed = xTaskDelayUntil( &xLastWakeTime, xFrequency );
        
        Serial.printf("burst capture %d %lld ms\n", ii, (esp_timer_get_time() - fr_start)/1000);
        if (!xWasDelayed) {
          Serial.println("burst capture not delayed");
        }
        if (ii < count) {
          user_framebuffer_task[ii].time = esp_timer_get_time();
        }
        if (ii < discard) {
          fb = esp_camera_fb_get();
          esp_camera_fb_return(fb);
          fb = NULL; 
        } else {
          fb = esp_camera_fb_get();

          user_framebuffer_task[ii-discard].len = fb->len;
          memcpy(user_framebuffer_task[ii-discard].buf, fb->buf, fb->len);

          esp_camera_fb_return(fb);
          fb = NULL; 
        }
        
      }
      xSemaphoreGive(*s_framebuffer_locked_task);
    }
  }
}
