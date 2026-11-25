// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../qmk_firmware/keyboards/split_ortho4x6/keymaps/vial/tb.c"

static uint32_t eeprom_value;
static unsigned writes, user_inits;
static bool left_side;
int vial_unlocked = 1;
uint32_t eeconfig_read_kb(void) { return eeprom_value; }
void eeconfig_update_kb(uint32_t value) { eeprom_value = value; writes++; }
void eeconfig_init_user(void) { user_inits++; }
bool is_keyboard_left(void) { return left_side; }
report_mouse_t pointing_device_combine_reports(report_mouse_t l, report_mouse_t r) {
    l.x += r.x; l.y += r.y; l.h += r.h; l.v += r.v; return l;
}
static void packet(uint8_t *data, uint8_t op, uint32_t value) {
    memset(data, 0, 32);
    data[0] = TB_SETTINGS_COMMAND; data[1] = 'T'; data[2] = 'B';
    data[3] = TB_SETTINGS_VERSION; data[4] = op;
    for (int i = 0; i < 4; i++) data[5+i] = value >> (8*i);
}
static uint32_t result(const uint8_t *data) {
    return (uint32_t)data[7] | ((uint32_t)data[8] << 8) |
           ((uint32_t)data[9] << 16) | ((uint32_t)data[10] << 24);
}
int main(void) {
    uint8_t data[32];
    eeconfig_init_kb(); assert(user_inits == 1);
    uint32_t defaults = pack_cfg();
    assert(defaults == (3u | (18u << 4) | (1u << 9) | (3u << 10) |
                        (6u << 14) | (1u << 20) | (4u << 21) | (1u << 24) | (3u << 28)));
    unsigned initial_writes = writes;
    vial_unlocked = 0;
    packet(data, TB_SETTINGS_GET, 0); raw_hid_receive_kb(data, 32);
    assert(data[5] == TB_SETTINGS_OK && data[6] == 2 && result(data) == defaults);
    assert(writes == initial_writes);
    packet(data, TB_SETTINGS_SET, 0); raw_hid_receive_kb(data, 32);
    assert(data[5] == TB_SETTINGS_LOCKED && pack_cfg() == defaults && writes == initial_writes);
    vial_unlocked = 1; left_side = true;
    packet(data, TB_SETTINGS_SET, defaults); raw_hid_receive_kb(data, 32);
    assert(data[5] == 0 && data[6] == 1 && writes == initial_writes);

    // Every indexed field rejects its first out-of-range value, atomically.
    const unsigned shifts[] = {0, 4, 10, 14, 21, 24, 28};
    const unsigned invalid[] = {5, 25, 5, 25, 6, 5, 7};
    for (unsigned i = 0; i < 7; i++) {
        packet(data, TB_SETTINGS_SET, invalid[i] << shifts[i]); raw_hid_receive_kb(data, 32);
        assert(data[5] == TB_SETTINGS_INVALID_CONFIG && pack_cfg() == defaults && writes == initial_writes);
    }
    packet(data, TB_SETTINGS_SET, 0); data[3]++; raw_hid_receive_kb(data, 32);
    assert(data[5] == TB_SETTINGS_BAD_VERSION && writes == initial_writes);
    packet(data, 99, 0); raw_hid_receive_kb(data, 32);
    assert(data[5] == TB_SETTINGS_BAD_OPERATION && writes == initial_writes);
    packet(data, TB_SETTINGS_SET, 0); data[31] = 1; raw_hid_receive_kb(data, 32);
    assert(data[5] == TB_SETTINGS_BAD_PACKET && writes == initial_writes);
    packet(data, TB_SETTINGS_GET, defaults); raw_hid_receive_kb(data, 32);
    assert(data[5] == TB_SETTINGS_BAD_PACKET && writes == initial_writes);
    packet(data, TB_SETTINGS_SET, 0); data[1] = 'X'; raw_hid_receive_kb(data, 32);
    assert(data[0] == 0xFF && writes == initial_writes);
    packet(data, TB_SETTINGS_SET, 0); raw_hid_receive_kb(data, 31);
    assert(data[0] == 0xFF && writes == initial_writes);
    raw_hid_receive_kb(NULL, 0);

    // Prime old cursor smoothing, then restore and verify stale motion is discarded.
    report_mouse_t motion = {.x = 100}; tb_apply_transform_side(&motion, false);
    packet(data, TB_SETTINGS_SET, 0); raw_hid_receive_kb(data, 32);
    assert(data[5] == 0 && result(data) == 0 && eeprom_value == 0 && writes == initial_writes + 1);
    motion = (report_mouse_t){0}; tb_apply_transform_side(&motion, false);
    assert(motion.x == 0 && motion.y == 0 && motion.h == 0 && motion.v == 0);
    tb_defaults(); tb_init(); assert(pack_cfg() == 0); // Zero survives a reload.
    uint32_t maximum = 4u | (24u << 4) | (1u << 9) | (4u << 10) | (24u << 14) |
                       (1u << 19) | (1u << 20) | (5u << 21) | (4u << 24) |
                       (1u << 27) | (6u << 28) | (1u << 31);
    packet(data, TB_SETTINGS_SET, maximum); raw_hid_receive_kb(data, 32);
    assert(data[5] == 0 && result(data) == maximum && eeprom_value == maximum);
    tb_defaults(); tb_init(); assert(pack_cfg() == maximum);
    packet(data, TB_SETTINGS_GET, 0); raw_hid_receive_kb(data, 32);
    assert(result(data) == maximum);
    eeprom_value = 0xFFFFFFFFu; tb_init(); assert(pack_cfg() == defaults);
    puts("PASS: firmware protocol, validation, locks, sides, EEPROM reload, motion reset");
}
