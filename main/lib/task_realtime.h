#include "driver/gptimer.h"


#include "defines.h"
#include "lib/structs.h"

#include "realtime_ops.h"


#define ISR_INTERVAL_US 1000 * SAMPLE_INTERVAL_MS / OVERSAMPLE_FACTOR

#define TIME_BASE_MS 1710165000000


volatile int64_t latest_time_offset_us = 0;
volatile int64_t hardware_time_us = 0;

uint32_t collected_vals_isr[COLLECTED_DATA_COUNT];
uint64_t collected_vals_isr_accum[COLLECTED_DATA_COUNT];

uint16_t num_accumulated_isr = 0;
timer_event_t evt_timer_isr;


static bool IRAM_ATTR timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;

    QueueHandle_t timer_queue = (QueueHandle_t)user_data;

    hardware_time_us += ISR_INTERVAL_US;

    read_realtime_inputs(collected_vals_isr, COLLECTED_DATA_COUNT);

    for (int i = 0; i < COLLECTED_DATA_COUNT; i++) {
        collected_vals_isr_accum[i] += collected_vals_isr[i];
    }

    num_accumulated_isr += 1;

    if (num_accumulated_isr >= OVERSAMPLE_FACTOR) {

        for (int i = 0; i < COLLECTED_DATA_COUNT; i++) {
            evt_timer_isr = {
                .collected_data = collected_vals_isr_accum[i],
                .data_index = (uint16_t) i,
                .time_us = hardware_time_us + latest_time_offset_us,
                .num_accumulated = num_accumulated_isr
            };
            xQueueSendFromISR(timer_queue, &evt_timer_isr, &high_task_awoken);

        }
        num_accumulated_isr = 0;
        for (int i = 0; i < COLLECTED_DATA_COUNT; i++) {
            collected_vals_isr_accum[i] = 0;
        }
    }
    
    return high_task_awoken == pdTRUE; // return whether we need to yield at the end of ISR
}

static void timer_setup(uint64_t alarm_interval, QueueHandle_t timer_queue)
{   

    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_isr_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, timer_queue));

    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    gptimer_alarm_config_t alarm_config1 = {
        .alarm_count = alarm_interval,
        .reload_count = 0,
        .flags={.auto_reload_on_alarm = true},
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config1));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
}


void RealTimeTask( void * pvParameters ){

    Serial.printf("RealTimeTask started on core: %d\n", xPortGetCoreID());

    auto const & params = *((std::tuple<QueueHandle_t, ControlQueue *> *) pvParameters);
    QueueHandle_t ipc_data_queue = std::get<0>(params);
    ControlQueue *ipc_control_queue = std::get<1>(params);

    QueueHandle_t timer_queue = xQueueCreate(128, sizeof(timer_event_t));
    
    initialize_realtime_inputs(COLLECTED_DATA_COUNT);
 
    timer_setup(ISR_INTERVAL_US, timer_queue);
    
    char * collected_data_names[] = COLLECTED_DATA_NAMES;

    timer_event_t evt_timer;
    ipc_control_event_t evt_ipc_control;
    
    double collected_data_loop_accum[COLLECTED_DATA_COUNT];
    uint64_t num_accumulated_loop[COLLECTED_DATA_COUNT];

    for (int i = 0; i < COLLECTED_DATA_COUNT; i++) {
        collected_data_loop_accum[i] = 0;
        num_accumulated_loop[i] = 0;
    }

    while (1) {  
        if (xQueueReceive(ipc_control_queue->get_queue(), &evt_ipc_control, 0)) 
        {  
            if (evt_ipc_control.control_type == UPDATE_SETTINGS) {
                //change settings (ex:  evt_ipc_control.outlier_setting )

            } else if (evt_ipc_control.control_type == TIME_SYNC) {
                //sync in time from ntp or file system
                if (evt_ipc_control.time_sync_us > hardware_time_us && evt_ipc_control.time_sync_us > TIME_BASE_MS * 1000) {
                    //only use it if we're being told our existing hardware time is actually in the past
                    latest_time_offset_us = evt_ipc_control.time_sync_us - hardware_time_us;
                } else {
                    latest_time_offset_us = TIME_BASE_MS * 1000 - hardware_time_us;
                }
           }
        }
        if (xQueueReceive(timer_queue, &evt_timer, pdMS_TO_TICKS(100))) 
        {
            /*  
                2 layers of accumulation/averaging, one as an oversample in the ISR, and one here
                if you need a reaction to happen at the interim timescale but to log at a slower 
                timescale
            */    

            double collected_data = double(evt_timer.collected_data) / evt_timer.num_accumulated;
            
            collected_data_loop_accum[evt_timer.data_index] += collected_data;
            num_accumulated_loop[evt_timer.data_index] += 1;


            if (num_accumulated_loop[evt_timer.data_index] >= DATA_IPC_INTERVAL_MS / SAMPLE_INTERVAL_MS) {
                

                ipc_event_t evt_ipc = { 
                    .name = collected_data_names[evt_timer.data_index], 
                    .time_us=evt_timer.time_us, 
                    .value= collected_data_loop_accum[evt_timer.data_index] / num_accumulated_loop[evt_timer.data_index], 
                    .data_ipc_interval_ms=DATA_IPC_INTERVAL_MS
                };
                xQueueSend(ipc_data_queue, &evt_ipc, 0);


                collected_data_loop_accum[evt_timer.data_index] = 0;
                num_accumulated_loop[evt_timer.data_index] = 0;           
            }    
        }
 
    }
}
