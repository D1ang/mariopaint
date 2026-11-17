/*
 * Mario Paint — Title screen animation loop and Bank 0F routines.
 *
 * The title screen has a 12-state animation machine that runs
 * the Nintendo/Mario Paint logo sequence, then waits for input
 * or times out into the demo playback.
 *
 * Bank $0F contains the demo playback system that feeds
 * recorded mouse/button data into the input state.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $01:82B1 — Title screen cursor position update
 *
 * Applies mouse displacement ($04C6/$04C8) to cursor position
 * ($04DC/$04DE), clamped to title screen bounds.
 * ======================================================================== */
void mp_0182B1(void) {
    /* X displacement: signed magnitude → two's complement */
    uint8_t raw_dx = bus_wram_read8(0x04C6);
    int16_t dx = raw_dx & 0x7F;
    if (raw_dx & 0x80) dx = -dx;

    int16_t cx = (int16_t)bus_wram_read16(0x04DC);
    int16_t new_cx = cx + dx;
    if (new_cx < 0x00E8 && new_cx >= 0x0018) {
        bus_wram_write16(0x04DC, (uint16_t)new_cx);
    }

    /* Y displacement */
    uint8_t raw_dy = bus_wram_read8(0x04C8);
    int16_t dy = raw_dy & 0x7F;
    if (raw_dy & 0x80) dy = -dy;

    int16_t cy = (int16_t)bus_wram_read16(0x04DE);
    int16_t new_cy = cy + dy;
    if (new_cy < 0x00C0 && new_cy >= 0x0020) {
        bus_wram_write16(0x04DE, (uint16_t)new_cy);
    }
}

/* ========================================================================
 * $01:82F6 — Title screen cursor sprite draw
 *
 * Draws the cursor sprite at the current position using
 * the sprite ID stored at $7F:0005.
 * ======================================================================== */
void mp_0182F6(void) {
    uint16_t x = bus_wram_read16(0x04DC);
    uint16_t y = bus_wram_read16(0x04DE);
    uint16_t sprite_id = bus_read16(0x7F, 0x0005);

    g_cpu.X = x;
    g_cpu.Y = y;
    CPU_SET_A16(sprite_id);
    mp_01F91E();
}

/* ========================================================================
 * $01:8308 — Title screen state machine
 *
 * 12-state animation machine that runs the title screen sequence.
 * State stored at $7F:0001. Each state handles part of the
 * logo animation, then advances to the next.
 *
 * Most states involve complex sprite animations. We dispatch
 * each to func_table_call so they work progressively.
 * ======================================================================== */
void mp_018308(void) {
    /* LDA $0001 (DB = $7F here); ASL; TAX; JMP ($8310,x) in bank $01 */
    uint16_t x = (uint16_t)(bus_read16(g_cpu.DB, 0x0001) << 1);
    g_cpu.X = x;
    uint16_t h = bus_read16(0x01, (uint16_t)(0x8310 + x));
    CPU_SET_A16(x);
    tail_jmp(0x010000u | h);
}

/* ========================================================================
 * $01:8C43 — Title screen sprite setup
 *
 * Renders the 24 title screen sprites (tool icons, text, etc.).
 * Skips the sprite at index A (used to hide a specific one).
 *
 * Sprite data tables at $01:8C6F (X), $01:8C87 (Y), $01:8C9F (tile).
 * ======================================================================== */
void mp_018C43(void) {
    uint16_t skip_idx = CPU_A16();

    for (int x = 0x17; x >= 0; x--) {
        if ((uint16_t)x == skip_idx) continue;

        /* Read sprite X, Y, tile from ROM tables */
        uint8_t spr_x = bus_read8(0x01, 0x8C6F + x);
        uint8_t spr_y = bus_read8(0x01, 0x8C87 + x);
        uint8_t spr_tile = bus_read8(0x01, 0x8C9F + x);

        g_cpu.X = spr_x;
        g_cpu.Y = spr_y;
        CPU_SET_A16(spr_tile & 0xFF);
        mp_01F91E();
    }
}

/* ========================================================================
 * $01:8CB7 — Title screen: animate sprite slot 0
 * ======================================================================== */
void mp_018CB7(void) {
    op_lda_imm16(0x0000);
    func_table_call(0x01962C);
}

/* Set by $018CBF when the title screen should hand over to the canvas.
 * The original does this with a stack trick (LDA $0003; TCS; RTS at $01:8D2B)
 * that unwinds straight out of the title loop; a flag is the C equivalent.
 * This used to borrow g_quit, which conflated "leave the title" with "close
 * the window". */
static bool s_title_exit = false;

