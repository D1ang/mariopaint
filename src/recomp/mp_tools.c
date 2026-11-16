/*
 * Mario Paint — Drawing tools, palette selection, and undo.
 *
 * These routines handle the core canvas interaction:
 *   - Tool selection from toolbar
 *   - Palette color selection
 *   - Pencil drawing (main canvas draw routine)
 *   - Fill tool, stamp tool
 *   - Undo buffer (save/restore canvas)
 *   - Pen graphics loading
 *   - Tool state management
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $00:8B03 — Save canvas to undo buffer
 *
 * Copies $6000 bytes from the canvas buffer (the long pointer at DP $0A,
 * carrying into the next bank like [$0A],Y) to the undo buffer at $7F:A000.
 * ======================================================================== */
void mp_008B03(void) {
    op_rep(0x30);
    uint32_t canvas = dp_long_ptr(g_cpu.DP, 0x0A);
    for (int y = 0x5FFE; y >= 0; y -= 2)                    /* LDA [$0A],y */
        bus_write16(0x7F, (uint16_t)(0xA000 + y), read_long16(canvas + (uint32_t)y));
    g_cpu.X = 0xFFFE;
    g_cpu.Y = 0xFFFE;
}

/* ========================================================================
 * $00:8B18 — Restore canvas from undo buffer
 *
 * Copies $6000 bytes from the undo buffer ($7F:A000) back to
 * the canvas buffer.
 * ======================================================================== */
void mp_008B18(void) {
    op_rep(0x30);
    uint32_t canvas = dp_long_ptr(g_cpu.DP, 0x0A);
    for (int y = 0x5FFE; y >= 0; y -= 2)                    /* STA [$0A],y */
        write_long16(canvas + (uint32_t)y, bus_read16(0x7F, (uint16_t)(0xA000 + y)));
    g_cpu.X = 0xFFFE;
    g_cpu.Y = 0xFFFE;
}

/* ========================================================================
 * $00:8B29 — Clear $7F:0000-$5FFE (scratch buffer)
 * ======================================================================== */
void mp_008B29(void) {
    for (int x = 0x5FFE; x >= 0; x -= 2) {
        bus_write16(0x7F, x, 0x0000);
    }
}

/* ========================================================================
 * $00:96AB — Tool mode restore/set
 *
 * If current tool is stamp (6) or special (7), restores the
 * previous tool from $1B10. Then sets the tool mode combining
 * the low byte from $AC with the high byte from current $04D0.
 * ======================================================================== */
void mp_0096AB(void) {
    uint16_t tool = bus_wram_read16(0x04D0) & 0xFF;
    if (tool == 0x06 || tool == 0x07) {
        bus_wram_write16(0x04D0, bus_wram_read16(0x1B10));
    }

    uint16_t d0 = bus_wram_read16(0x04D0) & 0xFF00;
    uint8_t ac = bus_wram_read8(0x00AC);
    bus_wram_write16(0x04D0, d0 | ac);
}

/* ========================================================================
 * $00:96E1 — Pen graphics update
 *
 * Loads the pen's tile graphics for the current tool and colour:
 * index = XBA($A8) | $A6 into $B66C, then $B6F4 (with $198A for tool 8).
 * Tool 3 has two other paths: $0EB8 set ($9725: $B780 with $0EBC, $B66C
 * with $0EBE, $A451) and colour row >= $70 ($9714: $B6F4 with the index,
 * $B66C with $0D, $A451).
 * ======================================================================== */
