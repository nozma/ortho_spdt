// keyboards/split_ortho4x6/keymaps/vial/tb.c
#include "tb.h"
#include "quantum.h"
#include "pointing_device.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // abs

// ==== 可変パラメータ（必要に応じて調整） =========================
static const uint16_t kCpiList[]   = { 200, 400, 800, 1600, 3200 };
// 回転角の順序（インデックス）: [-90, -60, -45, -30, -15, 0, 15, 30, 45, 60, 90]
static const int16_t COS_Q10[] = {    0,  512,  724,  887,  990, 1024,  990,  887,  724,  512,    0};
static const int16_t SIN_Q10[] = {-1024, -887, -724, -512, -259,    0,  259,  512,  724,  887, 1024};
static const uint8_t kScrDivList[] = { 3, 4, 5, 6, 7, 8 }; // 2^div のシフト量（全体弱め）

// 加速度（カーソル加速）関連：Q8 係数（256 = 1.0）
// gain は強さ（最大加速に対する比率）として扱う。Keyball の“滑らかな増加”の感触を意識し、
// 速度に対して滑らかな S カーブ（smoothstep）で加速度を決める。
static const uint16_t kAccelGainQ8[] = { 0, 16, 24, 32, 40, 48, 64, 80 };
static const uint8_t  kAccelGainMaxIdx = (ARRAY_SIZE(kAccelGainQ8) - 1);
static const uint8_t  kAccelThreshold = 4;       // この合計移動（|dx|+|dy|）を超えると加速開始（デッドゾーン）
static const uint16_t ACCEL_MUL_BASE_Q8 = 256;   // 1.0x
static const uint16_t ACCEL_MUL_MAX_Q8  = 1024;  // 4.0x 上限
// 加速が最大に達する“速さ”。しきい値からこの値までを S カーブで補間する。
static const uint8_t  kAccelFullSpeed = 24;      // ≈ 24count で最大加速（要調整）

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
static bool    gAccelEnable = true;
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
    gAccelEnable   = true;  // 既定はON
    gAccelGainIdx  = 3;     // ほどほど（32）
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

static void apply_scroll(report_mouse_t* r, bool invert, uint8_t scr_div_idx, bool is_left, uint8_t rot_idx) {
    int *v_acc = is_left ? &v_acc_l : &v_acc_r;
    int *h_acc = is_left ? &h_acc_l : &h_acc_r;

    int8_t x = r->x, y = r->y;
    int8_t ox = x, oy = y; // 速度評価用に原値を保持
    // どちらかを0にして1D寄せ
    if (abs((int)x) > abs((int)y)) y = 0; else x = 0;

    if (invert) { x = -x; y = -y; }

    // 加速度倍率（Q8）：回転後Q10で評価し、左右別ローパスで段差を低減
    int32_t rx_q10 = (int32_t)ox * COS_Q10[rot_idx] - (int32_t)oy * SIN_Q10[rot_idx];
    int32_t ry_q10 = (int32_t)ox * SIN_Q10[rot_idx] + (int32_t)oy * COS_Q10[rot_idx];
    uint16_t mul_q8 = calc_accel_mul_q8_q10(rx_q10, ry_q10, is_left);
    // 近似的な四捨五入付きスケーリング
    int32_t sx = (int32_t)x * (int32_t)mul_q8;
    int32_t sy = (int32_t)y * (int32_t)mul_q8;
    if (sx >= 0) sx += ACCEL_MUL_BASE_Q8 / 2; else sx -= ACCEL_MUL_BASE_Q8 / 2;
    if (sy >= 0) sy += ACCEL_MUL_BASE_Q8 / 2; else sy -= ACCEL_MUL_BASE_Q8 / 2;
    sx /= ACCEL_MUL_BASE_Q8; sy /= ACCEL_MUL_BASE_Q8;

    uint8_t div = kScrDivList[scr_div_idx];
    *h_acc += (int)sx; *v_acc += (int)sy;

    int8_t hs = (int8_t)(*h_acc >> div);
    int8_t vs = (int8_t)(*v_acc >> div);

    if (hs) { r->h += hs; *h_acc -= (hs << div); }
    if (vs) { r->v += vs; *v_acc -= (vs << div); }

    // スクロール時はXYを出さない
    r->x = 0;
    r->y = 0;
}