/* ========================================================================
 * $01:8CBF — Title screen input check
 *
 * Checks for player input to skip the title screen:
 *   - P2 controller: L+R+A+B ($9080) → skip to canvas ($0565=0)
 *   - P2 controller: L+R+A+X ($A080) → skip to canvas ($0565=1)
 *   - P2 buttons = $FFFF → skip ($0565=1)
 *   - Mouse click on the logo → skip ($0565=0)
 *
 * If no input, calls E2CE (frame sync) and returns normally.
 * On skip, manipulates the stack to return directly from $018260.
 * ======================================================================== */
void mp_018CBF(void) {
#define L16(o) bus_read16(0x00, (uint16_t)(o))
    int exit = -1;                                       /* $0565 value on the way out */
    if ((L16(0x013C) & 0x9080) && L16(0x0134) == 0x9080) exit = 0;          /* $8D1B */
    else if ((L16(0x013C) & 0xA080) && L16(0x0134) == 0xA080) exit = 1;     /* $8D24 */
    else if (L16(0x0134) == 0xFFFF) exit = 1;
    else if (L16(0x04CA) & 0x0020) {
        /* A click on the start sprite at $0792/$0794 (positions doubled):
         * unsigned window tests, as the ROM does them. */
        uint16_t dy = (uint16_t)((uint16_t)(L16(0x04DE) << 1) - L16(0x0794) + 0x0040);
        uint16_t dx = (uint16_t)((uint16_t)(L16(0x04DC) << 1) - L16(0x0792) + 0x0030);
        if (dy < 0x0048 && dx < 0x0060) exit = 0;
    }
    if (exit < 0) { call_jsl(0x01E2CE); return; }        /* $8D30 */
    bus_write16(0x00, 0x0565, (uint16_t)exit);
    /* $8D2B: LDA $0003; TCS; RTS - straight out of the title loop to the
     * stack point $01:8000 kept in $7F:0003 (DB is $7F here). */
    g_cpu.S = bus_read16(g_cpu.DB, 0x0003);
    CPU_SET_A16(g_cpu.S);
    s_title_exit = true;
#undef L16
}

/* ========================================================================
 * $01:8260 — Title screen loop
 *
 * Each frame: OAM cleared, the cursor moved and drawn, the title state
 * machine, the input check ($8CBF, which leaves the loop itself through
 * the stack point in $7F:0003). Any mouse activity clears the idle count
 * $7F:0411; $0800 idle frames start the attract demo, and the loop leaves
 * the same way, through the stack point.
 * ======================================================================== */
void mp_018260(void) {
#define R16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
    s_title_exit = false;
    for (;;) {
        if (g_quit) return;
        call_jsl(0x01E06F);
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        call_jsr(0x018308);
        call_jsr(0x018CBF);
        if (s_title_exit) break;                         /* S already at the stack point */
        uint16_t act = (uint16_t)(bus_read16(0x00, 0x04C6) | bus_read16(0x00, 0x04C8)
                                  | bus_read16(0x00, 0x04CA));
        if (act != 0) { W16(0x0411, 0); continue; }
        W16(0x0411, R16(0x0411) + 1);
        if (R16(0x0411) == 0x0800) {
            bus_write16(0x00, 0x04DC, 0x0080);
            bus_write16(0x00, 0x04DE, 0x0080);
            call_jsl(0x0FC00E);
            bus_write16(0x00, 0x0448, 0x0100);
            CPU_SET_A16(bus_read16(0x00, 0x0002));
            call_jsl(0x01E238);
            break;
        }
    }
    s_title_exit = false;
    g_cpu.S = R16(0x0003);                               /* LDA $0003; TCS; RTS */
    CPU_SET_A16(g_cpu.S);
#undef R16
#undef W16
}

/* ========================================================================
 * $0F:C000 — Bank 0F: per-frame demo dispatch
 *
 * Called from the NMI handler every frame. If demo is not active
 * ($04E2 = 0), just returns. If active, plays back recorded input.
 * ======================================================================== */
