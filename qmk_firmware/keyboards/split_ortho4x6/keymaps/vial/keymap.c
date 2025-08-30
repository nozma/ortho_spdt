#include QMK_KEYBOARD_H
#include "quantum.h"
#include "pointing_device.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // abs

// 既存のレイアウト配列（環境依存のため空のまま）
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {};

// ==== 可変パラメータ（必要に応じて調整） =========================
static const uint16_t kCpiList[]   = { 200, 400, 800, 1600, 3200 };
static const int8_t   kAngleList[] = { -90,-60,-45,-30,-15,0,15,30,45,60,90 };
static const uint8_t  kScrDivList[]= { 0,1,2,3,4,5 }; // 2^div のシフト量

// 「CPI」はここでは移動量のソフト倍率で再現（ハードCPIは後述）
#define CPI_BASE 800  // 800cpi を基準1.0として扱う

// ==== EEPROM保存（左右まとめて 32bit にパック） ==================
typedef struct {
    uint8_t cpi_idx;     // 0..15
    uint8_t rot_idx;     // 0..15
    uint8_t scr_div_idx; // 0..7
    bool    scr_invert;  // 0/1
    bool    scroll_mode; // 0:cursor 1:scroll
} side_cfg_t;

static side_cfg_t gL, gR;

static inline uint32_t pack_cfg(void) {
    uint32_t v = 0;
    v |= (uint32_t)(gL.cpi_idx     & 0xF)      << 0;
    v |= (uint32_t)(gL.rot_idx     & 0xF)      << 4;
    v |= (uint32_t)(gL.scr_div_idx & 0x7)      << 8;
    v |= (uint32_t)(gL.scr_invert  ? 1 : 0)    << 11;
    v |= (uint32_t)(gL.scroll_mode ? 1 : 0)    << 12;

    v |= (uint32_t)(gR.cpi_idx     & 0xF)      << 13;
    v |= (uint32_t)(gR.rot_idx     & 0xF)      << 17;
    v |= (uint32_t)(gR.scr_div_idx & 0x7)      << 21;
    v |= (uint32_t)(gR.scr_invert  ? 1 : 0)    << 24;
    v |= (uint32_t)(gR.scroll_mode ? 1 : 0)    << 25;
    return v;
}
static inline void unpack_cfg(uint32_t v) {
    gL.cpi_idx     = (v >> 0)  & 0xF;
    gL.rot_idx     = (v >> 4)  & 0xF;
    gL.scr_div_idx = (v >> 8)  & 0x7;
    gL.scr_invert  = ((v >> 11) & 1) != 0;
    gL.scroll_mode = ((v >> 12) & 1) != 0;

    gR.cpi_idx     = (v >> 13) & 0xF;
    gR.rot_idx     = (v >> 17) & 0xF;
    gR.scr_div_idx = (v >> 21) & 0x7;
    gR.scr_invert  = ((v >> 24) & 1) != 0;
    gR.scroll_mode = ((v >> 25) & 1) != 0;
}
static void tb_eeprom_defaults(void) {
    gL.cpi_idx     = (ARRAY_SIZE(kCpiList)   > 2) ? 2 : 0; // 800
    gL.rot_idx     = (ARRAY_SIZE(kAngleList) > 5) ? 5 : 0; // 0°
    gL.scr_div_idx = (ARRAY_SIZE(kScrDivList)> 2) ? 2 : 0;
    gL.scr_invert  = false;
    gL.scroll_mode = false;

    gR = gL;
}
static void tb_load_eeprom(void) {
    uint32_t raw = eeconfig_read_kb();
    unpack_cfg(raw);

    bool bad =
        gL.cpi_idx     >= ARRAY_SIZE(kCpiList)   ||
        gL.rot_idx     >= ARRAY_SIZE(kAngleList) ||
        gL.scr_div_idx >= ARRAY_SIZE(kScrDivList)||
        gR.cpi_idx     >= ARRAY_SIZE(kCpiList)   ||
        gR.rot_idx     >= ARRAY_SIZE(kAngleList) ||
        gR.scr_div_idx >= ARRAY_SIZE(kScrDivList);

    if (bad) {
        tb_eeprom_defaults();
        eeconfig_update_kb(pack_cfg());
    }
}
static inline void tb_save(void) { eeconfig_update_kb(pack_cfg()); }

// レガシー互換の薄いラッパ（以前の関数名を残したい場合に使用）
static inline void tb_load_eeprom_side(bool _is_left){ (void)_is_left; tb_load_eeprom(); }
static inline void tb_save_eeprom_side(bool _is_left){ (void)_is_left; tb_save(); }

// ==== 2D回転（整数近似） ==========================================
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

// ==== スクロール変換（左右別の蓄積） ==============================
static int v_acc_l = 0, h_acc_l = 0;
static int v_acc_r = 0, h_acc_r = 0;

