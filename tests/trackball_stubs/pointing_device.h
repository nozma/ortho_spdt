#pragma once
#include <stdint.h>
typedef struct { int16_t x, y, h, v; uint8_t buttons; } report_mouse_t;
report_mouse_t pointing_device_combine_reports(report_mouse_t left, report_mouse_t right);
