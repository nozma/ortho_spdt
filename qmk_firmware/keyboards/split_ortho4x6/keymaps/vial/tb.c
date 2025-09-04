// keyboards/split_ortho4x6/keymaps/vial/tb.c
#include "tb.h"
#include "quantum.h"
#include "pointing_device.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // abs
#include "timer.h"

// ==== 可変パラメータ（必要に応じて調整） =========================
static const uint16_t kCpiList[]   = { 200, 400, 800, 1600, 3200 };
// 回転角の順序（インデックス）: [-90, -60, -45, -30, -15, 0, 15, 30, 45, 60, 90]
static const int16_t COS_Q10[] = {    0,  512,  724,  887,  990, 1024,  990,  887,  724,  512,    0};
static const int16_t SIN_Q10[] = {-1024, -887, -724, -512, -259,    0,  259,  512,  724,  887, 1024};
static const uint8_t kScrDivList[] = { 3, 4, 5, 6, 7, 8 }; // 2^div のシフト量（全体弱め）

// Keyball風レポート間隔（ms）: 8ms=125Hz。0で無効（毎スキャン出力）
static const uint16_t kReportIntervalMs = 8;

// 加速度（カーソル加速）関連：Q8 係数（256 = 1.0）
// gain は『(speed - thr) * gain / 256』を加算する傾き。大きいほど強い。
static const uint16_t kAccelGainQ8[] = { 0, 16, 24, 32, 40, 48, 64, 80 };
static const uint8_t  kAccelGainMaxIdx = (ARRAY_SIZE(kAccelGainQ8) - 1);
static const uint8_t  kAccelThreshold = 4;       // この合計移動（|dx|+|dy|）を超えると加速
static const uint16_t ACCEL_MUL_BASE_Q8 = 256;   // 1.0x
static const uint16_t ACCEL_MUL_MAX_Q8  = 1024;  // 4.0x 上限

// 前方宣言（apply_scroll から使用）
static inline uint16_t calc_accel_mul_q8(int8_t x, int8_t y);
// Q10回転後の成分から連続値で加速係数を算出（左右別に平滑）
static inline uint16_t calc_accel_mul_q8_q10(int32_t x_q10, int32_t y_q10, bool is_left);

#define ROT_STEPS (ARRAY_SIZE(COS_Q10))
// 「CPI」はここでは移動量のソフト倍率で再現
#define CPI_BASE 800  // 800cpi を基準1.0として扱う

// ==== EEPROM保存（左右まとめて 32bit にパック） ==================
typedef struct {
    uint8_t cpi_idx;     // 0..15
    uint8_t rot_idx;     // 0..ROT_STEPS-1
    uint8_t scr_div_idx; // 0..7
    bool    scr_invert;  // 0/1
    bool    scroll_mode; // 0:cursor 1:scroll
} side_cfg_t;

static side_cfg_t gL, gR;

// 共有（左右で共通）設定：カーソル加速度
static bool    gAccelEnable = false; // Keyball風: 既定はオフ（素直なリニア挙動）
static uint8_t gAccelGainIdx = 0; // 0..kAccelGainMaxIdx

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

    // 共有設定（残りビットの中でパック）
    v |= (uint32_t)(gAccelEnable ? 1 : 0)      << 26;           // bit26
    v |= (uint32_t)(gAccelGainIdx & 0x7)       << 27;           // bit27..29
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

    gAccelEnable   = ((v >> 26) & 1) != 0;
    gAccelGainIdx  = (v >> 27) & 0x7;
}

static void tb_eeprom_defaults(void) {
    // 左
    gL.cpi_idx     = 2;   // 800cpi
    gL.rot_idx     = 5;   // 0°
    gL.scr_div_idx = 5;   // スクロール弱め
    gL.scr_invert  = false;
    gL.scroll_mode = true;

    // 右
    gR.cpi_idx     = 2;   // 800cpi
    gR.rot_idx     = 5;   // 0°
    gR.scr_div_idx = 3;
    gR.scr_invert  = false;
    gR.scroll_mode = false;

    // 共有
    gAccelEnable   = false; // Keyball風: デフォルトは加速オフ
    gAccelGainIdx  = 0;     // 無効時のダミー
}

static void tb_load_eeprom(void) {
    uint32_t raw = eeconfig_read_kb();
    unpack_cfg(raw);

    bool bad =
        gL.cpi_idx     >= ARRAY_SIZE(kCpiList)   ||
        gL.rot_idx     >= ROT_STEPS              ||
        gL.scr_div_idx >= ARRAY_SIZE(kScrDivList)||
        gR.cpi_idx     >= ARRAY_SIZE(kCpiList)   ||
        gR.rot_idx     >= ROT_STEPS              ||
        gR.scr_div_idx >= ARRAY_SIZE(kScrDivList) ||
        gAccelGainIdx  >  kAccelGainMaxIdx;

    if (bad) {
        tb_eeprom_defaults();
        eeconfig_update_kb(pack_cfg());
    }
}

