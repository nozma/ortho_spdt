#include QMK_KEYBOARD_H
#include "quantum.h"
#include "pointing_device.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // abs

// 既存のレイアウト配列は環境依存のため、空配列のまま残します
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {};

// ------------------------------------------------------------
// 可変パラメータ
// ------------------------------------------------------------
static const uint16_t kCpiList[]   = { 200, 400, 800, 1600, 3200 };
static const int8_t   kAngleList[] = { -90,-60,-45,-30,-15,0,15,30,45,60,90 };
static const uint8_t  kScrDivList[]= { 0,1,2,3,4,5 }; // 2^div のシフト量

#define ARRAY_SIZE(a) ((uint8_t)(sizeof(a)/sizeof((a)[0])))

// ------------------------------------------------------------
// 左右の設定を1つの32bitに圧縮してEEPROM保存
//  (13bit×2=26bit + 予約6bit = 32bit)
// ------------------------------------------------------------
typedef union {
    uint32_t raw;
    struct {
        // 左
        uint32_t l_cpi_idx     : 4; // 0..15
        uint32_t l_rot_idx     : 4; // 0..15
        uint32_t l_scr_div_idx : 3; // 0..7
        uint32_t l_scr_invert  : 1; // bool
        uint32_t l_scroll_mode : 1; // bool
        // 右
        uint32_t r_cpi_idx     : 4;
        uint32_t r_rot_idx     : 4;
        uint32_t r_scr_div_idx : 3;
        uint32_t r_scr_invert  : 1;
        uint32_t r_scroll_mode : 1;
        // 予約
        uint32_t _reserved     : 6;
    };
} tb_cfg_t;

static tb_cfg_t g_cfg;

// 破損時の初期化
static void tb_eeprom_init(void) {
    g_cfg.raw = 0;
    // 既定値: CPI=800(index 2), 角度=0°(index 5), スクロール分割=2 (中くらい)
    g_cfg.l_cpi_idx     = (ARRAY_SIZE(kCpiList)   > 2) ? 2 : 0;
    g_cfg.l_rot_idx     = (ARRAY_SIZE(kAngleList) > 5) ? 5 : 0;
    g_cfg.l_scr_div_idx = (ARRAY_SIZE(kScrDivList)> 2) ? 2 : 0;
    g_cfg.l_scr_invert  = 0;
    g_cfg.l_scroll_mode = 0;

    g_cfg.r_cpi_idx     = g_cfg.l_cpi_idx;
    g_cfg.r_rot_idx     = g_cfg.l_rot_idx;
    g_cfg.r_scr_div_idx = g_cfg.l_scr_div_idx;
    g_cfg.r_scr_invert  = 0;
    g_cfg.r_scroll_mode = 0;

    eeconfig_update_kb(g_cfg.raw);
}

static void tb_load_eeprom(void) {
    g_cfg.raw = eeconfig_read_kb();
    bool bad =
        (g_cfg.l_cpi_idx     >= ARRAY_SIZE(kCpiList))    ||
        (g_cfg.l_rot_idx     >= ARRAY_SIZE(kAngleList))  ||
        (g_cfg.l_scr_div_idx >= ARRAY_SIZE(kScrDivList)) ||
        (g_cfg.r_cpi_idx     >= ARRAY_SIZE(kCpiList))    ||
        (g_cfg.r_rot_idx     >= ARRAY_SIZE(kAngleList))  ||
        (g_cfg.r_scr_div_idx >= ARRAY_SIZE(kScrDivList));

    if (bad) tb_eeprom_init();
}

static inline void tb_save(void) { eeconfig_update_kb(g_cfg.raw); }

// 互換のためのラッパ（呼び出し側のシグネチャを維持）
static void tb_load_eeprom_side(bool _is_left) { (void)_is_left; tb_load_eeprom(); }
static void tb_save_eeprom_side(bool _is_left) { (void)_is_left; tb_save(); }

