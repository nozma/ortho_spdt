#pragma once
#include <stdbool.h>
#include <stdint.h>
#define QK_KB_0 0x7e00
#define MOUSEKEY_ENABLE
#define MOUSE_EXTENDED_REPORT
#define WHEEL_EXTENDED_REPORT
typedef struct { struct { bool pressed; } event; } keyrecord_t;
uint32_t eeconfig_read_kb(void);
void eeconfig_update_kb(uint32_t value);
void eeconfig_init_user(void);