void mp_0096E1(void) {
    uint16_t tool = bus_wram_read16(0x04D0) & 0x00FF;
    if (tool == 0x0003) {
        if (bus_wram_read16(0x0EB8) != 0) {                 /* 96EC -> 9725 */
            CPU_SET_A16(bus_wram_read16(0x0EBC));
            call_jsr(0x00B780);
            CPU_SET_A16(bus_wram_read16(0x0EBE));
            call_jsr(0x00B66C);
            tail_jmp(0x00A451);
            return;
        }
        if (bus_wram_read16(0x00A6) >= 0x0070) {           /* 96F1 -> 9714 */
            uint16_t a8 = bus_wram_read16(0x00A8);
            CPU_SET_A16((uint16_t)(((a8 << 8) | (a8 >> 8)) | bus_wram_read16(0x00A6)));
            call_jsr(0x00B6F4);
            CPU_SET_A16(0x000D);
            call_jsr(0x00B66C);
            tail_jmp(0x00A451);
            return;
        }
    }
    {                                                       /* 96F8 */
        uint16_t a8 = bus_wram_read16(0x00A8);
        CPU_SET_A16((uint16_t)(((a8 << 8) | (a8 >> 8)) | bus_wram_read16(0x00A6)));  /* XBA; ORA $A6 */
        call_jsr(0x00B66C);
        if ((bus_wram_read16(0x04D0) & 0x00FF) == 0x0008)
            CPU_SET_A16(bus_wram_read16(0x198A));          /* 970E */
        tail_jmp(0x00B6F4);                                 /* JMP $B6F4 */
    }
}

/* ========================================================================
 * $00:9853 — Toolbar: change drawing tool
 *
 * Changes the current drawing tool. Sends a sound command,
 * updates $04D0 with the new tool ID, refreshes pen graphics.
 * ======================================================================== */
void mp_009853(void) {
    /* Sound effect */
    op_lda_imm16(0x0004);
    func_table_call(0x01D368);

    /* Get the toolbar slot that was clicked */
    uint16_t slot = bus_wram_read16(0x00A8) & 0xFF;
    uint16_t a8_hi = slot << 8;

    /* Save previous tool for stamp mode */
    uint16_t cur_tool = bus_wram_read16(0x04D0);
    if ((cur_tool & 0xFF) == 0x07) {
        cur_tool = bus_wram_read16(0x1B10);
    }

    /* Set new tool mode */
    uint16_t new_d0 = (cur_tool & 0xFF) | a8_hi;
    bus_wram_write16(0x04D0, new_d0);

    /* Update pen graphics and palette display */
    mp_0096E1();
    mp_00E64F();
}

/* ========================================================================
 * $00:987B — Toolbar: change palette row (scroll up/down)
 *
 * Changes the current palette row. Left click = scroll up (+$10),
 * right click = scroll down (-$10). Wraps at boundaries.
 * ======================================================================== */
void mp_00987B(void) {
    int16_t delta = 0x0010;
    uint16_t buttons = bus_wram_read16(0x04CA);
    if (!(buttons & 0x0044)) return;  /* No left/right click */

    if (!(buttons & 0x0040)) {
        delta = -0x0010;  /* Right click = scroll down */
    }

    uint16_t pal = bus_wram_read16(0x00A6);
    pal += delta;
    if (pal == 0x0060) pal += delta;  /* Skip row $60 */
    pal &= 0x00F0;
    bus_wram_write16(0x00A6, pal);

    /* Sound effect based on palette row */
    uint16_t sound = (pal >> 4);
    if (sound >= 6) sound--;
    sound += 0x0062;
    CPU_SET_A16(sound);
    func_table_call(0x01D368);

    /* Update tool display */
    uint16_t d0 = bus_wram_read16(0x04D0) & 0xFF00;
    bus_wram_write16(0x00A8, d0);
    mp_0096E1();
    mp_00A363();
}

/* ========================================================================
 * $00:98C3 — Palette: select tool from palette bar
 *
 * Selects a drawing tool from the bottom palette/tool bar.
 * ======================================================================== */
void mp_0098C3(void) {
    /* Sound */
    op_lda_imm16(0x0001);
    func_table_call(0x01D368);

    /* Clear spray/erase state */
    bus_wram_write16(0x00B8, 0x0000);
    bus_wram_write16(0x1992, 0x0000);

    uint16_t page = bus_wram_read16(0x19FA);
    if (page != 0) {
        uint8_t ac = bus_wram_read8(0x00AC);
        bus_wram_write8(0x00AC, ac - 1);
    }

    /* Update tool state */
    mp_0096AB();

    /* Push args for tool change: tool_mode, tool_id, flag */
    uint16_t d0 = bus_wram_read16(0x04D0) & 0xFF;
    op_pha16(); bus_wram_write16(g_cpu.S + 1, d0);
    uint16_t aa = bus_wram_read16(0x00AA);
    op_pha16(); bus_wram_write16(g_cpu.S + 1, aa);
    op_lda_imm16(0x0001);
    op_pha16(); bus_wram_write16(g_cpu.S + 1, 0x0001);
    mp_009FC4();
    g_cpu.S += 6;  /* Clean stack */

    uint16_t a8 = bus_wram_read16(0x04D0) & 0xFF00;
    bus_wram_write16(0x00A8, a8);
    mp_0096E1();
    mp_00A363();
}

