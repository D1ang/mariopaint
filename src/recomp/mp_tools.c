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
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    uint16_t a6 = bus_read16(0x00, (uint16_t)(dp + 0xA6));  /* LDA $A6 (direct page) */
    uint16_t a8 = bus_read16(0x00, (uint16_t)(dp + 0xA8));
    uint16_t pick = (uint16_t)(((a8 << 8) | (a8 >> 8)) | a6);   /* LDA $A8; XBA; ORA $A6 */
    uint16_t tool = bus_read16(db, 0x04D0) & 0x00FF;
    if (tool == 0x0003) {
        if (bus_read16(db, 0x0EB8) != 0) {                  /* 96EC -> 9725 */
            CPU_SET_A16(bus_read16(db, 0x0EBC));
            call_jsr(0x00B780);
            CPU_SET_A16(bus_read16(db, 0x0EBE));
            call_jsr(0x00B66C);
            tail_jmp(0x00A451);
            return;
        }
        if (a6 >= 0x0070) {                                 /* 96F1 -> 9714 */
            CPU_SET_A16(pick);
            call_jsr(0x00B6F4);
            CPU_SET_A16(0x000D);
            call_jsr(0x00B66C);
            tail_jmp(0x00A451);
            return;
        }
    }
    CPU_SET_A16(pick);                                      /* 96F8 */
    call_jsr(0x00B66C);
    tool = bus_read16(db, 0x04D0) & 0x00FF;                 /* LDA $04D0; AND #$FF */
    CPU_SET_A16(tool == 0x0008 ? bus_read16(db, 0x198A) : tool);
    tail_jmp(0x00B6F4);                                     /* JMP $B6F4 */
}

/* ========================================================================
 * $00:9853 — Toolbar: change drawing tool
 *
 * Changes the current drawing tool. Sends a sound command,
 * updates $04D0 with the new tool ID, refreshes pen graphics.
 * ======================================================================== */