static void apply_scroll(report_mouse_t* r, bool invert, uint8_t scr_div_idx, bool is_left) {
    int *v_acc = is_left ? &v_acc_l : &v_acc_r;
    int *h_acc = is_left ? &h_acc_l : &h_acc_r;

    int8_t x = r->x, y = r->y;
    // どちらかを0にして1D寄せ
    if (abs((int)x) > abs((int)y)) y = 0; else x = 0;

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

// ==== 移動量のソフト倍率（左右別） ================================
static int x_acc_l = 0, y_acc_l = 0;
static int x_acc_r = 0, y_acc_r = 0;

static void apply_move_scale(report_mouse_t* r, uint16_t cpi, bool is_left) {
    int *xa = is_left ? &x_acc_l : &x_acc_r;
    int *ya = is_left ? &y_acc_l : &y_acc_r;

    *xa += (int)r->x * (int)cpi;
    *ya += (int)r->y * (int)cpi;

    int8_t ox = (int8_t)(*xa / CPI_BASE);
    int8_t oy = (int8_t)(*ya / CPI_BASE);

    r->x = ox;
    r->y = oy;

    *xa -= ox * CPI_BASE;
    *ya -= oy * CPI_BASE;
}

// ==== カスタムキーコード ==========================================
enum custom_keycodes {
    TB_L_CPI_NEXT = QK_KB_0, TB_L_CPI_PREV,
    TB_L_ROT_NEXT, TB_L_ROT_PREV,
    TB_L_SCR_TOG,  TB_L_SCR_DIV,
    TB_L_SCR_INV,

    TB_R_CPI_NEXT, TB_R_CPI_PREV,
    TB_R_ROT_NEXT, TB_R_ROT_PREV,
    TB_R_SCR_TOG,  TB_R_SCR_DIV,
    TB_R_SCR_INV,
};

// ==== 初期化フック（_user を使用） ================================
void keyboard_post_init_user(void) {
    tb_load_eeprom();
}

// ==== キー処理（押下時）（_user を使用） ==========================
bool process_record_user(uint16_t keycode, keyrecord_t* record) {
    if (!record->event.pressed) return true;

    switch (keycode) {
        // 左
        case TB_L_CPI_NEXT:
            gL.cpi_idx = (gL.cpi_idx + 1) % ARRAY_SIZE(kCpiList); tb_save(); return false;
        case TB_L_CPI_PREV:
            gL.cpi_idx = (gL.cpi_idx + ARRAY_SIZE(kCpiList) - 1) % ARRAY_SIZE(kCpiList); tb_save(); return false;
        case TB_L_ROT_NEXT:
            gL.rot_idx = (gL.rot_idx + 1) % ARRAY_SIZE(kAngleList); tb_save(); return false;
        case TB_L_ROT_PREV:
            gL.rot_idx = (gL.rot_idx + ARRAY_SIZE(kAngleList) - 1) % ARRAY_SIZE(kAngleList); tb_save(); return false;
        case TB_L_SCR_TOG:
            gL.scroll_mode ^= 1; tb_save(); return false;
        case TB_L_SCR_DIV:
            gL.scr_div_idx = (gL.scr_div_idx + 1) % ARRAY_SIZE(kScrDivList); tb_save(); return false;
        case TB_L_SCR_INV:
            gL.scr_invert ^= 1; tb_save(); return false;

        // 右
        case TB_R_CPI_NEXT:
            gR.cpi_idx = (gR.cpi_idx + 1) % ARRAY_SIZE(kCpiList); tb_save(); return false;
        case TB_R_CPI_PREV:
            gR.cpi_idx = (gR.cpi_idx + ARRAY_SIZE(kCpiList) - 1) % ARRAY_SIZE(kCpiList); tb_save(); return false;
        case TB_R_ROT_NEXT:
            gR.rot_idx = (gR.rot_idx + 1) % ARRAY_SIZE(kAngleList); tb_save(); return false;
        case TB_R_ROT_PREV:
            gR.rot_idx = (gR.rot_idx + ARRAY_SIZE(kAngleList) - 1) % ARRAY_SIZE(kAngleList); tb_save(); return false;
        case TB_R_SCR_TOG:
            gR.scroll_mode ^= 1; tb_save(); return false;
        case TB_R_SCR_DIV:
            gR.scr_div_idx = (gR.scr_div_idx + 1) % ARRAY_SIZE(kScrDivList); tb_save(); return false;
        case TB_R_SCR_INV:
            gR.scr_invert ^= 1; tb_save(); return false;
    }
    return true;
}

// ==== COMBINED モードのポインティング処理（_user を使用） =========
report_mouse_t pointing_device_task_combined_user(report_mouse_t left, report_mouse_t right) {
    // 左
    {
        int8_t x = left.x, y = left.y;
        int8_t deg = kAngleList[gL.rot_idx];
        rotate_xy(&x, &y, deg);
        left.x = x; left.y = y;

        if (gL.scroll_mode) {
            apply_scroll(&left, gL.scr_invert, gL.scr_div_idx, true);
        } else {
            apply_move_scale(&left, kCpiList[gL.cpi_idx], true);
        }
    }

    // 右
    {
        int8_t x = right.x, y = right.y;
        int8_t deg = kAngleList[gR.rot_idx];
        rotate_xy(&x, &y, deg);
        right.x = x; right.y = y;

        if (gR.scroll_mode) {
            apply_scroll(&right, gR.scr_invert, gR.scr_div_idx, false);
        } else {
            apply_move_scale(&right, kCpiList[gR.cpi_idx], false);
        }
    }

    return pointing_device_combine_reports(left, right);
}