/* ========================================================================
 * $00:98F8 — Palette: select stamp tool
 *
 * Selects the stamp tool (tool $03) from the palette bar.
 * ======================================================================== */
void mp_0098F8(void) {
    op_lda_imm16(0x0001);
    func_table_call(0x01D368);

    bus_wram_write16(0x00B8, 0x0000);
    bus_wram_write16(0x1992, 0x0000);

    bus_wram_write8(0x00AC, 0x03);
    mp_0096AB();

    /* Push tool change args */
    op_lda_imm16(0x0003);
    op_pha16(); bus_wram_write16(g_cpu.S + 1, 0x0003);
    uint16_t aa = bus_wram_read16(0x00AA);
    op_pha16(); bus_wram_write16(g_cpu.S + 1, aa);
    op_lda_imm16(0x0001);
    op_pha16(); bus_wram_write16(g_cpu.S + 1, 0x0001);
    mp_009FC4();
    g_cpu.S += 6;

    bus_wram_write16(0x00A8, bus_wram_read16(0x04D0) & 0xFF00);
    mp_0096E1();
    mp_00A363();
}

/* ========================================================================
 * $00:9C25 — Pencil/drawing tool handler
 *
 * Core drawing routine. Handles:
 *   - First click: save cursor pos, start drawing
 *   - Drag: track cursor movement, draw pixels
 *   - Release: end drawing, play sound
 *
 * Updates draw state at DP $20 and cursor tracking at DP $22-$28.
 * ======================================================================== */
void mp_009C25(void) {
    uint16_t ae = bus_wram_read16(0x00AE);
    if (ae != 0) {
        func_table_call(0x009CC7);  /* Alternate draw mode */
        return;
    }

    bus_wram_write16(0x0050, 0x0000);

    uint16_t line_mode = bus_wram_read16(0x09D4);
    if (line_mode != 0) {
        /* Line drawing mode */
        uint8_t draw = bus_wram_read8(0x0020);
        if (draw == 0) {
            bus_wram_write16(0x00BA, 0x0001);
            bus_wram_write16(0x00BC, 0x0000);
            bus_wram_write16(0x00BE, 0x0000);
            /* Save start position */
            bus_wram_write16(0x0022, bus_wram_read16(0x04DC));
            bus_wram_write16(0x0026, bus_wram_read16(0x04DC));
            bus_wram_write16(0x0024, bus_wram_read16(0x04DE));
            bus_wram_write16(0x0028, bus_wram_read16(0x04DE));
        } else {
            /* Update line endpoint */
            bus_wram_write16(0x0022, bus_wram_read16(0x0026));
            bus_wram_write16(0x0024, bus_wram_read16(0x0028));
            bus_wram_write16(0x0026, bus_wram_read16(0x04DC));
            bus_wram_write16(0x0028, bus_wram_read16(0x04DE));
        }

        uint16_t buttons = bus_wram_read16(0x04CA);
        uint8_t draw_state = (buttons & 0x0010) ? 1 : 0;
        bus_wram_write8(0x0020, draw_state);
        func_table_call(0x00AAEF);  /* Execute line draw */
        return;
    }

    /* Normal drawing (not line mode) */
    uint8_t draw = bus_wram_read8(0x0020);
    if (draw == 0) {
        bus_wram_write16(0x00BA, 0x0001);
        bus_wram_write16(0x00BC, 0x0000);
        bus_wram_write16(0x00BE, 0x0000);
    }

    /* Check cursor movement for drawing */
    if (draw != 0) {
        uint16_t movement = bus_wram_read16(0x1B26);
        if (movement & 0x0002) {
            /* Cursor moved while drawing */
            func_table_call(0x009D7D);
        } else if (!(movement & 0x0001)) {
            /* No movement — check for continuous draw sound */
            uint16_t tool = bus_wram_read16(0x04D0);
            if (tool != 0x0007) {
                op_lda_imm16(0x0001);
                func_table_call(0x01D348);
            }
        }
    } else {
        func_table_call(0x009D7D);  /* Initial draw stroke */
    }

    /* Check button state */
    uint16_t buttons = bus_wram_read16(0x04CA);
    uint8_t new_draw = (buttons & 0x0010) ? 1 : 0;
    if (!(buttons & 0x0010)) {
        /* Button released — end draw, play sound */
        op_lda_imm16(0x0018);
        func_table_call(0x01D368);
    }
    bus_wram_write8(0x0020, new_draw);

    /* Save cursor position for next frame */
    bus_wram_write16(0x0022, bus_wram_read16(0x04DC));
    bus_wram_write16(0x0024, bus_wram_read16(0x04DE));

    func_table_call(0x00AAEF);  /* Pixel plot */
}

