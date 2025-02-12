#ifndef CONTROL_QUEUE_H
#define CONTROL_QUEUE_H

#include "freertos/FreeRTOS.h"
#include "structs.h"

class ControlQueue {
public:
    ControlQueue();
    ~ControlQueue();
    QueueHandle_t get_queue();
    void send(int outlier_setting, int64_t time_sync_us, ipc_control_type control_type);     
private:
    QueueHandle_t queue;
};

#endif