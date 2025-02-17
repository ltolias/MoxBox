#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "defines.h"

static TaskHandle_t *task_handles;
static uint32_t * previous_run_times;
static uint32_t previous_total_run_time;
static int i_loop;

static char *task_names[2] = {"IDLE0", "IDLE1"};
static int other_fields_count = 6;

static uint32_t total_run_time;
static uint32_t *total_run_time_p;

//do whatever setup of inputs you need here
inline __attribute__((always_inline))
void initialize_realtime_inputs(int count) {
    previous_total_run_time = 0;
    total_run_time = 0;
    total_run_time_p = &total_run_time;

    task_handles = new TaskHandle_t[count - other_fields_count];
    previous_run_times = new uint32_t[count - other_fields_count];
    for (i_loop = 0; i_loop < count - other_fields_count; i_loop++) {
        task_handles[i_loop] = xTaskGetHandle(task_names[i_loop]);
        previous_run_times[i_loop] = 0;
    }
}

static TaskStatus_t task_details;

static uint32_t run_time_elapsed;
static uint32_t task_time_elapsed;


// the system timer is 32 bit and overflows every ~30s or so
inline __attribute__((always_inline))
uint32_t timer_wrap_helper(uint32_t new_value, uint32_t *old_value) {
    uint32_t return_val;
    if (new_value > *old_value) {
        return_val = new_value - *old_value;
    } else {
        return_val = new_value + (4294967296UL - *old_value);
    }
    *old_value = new_value;
    return return_val;
}

//write things to the data[] here in the order that your data collection labels appear in config.h
inline __attribute__((always_inline))
void read_realtime_inputs(uint32_t data[], int count) {

    // get total runtime since last check
    #ifdef portALT_GET_RUN_TIME_COUNTER_VALUE
        portALT_GET_RUN_TIME_COUNTER_VALUE( *total_run_time_p );
    #else
        total_run_time = portGET_RUN_TIME_COUNTER_VALUE();
    #endif
    run_time_elapsed = timer_wrap_helper(total_run_time, &previous_total_run_time);

    //we're going to use this for a percentage calculation:
    run_time_elapsed /= 100UL;

    for (i_loop = 0; i_loop < count - other_fields_count; i_loop++) {
        vTaskGetInfo( task_handles[i_loop], &task_details, pdFALSE, eRunning ); // dont query high water mark or state, as these are slow

        task_time_elapsed = timer_wrap_helper(task_details.ulRunTimeCounter, &(previous_run_times[i_loop]));

        if (run_time_elapsed != 0) {
            data[i_loop] =  100 - (task_time_elapsed / run_time_elapsed);
        } else {
            data[i_loop] = 0;
        }
    }

    data[2] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024;

    data[3] =  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)/1024;

    data[4] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)/1024;


    data[5] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024;

    data[6] =  heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)/1024;

    data[7] = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)/1024;
    
}