static inline void tb_save(void) { eeconfig_update_kb(pack_cfg()); }

// ==== 回転（固定小数点 Q10） ======================================
static inline void rotate_xy_idx(int8_t* x, int8_t* y, uint8_t idx) {
    int8_t  ox = *x, oy = *y;
    int32_t rx = (int32_t)ox * COS_Q10[idx] - (int32_t)oy * SIN_Q10[idx];
    int32_t ry = (int32_t)ox * SIN_Q10[idx] + (int32_t)oy * COS_Q10[idx];
    rx += (rx >= 0 ? 512 : -512); ry += (ry >= 0 ? 512 : -512); // 四捨五入
    rx >>= 10; ry >>= 10;
    if (rx > 127) rx = 127; else if (rx < -127) rx = -127;
    if (ry > 127) ry = 127; else if (ry < -127) ry = -127;
    *x = (int8_t)rx; *y = (int8_t)ry;
}

// ==== スクロール変換（左右別の蓄積） ==============================
static int v_acc_l = 0, h_acc_l = 0;
static int v_acc_r = 0, h_acc_r = 0;
// スクロールの蓄積のみ行う（出力は emit 側）
static void scroll_accumulate(report_mouse_t in, bool invert, uint8_t scr_div_idx, bool is_left, uint8_t rot_idx) {
    (void)scr_div_idx; // 蓄積段階では未使用
    int *v_acc = is_left ? &v_acc_l : &v_acc_r;
    int *h_acc = is_left ? &h_acc_l : &h_acc_r;

    int8_t x = in.x, y = in.y;
    // 回転後に1D寄せ
    rotate_xy_idx(&x, &y, rot_idx);
    if (abs((int)x) > abs((int)y)) y = 0; else x = 0;
    if (invert) { x = -x; y = -y; }

    // Keyball風: 加速度なし（リニア）
    *h_acc += (int)x;
    *v_acc += (int)y;
}

// スクロールの量子化と出力値取得（量子化した分だけ蓄積から減算）
static void scroll_emit(report_mouse_t* out, uint8_t scr_div_idx, bool is_left) {
    int *v_acc = is_left ? &v_acc_l : &v_acc_r;
    int *h_acc = is_left ? &h_acc_l : &h_acc_r;
    uint8_t div = kScrDivList[scr_div_idx];
    int8_t hs = (int8_t)(*h_acc >> div);
    int8_t vs = (int8_t)(*v_acc >> div);
    if (hs) { out->h += hs; *h_acc -= (hs << div); }
    if (vs) { out->v += vs; *v_acc -= (vs << div); }
}

// ==== 移動量のソフト倍率（左右別。64bit蓄積） ====================
static int64_t x_acc_l=0, y_acc_l=0, x_acc_r=0, y_acc_r=0;
// 加速用 速度平滑（Q10）
static int32_t speed_q10_lp_l = 0;
static int32_t speed_q10_lp_r = 0;

static inline uint16_t calc_accel_mul_q8(int8_t x, int8_t y) {
    if (!gAccelEnable) return ACCEL_MUL_BASE_Q8;
    uint16_t mul = ACCEL_MUL_BASE_Q8;
    uint16_t speed = (uint16_t)abs((int)x) + (uint16_t)abs((int)y);
    if (speed > kAccelThreshold) {
        uint16_t over = speed - kAccelThreshold;
        uint32_t add  = (uint32_t)over * (uint32_t)kAccelGainQ8[gAccelGainIdx];
        mul += (uint16_t)MIN(add, (uint32_t)(ACCEL_MUL_MAX_Q8 - ACCEL_MUL_BASE_Q8));
        if (mul > ACCEL_MUL_MAX_Q8) mul = ACCEL_MUL_MAX_Q8;
    }
    return mul;
}

// 回転後Q10の速度から、連続しきい値＋ローパスで滑らかに加速係数を算出
static inline uint16_t calc_accel_mul_q8_q10(int32_t x_q10, int32_t y_q10, bool is_left) {
    if (!gAccelEnable) return ACCEL_MUL_BASE_Q8;
    // L1ノルム（Q10）
    int32_t s_q10 = abs(x_q10) + abs(y_q10);
    // 簡易ローパス（alpha = 1/8）。左右別に状態を持つ
    int32_t* plp = is_left ? &speed_q10_lp_l : &speed_q10_lp_r;
    int32_t lp = *plp + ((s_q10 - *plp) >> 3);
    *plp = lp;

    // 連続しきい値: over_q10 = max(0, lp - thr<<10)
    int32_t thr_q10 = ((int32_t)kAccelThreshold) << 10;
    int32_t over_q10 = lp - thr_q10;
    if (over_q10 <= 0) return ACCEL_MUL_BASE_Q8;

    // add_q8 = (over_q10 * gain_q8) >> 10 （Q10→整数カウントへ変換しつつ連続化）
    uint32_t add_q8 = ((uint64_t)(uint32_t)over_q10 * (uint32_t)kAccelGainQ8[gAccelGainIdx] + 512) >> 10;
    uint32_t max_add = (uint32_t)(ACCEL_MUL_MAX_Q8 - ACCEL_MUL_BASE_Q8);
    if (add_q8 > max_add) add_q8 = max_add;
    return (uint16_t)(ACCEL_MUL_BASE_Q8 + add_q8);
}