// ==== 移動量のソフト倍率（左右別。64bit蓄積） ====================
static int64_t x_acc_l=0, y_acc_l=0, x_acc_r=0, y_acc_r=0;
// 加速用 速度平滑（Q10）
static int32_t speed_q10_lp_l = 0;
static int32_t speed_q10_lp_r = 0;

static inline uint16_t calc_accel_mul_q8(int8_t x, int8_t y) {
    if (!gAccelEnable) return ACCEL_MUL_BASE_Q8;
    // 従来の単純線形はピーキーになりやすいため、ここでも S カーブで補間
    uint16_t speed = (uint16_t)abs((int)x) + (uint16_t)abs((int)y);
    if (speed <= kAccelThreshold) return ACCEL_MUL_BASE_Q8;

    uint16_t span = (kAccelFullSpeed > kAccelThreshold) ? (uint16_t)(kAccelFullSpeed - kAccelThreshold) : 1;
    uint16_t over = (speed - kAccelThreshold);
    if (over > span) over = span;

    // t in Q15 (0..1)
    uint32_t t_q15 = ((uint32_t)over << 15) / (uint32_t)span;
    // smoothstep: 3t^2 - 2t^3 (Q15)
    uint32_t t2_q15 = (t_q15 * t_q15 + (1 << 14)) >> 15;
    uint32_t t3_q15 = (t2_q15 * t_q15 + (1 << 14)) >> 15;
    int32_t  s_q15  = (int32_t)(3 * t2_q15 - 2 * t3_q15);
    if (s_q15 < 0) s_q15 = 0; else if (s_q15 > 32767) s_q15 = 32767;

    // gain スケール（Q15）: 0..1 に正規化
    uint32_t gain_max = (uint32_t)kAccelGainQ8[kAccelGainMaxIdx];
    uint32_t gain_q15 = (gain_max ? ((uint32_t)kAccelGainQ8[gAccelGainIdx] << 15) / gain_max : 0);

    uint32_t max_add = (uint32_t)(ACCEL_MUL_MAX_Q8 - ACCEL_MUL_BASE_Q8);
    uint32_t max_add_scaled_q15 = (max_add * gain_q15 + (1 << 14)) >> 15;
    uint32_t add_q8 = (s_q15 * max_add_scaled_q15 + (1 << 14)) >> 15;
    uint32_t mul = (uint32_t)ACCEL_MUL_BASE_Q8 + add_q8;
    if (mul > ACCEL_MUL_MAX_Q8) mul = ACCEL_MUL_MAX_Q8;
    return (uint16_t)mul;
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

    // 連続しきい値＋Sカーブで滑らかに（Keyball風の“滑り出しなめらか”）
    int32_t thr_q10 = ((int32_t)kAccelThreshold) << 10;
    if (lp <= thr_q10) return ACCEL_MUL_BASE_Q8;

    int32_t full_q10 = ((int32_t)kAccelFullSpeed) << 10;
    int32_t span_q10 = full_q10 - thr_q10; if (span_q10 <= 0) span_q10 = 1;
    int32_t over_q10 = lp - thr_q10; if (over_q10 > span_q10) over_q10 = span_q10;

    // t in Q15
    uint32_t t_q15 = ((uint64_t)(uint32_t)over_q10 << 15) / (uint32_t)span_q10;
    uint32_t t2_q15 = (t_q15 * t_q15 + (1 << 14)) >> 15;
    uint32_t t3_q15 = (t2_q15 * t_q15 + (1 << 14)) >> 15;
    int32_t  s_q15  = (int32_t)(3 * t2_q15 - 2 * t3_q15);
    if (s_q15 < 0) s_q15 = 0; else if (s_q15 > 32767) s_q15 = 32767;

    // gain スケール（Q15）
    uint32_t gain_max = (uint32_t)kAccelGainQ8[kAccelGainMaxIdx];
    uint32_t gain_q15 = (gain_max ? ((uint32_t)kAccelGainQ8[gAccelGainIdx] << 15) / gain_max : 0);

    uint32_t max_add = (uint32_t)(ACCEL_MUL_MAX_Q8 - ACCEL_MUL_BASE_Q8);
    uint32_t max_add_scaled_q15 = (max_add * gain_q15 + (1 << 14)) >> 15;
    uint32_t add_q8 = (s_q15 * max_add_scaled_q15 + (1 << 14)) >> 15;
    return (uint16_t)(ACCEL_MUL_BASE_Q8 + add_q8);
}