/* ========================================================================
 * $00:9DAB — Fill tool handler
 *
 * Initiates a flood fill at the cursor position.
 * ======================================================================== */
void mp_009DAB(void) {
    /* Save cursor position */
    bus_wram_write16(0x0086, bus_wram_read16(0x04DC));
    bus_wram_write16(0x0088, bus_wram_read16(0x04DE));

    /* Get fill color from tool high byte */
    uint16_t fill_idx = (bus_wram_read16(0x04D0) >> 8) & 0x0F;
    /* Look up fill parameter from ROM table */
    uint8_t fill_param = bus_read8(0x00, 0x9DEB + fill_idx);
    bus_wram_write16(0x006E, fill_param);

    /* Sound */
    op_lda_imm16(0x0025);
    func_table_call(0x01D368);

    /* Set audio state for fill */
    bus_wram_write16(0x053B, 0x0004);
    bus_wram_write16(0x053D, 0x0000);
    bus_wram_write16(0x053F, 0x0003);

    /* Execute fill */
    func_table_call(0x00B3FF);

    /* Wait 8 frames */
    for (int i = 8; i > 0 && !g_quit; i--) {
        mp_01E2CE();
    }
}

/* ========================================================================
 * $00:9DFB — Stamp tool handler
 *
 * Initiates stamp placement at the cursor position.
 * Sets up stamp boundaries and draws the stamp.
 * ======================================================================== */
void mp_009DFB(void) {
    bus_wram_write16(0x1996, 0x0000);

    /* Set up stamp bounds from canvas bounds with offsets */
    bus_wram_write16(0x19B6, bus_wram_read16(0x19AE) + 4);
    bus_wram_write16(0x19B8, bus_wram_read16(0x19B0) + 3);
    bus_wram_write16(0x19BA, bus_wram_read16(0x19B2) - 1);
    bus_wram_write16(0x19BC, bus_wram_read16(0x19B4) - 2);

    uint8_t draw = bus_wram_read8(0x0020);
    if (draw == 0) {
        /* First click: start stamp placement */
        op_lda_imm16(0x0026);
        func_table_call(0x01D368);

        bus_wram_write16(0x053B, 0x0004);
        bus_wram_write16(0x053D, 0x0000);
        bus_wram_write16(0x053F, 0x0003);
        bus_wram_write16(0x1998, 0x0000);
        bus_wram_write16(0x199A, 0x0000);

        uint16_t mode19c0 = bus_wram_read16(0x19C0);
        if (mode19c0 == 0) {
            /* Normal stamp — save cursor as start position */
            bus_wram_write16(0x0022, bus_wram_read16(0x04DC));
            bus_wram_write16(0x0026, bus_wram_read16(0x04DC));
            bus_wram_write16(0x0024, bus_wram_read16(0x04DE));
            bus_wram_write16(0x0028, bus_wram_read16(0x04DE));
        } else {
            /* Constrained stamp — use bounds */
            bus_wram_write16(0x0022, bus_wram_read16(0x19B6));
            bus_wram_write16(0x0024, bus_wram_read16(0x19B8));
        }
    }

    /* Continue stamp logic via sub-routines */
    func_table_call(0x009E76);
}

/* ========================================================================
 * $00:A363 — Tool palette/graphics display update
 *
 * Queues a DMA transfer to update the tool palette display on screen.
 * Reads tile data from the appropriate source (ROM or SRAM) based on
 * the current palette row and tool state.
 * ======================================================================== */