void mp_0FC000(void) {
    uint16_t demo = bus_wram_read16(0x04E2);
    if (demo == 0) return;

    /* Demo is active — play back recorded input */
    /* Check if mouse/button activity should cancel demo */
    uint16_t mouse_any = bus_wram_read16(0x04C6) |
                         bus_wram_read16(0x04C8) |
                         bus_wram_read16(0x04CA);
    if (mouse_any != 0) goto end_demo;

    /* Read demo data from ROM.
     * Demo data is in banks $10-$12, structured as 3 parallel streams
     * (buttons, Y displacement, X displacement) indexed by $04E6. */
    uint16_t data_idx = bus_wram_read16(0x04E6);
    uint16_t bank_offset = bus_wram_read16(0x04E8);

    /* Check for delay frames */
    uint16_t delay = bus_wram_read16(0x04EA);
    if (delay != 0) {
        /* Still in delay — clear mouse state */
        bus_wram_write8(0x04CA, 0x00);
        bus_wram_write8(0x04C8, 0x00);
        bus_wram_write8(0x04C6, 0x00);
        bus_wram_write16(0x04EA, delay - 1);
        goto advance;
    }

    /* Read next demo frame */
    {
        uint8_t bank1 = 0x10 + (uint8_t)bank_offset;
        uint8_t bank2 = 0x11 + (uint8_t)bank_offset;
        uint8_t bank3 = 0x12 + (uint8_t)bank_offset;

        uint8_t cmd = bus_read8(bank1, 0x8000 + data_idx);

        if (cmd & 0x80) {
            /* Delay command */
            uint8_t delay_y = bus_read8(bank2, 0x8000 + data_idx);
            uint8_t delay_x = bus_read8(bank3, 0x8000 + data_idx);
            bus_wram_write8(0x04EB, delay_y);
            bus_wram_write16(0x04EA, (uint16_t)delay_x);
            bus_wram_write8(0x04CA, 0x00);
            bus_wram_write8(0x04C8, 0x00);
            bus_wram_write8(0x04C6, 0x00);
        } else {
            /* Normal frame: buttons, Y disp, X disp */
            bus_wram_write8(0x04CA, cmd);
            bus_wram_write8(0x04C8, bus_read8(bank2, 0x8000 + data_idx));
            bus_wram_write8(0x04C6, bus_read8(bank3, 0x8000 + data_idx));
        }
    }

advance:
    /* Advance data pointer */
    {
        uint16_t idx = bus_wram_read16(0x04E6) + 1;
        idx &= 0x7FFF;
        if (idx == 0) {
            /* Bank boundary — flip bank offset */
            uint16_t bo = bus_wram_read16(0x04E8);
            bo ^= 0x0003;
            bus_wram_write16(0x04E8, bo);
        }
        bus_wram_write16(0x04E6, idx);

        /* Check for end of demo data */
        uint8_t demo_num = bus_wram_read8(0x0008);
        static const uint16_t demo_ends[5] = {
            0x1938, 0x3631, 0x56CD, 0x5B4D, 0x7E4D
        };
        if (demo_num < 5 && idx >= demo_ends[demo_num]) {
            goto end_demo;
        }
    }
    return;

end_demo:
    /* End demo — advance to next demo, fade out, reset */
    {
        uint8_t demo_num = bus_wram_read8(0x0008);
        demo_num++;
        if (demo_num >= 5) demo_num = 0;
        bus_wram_write8(0x0008, demo_num);

        bus_wram_write16(0x04E2, 0x0000);  /* Demo no longer active */

        mp_01E7C9();  /* Fade out */

        /* Jump to soft reset (CODE_0082B8) */
        func_table_call(0x0082B8);
    }
}

/* ========================================================================
 * $0F:C00E — Start demo playback
 *
 * Initializes demo state and sets the demo active flag.
 * ======================================================================== */
void mp_0FC00E(void) {
    bus_wram_write16(0x0002, 0x0000);
    bus_wram_write16(0x04E8, 0x0000);
    bus_wram_write16(0x04EA, 0x0000);

    /* Look up demo start offset */
    uint8_t demo_num = bus_wram_read8(0x0008);
    static const uint16_t demo_starts[5] = { 0, 0x1937, 0x3630, 0x56CC, 0x5B4C };
    if (demo_num < 5) {
        bus_wram_write16(0x04E6, demo_starts[demo_num]);
    }

    bus_wram_write16(0x04E2, 0x0001);  /* Demo active */
}

/* ========================================================================
 * $00:8B40 — Wrapper: calls 008BEB (canvas click handler)
 * ======================================================================== */
void mp_008B40(void) {
    mp_008BEB();
}

/* ========================================================================
 * $00:8B44 — Wrapper: calls 009378 (cursor rendering)
 * ======================================================================== */
void mp_008B44(void) {
    mp_009378();
}

/* ========================================================================
 * Register all title loop and Bank 0F functions.
 * ======================================================================== */
void mp_register_titleloop(void) {
    func_table_register(0x018260, mp_018260);
    func_table_register(0x0182B1, mp_0182B1);
    func_table_register(0x0182F6, mp_0182F6);
    func_table_register(0x018308, mp_018308);
    func_table_register(0x018C43, mp_018C43);
    func_table_register(0x018CB7, mp_018CB7);
    func_table_register(0x018CBF, mp_018CBF);
    func_table_register(0x0FC000, mp_0FC000);
    func_table_register(0x0FC00E, mp_0FC00E);
    func_table_register(0x008BEB, mp_008BEB);
    func_table_register(0x008B40, mp_008B40);
    func_table_register(0x008B44, mp_008B44);
}