// 角度回転をQ10精度のまま保持してからスケーリング・量子化する版
static void apply_move_scale_precise(report_mouse_t* r, uint16_t cpi, bool is_left, uint8_t rot_idx) {
    int64_t *xa = is_left ? &x_acc_l : &x_acc_r;
    int64_t *ya = is_left ? &y_acc_l : &y_acc_r;

    // 元のセンサ値（int8）
    int8_t ox = r->x, oy = r->y;

    // Q10で回転（ここでは四捨五入せず精度維持）
    int32_t rx_q10 = (int32_t)ox * COS_Q10[rot_idx] - (int32_t)oy * SIN_Q10[rot_idx];
    int32_t ry_q10 = (int32_t)ox * SIN_Q10[rot_idx] + (int32_t)oy * COS_Q10[rot_idx];

    // 加速度倍率（Q8）を計算：回転後のQ10で評価し、左右別ローパスで段差を低減
    uint16_t mul_q8 = calc_accel_mul_q8_q10(rx_q10, ry_q10, is_left);

    // 64bit蓄積（Q10精度を保ったまま）
    *xa += (int64_t)rx_q10 * (int64_t)cpi * (int64_t)mul_q8;
    *ya += (int64_t)ry_q10 * (int64_t)cpi * (int64_t)mul_q8;

    // 出力への変換：分母に Q10 を含めて符号付き四捨五入
    int64_t den = (int64_t)CPI_BASE * (int64_t)ACCEL_MUL_BASE_Q8 * (int64_t)1024; // = 800 * 256 * 1024
    int64_t ax = *xa;
    int64_t ay = *ya;
    ax += (ax >= 0 ? den / 2 : -den / 2);
    ay += (ay >= 0 ? den / 2 : -den / 2);
    int32_t ox_q = (int32_t)(ax / den);
    int32_t oy_q = (int32_t)(ay / den);
    if (ox_q > 127) ox_q = 127; else if (ox_q < -127) ox_q = -127;
    if (oy_q > 127) oy_q = 127; else if (oy_q < -127) oy_q = -127;

    r->x = (int8_t)ox_q; r->y = (int8_t)oy_q;
    *xa -= (int64_t)ox_q * den;
    *ya -= (int64_t)oy_q * den;

    // 残差クリップ（暴走保険）: 実運用で到達しない十分大きな範囲に拡大
    // 小さすぎると長時間の連続移動や大きな円運動でクリップが発生し、
    // 系統的なズレやカクつきの原因になるため、ここでは事実上無制限に近い値を採用
    const int64_t bound = (int64_t)1 << 60; // ≒1.15e18
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

report_mouse_t tb_task_combined(report_mouse_t left, report_mouse_t right) {
    // 左
    {
        if (gL.scroll_mode) {
            // スクロール時のみ従来どおり回転->1D化
            int8_t x = left.x, y = left.y;
            rotate_xy_idx(&x, &y, gL.rot_idx);
            left.x = x; left.y = y;
            apply_scroll(&left, gL.scr_invert, gL.scr_div_idx, true, gL.rot_idx);
        } else {
            // カーソル移動は高精度回転経路で処理
            apply_move_scale_precise(&left, kCpiList[gL.cpi_idx], true, gL.rot_idx);
        }
    }

    // 右
    {
        if (gR.scroll_mode) {
            int8_t x = right.x, y = right.y;
            rotate_xy_idx(&x, &y, gR.rot_idx);
            right.x = x; right.y = y;
            apply_scroll(&right, gR.scr_invert, gR.scr_div_idx, false, gR.rot_idx);
        } else {
            apply_move_scale_precise(&right, kCpiList[gR.cpi_idx], false, gR.rot_idx);
        }
    }

    return pointing_device_combine_reports(left, right);
}
