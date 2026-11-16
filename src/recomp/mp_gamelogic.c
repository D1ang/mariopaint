/*
 * Mario Paint — Recompiled game logic dispatch and cursor rendering.
 *
 * Key routines:
 *   $008683 — Game logic dispatch (toolbar show/hide timer)
 *   $0087A8 — Post-logic dispatch (jump table on game state $09D6)
 *   $00878F — Animation frame check
 *   $009378 — Cursor sprite rendering (reads sprite frames from ROM)
 *   $01E8F6 — Empty tile row optimization check
 *   $01E393 — Indexed subroutine dispatch helper
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <snesrecomp/snesrecomp.h>

/* OAM buffer addresses */
#define OAM_BUF       0x0226
#define OAM_HI_BUF    0x0426

/* ========================================================================
 * $01:E8F6 — Empty tile row optimization check
 *
 * Checks if the current palette/tool state means we can skip certain
 * rendering. Sets $0567 based on empty tile row flags in $0569.
 * ======================================================================== */
void mp_01E8F6(void) {
    bus_wram_write16(0x0567, 0x0000);

    uint16_t eb8 = bus_wram_read16(0x0EB8);
    if (eb8 != 0) {
        uint16_t d0 = bus_wram_read16(0x04D0);
        if (d0 == 0x0007) goto done;

        uint16_t ebe = bus_wram_read16(0x0EBE);
        if (ebe < 0x00F0) goto done;

        uint16_t idx = ebe & 0x000F;
        idx *= 2;
        bus_wram_write16(0x0567, bus_wram_read16(0x0569 + idx));
        return;
    }

    /* No $0EB8 active */
    uint16_t palette = bus_wram_read16(0x00A6);  /* CurrentPaletteRow */
    if (palette != 0x00F0) goto done;

    uint16_t d0 = bus_wram_read16(0x04D0) & 0x00FF;
    if (d0 == 0x0008) goto use_d0;
    if (d0 >= 0x0006) goto done;

use_d0:
    {
        uint16_t hi = (bus_wram_read16(0x04D0) >> 8) & 0xFF;
        uint16_t idx = hi * 2;
        bus_wram_write16(0x0567, bus_wram_read16(0x0569 + idx));
    }
    return;

done:
    return;
}

/* ========================================================================
 * $01:E393 — Indexed subroutine dispatch helper
 *
 * Takes A (8-bit index), reads a 16-bit pointer from the address
 * on stack + index*2+1, stores it. Used for indexed jump tables.
 *
 * This is a complex stack-manipulation routine. In practice, it's
 * used for animation frame dispatch. We implement it as a helper
 * that reads from ROM.
 * ======================================================================== */
void mp_01E393(void) {
    /* This routine is called with A = index and a pointer on the stack.
     * In practice, the callers that use this are complex animation
     * dispatchers. For now, register it so func_table_call doesn't no-op,
     * but the actual behavior depends on calling context. */
}

/* ========================================================================
 * $00:8683 — Game logic dispatch (toolbar visibility timer)
 *
 * Manages the toolbar show/hide state based on cursor activity.
 * When the cursor is active ($04CA buttons or $1B26 movement),
 * resets the idle timer ($09A1). When idle, runs toolbar
 * show/hide animations via the sprite engine.
 *
 * Also handles the special cursor mode ($05A7 "cursor tool")
 * which shows/hides a toolbar sprite.
 * ======================================================================== */
