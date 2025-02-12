#include "control_queue.h"

ControlQueue::ControlQueue() {
    queue = xQueueCreate(32, sizeof(ipc_control_event_t));
}
QueueHandle_t ControlQueue::get_queue() { return queue; };

void ControlQueue::send(int outlier_setting, int64_t time_sync_us, ipc_control_type control_type) {
    ipc_control_event_t ipc_control_event = {
        .outlier_setting = outlier_setting,
        .time_sync_us = time_sync_us,
        .control_type = control_type
    };
    xQueueSend(queue, &ipc_control_event, 0);
};