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

/* State 2+: Complex animation — dispatch via func_table_call */

/* mp_0191FE and mp_01934F are in mp_misc.c */

/* ========================================================================
 * Register all interaction functions.
 * ======================================================================== */
void mp_register_interact(void) {
    func_table_register(0x009001, mp_009001);
    func_table_register(0x009107, mp_009107);
    func_table_register(0x009082, mp_009082);
    func_table_register(0x0091C7, mp_0091C7);
    func_table_register(0x009564, mp_009564);
    func_table_register(0x009598, mp_009598);
    func_table_register(0x0095E7, mp_0095E7);
    func_table_register(0x008D2C, mp_008D2C);
    func_table_register(0x008E0B, mp_008E0B);
    func_table_register(0x018328, mp_018328);
}
