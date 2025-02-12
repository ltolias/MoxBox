
#ifndef STRUCTS_H
#define STRUCTS_H

#include <cstdint>   // For C++
#include <stddef.h>

typedef struct timings_s {
    int ms_per_row;
    int mins_per_file;
    int discard_after_hrs;
    bool batch_writes;
} timings_t;

typedef struct frame_record_s {
    uint8_t * buf;
    size_t len; 
    int64_t time;
} frame_record_t;


typedef struct ipc_event_s{
    char *name;
    int64_t time_us;
    double value;
    int data_ipc_interval_ms;
} ipc_event_t;



typedef enum ipc_control_type_e {UPDATE_SETTINGS, TIME_SYNC} ipc_control_type;
typedef struct ipc_control_event_s {
    int outlier_setting;
    int64_t time_sync_us;
    ipc_control_type control_type;
} ipc_control_event_t;


typedef struct timer_event_s{
    uint64_t collected_data;
    uint16_t data_index;
    int64_t time_us;
    uint64_t num_accumulated;
} timer_event_t;



#endif