void mp_008683(void) {
    /* Check if mouse is active (buttons or movement) */
    uint16_t buttons = bus_wram_read16(0x04CA);
    if (buttons != 0) goto active;

    uint16_t movement = bus_wram_read16(0x1B26);
    if (!(movement & 0x0001)) goto inactive;

active:
    /* Check cursor tool mode */
    {
        uint8_t cursor_tool = bus_wram_read8(0x05A7);
        if (cursor_tool == 0) goto reset_timer;

        /* Cursor tool active: show toolbar sprite */
        bus_wram_write16(0x04CA, 0x0000);  /* Clear button state */
        op_sep(0x20);
        bus_wram_write8(0x05A6, 0x01);
        op_rep(0x20);

        /* JSL CODE_01962C — animate toolbar sprite (#$27) */
        op_lda_imm16(0x0027);
        func_table_call(0x01962C);

        /* JSL CODE_01F91E — draw toolbar sprite at (D8, CA) */
        g_cpu.X = 0x00D8;
        g_cpu.Y = 0x00CA;
        op_lda_imm16(0x0195);
        func_table_call(0x01F91E);
    }

reset_timer:
    /* Reset idle timer to $0258 frames (~10 sec at 60fps) */
    bus_wram_write16(0x09A1, 0x0258);
    return;

inactive:
    /* Check if cursor tool sprite needs cleanup */
    {
        uint8_t flag = bus_wram_read8(0x05A6);
        if (flag != 0) goto active;  /* Still showing, run show logic */
    }

    /* Check current tool mode for toolbar behavior */
    {
        uint16_t d0 = bus_wram_read16(0x04D0);
        if (d0 == 0x0006 || d0 == 0x0007) goto done;

        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if (eb8 == 0x0001) goto done;

        /* Look up toolbar behavior from DATA_00877F */
        uint8_t tool = bus_wram_read8(0x00AA);  /* Current tool index */
        /* Read byte from ROM data table at $00:877F + tool */
        uint8_t behavior = bus_read8(0x00, 0x877F + tool);
        bus_wram_write16(0x09A5, behavior & 0xFF);

        if (behavior == 0xFF) goto done;
    }

    /* Idle timer logic */
    {
        uint16_t timer = bus_wram_read16(0x09A1);
        if (timer == 0) goto timer_expired;
        if ((int16_t)timer < 0) goto timer_negative;
        bus_wram_write16(0x09A1, timer - 1);
        if (timer - 1 != 0) goto done;
    }

timer_expired:
    /* Timer just hit zero — start toolbar hide animation */
    {
        /* JSL CODE_01CDE1 with pointer to DATA_01CFB4 */
        op_lda_imm16(0xCFB4);  /* DATA_01CFB4 */
        func_table_call(0x01CDE1);

        uint16_t a5 = bus_wram_read16(0x09A5);
        if (a5 == 0x0002) goto show_toolbar_sprite;

        /* Check palette row for tool color bar */
        uint16_t palette = bus_wram_read16(0x00A6);
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if ((int16_t)eb8 < 0) {
            palette = bus_wram_read16(0x0EBE);
        }
        if (palette >= 0x00E0) goto skip_color_bar;

        /* Show color bar sprite (#$26) */
        op_lda_imm16(0x0026);
        func_table_call(0x019DFE);

skip_color_bar:
        a5 = bus_wram_read16(0x09A5);
        if (a5 == 0) goto dec_timer;
    }

show_toolbar_sprite:
    /* Show toolbar sprite (#$27) */
    op_lda_imm16(0x0027);
    func_table_call(0x019DFE);

dec_timer:
    bus_wram_write16(0x09A1, bus_wram_read16(0x09A1) - 1);

timer_negative:
    /* Timer is negative — animate toolbar sprites */
    {
        uint16_t a5 = bus_wram_read16(0x09A5);
        if (a5 == 0x0002) goto animate_toolbar;

        uint16_t palette = bus_wram_read16(0x00A6);
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if ((int16_t)eb8 < 0) {
            palette = bus_wram_read16(0x0EBE);
        }
        if (palette >= 0x00E0) goto check_toolbar;

        /* Animate color bar sprite */
        op_lda_imm16(0x0026);
        func_table_call(0x01962C);
        g_cpu.X = 0x00EC;
        g_cpu.Y = 0x0007;
        op_lda_imm16(0x0194);
        func_table_call(0x01F91E);

check_toolbar:
        a5 = bus_wram_read16(0x09A5);
        if (a5 == 0) goto done;
    }

animate_toolbar:
    /* Animate toolbar sprite */
    op_lda_imm16(0x0027);
    func_table_call(0x01962C);
    g_cpu.X = 0x00D8;
    g_cpu.Y = 0x00CA;
    op_lda_imm16(0x0195);
    func_table_call(0x01F91E);

done:
    return;
}

/* ========================================================================
 * $00:878F — Animation frame check
 *
 * Checks if animation is still running. Decrements $0545
 * and returns carry set when animation is done (or buttons pressed).
 * ======================================================================== */
void mp_00878F(void) {
    if (bus_wram_read16(0x04CA) != 0) goto done;

    uint16_t cy = bus_wram_read16(0x04DE);  /* Cursor Y */
    if (cy >= 0x00CC) goto done;

    uint16_t timer = bus_wram_read16(0x0545);
    timer--;
    bus_wram_write16(0x0545, timer);
    if (timer == 0) goto done;

    /* Animation still running */
    g_cpu.flag_C = false;
    return;

done:
    bus_wram_write16(0x0545, 0x0000);
    g_cpu.flag_C = true;
}

/* ========================================================================
 * $00:87A8 — Post-logic dispatch (game state jump table)
 *
 * Dispatches to the handler for the current game state ($09D6).
 * State 0 = main drawing canvas, other states = various tools,
 * menus, minigames, etc.
 *
 * Jump table has 31 entries. Most target complex routines that
 * aren't recompiled yet — they'll be no-ops via func_table_call.
 * ======================================================================== */
