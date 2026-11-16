/*
 * Mario Paint — Canvas interaction routines and title screen states.
 *
 * These routines handle cursor hit-testing, toolbar/palette clicks,
 * drawing tool dispatch, and title screen animation states.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $00:91C7 — Cursor zone check
 *
 * Checks if the cursor is in a clickable zone. Returns carry set
 * if cursor is in a valid zone, carry clear if not.
 * ======================================================================== */
void mp_0091C7(void) {
    uint16_t zone = bus_wram_read16(0x19AA);
    if (zone == 0) {
        g_cpu.flag_C = true;
        return;
    }

    /* Zone is active — do bounds checking */
    uint16_t page = bus_wram_read16(0x19FA);
    uint16_t tbl_idx = 0;
    if (page != 0) {
        tbl_idx = bus_wram_read16(0x19C2) + 1;
    }
    tbl_idx *= 2;

    uint16_t zone_ptr = bus_read16(0x00, 0x926C + tbl_idx);

    uint16_t zone_ofs = (bus_wram_read16(0x19A6) & 0xFF) * 4 + 4;

    /* Check cursor still within zone bounds */
    uint16_t tool = bus_wram_read16(0x04D0) & 0xFF;
    uint16_t cursor_x = bus_wram_read16(0x04DC);

    if (tool >= 5 && tool != 7) {
        cursor_x += 8;
    }

    uint16_t x_min = bus_read16(0x00, zone_ptr + zone_ofs);
    uint16_t x_max = bus_read16(0x00, zone_ptr + zone_ofs + 2);

    if ((int16_t)cursor_x < (int16_t)x_min || (int16_t)cursor_x >= (int16_t)x_max) {
        g_cpu.flag_C = false;
        return;
    }

    g_cpu.flag_C = true;
}

/* ========================================================================
 * Title screen animation states (12 states)
 *
 * These are registered individually for the state machine dispatch.
 * Each handles one phase of the title screen animation.
 * ======================================================================== */

/* State 0: Initialize cursor sprite and logo sprite slot */
void mp_018328(void) {
    bus_write16(0x7F, 0x0005, 0x00EF);  /* Cursor sprite ID */
    op_lda_imm16(0x0000);
    mp_019DFE();  /* Init sprite slot 0 */
    CPU_SET_A16(0xFFFF);
    mp_018C43();  /* Draw all title sprites */
    uint16_t state = bus_read16(0x7F, 0x0001);
    bus_write16(0x7F, 0x0001, state + 1);
}

/* State 1: Animate logo + check for click */
void mp_01833F(void) {
    CPU_SET_A16(0xFFFF);
    mp_018C43();
    mp_018CB7();
    func_table_call(0x018D35);  /* Check for title click */
}

/* State 2+: Complex animation — dispatch via func_table_call */
void mp_01834B(void) { /* Title state 2+: complex animation — not yet recompiled */ }

/* mp_0191FE and mp_01934F are in mp_misc.c */

/* ========================================================================
 * Register all interaction functions.
 * ======================================================================== */
void mp_register_interact(void) {
    func_table_register(0x009001, mp_009001);
    func_table_register(0x009107, mp_009107);
    func_table_register(0x0091C7, mp_0091C7);
    func_table_register(0x009564, mp_009564);
    func_table_register(0x009598, mp_009598);
    func_table_register(0x0095E7, mp_0095E7);
    func_table_register(0x008D2C, mp_008D2C);
    func_table_register(0x008E0B, mp_008E0B);
    func_table_register(0x018328, mp_018328);
    func_table_register(0x01833F, mp_01833F);
}
