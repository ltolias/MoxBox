#include "driver/touch_sensor.h"
#include "defines.h"


//do whatever setup of inputs you need here
inline __attribute__((always_inline))
void initialize_realtime_inputs() {
   
    touch_pad_init();

    touch_pad_set_meas_time(0x1000, 0xffff);
    touch_pad_set_voltage(TOUCH_HVOLT_2V4, TOUCH_LVOLT_0V8, TOUCH_HVOLT_ATTEN_0V5);

    // touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_0V);
    touch_pad_set_cnt_mode(TOUCH_PAD_SENSE, TOUCH_PAD_SLOPE_7, TOUCH_PAD_TIE_OPT_LOW);
    touch_pad_set_cnt_mode(TOUCH_PAD_CAL, TOUCH_PAD_SLOPE_7, TOUCH_PAD_TIE_OPT_LOW);

    touch_pad_config(TOUCH_PAD_SENSE);
    touch_pad_config(TOUCH_PAD_CAL);
    touch_pad_filter_enable();
    touch_pad_sw_start();
}


//write things to the data[] here in the order that your data collection labels appear in config.h
inline __attribute__((always_inline))
void read_realtime_inputs(uint32_t data[], int count) {
    touch_pad_read_raw_data(TOUCH_PAD_SENSE, &(data[0]));
    touch_pad_read_raw_data(TOUCH_PAD_CAL, &(data[1]));
}