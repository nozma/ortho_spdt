#include QMK_KEYBOARD_H
#include "quantum.h"
#include "pointing_device.h"

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {};

// === 可変パラメータの候補 ===
static const uint16_t kCpiList[]   = { 200, 400, 800, 1600, 3200 };
static const int8_t   kAngleList[] = { -90,-60,-45,-30,-15,0,15,30,45,60,90 };
static const uint8_t  kScrDivList[]= { 0,1,2,3,4,5 }; // 右シフト量(2^div)として使用

typedef union {
    uint32_t raw;
    struct {
        uint8_t cpi_idx      : 4; // 0..15
        uint8_t rot_idx      : 4; // 0..15
        uint8_t scr_div_idx  : 3; // 0..7
        bool    scr_invert   : 1; // 0/1
        bool    scroll_mode  : 1; // 0:cursor 1:scroll
        uint8_t _reserved    : 19;
    };
} tb_side_cfg_t;

static tb_side_cfg_t g_left_cfg, g_right_cfg;  // マスターで保持

// EEPROM保存/読込（キーボード側の領域を使う簡易版）
static void tb_load_eeprom_side(bool is_left) {
    tb_side_cfg_t tmp; tmp.raw = eeconfig_read_kb();
    if (is_left) g_left_cfg = tmp; else g_right_cfg = tmp;
}
static void tb_save_eeprom_side(bool is_left) {
    tb_side_cfg_t src = is_left ? g_left_cfg : g_right_cfg;
    eeconfig_update_kb(src.raw);
}

// 角度回転（度→ラジアン）
static inline void rotate_xy(int8_t* x, int8_t* y, int8_t deg) {
    // 単純な近似（コストが軽い）：±90, ±60, ±45, ±30, ±15, 0 のみ対応
    // 必要ならfloat/cos/sinに差し替え
    int8_t ox=*x, oy=*y;
    switch (deg) {
        case 90:  *x = -oy; *y =  ox; break;
        case -90: *x =  oy; *y = -ox; break;
        case 60:  *x = ( -oy*866 + ox*500 )/1000, *y = ( ox*866 + oy*500 )/1000; break;
        case -60: *x = (  oy*866 + ox*500 )/1000, *y = ( -ox*866 + oy*500 )/1000; break;
        case 45:  *x = (ox - oy); *y = (ox + oy); break;   // √2は省略（ざっくり）
        case -45: *x = (ox + oy); *y = (oy - ox); break;
        case 30:  *x = ( ox*866 - oy*500 )/1000, *y = ( ox*500 + oy*866 )/1000; break;
        case -30: *x = ( ox*866 + oy*500 )/1000, *y = ( oy*866 - ox*500 )/1000; break;
        case 15:  *x = ( ox*966 - oy*259 )/1000, *y = ( ox*259 + oy*966 )/1000; break;
        case -15: *x = ( ox*966 + oy*259 )/1000, *y = ( oy*966 - ox*259 )/1000; break;
        default: break;
    }
}

// スクロール変換（簡易1D優先＋蓄積→分配）
static void apply_scroll(report_mouse_t* r, const tb_side_cfg_t* cfg) {
    static int v_acc_l=0, h_acc_l=0, v_acc_r=0, h_acc_r=0; // 片側ごとに持ちたいなら配列化
    int &v_acc = (r == NULL) ? v_acc_l : v_acc_r; // 実装を簡略化（必要なら左右分離）
    int &h_acc = (r == NULL) ? h_acc_l : h_acc_r;

    int8_t x = r->x, y = r->y;
    // どちらかを0にして1Dスクロールに寄せる
    if (abs(x) > abs(y)) y = 0; else x = 0;

    // 反転
    if (cfg->scr_invert) { x = -x; y = -y; }

    // 蓄積→シフト（2^div）
    uint8_t div = kScrDivList[cfg->scr_div_idx];
    h_acc += x; v_acc += y;
    int8_t hs = (int8_t)(h_acc >> div);
    int8_t vs = (int8_t)(v_acc >> div);
    if (hs) { r->h += hs; h_acc -= (hs << div); }
    if (vs) { r->v += vs; v_acc -= (vs << div); }

    r->x = r->y = 0; // スクロール時はXYは出さない
}

