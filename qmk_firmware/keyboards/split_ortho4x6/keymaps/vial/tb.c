// keyboards/split_ortho4x6/keymaps/vial/tb.c

#include "tb.h"
#include "quantum.h"
#include "pointing_device.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// ====== Config (per-side controls) ===============================
#ifndef COCOT_SCROLL_INV_DEFAULT
#    define COCOT_SCROLL_INV_DEFAULT true
#endif

static const uint16_t k_cpi_opts[] = {200, 400, 800, 1600, 3200};
static const uint8_t  k_scr_divs[] = {1, 2, 3, 4, 5, 6}; // shift amount
// 15度刻みで -180..+180 をサポート
static const int16_t  k_angles[]   = {
    -180, -165, -150, -135, -120, -105, -90, -75, -60, -45, -30, -15,
       0,   15,   30,   45,   60,   75,   90,  105,  120,  135,  150,  165,  180
};

#define CPI_OPTION_SIZE (sizeof(k_cpi_opts) / sizeof(k_cpi_opts[0]))
#define SCRL_DIV_SIZE   (sizeof(k_scr_divs) / sizeof(k_scr_divs[0]))
#define ANGLE_SIZE      (sizeof(k_angles)   / sizeof(k_angles[0]))

typedef struct {
    uint8_t cpi_idx;        // 0..CPI_OPTION_SIZE-1
    uint8_t rot_idx;        // 0..ANGLE_SIZE-1
    bool    scroll_mode;    // per-side scroll mode
} tb_side_t;

static tb_side_t gL, gR;
static bool      g_scrl_inv = COCOT_SCROLL_INV_DEFAULT;
static uint8_t   g_scrl_div = 4; // default divider index

// ====== EEPROM pack/unpack =======================================
static inline uint32_t pack_cfg(void) {
    uint32_t v = 0;
    v |= (uint32_t)(gL.cpi_idx & 0xF)      << 0;  // 4 bits
    v |= (uint32_t)(gL.rot_idx & 0x1F)     << 4;  // 5 bits
    v |= (uint32_t)(gL.scroll_mode ? 1:0)  << 9;  // 1 bit
    v |= (uint32_t)(gR.cpi_idx & 0xF)      << 10; // 4 bits
    v |= (uint32_t)(gR.rot_idx & 0x1F)     << 14; // 5 bits
    v |= (uint32_t)(gR.scroll_mode ? 1:0)  << 19; // 1 bit
    v |= (uint32_t)(g_scrl_inv ? 1:0)      << 20; // 1 bit
    v |= (uint32_t)(g_scrl_div & 0x7)      << 21; // 3 bits
    return v;
}

static inline void unpack_cfg(uint32_t v) {
    gL.cpi_idx     = (v >> 0)  & 0xF;
    gL.rot_idx     = (v >> 4)  & 0x1F;
    gL.scroll_mode = ((v >> 9)  & 1) != 0;
    gR.cpi_idx     = (v >> 10) & 0xF;
    gR.rot_idx     = (v >> 14) & 0x1F;
    gR.scroll_mode = ((v >> 19) & 1) != 0;
    g_scrl_inv     = ((v >> 20) & 1) != 0;
    g_scrl_div     = (v >> 21) & 0x7;
}

static inline void tb_save(void) { eeconfig_update_kb(pack_cfg()); }

static uint8_t rot_index_for_angle(int16_t deg) {
    for (uint8_t i = 0; i < ANGLE_SIZE; ++i) {
        if (k_angles[i] == deg) return i;
    }
    return 0; // fallback
}

static void tb_defaults(void) {
    gL.cpi_idx = 3; // 1600
    gL.rot_idx = rot_index_for_angle(90);
    gL.scroll_mode = true;

    gR.cpi_idx = 3; // 1600
    gR.rot_idx = rot_index_for_angle(-90);
    gR.scroll_mode = false;

    g_scrl_inv = COCOT_SCROLL_INV_DEFAULT;
    g_scrl_div = 4; // >> 5
}

static void tb_load(void) {
    uint32_t raw = eeconfig_read_kb();
    if (raw == 0 || raw == 0xFFFFFFFFu) {
        tb_defaults();
        tb_save();
        return;
    }
    unpack_cfg(raw);
    bool bad = false;
    if (gL.cpi_idx >= CPI_OPTION_SIZE || gR.cpi_idx >= CPI_OPTION_SIZE) bad = true;
    if (gL.rot_idx >= ANGLE_SIZE || gR.rot_idx >= ANGLE_SIZE) bad = true;
    if (g_scrl_div >= SCRL_DIV_SIZE) bad = true;
    if (bad) {
        tb_defaults();
        tb_save();
    }
}

// ====== Public API ===============================================
void tb_init(void) { tb_load(); }