void mp_0087A8(void) {
    /* Read game state and dispatch */
    uint16_t state = bus_wram_read16(0x09D6);

    /* Jump table addresses from DATA_0087B0 */
    static const uint32_t dispatch_table[31] = {
        0x008BEB,  /*  0: Main canvas (click handler) */
        0x00C900,  /*  1: */
        0x00CC10,  /*  2: */
        0x00C460,  /*  3: */
        0x00C4D6,  /*  4: */
        0x00BBD8,  /*  5: */
        0x00CF9B,  /*  6: */
        0x00D03E,  /*  7: */
        0x00D069,  /*  8: */
        0x00D2F6,  /*  9: */
        0x00D321,  /* 10: */
        0x00D505,  /* 11: */
        0x00D50D,  /* 12: */
        0x0094E5,  /* 13: (RTS — no-op) */
        0x0094E5,  /* 14: (RTS — no-op) */
        0x0094E5,  /* 15: (RTS — no-op) */
        0x0094E5,  /* 16: (RTS — no-op) */
        0x0094E5,  /* 17: (RTS — no-op) */
        0x0094E5,  /* 18: (RTS — no-op) */
        0x0094E5,  /* 19: (RTS — no-op) */
        0x0094E5,  /* 20: (RTS — no-op) */
        0x00D8F1,  /* 21: */
        0x00DB59,  /* 22: */
        0x00DB84,  /* 23: */
        0x00DBE4,  /* 24: */
        0x00DC29,  /* 25: */
        0x00DC8B,  /* 26: */
        0x00DCC5,  /* 27: */
        0x00DD6D,  /* 28: */
        0x00E4B9,  /* 29: */
        0x00EC88,  /* 30: */
    };

    if (state < 31) {
        func_table_call(dispatch_table[state]);
    }
}

/* ========================================================================
 * $00:9378 — Cursor sprite
 *
 * Builds OAM entry $0226-$0229 (and its two high-table bits in $0426) for
 * the mouse cursor. The sprite is an animation from the table at $94E6,
 * picked by tool ($04D0; +$0A for the stamp tool on the second page), or
 * #$0C (the plain hand) over the bars, the menus and the eraser screen.
 * Each animation record: X offset, Y offset (7-bit signed; bit 7 of X =
 * animate, of Y = large sprite), frame length, then tile numbers.
 * $1998 counts frames within a tile, $199A is the tile index; switching to
 * the hand saves them in $199C/$199E ($19A0 = saved) and switching back
 * restores them. $1996 is restored on exit (it is only a scratch flag).
 * ======================================================================== */

/* ASL; CMP #$80; ROR: sign-extend a 7-bit offset into 8 bits. */
static inline uint8_t sext7(uint8_t v) { return (uint8_t)((v & 0x7F) | ((v & 0x40) << 1)); }