// ------------------------------------------------------------
// 2D回転（整数近似）
// ------------------------------------------------------------
static inline void rotate_xy(int8_t* x, int8_t* y, int8_t deg) {
    int8_t ox = *x, oy = *y;
    switch (deg) {
        case 90:   *x = -oy; *y =  ox; break;
        case -90:  *x =  oy; *y = -ox; break;
        case 45:   *x = (ox - oy); *y = (ox + oy); break;
        case -45:  *x = (ox + oy); *y = (oy - ox); break;
        case 60:   *x = ( -oy*866 + ox*500 )/1000, *y = ( ox*866 + oy*500 )/1000; break;
        case -60:  *x = (  oy*866 + ox*500 )/1000, *y = ( -ox*866 + oy*500 )/1000; break;
        case 30:   *x = ( ox*866 - oy*500 )/1000, *y = ( ox*500 + oy*866 )/1000; break;
        case -30:  *x = ( ox*866 + oy*500 )/1000, *y = ( oy*866 - ox*500 )/1000; break;
        case 15:   *x = ( ox*966 - oy*259 )/1000, *y = ( ox*259 + oy*966 )/1000; break;
        case -15:  *x = ( ox*966 + oy*259 )/1000, *y = ( oy*966 - ox*259 )/1000; break;
        default:   break; // 0°
    }
}

// ------------------------------------------------------------
// スクロール変換（左右別の蓄積）
//  ※ config.h で WHEEL_EXTENDED_REPORT / MOUSE_EXTENDED_REPORT を有効推奨
// ------------------------------------------------------------
static int v_acc_l = 0, h_acc_l = 0;
static int v_acc_r = 0, h_acc_r = 0;

static void apply_scroll(report_mouse_t* r, bool invert, uint8_t scr_div_idx, bool is_left) {
    int *v_acc = is_left ? &v_acc_l : &v_acc_r;
    int *h_acc = is_left ? &h_acc_l : &h_acc_r;

    int8_t x = r->x, y = r->y;

    // どちらかを0にして1D寄せ（好みに応じて調整可）
    if (abs(x) > abs(y)) y = 0; else x = 0;

    if (invert) { x = -x; y = -y; }

    uint8_t div = kScrDivList[scr_div_idx];
    *h_acc += x; *v_acc += y;

    int8_t hs = (int8_t)(*h_acc >> div);
    int8_t vs = (int8_t)(*v_acc >> div);

    if (hs) { r->h += hs; *h_acc -= (hs << div); }
    if (vs) { r->v += vs; *v_acc -= (vs << div); }

    // スクロール時はXYを出さない
    r->x = 0;
    r->y = 0;
}

// ------------------------------------------------------------
// ハードウェアCPI（左右独立）
// ※ 環境のQMKに point ing_device_set_cpi_on_side がある前提
// ------------------------------------------------------------
static void apply_cpi(bool is_left) {
    uint16_t cpi = is_left ? kCpiList[g_cfg.l_cpi_idx] : kCpiList[g_cfg.r_cpi_idx];
    pointing_device_set_cpi_on_side(is_left, cpi);
}

// ------------------------------------------------------------
// カスタムキーコード
// ------------------------------------------------------------
enum custom_keycodes {
    TB_L_CPI_NEXT = SAFE_RANGE, TB_L_CPI_PREV,
    TB_L_ROT_NEXT, TB_L_ROT_PREV,
    TB_L_SCR_TOG,  TB_L_SCR_DIV,
    TB_L_SCR_INV,

    TB_R_CPI_NEXT, TB_R_CPI_PREV,
    TB_R_ROT_NEXT, TB_R_ROT_PREV,
    TB_R_SCR_TOG,  TB_R_SCR_DIV,
    TB_R_SCR_INV,
};

// ------------------------------------------------------------
// 初期化フック
// ------------------------------------------------------------
void keyboard_post_init_kb(void) {
    tb_load_eeprom_side(true);
    // 左右分は1つの構造体に格納しているため、両側読み込みは不要だが互換のため残す
    tb_load_eeprom_side(false);
    apply_cpi(true);
    apply_cpi(false);
}