bool tb_process_record(uint16_t keycode, keyrecord_t* record) {
    // process_record_user() から本関数が呼ばれるため、ここで再帰呼出ししないこと。

    switch (keycode) {
#ifndef MOUSEKEY_ENABLE
        case KC_MS_BTN1 ... KC_MS_BTN8: {
            extern void register_button(bool, enum mouse_buttons);
            register_button(record->event.pressed, MOUSE_BTN_MASK(keycode - KC_MS_BTN1));
            return false;
        }
#endif
        case TB_L_CPI_NEXT:
            if (record->event.pressed) { gL.cpi_idx = (gL.cpi_idx + 1) % CPI_OPTION_SIZE; tb_save(); }
            return false;
        case TB_L_CPI_PREV:
            if (record->event.pressed) { gL.cpi_idx = (gL.cpi_idx + CPI_OPTION_SIZE - 1) % CPI_OPTION_SIZE; tb_save(); }
            return false;
        case TB_L_ROT_R15:
            if (record->event.pressed) { gL.rot_idx = (gL.rot_idx + 1) % ANGLE_SIZE; tb_save(); }
            return false;
        case TB_L_ROT_L15:
            if (record->event.pressed) { gL.rot_idx = (gL.rot_idx + ANGLE_SIZE - 1) % ANGLE_SIZE; tb_save(); }
            return false;
        case TB_R_CPI_NEXT:
            if (record->event.pressed) { gR.cpi_idx = (gR.cpi_idx + 1) % CPI_OPTION_SIZE; tb_save(); }
            return false;
        case TB_R_CPI_PREV:
            if (record->event.pressed) { gR.cpi_idx = (gR.cpi_idx + CPI_OPTION_SIZE - 1) % CPI_OPTION_SIZE; tb_save(); }
            return false;
        case TB_R_ROT_R15:
            if (record->event.pressed) { gR.rot_idx = (gR.rot_idx + 1) % ANGLE_SIZE; tb_save(); }
            return false;
        case TB_R_ROT_L15:
            if (record->event.pressed) { gR.rot_idx = (gR.rot_idx + ANGLE_SIZE - 1) % ANGLE_SIZE; tb_save(); }
            return false;
        case TB_SCR_TOG:
            if (record->event.pressed) {
                gL.scroll_mode = !gL.scroll_mode; // left only
                gR.scroll_mode = false;           // right stays cursor
                tb_save();
            }
            return false;
        case TB_SCR_DIV:
            if (record->event.pressed) {
                g_scrl_div = (g_scrl_div + 1) % SCRL_DIV_SIZE;
                tb_save();
            }
            return false;
        default:
            break;
    }

    return true;
}

// ====== Core transform (ported from picot_o44) ===================
static void tb_apply_transform_side(report_mouse_t* mr, bool is_left) {
    static float prev_x_l = 0.0f, prev_y_l = 0.0f;
    static float prev_x_r = 0.0f, prev_y_r = 0.0f;
    static float x_acc_l = 0.0f, y_acc_l = 0.0f;
    static float x_acc_r = 0.0f, y_acc_r = 0.0f;
    static int   h_acm_l = 0,    v_acm_l = 0;
    static int   h_acm_r = 0,    v_acm_r = 0;

    const float sensitivity = 0.5f;            // base cursor sensitivity
    const float smoothing_factor = 0.7f;       // IIR smoothing
    const float sensitivity_multiplier = 1.5f; // base multiplier

    const tb_side_t* s = is_left ? &gL : &gR;
    double rad = (double)k_angles[s->rot_idx] * (M_PI / 180.0) * -1.0;
    // 回転を適用（Xの余計な反転は行わない）
    float rx = (mr->x * cos(rad) - mr->y * sin(rad));
    float ry = (mr->x * sin(rad) + mr->y * cos(rad));

    float prev_x = is_left ? prev_x_l : prev_x_r;
    float prev_y = is_left ? prev_y_l : prev_y_r;
    float sx = prev_x * smoothing_factor + rx * (1.0f - smoothing_factor);
    float sy = prev_y * smoothing_factor + ry * (1.0f - smoothing_factor);
    if (is_left) { prev_x_l = sx; prev_y_l = sy; } else { prev_x_r = sx; prev_y_r = sy; }

    float mag = sqrtf(sx * sx + sy * sy);
    float dyn = 1.0f + mag / 10.0f;
    if (dyn < 0.5f) dyn = 0.5f; else if (dyn > 3.0f) dyn = 3.0f;

    // Per-side CPI scaling relative to 800 CPI baseline
    float cpi_scale = (float)k_cpi_opts[s->cpi_idx] / 800.0f;
    sx *= sensitivity_multiplier * dyn * cpi_scale;
    sy *= sensitivity_multiplier * dyn * cpi_scale;

    bool scroll = is_left ? s->scroll_mode : false;
    if (scroll) {
        // 1D scroll selection per side
        if (abs((int)sx) > abs((int)sy)) sy = 0; else sx = 0;
        int* ph = is_left ? &h_acm_l : &h_acm_r;
        int* pv = is_left ? &v_acm_l : &v_acm_r;
        if (g_scrl_inv) { *ph += (int)sx; *pv -= (int)sy; }
        else            { *ph -= (int)sx; *pv += (int)sy; }

        int8_t h = (int8_t)(*ph >> k_scr_divs[g_scrl_div]);
        int8_t v = (int8_t)(*pv >> k_scr_divs[g_scrl_div]);
        if (h) { mr->h += h; *ph -= (h << k_scr_divs[g_scrl_div]); }
        if (v) { mr->v += v; *pv -= (v << k_scr_divs[g_scrl_div]); }
        mr->x = 0; mr->y = 0;
    } else {
        float* pax = is_left ? &x_acc_l : &x_acc_r;
        float* pay = is_left ? &y_acc_l : &y_acc_r;
        *pax += sx * sensitivity;
        *pay += sy * sensitivity;
        if (fabsf(*pax) >= 1.0f) { mr->x = (int8_t)(*pax); *pax -= mr->x; } else { mr->x = 0; }
        if (fabsf(*pay) >= 1.0f) { mr->y = (int8_t)(*pay); *pay -= mr->y; } else { mr->y = 0; }
    }
}

report_mouse_t tb_task_combined(report_mouse_t left, report_mouse_t right) {
    tb_apply_transform_side(&left, true);
    tb_apply_transform_side(&right, false);
    return pointing_device_combine_reports(left, right);
}