void mp_00A363(void) {
    bus_wram_write16(0x0202, 0x0000);
    uint16_t wp = bus_wram_read16(0x0204);

    /* Copy base DMA record from DATA_00A443 (ROM at $00:A443) */
    for (int y = 0; y < 10; y += 2) {
        uint16_t val = bus_read16(0x00, 0xA443 + y);
        bus_wram_write16(0x0182 + wp + y, val);
    }

    uint16_t ab = bus_wram_read16(0x09AB);
    if (ab != 0) {
        /* Special mode — skip to end */
        goto finish;
    }

    /* Determine source address for palette graphics */
    {
        uint16_t erase = bus_wram_read16(0x1992);
        if (erase != 0) {
            /* Erase tool — use fixed source */
            bus_wram_write16(0x0183 + wp, 0xB000);  /* DATA_058000+$3000 */
            goto finish;
        }

        uint16_t eb8 = bus_wram_read16(0x0EB8);
        uint16_t ebe = bus_wram_read16(0x0EBE);
        if (eb8 != 0 && (int16_t)eb8 >= 0) {
            /* eb8 positive and nonzero — use alternate */
            goto finish;
        }

        uint16_t pal;
        if ((int16_t)eb8 < 0) {
            pal = ebe & 0x00F0;
        } else {
            uint16_t state = bus_wram_read16(0x09D6);
            if (state == 0x001E) {
                /* Music tool */
                bus_wram_write16(0x0183 + wp, 0xF000);  /* DATA_078000+$7000 */
                op_sep(0x20);
                bus_wram_write8(0x0185 + wp, 0x07);
                op_rep(0x20);
                goto finish;
            }
            pal = bus_wram_read16(0x00A6);
        }

        if (pal == 0x00F0) {
            /* Custom stamp palette */
            bus_wram_write16(0x0183 + wp, 0x1A58);
            op_sep(0x20);
            bus_wram_write8(0x0185 + wp, 0x00);  /* Bank $00 (WRAM) */
            op_rep(0x20);
        } else if (pal == 0x0100) {
            /* Special stamps */
            bus_wram_write16(0x0183 + wp, 0xE800);
            op_sep(0x20);
            bus_wram_write8(0x0185 + wp, 0x07);
            op_rep(0x20);
        } else {
            /* Standard ROM palette graphics */
            uint16_t addr = (pal >> 1) + 0x8000;  /* DATA_058000 base */
            bus_wram_write16(0x0183 + wp, addr);
        }
    }

finish:
    /* Finish building DMA record and mark pending */
    bus_wram_write16(0x0204, wp + 9);
    bus_wram_write16(0x0202, 0x0001);
}

/* ========================================================================
 * $00:F921 — Palette row multiplier init
 *
 * Computes a multiplicative palette display value and stores
 * in $0E9C, $0C2A, $0C2C.
 *
 * The 24-bit multiplier is region-specific: $323819 in the Japan/USA ROM,
 * $3C4351 in the European one (x1.2 = 60 Hz / 50 Hz, a PAL timing scale).
 * It is read from the ROM's own immediates (LDA #lo at $F93D, LDA #hi at
 * $F947) so either region computes what its own code does.
 * ======================================================================== */
void mp_00F921(void) {
    uint16_t a = (uint16_t)(CPU_A16() + 0x000E);         /* CLC; ADC #$0E */
    a = (uint16_t)((a << 8) | (a >> 8));                    /* XBA */
    bus_wram_write16(0x0E9C, a);
    const uint32_t mul = ((uint32_t)bus_read16(0x00, 0xF948) << 16) |
                         bus_read16(0x00, 0xF93E);
    bus_wram_write16(0x0C2A, 0x0000);
    bus_wram_write16(0x0C2C, 0x0000);

    for (int i = 8; i > 0; i--) {
        /* Shift left $0C2A:$0C2C */
        uint32_t val = ((uint32_t)bus_wram_read16(0x0C2C) << 16) |
                       bus_wram_read16(0x0C2A);
        val <<= 1;

        /* If carry from $0E9C shift, add constant */
        uint16_t e9c = bus_wram_read16(0x0E9C);
        bus_wram_write16(0x0E9C, e9c << 1);
        if (e9c & 0x8000) {
            val += mul;
        }

        bus_wram_write16(0x0C2A, (uint16_t)(val & 0xFFFF));
        bus_wram_write16(0x0C2C, (uint16_t)(val >> 16));
    }
}

