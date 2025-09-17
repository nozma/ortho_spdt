#pragma once
#include "quantum.h"
#include "pointing_device.h"

// keymap.c から呼び出される公開 API
void tb_init(void);
bool tb_process_record(uint16_t keycode, keyrecord_t* record);
report_mouse_t tb_task_combined(report_mouse_t left, report_mouse_t right);

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
    // スクロールカーブ調整（グローバル）
    TB_SC_GAIN_UP,
    TB_SC_GAIN_DN,
    TB_SC_GAMMA_UP,
    TB_SC_GAMMA_DN,
    TB_SC_RESET,
};
