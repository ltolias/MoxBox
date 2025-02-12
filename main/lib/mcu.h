
#include "hal/gpio_ll.h"
#include "Arduino.h"

#include "structs.h"
#include "defines.h"
#include "esp_log.h"


static inline __attribute__((always_inline))
void directWriteLow(int pin)
{
    if ( pin < 32 )
        GPIO.out_w1tc = ((uint32_t)1 << pin);
    else if ( pin < 34 )
        GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32));
}

static inline __attribute__((always_inline))
void directWriteHigh(int pin)
{
    if ( pin < 32 )
        GPIO.out_w1ts = ((uint32_t)1 << pin);
    else if ( pin < 34 )
        GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32));
}


void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps, const char *function_name)
{
  printf("%s was called but failed to allocate %d bytes with %lu capabilities. \n",function_name, requested_size, caps);
  //CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS true to auto restart
}


// Serial input (debugging controls)
void handleSerial() {
    if (Serial.available()) {
        char cmd = Serial.read();
    }
    while (Serial.available()) Serial.read();  // chomp the buffer
}

// Notification LED
void flashLED(int flashtime_ms) {
#if defined(LED_PIN)  
    directWriteHigh(LED_PIN);
    vTaskDelay(pdMS_TO_TICKS(flashtime_ms));
    directWriteLow(LED_PIN);
#else
    return;                         // No notifcation LED, do nothing, no delay
#endif
}

void initialize_io() {
    #if defined(LED_PIN)  // If we have a notification LED, set it to output
        pinMode(LED_PIN, OUTPUT);
    #endif
    #if defined(USER_RELAY_PIN)  // If we have a notification LED, set it to output
        directWriteLow(USER_RELAY_PIN);
        pinMode(USER_RELAY_PIN, OUTPUT);
    #endif
    #if defined(RELAY_PIN)  // If we have a notification LED, set it to output
        directWriteLow(RELAY_PIN);
        pinMode(RELAY_PIN, OUTPUT);
    #endif
    #if defined(LAMP_PIN)
        directWriteLow(LAMP_PIN);
        pinMode(LAMP_PIN, OUTPUT);
    #endif

}


void set_lamp(int newVal) {
    #if defined(LAMP_PIN)
        if (newVal == 1) {
            directWriteHigh(LAMP_PIN);
        } else {
          directWriteLow(LAMP_PIN);
        }
    #endif
}

void set_relay(int newVal) {
    #if defined(USER_RELAY_PIN)
        if (newVal == 1) {
            directWriteHigh(USER_RELAY_PIN);
        } else {
          directWriteLow(USER_RELAY_PIN);
        }
    #endif
}