/* ========================================================================
 * Pen tile transforms — $01:D56D / $01:D597 and the modes they dispatch
 *
 * The 16x16 pen is four 4bpp tiles of $20 bytes (top-left, top-right,
 * bottom-left, bottom-right). Mode = $0997 (0 while the eraser menu,
 * $1992, is up), via the table at $D587/$D5B1:
 *   0 $D5C1 copy             4 $D6A1 rotate (ASL/ROR)
 *   1 $D5CF mirror left-right 5 $D6E8 rotate (LSR/ROL)
 *   2 $D625 mirror up-down    6 $D72F 4 then 2
 *   3 $D68B 1 then 2          7 $D745 5 then 2
 * The two-step modes go through a scratch pen at $1D2A, as the ROM does.
 * ======================================================================== */
#define PEN_SCRATCH 0x1D2A

static void pen_copy(uint16_t src, uint16_t dst) {             /* $D5C1 */
    for (int y = 0x7E; y >= 0; y -= 2)
        bus_wram_write16(dst + y, bus_wram_read16(src + y));
}

static void pen_mirror_lr(uint16_t src, uint16_t dst) {        /* $D5CF */
    /* Swap the left and right tiles, bit-reversing every byte with the
     * table at $02:F000. Rows run $1F down to 0, as in the ROM. */
    for (int y = 0x1F; y >= 0; y--) {
        bus_wram_write8(dst + 0x20 + y, bus_read8(0x02, 0xF000 + bus_wram_read8(src + y)));
        bus_wram_write8(dst + y,        bus_read8(0x02, 0xF000 + bus_wram_read8(src + 0x20 + y)));
        bus_wram_write8(dst + 0x60 + y, bus_read8(0x02, 0xF000 + bus_wram_read8(src + 0x40 + y)));
        bus_wram_write8(dst + 0x40 + y, bus_read8(0x02, 0xF000 + bus_wram_read8(src + 0x60 + y)));
    }
}

static void pen_mirror_ud(uint16_t src, uint16_t dst) {        /* $D625 */
    /* Swap top and bottom tiles and reverse the 8 rows inside each
     * bitplane pair: rows are read $0E,$0C..$00 then $1E..$10 while the
     * destination pointers step forward. */
    uint16_t out = 0;
    int y = 0x0E;
    for (;;) {
        bus_wram_write16(dst + 0x40 + out, bus_wram_read16(src + y));
        bus_wram_write16(dst + 0x60 + out, bus_wram_read16(src + 0x20 + y));
        bus_wram_write16(dst + out,        bus_wram_read16(src + 0x40 + y));
        bus_wram_write16(dst + 0x20 + out, bus_wram_read16(src + 0x60 + y));
        out += 2;
        y -= 2;
        if (y >= 0x10) continue;                                   /* D66F */
        if (y == 0x0E) break;                                      /* D674 */
        if (y >= 0) continue;                                      /* D679 */
        y = 0x1E;                                                  /* D67E */
    }
}

/* Modes 4/5: rotate by a quarter turn. Each source word gives one bit to
 * each of 16 consecutive destination bytes; the high and low byte take
 * turns (XBA), most significant bit first (ASL into ROR) for mode 4, least
 * significant first (LSR into ROL) for mode 5. Every 8 words the
 * destination moves to the offset in the table. */
static void pen_rotate(uint16_t src, uint16_t dst, uint16_t table, bool msb_first) {
    uint16_t ptr = dst;
    for (int y = 0x7E; y >= 0; y -= 2) {
        if ((y & 0x0F) == 0x0E)
            ptr = (uint16_t)(dst + bus_read8(0x01, (uint16_t)(table + (y >> 4))));
        uint16_t w = bus_wram_read16(src + y);
        uint8_t cur = (uint8_t)(w >> 8), other = (uint8_t)w;      /* XBA */
        for (int k = 0x0F; k >= 0; k--) {
            uint8_t d = bus_wram_read8(ptr + k);
            if (msb_first) {
                uint8_t bit = cur >> 7; cur = (uint8_t)(cur << 1);   /* ASL */
                d = (uint8_t)((d >> 1) | (bit << 7));                /* ROR */
            } else {
                uint8_t bit = cur & 1; cur >>= 1;                    /* LSR */
                d = (uint8_t)((d << 1) | bit);                       /* ROL */
            }
            bus_wram_write8(ptr + k, d);
            uint8_t t = cur; cur = other; other = t;                 /* XBA */
        }
    }
}