// COMBINED: 左右別レポートを受け取り、個別に加工して合成
report_mouse_t pointing_device_task_combined_kb(report_mouse_t left, report_mouse_t right) {
    // 左
    {
        int8_t x = left.x, y = left.y;
        int8_t deg = kAngleList[g_left_cfg.rot_idx];
        rotate_xy(&x, &y, deg);
        left.x = x; left.y = y;
        if (g_left_cfg.scroll_mode) apply_scroll(&left, &g_left_cfg);
    }
    // 右
    {
        int8_t x = right.x, y = right.y;
        int8_t deg = kAngleList[g_right_cfg.rot_idx];
        rotate_xy(&x, &y, deg);
        right.x = x; right.y = y;
        if (g_right_cfg.scroll_mode) apply_scroll(&right, &g_right_cfg);
    }
    return pointing_device_combine_reports(&left, &right);
}

// === カスタムキーコード ===
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

// CPI反映
static void apply_cpi(bool is_left) {
    const tb_side_cfg_t* c = is_left ? &g_left_cfg : &g_right_cfg;
    pointing_device_set_cpi_on_side(is_left, kCpiList[c->cpi_idx]); // 片側のみ適用
}

// 初期化
void keyboard_post_init_kb(void) {
    tb_load_eeprom_side(true);
    tb_load_eeprom_side(false);
    apply_cpi(true);
    apply_cpi(false);
}

// 押下→対象側を更新（rowで左右判定してもよいが、ここはキー名で明示）
bool process_record_kb(uint16_t keycode, keyrecord_t* record) {
    if (!record->event.pressed) return true;

    switch (keycode) {
        case TB_L_CPI_NEXT: g_left_cfg.cpi_idx = (g_left_cfg.cpi_idx + 1) % (sizeof(kCpiList)/2); apply_cpi(true); tb_save_eeprom_side(true); return false;
        case TB_L_CPI_PREV: g_left_cfg.cpi_idx = (g_left_cfg.cpi_idx + sizeof(kCpiList)/2 - 1) % (sizeof(kCpiList)/2); apply_cpi(true); tb_save_eeprom_side(true); return false;
        case TB_L_ROT_NEXT: g_left_cfg.rot_idx = (g_left_cfg.rot_idx + 1) % (sizeof(kAngleList)); tb_save_eeprom_side(true); return false;
        case TB_L_ROT_PREV: g_left_cfg.rot_idx = (g_left_cfg.rot_idx + sizeof(kAngleList) - 1) % (sizeof(kAngleList)); tb_save_eeprom_side(true); return false;
        case TB_L_SCR_TOG:  g_left_cfg.scroll_mode ^= 1; tb_save_eeprom_side(true); return false;
        case TB_L_SCR_DIV:  g_left_cfg.scr_div_idx = (g_left_cfg.scr_div_idx + 1) % (sizeof(kScrDivList)); tb_save_eeprom_side(true); return false;
        case TB_L_SCR_INV:  g_left_cfg.scr_invert ^= 1; tb_save_eeprom_side(true); return false;

        case TB_R_CPI_NEXT: g_right_cfg.cpi_idx = (g_right_cfg.cpi_idx + 1) % (sizeof(kCpiList)/2); apply_cpi(false); tb_save_eeprom_side(false); return false;
        case TB_R_CPI_PREV: g_right_cfg.cpi_idx = (g_right_cfg.cpi_idx + sizeof(kCpiList)/2 - 1) % (sizeof(kCpiList)/2); apply_cpi(false); tb_save_eeprom_side(false); return false;
        case TB_R_ROT_NEXT: g_right_cfg.rot_idx = (g_right_cfg.rot_idx + 1) % (sizeof(kAngleList)); tb_save_eeprom_side(false); return false;
        case TB_R_ROT_PREV: g_right_cfg.rot_idx = (g_right_cfg.rot_idx + sizeof(kAngleList) - 1) % (sizeof(kAngleList)); tb_save_eeprom_side(false); return false;
        case TB_R_SCR_TOG:  g_right_cfg.scroll_mode ^= 1; tb_save_eeprom_side(false); return false;
        case TB_R_SCR_DIV:  g_right_cfg.scr_div_idx = (g_right_cfg.scr_div_idx + 1) % (sizeof(kScrDivList)); tb_save_eeprom_side(false); return false;
        case TB_R_SCR_INV:  g_right_cfg.scr_invert ^= 1; tb_save_eeprom_side(false); return false;
    }
    return true;
}