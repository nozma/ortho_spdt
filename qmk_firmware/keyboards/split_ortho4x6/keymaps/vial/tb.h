// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "quantum.h"
#include "pointing_device.h"

// keymap.c から呼び出される公開 API
void tb_init(void);
bool tb_process_record(uint16_t keycode, keyrecord_t* record);
report_mouse_t tb_task_combined(report_mouse_t left, report_mouse_t right);

// USB Raw HID: docs/trackball_settings.md の32バイトプロトコル。
#define TB_SETTINGS_COMMAND 0x40
#define TB_SETTINGS_VERSION 1
#define TB_SETTINGS_GET 1
#define TB_SETTINGS_SET 2

enum tb_settings_status {
    TB_SETTINGS_OK,
    TB_SETTINGS_BAD_VERSION,
    TB_SETTINGS_INVALID_CONFIG,
    TB_SETTINGS_LOCKED,
    TB_SETTINGS_BAD_OPERATION,
    TB_SETTINGS_BAD_PACKET,
};

// カスタムキーコード（Vial の QK_KB_0 連番に整列）
// 左右独立の制御（CPI/回転）
enum tb_keycodes {
    TB_KB_BASE = QK_KB_0,
    TB_L_CPI_NEXT = TB_KB_BASE,
    TB_L_CPI_PREV,
    TB_L_ROT_R15,
    TB_L_ROT_L15,
    TB_R_CPI_NEXT,
    TB_R_CPI_PREV,
    TB_R_ROT_R15,
    TB_R_ROT_L15,
    TB_SCR_TOG,   // 左のみスクロール/カーソル切替
    TB_SCR_DIV,   // スクロール速度（分割）を変更（グローバル）
    TB_SCR_AX_TOG,// スクロール方向: 縦横限定/斜め許可
    // スクロールカーブ調整（グローバル）
    TB_SC_GAIN_UP,
    TB_SC_GAIN_DN,
    TB_SC_GAMMA_UP,
    TB_SC_GAMMA_DN,
    TB_SC_RESET,
    TB_CUR_ACCEL_TOG,
};