// ------------------------------------------------------------
// キー処理（押下時）
// ------------------------------------------------------------
bool process_record_kb(uint16_t keycode, keyrecord_t* record) {
    if (!record->event.pressed) return true;

    switch (keycode) {
        // 左
        case TB_L_CPI_NEXT:
            g_cfg.l_cpi_idx = (g_cfg.l_cpi_idx + 1) % ARRAY_SIZE(kCpiList);
            tb_save_eeprom_side(true);
            apply_cpi(true);
            return false;
        case TB_L_CPI_PREV:
            g_cfg.l_cpi_idx = (g_cfg.l_cpi_idx + ARRAY_SIZE(kCpiList) - 1) % ARRAY_SIZE(kCpiList);
            tb_save_eeprom_side(true);
            apply_cpi(true);
            return false;
        case TB_L_ROT_NEXT:
            g_cfg.l_rot_idx = (g_cfg.l_rot_idx + 1) % ARRAY_SIZE(kAngleList);
            tb_save_eeprom_side(true);
            return false;
        case TB_L_ROT_PREV:
            g_cfg.l_rot_idx = (g_cfg.l_rot_idx + ARRAY_SIZE(kAngleList) - 1) % ARRAY_SIZE(kAngleList);
            tb_save_eeprom_side(true);
            return false;
        case TB_L_SCR_TOG:
            g_cfg.l_scroll_mode ^= 1;
            tb_save_eeprom_side(true);
            return false;
        case TB_L_SCR_DIV:
            g_cfg.l_scr_div_idx = (g_cfg.l_scr_div_idx + 1) % ARRAY_SIZE(kScrDivList);
            tb_save_eeprom_side(true);
            return false;
        case TB_L_SCR_INV:
            g_cfg.l_scr_invert ^= 1;
            tb_save_eeprom_side(true);
            return false;

        // 右
        case TB_R_CPI_NEXT:
            g_cfg.r_cpi_idx = (g_cfg.r_cpi_idx + 1) % ARRAY_SIZE(kCpiList);
            tb_save_eeprom_side(false);
            apply_cpi(false);
            return false;
        case TB_R_CPI_PREV:
            g_cfg.r_cpi_idx = (g_cfg.r_cpi_idx + ARRAY_SIZE(kCpiList) - 1) % ARRAY_SIZE(kCpiList);
            tb_save_eeprom_side(false);
            apply_cpi(false);
            return false;
        case TB_R_ROT_NEXT:
            g_cfg.r_rot_idx = (g_cfg.r_rot_idx + 1) % ARRAY_SIZE(kAngleList);
            tb_save_eeprom_side(false);
            return false;
        case TB_R_ROT_PREV:
            g_cfg.r_rot_idx = (g_cfg.r_rot_idx + ARRAY_SIZE(kAngleList) - 1) % ARRAY_SIZE(kAngleList);
            tb_save_eeprom_side(false);
            return false;
        case TB_R_SCR_TOG:
            g_cfg.r_scroll_mode ^= 1;
            tb_save_eeprom_side(false);
            return false;
        case TB_R_SCR_DIV:
            g_cfg.r_scr_div_idx = (g_cfg.r_scr_div_idx + 1) % ARRAY_SIZE(kScrDivList);
            tb_save_eeprom_side(false);
            return false;
        case TB_R_SCR_INV:
            g_cfg.r_scr_invert ^= 1;
            tb_save_eeprom_side(false);
            return false;
    }
    return true;
}

// ------------------------------------------------------------
// COMBINED モード：左右レポートを個別加工して合成
// ------------------------------------------------------------
report_mouse_t pointing_device_task_combined_kb(report_mouse_t left, report_mouse_t right) {
    // 左
    {
        int8_t x = left.x, y = left.y;
        int8_t deg = kAngleList[g_cfg.l_rot_idx];
        rotate_xy(&x, &y, deg);
        left.x = x; left.y = y;

        if (g_cfg.l_scroll_mode) {
            apply_scroll(&left, g_cfg.l_scr_invert, g_cfg.l_scr_div_idx, true);
        }
        // カーソル移動モード時はXYをそのまま（CPIはハード側で反映）
    }

    // 右
    {
        int8_t x = right.x, y = right.y;
        int8_t deg = kAngleList[g_cfg.r_rot_idx];
        rotate_xy(&x, &y, deg);
        right.x = x; right.y = y;

        if (g_cfg.r_scroll_mode) {
            apply_scroll(&right, g_cfg.r_scr_invert, g_cfg.r_scr_div_idx, false);
        }
        // カーソル移動モード時はXYをそのまま（CPIはハード側で反映）
    }

    // 値渡しでOK（&は不要）
    return pointing_device_combine_reports(left, right);
}