void mp_009853(void) {
    /* $00:9853: a pencil-size button: the size from the clicked slot (DP $A8
     * low byte) into the high byte of $04D0 and of $A8, the tool kept (the
     * stamp editor's previous tool $1B10 when the whole $04D0 is 7), the
     * tool's palette ($96E1) and the window tables ($E64F). */
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    CPU_SET_A16(0x0004); call_jsl(0x01D368);
    uint16_t a8 = (uint16_t)((bus_read16(0x00, (uint16_t)(dp + 0xA8)) & 0x00FF) << 8);
    bus_write16(0x00, (uint16_t)(dp + 0xA8), a8);
    uint16_t t = bus_read16(db, 0x04D0);
    if (t == 0x0007) t = bus_read16(db, 0x1B10);
    bus_write16(db, 0x04D0, (uint16_t)((t & 0x00FF) | a8));
    call_jsr(0x0096E1);
    tail_jmp(0x00E64F);
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
    /* $00:98C3: pick a tool from the bottom bar: sound 1, no spray state,
     * the stamp page down one when $19FA, $96AB, the tool switch ($9FC4 with
     * the tool's low byte), its palette ($A8 = high byte, $96E1) and the
     * canvas palette record ($A363). */
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    CPU_SET_A16(0x0001); call_jsl(0x01D368);
    bus_write16(0x00, (uint16_t)(dp + 0xB8), 0);
    bus_write16(db, 0x1992, 0);
    if (bus_read16(db, 0x19FA) != 0)
        bus_write16(0x00, (uint16_t)(dp + 0xAC), (uint16_t)(bus_read16(0x00, (uint16_t)(dp + 0xAC)) - 1));
    call_jsr(0x0096AB);
    uint16_t w = bus_read16(db, 0x04D0) & 0x00FF;
    call_tool_switch(bus_read16(0x00, (uint16_t)(dp + 0xAA)), 1, &w);
    bus_write16(0x00, (uint16_t)(dp + 0xA8), (uint16_t)(bus_read16(db, 0x04D0) & 0xFF00));
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* ========================================================================
 * $00:98F8 — Palette: select stamp tool
 *
 * Selects the stamp tool (tool $03) from the palette bar.
 * ======================================================================== */
void mp_0098F8(void) {
    /* $00:98F8: pick the stamp tool from the bottom bar (as $98C3, page 3). */
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    CPU_SET_A16(0x0001); call_jsl(0x01D368);
    bus_write16(0x00, (uint16_t)(dp + 0xB8), 0);
    bus_write16(db, 0x1992, 0);
    bus_write16(0x00, (uint16_t)(dp + 0xAC), 0x0003);
    call_jsr(0x0096AB);
    uint16_t w = 0x0003;
    call_tool_switch(bus_read16(0x00, (uint16_t)(dp + 0xAA)), 1, &w);
    bus_write16(0x00, (uint16_t)(dp + 0xA8), (uint16_t)(bus_read16(db, 0x04D0) & 0xFF00));
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* $00:9CC7: the shape tools' part of $9C25 (it jumps here for $AE 1-3):
 * the corners and the canvas saved at the press, restored while dragging,
 * the shape's sound at the release, then line, rectangle or ellipse. */
void mp_009CC7(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define DR(o)    bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define DW(o,v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
    DW(0x50, 0x0001);                                        /* $9CC7: shapes */
    if (DR(0x20) == 0) {
        call_jsr(0x008B03);
        DW(0xBA, 0x0001); DW(0xBE, 0x0001);
        DW(0x22, R16(0x04DC)); DW(0x26, R16(0x04DC));
        DW(0x24, R16(0x04DE)); DW(0x28, R16(0x04DE));
    } else {
        DW(0x26, R16(0x04DC)); DW(0x28, R16(0x04DE));
    }
    uint16_t y = (R16(0x04CA) & 0x0010) ? 1 : 0;
    g_cpu.Y = y;
    DW(0x20, y);
    if (y == 0) {
        call_jsr(0x008B18);
    } else {
        uint16_t m = R16(0x1B26);
        if (m & 0x0001) return;
        if (m & 0x0008) call_jsr(0x008B18);
    }
    if (DR(0x20) == 0) {                                     /* released: its sound */
        uint16_t v = DR(0xB8) ? 0x0003 : R16(0x198A);
        uint16_t ae = DR(0xAE);
        if (ae == 0x0003) ae--;
        ae--;
        CPU_SET_A16((uint16_t)((ae << 2) + v + 0x001D));
        call_jsl(0x01D368);
        W16(0x053B, 0x0004); W16(0x053D, 0); W16(0x053F, 0x0003);
    }
    uint16_t ae = DR(0xAE);
    call_jsr(ae == 0x0002 ? 0x00AB26 : ae == 0x0003 ? 0x00AB8A : 0x00AAFB);
    if (DR(0x20) == 0) { CPU_SET_A16(0x0010); call_jsl(0x009C1B); }
    CPU_SET_A16(0x0001); call_jsl(0x01D348);
    CPU_SET_A16(0x0018); call_jsl(0x01D368);
#undef R16
#undef W16
#undef DR
#undef DW
}

/* $00:9C25: the drawing tools on the canvas (tool <= 4, 7, 8), by $AE:
 *   0, freehand ($09D4 clear): a dab at the cursor ($AAEF), the stroke
 *      joined by $9D7D, the scratch sound while it runs, sound $18 on
 *      release; $20 = 1 while the button is held.
 *   0, line mode ($09D4 set): rubber-band segments ($AAFB).
 *   1-3, shapes: corners $22/$24 and $26/$28 (the canvas saved by $8B03 at
 *      the press, restored by $8B18 while dragging), the shape's sound at
 *      the release, then line ($AAFB), rectangle ($AB26) or ellipse
 *      ($AB8A); after the release $10 frames and the closing sounds. */
void mp_009C25(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define DR(o)    bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define DW(o,v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
    if (DR(0xAE) == 0) {
        DW(0x50, 0);
        if (R16(0x09D4) == 0) {
            if (DR(0x20) == 0) { DW(0xBA, 0x0001); DW(0xBC, 0); DW(0xBE, 0); }
            if (DR(0x20) == 0 || (R16(0x1B26) & 0x0002)) {
                call_jsr(0x009D7D);
            } else if (!(R16(0x1B26) & 0x0001) && R16(0x04D0) != 0x0007) {
                CPU_SET_A16(0x0001); call_jsl(0x01D348);
            }
            uint16_t y = 0x0001;
            if (!(R16(0x04CA) & 0x0010)) {
                CPU_SET_A16(0x0018); call_jsl(0x01D368);
                y = 0;
            }
            g_cpu.Y = y;
            DW(0x20, y);
            DW(0x22, R16(0x04DC));
            DW(0x24, R16(0x04DE));
            tail_jmp(0x00AAEF);
            return;
        }
        if (DR(0x20) == 0) {                                 /* $9C87: line mode */
            DW(0xBA, 0x0001); DW(0xBC, 0); DW(0xBE, 0);
            DW(0x22, R16(0x04DC)); DW(0x26, R16(0x04DC));
            DW(0x24, R16(0x04DE)); DW(0x28, R16(0x04DE));
        } else {
            DW(0x22, DR(0x26)); DW(0x24, DR(0x28));
            DW(0x26, R16(0x04DC)); DW(0x28, R16(0x04DE));
        }
        uint16_t y = (R16(0x04CA) & 0x0010) ? 1 : 0;
        g_cpu.Y = y;
        DW(0x20, y);
        tail_jmp(0x00AAFB);
        return;
    }
    mp_009CC7();                                             /* JMP $9CC7: shapes */
#undef R16
#undef W16
#undef DR
#undef DW
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

/* $00:9DFB: the copy tool (rectangle select, then drag the copy).
 * DP $20 is its state: 0 first press (the corner at the cursor, or the
 * whole canvas when $19C0), 1 dragging the other corner, 2 release
 * (corners sorted, the area kept by $01:D75B), 3 moving the copy (the
 * rectangle follows the cursor, clamped to the canvas $19B6..$19BC; a
 * click lays it down via $01:D810). The frame is drawn by $ADFB. */
static bool neg16(uint16_t a, uint16_t b) { return ((uint16_t)(a - b) & 0x8000) != 0; }

void mp_009DFB(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define DR(o)    bus_read16(0x00, (uint16_t)(dp + (o)))
#define DW(o,v)  bus_write16(0x00, (uint16_t)(dp + (o)), (uint16_t)(v))
    W16(0x1996, 0);
    W16(0x19B6, R16(0x19AE) + 4);
    W16(0x19B8, R16(0x19B0) + 3);
    W16(0x19BA, R16(0x19B2) - 1);
    W16(0x19BC, R16(0x19B4) - 2);
    uint16_t st = DR(0x20);
    if (st == 0) {
        CPU_SET_A16(0x0026); call_jsl(0x01D368);
        W16(0x053B, 0x0004); W16(0x053D, 0); W16(0x053F, 0x0003);
        W16(0x1998, 0); W16(0x199A, 0);
        if (R16(0x19C0) == 0) {
            DW(0x22, R16(0x04DC)); DW(0x26, R16(0x04DC));
            DW(0x24, R16(0x04DE)); DW(0x28, R16(0x04DE));
        } else {
            DW(0x22, R16(0x19B6)); DW(0x26, R16(0x19B8));
            DW(0x24, R16(0x19BA) + 1); DW(0x28, R16(0x19BC) + 1);
            g_cpu.Y = 0x0002; DW(0x20, 0x0002);
            return;
        }
    } else if (st < 0x0002) {
        W16(0x1998, 0); W16(0x199A, 0x0001);
        DW(0x26, R16(0x04DC)); DW(0x28, R16(0x04DE));
        call_jsr(0x00ADFB);
    } else if (st != 0x0003) {
        W16(0x053B, 0);
        CPU_SET_A16(0x0027); call_jsl(0x01D368);
        uint16_t a = DR(0x22), x = DR(0x26);
        if (a >= x) { DW(0x22, x); DW(0x26, a); }
        a = DR(0x24); x = DR(0x28);
        if (a >= x) { DW(0x24, x); DW(0x28, a); }
        call_jsl(0x01D75B);
        if (R16(0x19C0) == 0) {
            uint16_t cx = (uint16_t)((uint16_t)(DR(0x22) + DR(0x26)) >> 1);
            W16(0x04DC, cx); W16(0x099B, cx);
            uint16_t cy = (uint16_t)((uint16_t)(DR(0x24) + DR(0x28)) >> 1);
            W16(0x04DE, cy); W16(0x099D, cy);
        }
        DW(0x20, DR(0x20) + 1);
        return;
    } else {
        if (R16(0x04CA) & 0x0020) {                          /* $9F50: lay it down */
            if (R16(0x19C0)) { DW(0x22, R16(0x19B6)); DW(0x24, R16(0x19BA) + 1); }
            call_jsr(0x008B03);
            CPU_SET_A16(0x0028); call_jsl(0x01D368);
            W16(0x053B, 0x0004); W16(0x053D, 0); W16(0x053F, 0x0003);
            W16(0x09A7, R16(0x09A7) + 1);
            call_jsl(0x01D810);
            W16(0x053B, 0); W16(0x09A7, 0);
            return;
        }
        W16(0x1996, R16(0x1996) + 1);
        if (R16(0x19C0) != 0) return;
        uint16_t dx = (uint16_t)(R16(0x04DC) - R16(0x099B));
        DW(0x22, DR(0x22) + dx); DW(0x26, DR(0x26) + dx);
        uint16_t dy = (uint16_t)(R16(0x04DE) - R16(0x099D));
        DW(0x24, DR(0x24) + dy); DW(0x28, DR(0x28) + dy);
        /* $9F87 on copies of $22 $26 $24 $28: kept inside the canvas */
        uint16_t l = DR(0x22), r = DR(0x26), t = DR(0x24), btm = DR(0x28);
        uint16_t v = R16(0x19B6);
        if (!neg16(v, l)) l = v;
        if (!neg16(v, r)) r = v;
        v = R16(0x19B8);
        if (neg16(v, l)) l = v;
        if (neg16(v, r)) r = v;
        v = R16(0x19BA);
        if (!neg16(v, t)) t = v;
        if (!neg16(v, btm)) btm = v;
        v = R16(0x19BC);
        if (neg16(v, t)) t = v;
        if (neg16(v, btm)) btm = v;
        uint16_t cx = (uint16_t)((uint16_t)(l + r) >> 1);
        W16(0x04DC, cx); W16(0x099B, cx);
        uint16_t cy = (uint16_t)((uint16_t)(t + btm) >> 1);
        W16(0x04DE, cy); W16(0x099D, cy);
        tail_jmp(0x00ADFB);
        return;
    }
    uint16_t y = 0x0002;                                      /* $9E91 */
    if (R16(0x04CA) & 0x0010) y--;
    g_cpu.Y = y;
    DW(0x20, y);
#undef R16
#undef W16
#undef DR
#undef DW
}

/* ========================================================================
 * $00:A363 — Tool palette/graphics display update
 *
 * Queues a DMA transfer to update the tool palette display on screen.
 * Reads tile data from the appropriate source (ROM or SRAM) based on
 * the current palette row and tool state.
 * ======================================================================== */
void mp_00A363(void) {
    /* $00:A363: queue the canvas palette record (5 words from $00:A443)
     * with the source picked by state: $09AB (one-shot, $07:F800), the
     * stamp editor $1992 ($B000), the colour in use ($0EBE, or $A6 direct
     * page; $F0 = $1144, $100 = $07:E800), $09D6 = $1E ($07:F000), or the
     * custom colour with $0EB8 ($0EBC, bank $0EBA + 6). Without $1992 the
     * window tables follow ($E64F); otherwise, and for $09AB, one frame. */
    uint8_t p = cpu_get_p();
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define W8(o, v)  bus_write8(db, (uint16_t)(o), (uint8_t)(v))
    W16(0x0202, 0);
    uint16_t x = R16(0x0204);
    for (uint16_t y = 0; y != 0x000A; y += 2) W16(0x0182 + x + y, R16(0xA443 + y));
    bool frame;
    if (R16(0x09AB) != 0) {
        W16(0x0183 + x, 0xF800); W8(0x0185 + x, 0x07);
        W16(0x0204, x + 9);
        frame = true;
    } else {
        if (R16(0x1992) != 0) {
            W16(0x0183 + x, 0xB000);
        } else {
            uint16_t a = R16(0x0EBE) & 0x00F0;
            uint16_t eb8 = R16(0x0EB8);
            int path;                                       /* 0: $A3A2, 1: $A3F2, 2: $A3D9 */
            if (eb8 & 0x8000) path = 0;
            else if (eb8 != 0) path = 1;
            else if (R16(0x09D6) == 0x001E) path = 2;
            else { a = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xA6)); path = 0; }
            if (path == 0) {
                if (a == 0x00F0) { W16(0x0183 + x, 0x1144); W8(0x0185 + x, 0x00); }
                else if (a == 0x0100) { W16(0x0183 + x, 0xE800); W8(0x0185 + x, 0x07); }
                else { uint16_t h = (uint16_t)(a >> 1); W16(0x0183 + x, (uint16_t)(((h << 8) | (h >> 8)) + 0x8000)); }
            } else if (path == 2) {
                W16(0x0183 + x, 0xF000); W8(0x0185 + x, 0x07);
            } else {
                uint16_t c = R16(0x0EBC) & 0x00F0;
                W16(0x0183 + x, (uint16_t)((uint16_t)(((c << 8) | (c >> 8)) >> 1) + 0x8000));
                W8(0x0185 + x, (uint8_t)(bus_read8(db, 0x0EBA) + 6));
            }
        }
        W16(0x0204, x + 9);                                 /* $A40E */
        frame = R16(0x1992) != 0;
        if (!frame) {
            g_cpu.X = x;
            call_jsr(0x00E64F);
            cpu_set_p(p);
            if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
            return;
        }
    }
    W16(0x09AB, 0);                                         /* $A437 */
    W16(0x0202, R16(0x0202) + 1);
    g_cpu.X = x;
    call_jsl(0x01E2CE);
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
#undef R16
#undef W16
#undef W8
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
/* The bottom-bar tool pick shared by $98F8/$9928/$995B: sound, spray state
 * $B8, page $AC, $96AB, the tool switch with one word, the tool's palette
 * ($A8 = high byte of $04D0, $96E1) and the canvas palette record ($A363). */
static void pick_tool(uint16_t snd, uint16_t b8, uint16_t ac, uint16_t word) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    CPU_SET_A16(snd); call_jsl(0x01D368);
    bus_write16(0x00, (uint16_t)(dp + 0xB8), b8);
    bus_write16(db, 0x1992, 0);
    bus_write16(0x00, (uint16_t)(dp + 0xAC), ac);
    call_jsr(0x0096AB);
    call_tool_switch(bus_read16(0x00, (uint16_t)(dp + 0xAA)), 1, &word);
    bus_write16(0x00, (uint16_t)(dp + 0xA8), (uint16_t)(bus_read16(db, 0x04D0) & 0xFF00));
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}
/* $00:94E5: an empty slot. */
void mp_0094E5(void) {}
/* $00:9928: the spray can (page 4, spray on). */
void mp_009928(void) { pick_tool(0x0001, 0x0001, 0x0004, 0x0004); }
/* $00:995B: page 5. */
void mp_00995B(void) { pick_tool(0x0006, 0x0000, 0x0005, 0x0005); }
/* $00:96A1: the stamp page arrows ($01:D3BA). */
void mp_0096A1(void) { call_jsl(0x01D3BA); }

/* $00:998B: the stamp tool button: remember the previous tool ($1B10,
 * unless it is the stamp editor), reset the stamp state, tool 6 in $04D0,
 * the tool switch (word 6 from the canvas, 0 otherwise), $09AB set, the
 * canvas palette record. Nothing while already stamping without a stamp. */
void mp_00998B(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    CPU_SET_A16(0x0006); call_jsl(0x01D368);
    uint16_t t = R16(0x04D0) & 0x00FF;
    if (t == 0x0006 && R16(0x19C0) == 0) return;
    if (t != 0x0007) W16(0x1B10, R16(0x04D0));
    bus_write16(0x00, (uint16_t)(dp + 0xB8), 0);
    W16(0x1996, 0); W16(0x1998, 0); W16(0x199A, 0); W16(0x19C0, 0);
    W16(0x04D0, (uint16_t)((R16(0x04D0) & 0xF000) | 0x0006));
    uint16_t aa = bus_read16(0x00, (uint16_t)(dp + 0xAA));
    uint16_t w = aa ? 0x0000 : 0x0006;
    call_tool_switch(aa, 1, &w);
    W16(0x09AB, R16(0x09AB) + 1);
    call_jsr(0x00A363);
#undef R16
#undef W16
}

/* $00:9A35: the pencil-size button: back from the stamp tools to the tool
 * remembered in $1B10, tool 8 with brush $1988 (size $AE = brush / 4 + 1,
 * $198A = brush & 3, spray on for 3), $AA = 1. */
void mp_009A35(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define DW(o, v)  bus_write16(0x00, (uint16_t)(dp + (o)), (uint16_t)(v))
#define DR(o)     bus_read16(0x00, (uint16_t)(dp + (o)))
    CPU_SET_A16(0x0002); call_jsl(0x01D368);
    W16(0x1992, 0);
    DW(0xB8, 0);
    W16(0x1B12, R16(0x04D0));
    uint16_t t = R16(0x04D0) & 0x00FF;
    if (t == 0x0006 || t == 0x0007) W16(0x04D0, R16(0x1B10));
    W16(0x04D0, (uint16_t)((R16(0x04D0) & 0xFF00) | 0x0008));
    DW(0xAE, (uint16_t)((R16(0x1988) >> 2) + 1));
    DW(0xAA, 0x0001);
    uint16_t brush = R16(0x1988);
    push_a16(brush);                                        /* kept across the switch */
    call_tool_switch(DR(0xAA), 1, &brush);
    uint16_t v = bus_wram_read16((uint16_t)(g_cpu.S + 1));  /* PLA */
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    W16(0x198A, v & 0x0003);
    if ((v & 0x0003) == 0x0003) DW(0xB8, DR(0xB8) + 1);
    DW(0xA8, (uint16_t)(R16(0x04D0) & 0xFF00));
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
#undef R16
#undef W16
#undef DW
#undef DR
}

/* $00:9BBA: the eraser-of-everything toggle ($AA 0 <-> 7): the switch
 * there, $10 frames, and the switch back to the plain view. */
void mp_009BBA(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    CPU_SET_A16(0x0003); call_jsl(0x01D368);
    uint16_t aa = (uint16_t)(bus_read16(0x00, (uint16_t)(dp + 0xAA)) ^ 0x0007);
    bus_write16(0x00, (uint16_t)(dp + 0xAA), aa);
    if (aa != 0x0007) {
        uint16_t w[2] = { 0x0009, (uint16_t)(bus_read16(db, 0x04D0) & 0x00FF) };
        call_tool_switch(aa, 2, w);
        CPU_SET_A16(0x0010); call_jsl(0x009C1B);
        uint16_t w1 = bus_read16(db, 0x04D0) & 0x00FF;
        call_tool_switch(bus_read16(0x00, (uint16_t)(dp + 0xAA)), 1, &w1);
    } else {
        uint16_t w0 = 0x0000;
        call_tool_switch(aa, 1, &w0);
        CPU_SET_A16(0x0010); call_jsl(0x009C1B);
        call_tool_switch(bus_read16(0x00, (uint16_t)(dp + 0xAA)), 0, 0);
    }
}

#define TR16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define TW16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define TDR(o)     bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define TDW(o, v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))

/* The stamp editor ($A4B4 from its button, $9B33 when returning to it):
 * $1992 set, its palette and picture ($B66C $0F, $B6F4 $1994 + 8), tool 7,
 * the switch (word 7 on the canvas, 6 elsewhere), canvas palette record. */
static void stamp_editor(bool upload) {
    TW16(0x1992, 0x0001);
    CPU_SET_A16(0x000F); call_jsr(0x00B66C);
    CPU_SET_A16((uint16_t)(TR16(0x1994) + 8)); call_jsr(0x00B6F4);
    TW16(0x04D0, 0x0007);
    if (upload) { call_jsr(0x00A525); TDW(0xAE, 0); }
    uint16_t aa = TDR(0xAA);
    uint16_t w = aa ? 0x0006 : 0x0007;
    call_tool_switch(aa, 1, &w);
    tail_jmp(0x00A363);
}

/* $00:A4B4: the stamp editor button (the tool before it kept in $1B10). */
void mp_00A4B4(void) {
    CPU_SET_A16(0x0007); call_jsl(0x01D368);
    uint16_t t = TR16(0x04D0) & 0x00FF;
    if (t == 0x0007) return;
    if (t != 0x0006) TW16(0x1B10, TR16(0x04D0));
    stamp_editor(true);
}

/* $00:9A9E: back from the colour/pencil-size panels to the tool in $1B12
 * (the stamp tools via $1B10), the canvas ($AA 0, or 3 with $19FA). */
void mp_009A9E(void) {
    CPU_SET_A16(0x0041); call_jsl(0x01D368);
    uint16_t t = TR16(0x1B12) & 0x00FF;
    if (t == 0x0006 || t == 0x0007) {
        TW16(0x1B10, (uint16_t)((TR16(0x04D0) & 0xFF00) | t));
        TW16(0x04D0, TR16(0x1B12));
    } else {
        TW16(0x04D0, (uint16_t)((TR16(0x04D0) & 0xFF00) | t));
    }
    TDW(0xAE, 0);
    TDW(0xAA, TR16(0x19FA) ? 0x0003 : 0x0000);
    TDW(0xB8, 0);
    uint16_t u = TR16(0x04D0) & 0x00FF;
    if (u == 0x0004) TDW(0xB8, TDR(0xB8) + 1);
    if (u == 0x0007) { stamp_editor(false); return; }
    if (u == 0x0006) {
        uint16_t w = 0x0006;
        call_tool_switch(TDR(0xAA), 1, &w);
        TW16(0x09AB, TR16(0x09AB) + 1);
        tail_jmp(0x00A363);
        return;
    }
    call_tool_switch(TDR(0xAA), 1, &u);
    TDW(0xA8, (uint16_t)(TR16(0x04D0) & 0xFF00));
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* $00:9B63 / $00:9B96: a brush size from the size panel ($AC - 1 into
 * $1988; size $AE = brush / 4 + 1; $9B63 also the brush shape $198A and
 * its picture); spray off / on; window tables ($E64F). */
void mp_009B63(void) {
    CPU_SET_A16(0x000B); call_jsl(0x01D368);
    TDW(0xB8, 0);
    uint16_t v = (uint16_t)(TDR(0xAC) - 1);
    TW16(0x1988, v);
    call_tool_switch(TDR(0xAA), 1, &v);
    v = (uint16_t)(TDR(0xAC) - 1);
    TW16(0x1988, v);
    TDW(0xAE, (uint16_t)((v >> 2) + 1));
    TW16(0x198A, v & 0x0003);
    CPU_SET_A16(v & 0x0003);
    call_jsr(0x00B6F4);
    tail_jmp(0x00E64F);
}
void mp_009B96(void) {
    CPU_SET_A16(0x000B); call_jsl(0x01D368);
    TDW(0xB8, TDR(0xB8) + 1);
    uint16_t v = (uint16_t)(TDR(0xAC) - 1);
    TW16(0x1988, v);
    TDW(0xAE, (uint16_t)((v >> 2) + 1));
    call_tool_switch(TDR(0xAA), 1, &v);
    tail_jmp(0x00E64F);
}
#undef TR16
#undef TW16
#undef TDR
#undef TDW

#define TR16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define TW16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define TDR(o)     bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define TDW(o, v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))

/* $00:C42A: into the music editor ($AC set: the sound $08, else $41):
 * the tool kept in $1A04, tool 9, $AA = 2, the switch, the canvas palette
 * record, mode $09D6 = 3. */
void mp_00C42A(void) {
    CPU_SET_A16(TDR(0xAC) ? 0x0008 : 0x0041); call_jsl(0x01D368);
    TW16(0x1A04, TR16(0x04D0));
    TW16(0x04D0, 0x0009);
    TDW(0xAA, 0x0002);
    call_tool_switch(0x0002, 0, 0);
    TW16(0x09AB, TR16(0x09AB) + 1);
    call_jsr(0x00A363);
    TW16(0x09D6, 0x0003);
}

/* $00:C6EF: the eraser-of-everything toggle in the animation/music modes
 * ($AA 3 <-> 4), each way a two-step switch with $10 frames between. */
void mp_00C6EF(void) {
    CPU_SET_A16(0x0003); call_jsl(0x01D368);
    uint16_t aa = (uint16_t)(TDR(0xAA) ^ 0x0007);
    TDW(0xAA, aa);
    uint16_t t = TR16(0x04D0) & 0x00FF;
    if (aa != 0x0004) {
        if (t == 0x0006) {
            uint16_t w = 0x0008;
            call_tool_switch(aa, 1, &w);
            CPU_SET_A16(0x0010); call_jsl(0x009C1B);
            call_tool_switch(TDR(0xAA), 0, 0);
            return;
        }
        if (t == 0x0007) t--;
        uint16_t w[2] = { t, 0x0008 };
        call_tool_switch(aa, 2, w);
        CPU_SET_A16(0x0010); call_jsl(0x009C1B);
        call_tool_switch(TDR(0xAA), 1, &t);
        return;
    }
    uint16_t v;
    if (t == 0x0006) v = TR16(0x19C0);
    else if (t == 0x0010) v = 0x0001;
    else {
        uint16_t w = 0x0003;
        call_tool_switch(aa, 1, &w);
        CPU_SET_A16(0x0010); call_jsl(0x009C1B);
        call_tool_switch(TDR(0xAA), 0, 0);
        return;
    }
    uint16_t w[2] = { v, 0x0003 };
    call_tool_switch(aa, 2, w);
    CPU_SET_A16(0x0010); call_jsl(0x009C1B);
    call_tool_switch(TDR(0xAA), 1, &v);
}

/* $00:CAD7: the colour panel: the mode kept in $09D8 (now 2), the tool in
 * $1990 (now 9), the colour in $1B14, $AA = 9, the switch, then the colour
 * cell's palette ($B66C below $70, else $B6F4) and the palette record. */
void mp_00CAD7(void) {
    CPU_SET_A16(0x0002); call_jsl(0x01D368);
    TW16(0x09D8, TR16(0x09D6));
    TW16(0x09D6, 0x0002);
    TW16(0x1990, TR16(0x04D0));
    TW16(0x04D0, 0x0009);
    TW16(0x1B14, TDR(0xA6));
    TDW(0xAA, 0x0009);
    call_tool_switch(0x0009, 0, 0);
    TW16(0x1992, 0);
    uint16_t a8 = (uint16_t)((TR16(0x04D0) & 0xFF00) >> 8);
    TDW(0xA8, a8);
    uint16_t a6 = TDR(0xA6);
    CPU_SET_A16((uint16_t)(a8 | a6));
    call_jsr(a6 < 0x0070 ? 0x00B66C : 0x00B6F4);
    tail_jmp(0x00A363);
}

/* $00:E7D9: the paint-can/pattern panel: $0EB8 set, spray state kept in
 * $0EC2, the tool in $0EC0 (now 3), the pattern picture ($EB3B), $AA = 8,
 * the switch with it and $0EBA, the tool palette and the record. */
void mp_00E7D9(void) {
    CPU_SET_A16(0x0002); call_jsl(0x01D368);
    TW16(0x0EB8, 0x0001);
    TW16(0x1992, 0);
    TW16(0x0EC2, TDR(0xB8));
    TDW(0xB8, 0);
    TW16(0x0EC0, TR16(0x04D0));
    TW16(0x04D0, 0x0003);
    call_jsr(0x00EB3B);
    uint16_t w[2] = { CPU_A16(), TR16(0x0EBA) };
    TDW(0xAA, 0x0008);
    call_tool_switch(0x0008, 2, w);
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* $00:99E1: the stamp-pick button (like $998B, with $19C0 = 1 and word 1). */
void mp_0099E1(void) {
    CPU_SET_A16(0x0006); call_jsl(0x01D368);
    uint16_t t = TR16(0x04D0) & 0x00FF;
    if (t == 0x0006 && TR16(0x19C0) != 0) return;
    if (t != 0x0007) TW16(0x1B10, TR16(0x04D0));
    TDW(0xB8, 0);
    TW16(0x1996, 0); TW16(0x1998, 0); TW16(0x199A, 0);
    TW16(0x19C0, 0x0001);
    TW16(0x04D0, (uint16_t)((TR16(0x04D0) & 0xF000) | 0x0006));
    uint16_t w = 0x0001;
    call_tool_switch(TDR(0xAA), 1, &w);
    TW16(0x09AB, TR16(0x09AB) + 1);
    call_jsr(0x00A363);
}
#undef TR16
#undef TW16
#undef TDR
#undef TDW

void mp_register_tools(void) {
    func_table_register(0x00C42A, mp_00C42A);
    func_table_register(0x00C6EF, mp_00C6EF);
    func_table_register(0x00CAD7, mp_00CAD7);
    func_table_register(0x00E7D9, mp_00E7D9);
    func_table_register(0x0099E1, mp_0099E1);

    func_table_register(0x00A4B4, mp_00A4B4);
    func_table_register(0x009A9E, mp_009A9E);
    func_table_register(0x009B63, mp_009B63);
    func_table_register(0x009B96, mp_009B96);

    func_table_register(0x009A35, mp_009A35);
    func_table_register(0x009BBA, mp_009BBA);
    func_table_register(0x0094E5, mp_0094E5);
    func_table_register(0x009928, mp_009928);
    func_table_register(0x00995B, mp_00995B);
    func_table_register(0x0096A1, mp_0096A1);
    func_table_register(0x00998B, mp_00998B);

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
    func_table_register(0x009CC7, mp_009CC7);
    func_table_register(0x009DAB, mp_009DAB);
    func_table_register(0x009DFB, mp_009DFB);
    func_table_register(0x00A363, mp_00A363);
    func_table_register(0x00F921, mp_00F921);
    func_table_register(0x01D56D, mp_01D56D);
    func_table_register(0x01D597, mp_01D597);
}