// 角度回転をQ10精度のまま保持して蓄積（出力は emit 側）
static void move_accumulate(report_mouse_t in, uint16_t cpi, bool is_left, uint8_t rot_idx) {
    int64_t *xa = is_left ? &x_acc_l : &x_acc_r;
    int64_t *ya = is_left ? &y_acc_l : &y_acc_r;
    int8_t ox = in.x, oy = in.y;
    int32_t rx_q10 = (int32_t)ox * COS_Q10[rot_idx] - (int32_t)oy * SIN_Q10[rot_idx];
    int32_t ry_q10 = (int32_t)ox * SIN_Q10[rot_idx] + (int32_t)oy * COS_Q10[rot_idx];
    uint16_t mul_q8 = ACCEL_MUL_BASE_Q8; // リニア
    *xa += (int64_t)rx_q10 * (int64_t)cpi * (int64_t)mul_q8;
    *ya += (int64_t)ry_q10 * (int64_t)cpi * (int64_t)mul_q8;
}

// 蓄積から量子化して出力（量子化した分だけ蓄積から減算）
static void move_emit(report_mouse_t* out, bool is_left) {
    int64_t *xa = is_left ? &x_acc_l : &x_acc_r;
    int64_t *ya = is_left ? &y_acc_l : &y_acc_r;
    int64_t den = (int64_t)CPI_BASE * (int64_t)ACCEL_MUL_BASE_Q8 * (int64_t)1024; // = 800 * 256 * 1024
    int64_t ax = *xa;
    int64_t ay = *ya;
    ax += (ax >= 0 ? den / 2 : -den / 2);
    ay += (ay >= 0 ? den / 2 : -den / 2);
    int32_t ox_q = (int32_t)(ax / den);
    int32_t oy_q = (int32_t)(ay / den);
    if (ox_q > 127) ox_q = 127; else if (ox_q < -127) ox_q = -127;
    if (oy_q > 127) oy_q = 127; else if (oy_q < -127) oy_q = -127;
    out->x += (int8_t)ox_q; out->y += (int8_t)oy_q;
    *xa -= (int64_t)ox_q * den;
    *ya -= (int64_t)oy_q * den;
    // 暴走保険
    const int64_t bound = (int64_t)1 << 60;
    if (*xa >  bound) { *xa =  bound; }
    if (*xa < -bound) { *xa = -bound; }
    if (*ya >  bound) { *ya =  bound; }
    if (*ya < -bound) { *ya = -bound; }
}

// ==== 残差リセット（設定変更時の引っかかり低減） ==================
static inline void tb_reset_acc(bool left, bool right) {
    if (left)  { x_acc_l = y_acc_l = 0; v_acc_l = h_acc_l = 0; }
    if (right) { x_acc_r = y_acc_r = 0; v_acc_r = h_acc_r = 0; }
}

// ==== カスタムキーコード ==========================================
// QK_KB_0 起点（vial.json の customKeycodes の並びと一致させる）
enum custom_keycodes {
    TB_L_CPI_NEXT = QK_KB_0, TB_L_CPI_PREV,
    TB_L_ROT_NEXT, TB_L_ROT_PREV,
    TB_L_SCR_TOG,  TB_L_SCR_DIV,
    TB_L_SCR_INV,

    TB_R_CPI_NEXT, TB_R_CPI_PREV,
    TB_R_ROT_NEXT, TB_R_ROT_PREV,
    TB_R_SCR_TOG,  TB_R_SCR_DIV,
    TB_R_SCR_INV,

    // 共有（カーソル加速度）
    TB_ACCEL_TOG,  // 有効/無効
    TB_ACCEL_UP,   // 強く
    TB_ACCEL_DOWN, // 弱く
};

// ==== 公開API（tb.h） =============================================
void tb_init(void) {
    tb_load_eeprom();
}