void mp_009378(void) {
    call_jsl(0x01E8F6);
    uint8_t saved_p = cpu_get_p();                          /* PHP */
    uint16_t saved_1996 = bus_wram_read16(0x1996);          /* PHA */
    uint16_t a = bus_wram_read16(0x04D0);
    uint16_t cy = bus_wram_read16(0x04DE);

    bool hand_zone;                                         /* 9384-93A3 */
    if (cy < 0x001C) hand_zone = true;
    else if (bus_wram_read16(0x19FA) != 0 && bus_wram_read16(0x19C2) == 0x0002)
        hand_zone = cy >= 0x00BC;
    else
        hand_zone = cy >= 0x00C4;

    bool to_hand = hand_zone && bus_wram_read16(0x19AA) == 0 && bus_wram_read16(0x09D1) == 0;
    for (;;) {
        if (to_hand) {                                      /* 93AF */
            if (bus_wram_read16(0x19A0) == 0) {
                bus_wram_write16(0x199C, bus_wram_read16(0x1998));
                bus_wram_write16(0x199E, bus_wram_read16(0x199A));
                bus_wram_write16(0x19A0, (uint16_t)(bus_wram_read16(0x19A0) + 1));
            }
            a = 0x000C;
        }
        if (a == 0x000C) break;                             /* 93C6 */
        if (bus_wram_read16(0x0567) != 0) { to_hand = true; continue; }  /* 93CE */
        if (bus_wram_read16(0x19A0) != 0) {                 /* 93D0: restore */
            bus_wram_write16(0x1998, bus_wram_read16(0x199C));
            bus_wram_write16(0x199A, bus_wram_read16(0x199E));
            bus_wram_write16(0x19A0, 0x0000);
        }
        break;
    }
    if (a == 0x0006) {                                      /* 93E4: stamp */
        if (bus_wram_read16(0x0020) == 0) {
            bus_wram_write16(0x1996, 0x0000);
            bus_wram_write16(0x1998, 0x0000);
            bus_wram_write16(0x199A, 0x0000);
        }
        if (bus_wram_read16(0x19C0) != 0) a = (uint16_t)(a + 0x000A);
    }

    /* 93FF: $19A2 = high byte of A, then the record's frame count etc. */
    bus_wram_write16(0x19A2, (uint16_t)((a << 8) | (a >> 8)));
    uint16_t x = (uint16_t)((a & 0x00FF) << 1);
    g_cpu.X = x;
    uint8_t db = g_cpu.DB;
    uint16_t rec = bus_read16(db, (uint16_t)(0x94E6 + x));
    uint16_t next = bus_read16(db, (uint16_t)(0x94E8 + x));
    bus_wram_write16(0x19A3, (uint16_t)(next - rec - 3));   /* tile count */
#define REC(i) bus_read8(db, (uint16_t)(rec + (i)))
    bus_wram_write8(0x19A4, REC(2));                        /* frame length */
    bus_wram_write8(0x19A5, 0x00);

    /* X position, with the 9th bit in $19A5 bit 0. */
    uint8_t ox = sext7(REC(0));
    if (ox & 0x80) bus_wram_write8(0x19A5, 0xFF);           /* DEC $19A5 */
    uint16_t sx = (uint16_t)(ox + bus_wram_read8(0x04DC));
    bus_wram_write8(0x0226, (uint8_t)sx);
    uint8_t hi = (uint8_t)(bus_wram_read8(0x19A5) + bus_wram_read8(0x04DD) + (sx >> 8));
    bus_wram_write8(0x19A5, hi & 0x01);
    bus_wram_write8(0x1996, (uint8_t)((REC(0) & 0x80) | bus_wram_read8(0x1996)));

    /* Y position; bit 7 of the Y byte becomes the size bit ($19A5 bit 1). */
    uint8_t oy = sext7(REC(1));
    bus_wram_write8(0x0227, (uint8_t)(oy + bus_wram_read8(0x04DE)));
    uint8_t v = REC(1);
    uint8_t rol = (uint8_t)((v << 1) | (v >> 7));          /* CMP #$80; ROL */
    bus_wram_write8(0x19A5, (uint8_t)(((uint8_t)(rol << 1)) | bus_wram_read8(0x19A5)));

    uint16_t y = 1;
    if (rec == 0x9515 &&                                    /* 946C */
        (bus_wram_read8(0x0EB8) != 0 || bus_wram_read8(0x00A6) >= 0x70)) {
        bus_wram_write8(0x0228, 0x6C);                      /* 9480 */
        bus_wram_write8(0x0229, bus_wram_read8(0x09CF));
    } else {
        if (bus_wram_read8(0x19A3) != 0) {                  /* 948D: animate */
            if (bus_wram_read8(0x1998) >= bus_wram_read8(0x19A4)) {
                bus_wram_write8(0x1998, 0x00);
                bus_wram_write8(0x199A, (uint8_t)(bus_wram_read8(0x199A) + 1));
            }
            if (bus_wram_read8(0x199A) >= bus_wram_read8(0x19A3))
                bus_wram_write8(0x199A, 0x00);
            if (bus_wram_read8(0x1996) != 0)
                bus_wram_write8(0x1998, (uint8_t)(bus_wram_read8(0x1998) + 1));
            y = (uint16_t)((bus_wram_read16(0x199A) & 0x00FF) + 2);
        }
        y++;                                                /* 94C0 */
        bus_wram_write8(0x0228, REC(y));                    /* tile */
        bus_wram_write8(0x0229, (uint8_t)(bus_wram_read8(0x19A2) | bus_wram_read8(0x09CF)));
    }
#undef REC
    uint8_t h = bus_wram_read8(0x0426);                     /* 94CF */
    bus_wram_write8(0x0426, (uint8_t)((h & ~0x03) | (bus_wram_read8(0x19A5) & 0x03)));

    bus_wram_write16(0x1996, saved_1996);                   /* PLA x2; STA */
    g_cpu.Y = y;
    CPU_SET_A16(saved_1996);
    cpu_set_p(saved_p);                                     /* PLP */
}

/* ========================================================================
 * Register all game logic functions.
 * ======================================================================== */
void mp_register_gamelogic(void) {
    func_table_register(0x01E8F6, mp_01E8F6);
    func_table_register(0x008683, mp_008683);
    func_table_register(0x00878F, mp_00878F);
    func_table_register(0x0087A8, mp_0087A8);
    func_table_register(0x009378, mp_009378);
}