/* What A holds on return matters: $B66C returns it, and $96E1 passes it on
 * to $B6F4 as that routine's input. Modes 0/4/5 end with PLA x2/x3, leaving
 * the source pointer; the others end with TSC; ADC #$10; TCS, leaving the
 * stack pointer as it is at the RTL (= S on entry from a JSL). */
static void pen_tile_transform(uint16_t src, uint16_t dst) {
    uint16_t entry_s = g_cpu.S;
    uint16_t mode = bus_wram_read16(0x1992) ? 0 : bus_wram_read16(0x0997);
    uint16_t a = entry_s;
    g_cpu.X = (uint16_t)(mode << 1);                            /* ASL; TAX */
    switch (mode) {
    case 0: pen_copy(src, dst); a = src; g_cpu.Y = 0xFFFE; break;
    case 1: pen_mirror_lr(src, dst); g_cpu.Y = 0x00FF; break;
    case 2: pen_mirror_ud(src, dst); g_cpu.Y = 0x000E; break;
    case 3: pen_mirror_lr(src, PEN_SCRATCH); pen_mirror_ud(PEN_SCRATCH, dst); g_cpu.Y = 0x000E; break;
    case 4: pen_rotate(src, dst, 0xD6E0, true); a = src; g_cpu.Y = 0xFFFE; break;
    case 5: pen_rotate(src, dst, 0xD727, false); a = src; g_cpu.Y = 0xFFFE; break;
    case 6: pen_rotate(src, PEN_SCRATCH, 0xD6E0, true); pen_mirror_ud(PEN_SCRATCH, dst); g_cpu.Y = 0x000E; break;
    case 7: pen_rotate(src, PEN_SCRATCH, 0xD727, false); pen_mirror_ud(PEN_SCRATCH, dst); g_cpu.Y = 0x000E; break;
    default: break;   /* the ROM would jump through bytes past its table */
    }
    g_cpu.flag_M = false;   /* every mode returns with 16-bit A/X/Y (REP #$30) */
    g_cpu.flag_X = false;
    CPU_SET_A16(a);
    cpu_update_nz16(a);
    g_cpu.flag_C = false;
}

/* ========================================================================
 * $01:D56D — Pen tile transform (foreground: $1BAA → $1CAA)
 *
 * Transforms pen tile data from raw format to drawing format.
 * ======================================================================== */
void mp_01D56D(void) {
    pen_tile_transform(0x1BAA, 0x1CAA);
}

/* ========================================================================
 * $01:D597 — Pen tile transform (background: $1B2A → $1C2A)
 *
 * Transforms pen tile data from raw format to drawing format.
 * This populates $1C2A which is read by B25E during pixel plotting.
 * ======================================================================== */
void mp_01D597(void) {
    pen_tile_transform(0x1B2A, 0x1C2A);
}

/* ========================================================================
 * Register all drawing tool functions.
 * ======================================================================== */
void mp_register_tools(void) {
    func_table_register(0x008B03, mp_008B03);
    func_table_register(0x008B14, mp_008B18);  /* Wrapper */
    func_table_register(0x008B18, mp_008B18);
    func_table_register(0x008B29, mp_008B29);
    func_table_register(0x0096AB, mp_0096AB);
    func_table_register(0x0096E1, mp_0096E1);
    func_table_register(0x009853, mp_009853);
    func_table_register(0x00987B, mp_00987B);
    func_table_register(0x0098C3, mp_0098C3);
    func_table_register(0x0098F8, mp_0098F8);
    func_table_register(0x009C25, mp_009C25);
    func_table_register(0x009DAB, mp_009DAB);
    func_table_register(0x009DFB, mp_009DFB);
    func_table_register(0x00A363, mp_00A363);
    func_table_register(0x00F921, mp_00F921);
    func_table_register(0x01D56D, mp_01D56D);
    func_table_register(0x01D597, mp_01D597);
}