bool tb_process_record(uint16_t keycode, keyrecord_t* record) {
    if (!record->event.pressed) return true;

    switch (keycode) {
        // 左
        case TB_L_CPI_NEXT:
            gL.cpi_idx = (gL.cpi_idx + 1) % ARRAY_SIZE(kCpiList); tb_save(); tb_reset_acc(true, false); return false;
        case TB_L_CPI_PREV:
            gL.cpi_idx = (gL.cpi_idx + ARRAY_SIZE(kCpiList) - 1) % ARRAY_SIZE(kCpiList); tb_save(); tb_reset_acc(true, false); return false;
        case TB_L_ROT_NEXT:
            gL.rot_idx = (gL.rot_idx + 1) % ROT_STEPS; tb_save(); tb_reset_acc(true, false); return false;
        case TB_L_ROT_PREV:
            gL.rot_idx = (gL.rot_idx + ROT_STEPS - 1) % ROT_STEPS; tb_save(); tb_reset_acc(true, false); return false;
        case TB_L_SCR_TOG:
            gL.scroll_mode ^= 1; tb_save(); tb_reset_acc(true, false); return false;
        case TB_L_SCR_DIV:
            gL.scr_div_idx = (gL.scr_div_idx + 1) % ARRAY_SIZE(kScrDivList); tb_save(); tb_reset_acc(true, false); return false;
        case TB_L_SCR_INV:
            gL.scr_invert ^= 1; tb_save(); tb_reset_acc(true, false); return false;

        // 右
        case TB_R_CPI_NEXT:
            gR.cpi_idx = (gR.cpi_idx + 1) % ARRAY_SIZE(kCpiList); tb_save(); tb_reset_acc(false, true); return false;
        case TB_R_CPI_PREV:
            gR.cpi_idx = (gR.cpi_idx + ARRAY_SIZE(kCpiList) - 1) % ARRAY_SIZE(kCpiList); tb_save(); tb_reset_acc(false, true); return false;
        case TB_R_ROT_NEXT:
            gR.rot_idx = (gR.rot_idx + 1) % ROT_STEPS; tb_save(); tb_reset_acc(false, true); return false;
        case TB_R_ROT_PREV:
            gR.rot_idx = (gR.rot_idx + ROT_STEPS - 1) % ROT_STEPS; tb_save(); tb_reset_acc(false, true); return false;
        case TB_R_SCR_TOG:
            gR.scroll_mode ^= 1; tb_save(); tb_reset_acc(false, true); return false;
        case TB_R_SCR_DIV:
            gR.scr_div_idx = (gR.scr_div_idx + 1) % ARRAY_SIZE(kScrDivList); tb_save(); tb_reset_acc(false, true); return false;
        case TB_R_SCR_INV:
            gR.scr_invert ^= 1; tb_save(); tb_reset_acc(false, true); return false;

        // 共有（加速度）
        case TB_ACCEL_TOG:
            gAccelEnable ^= 1; tb_save(); tb_reset_acc(true, true); return false;
        case TB_ACCEL_UP:
            if (gAccelGainIdx < kAccelGainMaxIdx) {
                gAccelGainIdx++;
            }
            tb_save();
            tb_reset_acc(true, true);
            return false;
        case TB_ACCEL_DOWN:
            if (gAccelGainIdx > 0) {
                gAccelGainIdx--;
            }
            tb_save();
            tb_reset_acc(true, true);
            return false;
    }
    return true;
}

report_mouse_t tb_task_combined(report_mouse_t left_in, report_mouse_t right_in) {
    // まず蓄積のみ行う（出力は後段のスロットリング判定で）
    if (gL.scroll_mode) {
        scroll_accumulate(left_in, gL.scr_invert, gL.scr_div_idx, true, gL.rot_idx);
    } else {
        move_accumulate(left_in, kCpiList[gL.cpi_idx], true, gL.rot_idx);
    }
    if (gR.scroll_mode) {
        scroll_accumulate(right_in, gR.scr_invert, gR.scr_div_idx, false, gR.rot_idx);
    } else {
        move_accumulate(right_in, kCpiList[gR.cpi_idx], false, gR.rot_idx);
    }

    // スロットリング：一定間隔でまとめて量子化・出力
    static uint32_t last_ms = 0;
    uint32_t now = timer_read32();
    if (kReportIntervalMs > 0) {
        if (TIMER_DIFF_32(now, last_ms) < kReportIntervalMs) {
            // 出力しないフレーム
            report_mouse_t zero = {0};
            return pointing_device_combine_reports(zero, zero);
        }
        last_ms = now;
    }

    report_mouse_t left = {0};
    report_mouse_t right = {0};
    if (gL.scroll_mode) {
        scroll_emit(&left, gL.scr_div_idx, true);
        left.x = 0; left.y = 0; // スクロール時はXY無効
    } else {
        move_emit(&left, true);
    }
    if (gR.scroll_mode) {
        scroll_emit(&right, gR.scr_div_idx, false);
        right.x = 0; right.y = 0;
    } else {
        move_emit(&right, false);
    }

    return pointing_device_combine_reports(left, right);
}
