#pragma once
#include "quantum.h"
#include "pointing_device.h"
void tb_init(void);
bool tb_process_record(uint16_t keycode, keyrecord_t *record);
report_mouse_t tb_task_combined(report_mouse_t left, report_mouse_t right);
