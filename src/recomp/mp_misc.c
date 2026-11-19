/*
 * Mario Paint — Miscellaneous unregistered functions.
 *
 * Fills remaining gaps where registered functions call unregistered ones.
 * Covers: undo-draw mode, stamp placement, right-click handlers,
 * palette computation, pixel row plotting, and title screen helpers.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/func_table.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $00:9CC7 — Undo-draw mode (draw with undo preview)
 *
 * Alternate drawing mode that saves canvas before drawing and
 * restores on release (for preview/undo behavior).
 * Used by rectangle, ellipse, and line tools.
 * ======================================================================== */
void mp_009CC7(void) {
    bus_wram_write16(0x0050, 0x0001);
    uint8_t draw = bus_wram_read8(0x0020);

    if (draw == 0) {
        /* First click — save canvas to undo buffer */
        mp_008B03();
        bus_wram_write16(0x00BA, 0x0001);
        bus_wram_write16(0x00BE, 0x0001);

        bus_wram_write16(0x0022, bus_wram_read16(0x04DC));
        bus_wram_write16(0x0026, bus_wram_read16(0x04DC));
        bus_wram_write16(0x0024, bus_wram_read16(0x04DE));
        bus_wram_write16(0x0028, bus_wram_read16(0x04DE));
    } else {
        /* Continuing — update endpoint */
        bus_wram_write16(0x0026, bus_wram_read16(0x04DC));
        bus_wram_write16(0x0028, bus_wram_read16(0x04DE));
    }

    /* Check button state */
    uint16_t buttons = bus_wram_read16(0x04CA);
    uint8_t new_draw = (buttons & 0x0010) ? 1 : 0;
    bus_wram_write8(0x0020, new_draw);

    if (new_draw == 0) {
        /* Released — restore canvas (undo preview) */
        mp_008B18();
    }

    if (new_draw != 0) {
        /* Still drawing — check for movement */
        uint16_t movement = bus_wram_read16(0x1B26);
        if (!(movement & 0x0001)) {
            /* No new movement */
            return;
        }
    }

    /* Determine shape type from $AE and draw */
    uint16_t shape = bus_wram_read16(0x00AE);
    if (shape == 0x0002) {
        func_table_call(0x00AB26);  /* Rectangle */
    } else if (shape == 0x0003) {
        func_table_call(0x00AB8A);  /* Ellipse */
    } else {
        func_table_call(0x00AAFB);  /* Line */
    }

    if (draw == 0) {
        /* End of preview — commit with sound */
        func_table_call(0x009C1B);
    }

    op_lda_imm16(0x0001);
    func_table_call(0x01D348);
    op_lda_imm16(0x0018);
    func_table_call(0x01D368);
}

/* ========================================================================
 * $00:9E76 — Stamp placement logic
 *
 * Handles the stamp tool placement state machine.
 * State transitions: position → place → done.
 * ======================================================================== */
void mp_009E76(void) {
    uint8_t draw = bus_wram_read8(0x0020);

    if (draw < 2) {
        /* Positioning phase */
        bus_wram_write16(0x1998, 0x0000);
        bus_wram_write16(0x199A, 0x0001);

        bus_wram_write16(0x0026, bus_wram_read16(0x04DC));
        bus_wram_write16(0x0028, bus_wram_read16(0x04DE));

        func_table_call(0x00ADFB);  /* Draw stamp preview */

        uint16_t buttons = bus_wram_read16(0x04CA);
        uint8_t new_draw = (buttons & 0x0010) ? 2 : 1;
        bus_wram_write8(0x0020, new_draw);
        return;
    }

    if (draw == 3) {
        /* Placement phase — check for click to confirm */
        uint16_t buttons = bus_wram_read16(0x04CA);
        if (buttons & 0x0020) {
            goto place_stamp;
        }

        /* Allow moving stamp with mouse */
        uint16_t mode = bus_wram_read16(0x19C0);
        if (mode == 0) return;

        /* Track cursor displacement for stamp movement */
        int16_t dx = (int16_t)bus_wram_read16(0x04DC) - (int16_t)bus_wram_read16(0x099B);
        bus_wram_write16(0x0022, bus_wram_read16(0x0022) + dx);
        bus_wram_write16(0x0026, bus_wram_read16(0x0026) + dx);

        int16_t dy = (int16_t)bus_wram_read16(0x04DE) - (int16_t)bus_wram_read16(0x099D);
        bus_wram_write16(0x0024, bus_wram_read16(0x0024) + dy);
        bus_wram_write16(0x0028, bus_wram_read16(0x0028) + dy);

        bus_wram_write16(0x099B, bus_wram_read16(0x04DC));
        bus_wram_write16(0x099D, bus_wram_read16(0x04DE));

        bus_wram_write16(0x1996, 0x0001);
        return;
    }

    /* draw == 2: just placed — compute stamp bounds */
    bus_wram_write16(0x053B, 0x0000);
    op_lda_imm16(0x0027);
    func_table_call(0x01D368);

    /* Normalize stamp bounds (ensure min < max) */
    {
        int16_t x1 = (int16_t)bus_wram_read16(0x0022);
        int16_t x2 = (int16_t)bus_wram_read16(0x0026);
        if (x1 > x2) {
            bus_wram_write16(0x0022, (uint16_t)x2);
            bus_wram_write16(0x0026, (uint16_t)x1);
        }
        int16_t y1 = (int16_t)bus_wram_read16(0x0024);
        int16_t y2 = (int16_t)bus_wram_read16(0x0028);
        if (y1 > y2) {
            bus_wram_write16(0x0024, (uint16_t)y2);
            bus_wram_write16(0x0028, (uint16_t)y1);
        }
    }

    /* Execute stamp placement */
    func_table_call(0x01D75B);

    /* Center cursor on stamp */
    uint16_t mode = bus_wram_read16(0x19C0);
    if (mode == 0) {
        uint16_t cx = (bus_wram_read16(0x0022) + bus_wram_read16(0x0026)) >> 1;
        bus_wram_write16(0x04DC, cx);
        bus_wram_write16(0x099B, cx);
        uint16_t cy = (bus_wram_read16(0x0024) + bus_wram_read16(0x0028)) >> 1;
        bus_wram_write16(0x04DE, cy);
        bus_wram_write16(0x099D, cy);
    }

    bus_wram_write8(0x0020, bus_wram_read8(0x0020) + 1);
    return;

place_stamp:
    /* Commit stamp */
    func_table_call(0x009F50);
}

/* ========================================================================
 * $00:9760 — Right-click / undo handler
 *
 * Handles right-click actions: triggers undo, restores canvas,
 * waits for VRAM transfer, then resets tool state.
 * ======================================================================== */
void mp_009760(void) {
    /* $00:9760: undo. Sound 9; switch to the tool's undo variant ($9843);
     * restore the canvas from the undo buffer ($8B18) and wait until the
     * upload is done ($0208); then switch back to the tool with what it
     * needs (the tool in use, the brush, the custom colour, the stamp
     * page...), or with nothing. */
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define R16(o)  bus_read16(db, (uint16_t)(o))
    CPU_SET_A16(0x0009); call_jsl(0x01D368);
    uint16_t aa = bus_read16(0x00, (uint16_t)(dp + 0xAA));
    uint16_t w = bus_read16(db, (uint16_t)(0x9843 + aa)) & 0x00FF;
    call_tool_switch(aa, 1, &w);
    bus_write16(db, 0x0206, 0);
    call_jsr(0x008B18);
    bus_write16(db, 0x0208, 0);
    bus_write16(db, 0x0206, R16(0x0206) + 1);
    do call_jsl(0x01E2CE); while (R16(0x0208) != 0);         /* $9784: LDA $0208; BNE */
    aa = bus_read16(0x00, (uint16_t)(dp + 0xAA));
    uint16_t v;
    switch (aa) {
    case 0x0000: v = R16(0x04D0) & 0x00FF; break;
    case 0x0001: v = R16(0x1988); break;
    case 0x0003: {
        uint16_t t = R16(0x04D0) & 0x00FF;
        if (t == 0x0006) goto none;
        v = t == 0x0007 ? 0x0006 : t;
        break;
    }
    case 0x0004:
        if ((R16(0x04D0) & 0x00FF) != 0x0006) goto none;
        v = R16(0x19C0);
        break;
    case 0x0008: {
        uint16_t ws[3];
        if (R16(0x1992) != 0) { ws[0] = 0x0008; ws[1] = R16(0x0EBA); call_tool_switch(aa, 2, ws); return; }
        call_jsr(0x00EB3B);
        uint16_t c = CPU_A16();
        if (R16(0x0EB8) & 0x8000) { ws[0] = c; ws[1] = 0x0007; ws[2] = R16(0x0EBA); call_tool_switch(aa, 3, ws); }
        else { ws[0] = c; ws[1] = R16(0x0EBA); call_tool_switch(aa, 2, ws); }
        return;
    }
    case 0x000F: v = R16(0x0997); break;
    default: goto none;
    }
    call_tool_switch(aa, 1, &v);                            /* $9837 */
    return;
none:
    call_tool_switch(aa, 0, 0);                             /* $97AD */
#undef R16
}

/* ========================================================================
 * $00:E233 — SRAM header computation
 *
 * Computes the expected SRAM header value for validation.
 * ======================================================================== */
void mp_00E233(void) {
    /* Simple header check — returns the expected value in A */
    uint16_t val = bus_read16(0x70, 0x07C0);
    CPU_SET_A16(val);
}

/* ========================================================================
 * $00:E6DA — Palette color data processing
 *
 * Inner loop of E64F — processes one palette entry's color data
 * through bit manipulation to compute the final display color.
 * ======================================================================== */
void mp_00E6DA(void) {
    /* This routine does complex 4-bitplane color extraction.
     * The full implementation involves shifting/rotating through
     * all 4 bitplanes to extract individual pixel colors.
     * For now, register so it's called (not no-op'd). */
    op_sep(0x20);

    /* Process 8 pixels worth of color data */
    for (int y = 0; y < 4; y++) {
        /* Shift bitplane data through carry */
        uint16_t bp0 = bus_wram_read16(0x1004);
        uint16_t bp1 = bus_wram_read16(0x1006);
        uint16_t bp2 = bus_wram_read16(0x100A);
        uint16_t bp3 = bus_wram_read16(0x100C);

        /* Extract one pixel's color from 4 bitplanes */
        uint8_t color = 0;
        color |= (bp0 >> 15) & 1;
        color |= ((bp1 >> 15) & 1) << 1;
        color |= ((bp2 >> 15) & 1) << 2;
        color |= ((bp3 >> 15) & 1) << 3;

        /* Shift bitplanes left */
        bus_wram_write16(0x1004, bp0 << 1);
        bus_wram_write16(0x1006, bp1 << 1);
        bus_wram_write16(0x100A, bp2 << 1);
        bus_wram_write16(0x100C, bp3 << 1);
    }

    op_rep(0x20);
}

/* ========================================================================
 * $00:82A7 — Soft reset (from NMI check)
 *
 * Similar to $0082B8 but reached from the NMI handler's
 * L+R+Start+Select check on port 2.
 * ======================================================================== */
void mp_0082A7(void) {
    op_sep(0x30);
    g_cpu.DP = 0x0000;
    g_cpu.DB = 0x00;
    func_table_call(0x0082D5);
    mp_008000();
}

/* ========================================================================
 * Title screen / canvas transition helpers
 * ======================================================================== */

/* $01:934F — Title screen frame processing */
/* $01:934F — the save wipe: with buttons $A0A0 newly pressed on pad 2
 * ($013C) while exactly $A0A0 is held ($0134), clear SRAM $70:0000-$7FFF.
 * X is kept. */
void mp_01934F(void) {
    uint16_t x = g_cpu.X;
    uint16_t a = bus_wram_read16(0x013C);
    CPU_SET_A16(a);
    if (a & 0xA0A0) {
        a = bus_wram_read16(0x0134);
        CPU_SET_A16(a);
        if (a == 0xA0A0) {
            for (int i = 0x7FFE; i >= 0; i -= 2) bus_write16(0x70, (uint16_t)i, 0x0000);
            CPU_SET_A16(0x0000);
        }
    }
    g_cpu.X = x;
}


/* ========================================================================
 * Register all miscellaneous functions.
 * ======================================================================== */
/* ======================================================================
 * Window HDMA tables ($01:9130-$91FD). Two tables at $7E:4000 and $7E:4200
 * ($0211 = 1 picks the first): byte pairs (left, right) per scanline, two
 * line-count bytes in between at the 128-line split. 16-bit.
 * ==================================================================== */
static uint16_t hdma_line_index(uint16_t y) {
    return y < 0x0070 ? (uint16_t)((y << 1) + 1) : (uint16_t)((y + 1) << 1);
}
/* $01:9130: Y = line, X = left edge (clamped at 0). */
void mp_019130(void) {
    uint16_t y = g_cpu.Y, x = g_cpu.X;
    if ((y & 0x8000) || !((uint16_t)(y - 0x00E0) & 0x8000)) return;
    if (x & 0x8000) x = 0;
    uint16_t i = hdma_line_index(y);
    uint16_t sel = bus_read16(g_cpu.DB, 0x0211);
    bus_write8(0x7E, (uint16_t)((sel == 1 ? 0x4000 : 0x4200) + i), (uint8_t)x);
    g_cpu.Y = i; g_cpu.X = i;
    CPU_SET_A16(x);
    g_cpu.flag_C = sel >= 1;                                /* CMP #$0001 */
    cpu_update_nz16(x);                                     /* PLA */
}
/* $01:916E: Y = line, X = right edge (clamped at $FF). */
void mp_01916E(void) {
    uint16_t y = g_cpu.Y, x = g_cpu.X;
    if ((y & 0x8000) || !((uint16_t)(y - 0x00E0) & 0x8000)) return;
    if (!((uint16_t)(x - 0x00FF) & 0x8000)) x = 0x00FF;
    uint16_t i = hdma_line_index(y);
    uint16_t sel = bus_read16(g_cpu.DB, 0x0211);
    bus_write8(0x7E, (uint16_t)((sel == 1 ? 0x4001 : 0x4201) + i), (uint8_t)x);
    g_cpu.Y = i; g_cpu.X = i;
    CPU_SET_A16(x);
    g_cpu.flag_C = sel >= 1;
    cpu_update_nz16(x);
}
/* $01:91AC: reset the current table: two $70-line blocks, all lines
 * (left 1, right 0) = empty window, terminator. */
void mp_0191AC(void) {
    uint16_t b = bus_read16(g_cpu.DB, 0x0211) == 1 ? 0x4000 : 0x4200;
    bus_write16(0x7E, b, 0x00F0);
    bus_write16(0x7E, (uint16_t)(b + 0xE1), 0x00F0);
    bus_write16(0x7E, (uint16_t)(b + 0x1C2), 0x0000);
    for (int x = 0xDE; x >= 0; x -= 2) {
        bus_write16(0x7E, (uint16_t)(b + 0x01 + x), 0x0001);
        bus_write16(0x7E, (uint16_t)(b + 0xE2 + x), 0x0001);
    }
    g_cpu.X = 0xFFFE;
    CPU_SET_A16(0x0001);
    g_cpu.flag_N = true; g_cpu.flag_Z = false;              /* DEX past 0 */
}

/* ======================================================================
 * Save-data compressor ($01:ECD5-$01:EFB9): LZ77 over the bytes at
 * $7E:4400 (DB = $7E) into $7F:2000. A 3-byte hash ($7F:0004 + 2h: last
 * position, or $FFFF) chains up to 128 recent positions in a ring
 * ($7F:0204 next, $7F:0304 prev / bucket | $4000, $7F:0404 position).
 * Locals live in the direct page the ROM carves out of the stack, so the
 * C code keeps them there too: $16 ring slot, $18 position, $1A bucket,
 * $1C chain cursor, $02 output, $04 input, ... 16-bit.
 * ==================================================================== */
#define DPR(o)     bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define DPW(o, v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
/* LDA/STA $7F:base,X: the 24-bit sum carries, so X = $FFFF (a chain's end
 * marker, written through by the unlink code) reaches bank $80. */
#define T7F(o)     read_long16(0x7F0000u + (uint32_t)(o))
#define W7F(o, v)  write_long16(0x7F0000u + (uint32_t)(o), (uint16_t)(v))

static uint16_t lz_hash(uint16_t pos) {
    uint8_t db = g_cpu.DB;
    uint16_t a = bus_read16(db, (uint16_t)(0x4400 + pos)) ^ bus_read16(db, (uint16_t)(0x4401 + pos)) ^
                 bus_read16(db, (uint16_t)(0x4402 + pos));
    return (uint16_t)((a & 0x00FF) << 1);
}

/* $01:ECD5: empty hash heads and ring; $16 = $18 = $1A = 0. */
void mp_01ECD5(void) {
    for (uint16_t x = 0; x != 0x0200; x += 2) W7F(0x0004 + x, 0xFFFF);
    for (uint16_t x = 0; x != 0x0100; x += 2) {
        W7F(0x0204 + x, 0xFFFF); W7F(0x0304 + x, 0xFFFF); W7F(0x0404 + x, 0xFFFF);
    }
    DPW(0x16, 0); DPW(0x18, 0); DPW(0x1A, 0);
    g_cpu.X = 0x0100;
    CPU_SET_A16(0xFFFF);
}

/* $01:ED03: insert position $18 into the ring at slot $16 (evicting the
 * slot's old entry from its chain), as the newest entry of its hash chain;
 * then advance the slot (wrapping at $100) and the position. X, Y kept. */
void mp_01ED03(void) {
    uint16_t sx = g_cpu.X, sy = g_cpu.Y;
    DPW(0x1A, lz_hash(DPR(0x18)));
    uint16_t slot = DPR(0x16);
    uint16_t a = T7F(0x0304 + slot);
    if (!(a & 0x8000)) {                                    /* the slot is in use: unlink it */
        if (!(a & 0x4000)) {                                /* prev is a ring slot */
            uint16_t prev = a;
            uint16_t nxt = T7F(0x0204 + slot);
            W7F(0x0204 + prev, nxt);
            W7F(0x0304 + nxt, prev);
        } else {                                            /* prev is the hash head */
            uint16_t head = a & 0x01FF;
            uint16_t nxt = T7F(0x0204 + slot);
            W7F(0x0004 + head, nxt);
            W7F(0x0304 + nxt, (uint16_t)(head | 0x4000));
        }
    }
    uint16_t x = DPR(0x1A);
    a = T7F(0x0004 + x);
    if (a & 0x8000) {                                       /* empty chain */
        W7F(0x0004 + x, DPR(0x16));
        uint16_t b = x;
        x = DPR(0x16);
        W7F(0x0304 + x, (uint16_t)(b | 0x4000));
    } else {                                                /* append at the chain's end */
        do { x = a; a = T7F(0x0204 + x); } while (!(a & 0x8000));
        W7F(0x0204 + x, DPR(0x16));
        uint16_t b = x;
        x = DPR(0x16);
        W7F(0x0304 + x, b);
    }
    W7F(0x0204 + x, 0xFFFF);
    W7F(0x0404 + x, DPR(0x18));
    x = (uint16_t)(x + 2);
    if (x == 0x0100) x = 0;
    DPW(0x16, x);
    DPW(0x18, DPR(0x18) + 1);
    CPU_SET_A16(DPR(0x18) - 1);
    g_cpu.X = sx; g_cpu.Y = sy;
}

/* $01:EDAB: X = position: first candidate of its hash chain. Carry set
 * with A = its position and $1C = its slot; clear when the chain is empty. */
void mp_01EDAB(void) {
    uint16_t x = lz_hash(g_cpu.X);
    uint16_t a = T7F(0x0004 + x);
    if (a & 0x8000) { g_cpu.X = x; CPU_SET_A16(a); g_cpu.flag_C = false; return; }
    g_cpu.X = a;
    CPU_SET_A16(T7F(0x0404 + a));
    DPW(0x1C, a);
    g_cpu.flag_C = true;
}
/* $01:EDCA: the next candidate after slot $1C (same results). */
void mp_01EDCA(void) {
    uint16_t x = DPR(0x1C);
    uint16_t a = T7F(0x0204 + x);
    g_cpu.X = x;
    if (a & 0x8000) { CPU_SET_A16(a); g_cpu.flag_C = false; return; }
    g_cpu.X = a;
    CPU_SET_A16(T7F(0x0404 + a));
    DPW(0x1C, a);
    g_cpu.flag_C = true;
}

/* $01:EFB9: progress: add the 32-bit step $0551 to $0555; every $054D
 * frames ($016C) call the progress routine whose address is at $0549
 * (entered by RTL, returning to $01:F00D), out of vblank with NMI off,
 * then $E09B and $E2F3. X, Y kept. */
void mp_01EFB9(void) {
    uint16_t lo = bus_wram_read16(0x0555), add = bus_wram_read16(0x0551);
    uint32_t sum = (uint32_t)lo + add;
    bus_wram_write16(0x0555, (uint16_t)sum);
    bus_wram_write16(0x0557, (uint16_t)(bus_wram_read16(0x0557) + bus_wram_read16(0x0553) + (sum >> 16)));
    if (bus_wram_read16(0x016C) < bus_wram_read16(0x054D)) return;
    uint16_t sx = g_cpu.X, sy = g_cpu.Y, s0 = g_cpu.S;
    g_cpu.S = (uint16_t)(g_cpu.S - 4);                      /* PHX PHY */
    bus_wram_write16(0x016C, 0x0000);
    bus_wram_write16(0x0446, 0x0004);
    call_jsl(0x01E30E);
    while ((bus_read8(0x00, 0x4212) & 0x80) && recomp_timed_spin(64)) {}
    uint32_t cb = (uint32_t)bus_wram_read8(0x054B) << 16 | (uint16_t)(bus_wram_read16(0x0549) + 1);
    call_jsl(cb);                                           /* RTL into it; it returns to $F00D */
    call_jsl(0x01E09B);
    call_jsl(0x01E2F3);
    g_cpu.S = s0;
    g_cpu.X = sx; g_cpu.Y = sy;
}

static void lz_step(void) { call_jsr(0x01EFB9); call_jsr(0x01ED03); }
static uint8_t lz_in(uint16_t pos) { return bus_read8(0x7E, (uint16_t)(0x4400 + pos)); }

/* ---- $01:EDDB: compress $BA52 bytes from $7E:4400 to $7F:2000. Output:
 * word 1, the first byte, then groups: a word count n (literals, up to
 * $7FFF) followed by n bytes, or a match word $8000 | len << 8 | distance
 * (len 4-$12) followed by a new count word. Ends with $FFFF; the total
 * size goes to $7F:0002 and A = 0. If the output would pass $7F:FEFC the
 * ROM leaves with A = $FFFF through an RTS (one byte short of its JSL
 * frame): that path is handed to the ROM's own return target. */
void mp_01EDDB(void) {
    uint16_t s_entry = g_cpu.S;
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    bus_wram_write8(s_entry, db);                           /* PHB */
    g_cpu.DB = 0x7E;
    bus_wram_write16((uint16_t)(s_entry - 2), dp);          /* PHD */
    uint16_t frame = (uint16_t)(s_entry - 3 - 0x1E);
    g_cpu.S = frame; g_cpu.DP = frame;                      /* TSC; SBC #$1E; TCS; TCD */

    call_jsr(0x01ECD5);
    DPW(0x14, 0xBA52);
    DPW(0x0E, 0xFEFC);
    DPW(0x10, 0x2000);
    DPW(0x12, 0x2000);
    write_long16(0x7F2000, 0x0001);
    bus_write8(0x7F, 0x2002, lz_in(0));
    DPW(0x02, 0x2003);
    DPW(0x04, 0x0001);
    lz_step();
    uint16_t y;
    for (;;) {
        DPW(0x0A, 0);                                       /* $EE22 */
        g_cpu.X = DPR(0x04);
        call_jsr(0x01EDAB);
        bool match = false;
        if (g_cpu.flag_C) {
            for (;;) {                                      /* $EE2B: try this candidate */
                uint16_t cand = CPU_A16();
                DPW(0x06, cand);
                uint16_t x = cand; y = DPR(0x04);
                DPW(0x08, 0);
                for (;;) {
                    if (lz_in(y) != lz_in(x)) break;
                    y++; x++;
                    DPW(0x08, DPR(0x08) + 1);
                    if (DPR(0x08) == 0x0012) break;
                }
                if (DPR(0x08) == 0x0012) {                  /* longest possible */
                    DPW(0x0A, DPR(0x08));
                    DPW(0x0C, DPR(0x06));
                    match = true;
                    break;
                }
                if (DPR(0x08) >= DPR(0x0A)) { DPW(0x0A, DPR(0x08)); DPW(0x0C, DPR(0x06)); }
                g_cpu.X = x; g_cpu.Y = y;
                call_jsr(0x01EDCA);
                if (!g_cpu.flag_C) break;
            }
            if (!match && DPR(0x0A) >= 0x0004) match = true;
        }
        if (match) {                                        /* $EE6D */
            for (y = DPR(0x0A); y; y--) { g_cpu.Y = y; lz_step(); }
            y = DPR(0x02);
            if (read_long16(0x7F0000u + DPR(0x12)) == 0) y = (uint16_t)(y - 2);   /* no literals: reuse */
            uint16_t x = y;
            DPW(0x0C, (uint16_t)(DPR(0x04) - DPR(0x0C)));
            uint16_t len = DPR(0x0A);
            write_long16(0x7F0000u + x, (uint16_t)(((len << 8) | (len >> 8)) | DPR(0x0C) | 0x8000));
            x = (uint16_t)(x + 2);
            write_long16(0x7F0000u + x, 0x0000);
            DPW(0x12, x);
            x = (uint16_t)(x + 2);
            if (x >= DPR(0x0E)) goto overflow;
            DPW(0x02, x);
            y = (uint16_t)(DPR(0x0A) + DPR(0x04));
        } else {                                            /* $EEB2: one literal */
            uint16_t x = DPR(0x02);
            y = DPR(0x04);
            bus_write8(0x7F, x, lz_in(y));
            x++;
            DPW(0x02, x);
            x = DPR(0x12);
            uint16_t n = (uint16_t)(read_long16(0x7F0000u + x) + 1);
            write_long16(0x7F0000u + x, n);
            if (n & 0x8000) {                               /* count full: new group */
                write_long16(0x7F0000u + x, (uint16_t)(n - 1));
                x = DPR(0x02);
                write_long16(0x7F0000u + x, 0x0000);
                DPW(0x12, x);
                x = (uint16_t)(x + 2);
                bus_write8(0x7F, x, lz_in(y));
                DPW(0x02, x);
            }
            g_cpu.Y = y;
            lz_step();
            if (DPR(0x02) >= DPR(0x0E)) goto overflow;
            y++;
        }
        DPW(0x04, y);                                       /* $EEFD */
        if (y >= DPR(0x14)) break;
    }
    {                                                       /* $EF06 */
        uint16_t x = (uint16_t)(DPR(0x02) - 2);
        write_long16(0x7F0000u + x, 0xFFFF);
        x = (uint16_t)(x + 3);
        uint16_t size = (uint16_t)(x - DPR(0x10));
        DPW(0x14, size);
        write_long16(0x7F0002, size);
        g_cpu.X = x; g_cpu.Y = y;
        g_cpu.S = s_entry; g_cpu.DP = dp; g_cpu.DB = db;
        CPU_SET_A16(0x0000);
        g_cpu.flag_Z = true; g_cpu.flag_N = false;
        return;
    }
overflow:                                                   /* $EF2A */
    g_cpu.S = s_entry; g_cpu.DP = dp; g_cpu.DB = db;
    CPU_SET_A16(0xFFFF);
    {
        uint16_t ret = bus_wram_read16((uint16_t)(s_entry + 1));
        g_cpu.S = (uint16_t)(s_entry + 2);                  /* RTS pops two of the JSL's three bytes */
        recomp_set_redirect(0x010000u | (uint16_t)(ret + 1));
    }
}

/* Stack pushes/pulls as PHA/PLA do them (16-bit), for routines that pass
 * arguments on the stack. */
static void push16(uint16_t v) {
    bus_wram_write8(g_cpu.S, (uint8_t)(v >> 8));
    bus_wram_write8((uint16_t)(g_cpu.S - 1), (uint8_t)v);
    g_cpu.S = (uint16_t)(g_cpu.S - 2);
}
static uint16_t pull16(void) {
    uint16_t v = bus_wram_read16((uint16_t)(g_cpu.S + 1));
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    return v;
}

/* ---- $01:DD79: 32-bit unsigned division with stack arguments. The caller
 * pushed the dividend (hi, lo) and then the divisor (hi, lo) before its
 * JSL; on return the divisor's 4 bytes are gone (the return frame and the
 * saved P are moved up over them) and the dividend's slots hold the
 * quotient. Restoring division, 33 steps, remainder in the routine's own
 * locals. A comes back as the moved word (return high byte, bank). */
void mp_01DD79(void) {
    uint16_t s0 = g_cpu.S;
    uint8_t p = cpu_get_p();
    uint32_t dvsr = bus_wram_read16((uint16_t)(s0 + 4)) | (uint32_t)bus_wram_read16((uint16_t)(s0 + 6)) << 16;
    uint32_t q = bus_wram_read16((uint16_t)(s0 + 8)) | (uint32_t)bus_wram_read16((uint16_t)(s0 + 10)) << 16;
    uint32_t rem = 0;
    for (int i = 0; i < 0x21; i++) {
        uint32_t c = rem >= dvsr;
        if (c) rem -= dvsr;
        uint32_t qc = q >> 31;
        q = (q << 1) | c;
        rem = (rem << 1) | qc;
    }
    /* PHP PHD PHX PEA PEA: the locals frame (remainder at its $01/$03). */
    bus_wram_write8(s0, p);
    bus_wram_write16((uint16_t)(s0 - 2), g_cpu.DP);
    bus_wram_write16((uint16_t)(s0 - 4), g_cpu.X);
    bus_wram_write16((uint16_t)(s0 - 8), (uint16_t)rem);
    bus_wram_write16((uint16_t)(s0 - 6), (uint16_t)(rem >> 16));
    bus_wram_write16((uint16_t)(s0 + 8), (uint16_t)q);
    bus_wram_write16((uint16_t)(s0 + 10), (uint16_t)(q >> 16));
    uint16_t w1 = bus_wram_read16(s0);                      /* P, return low */
    uint16_t w2 = bus_wram_read16((uint16_t)(s0 + 2));      /* return high, bank */
    bus_wram_write16((uint16_t)(s0 + 4), w1);
    bus_wram_write16((uint16_t)(s0 + 6), w2);
    CPU_SET_A16(w2);
    g_cpu.S = (uint16_t)(s0 + 4);                           /* PLP; RTL from the moved frame */
    cpu_set_p(p);
}

/* ---- $00:D659: the save/load progress in percent: $0555/$0557 (done,
 * 0 while negative) / $0559/$055B (total) via $01:DD79, as sprites $C2+
 * digit at y $62 ("100" with the leading 1 at x $78). Only while $055D. */
void mp_00D659(void) {
    if (bus_read16(g_cpu.DB, 0x055D) == 0) return;
    uint16_t hi = bus_read16(g_cpu.DB, 0x0557);
    if (hi & 0x8000) { push16(0); push16(0); }
    else { push16(hi); push16(bus_read16(g_cpu.DB, 0x0555)); }
    push16(bus_read16(g_cpu.DB, 0x055B));
    push16(bus_read16(g_cpu.DB, 0x0559));
    call_jsl(0x01DD79);
    uint16_t a = pull16();
    (void)pull16();                                         /* PLY: quotient high */
    uint16_t tens = 0;
    while (a >= 0x000A) { a = (uint16_t)(a - 10); tens++; }
    uint16_t units = a;
    if (tens >= 0x000A) {
        g_cpu.X = 0x0078; g_cpu.Y = 0x0062; CPU_SET_A16(0x00C3); call_jsl(0x01F91E);
        g_cpu.X = 0x0080; g_cpu.Y = 0x0062; CPU_SET_A16(0x00C2); call_jsl(0x01F91E);
        units = 0;
    } else if (tens != 0) {
        g_cpu.X = 0x0080; g_cpu.Y = 0x0062; CPU_SET_A16((uint16_t)(tens + 0x00C2)); call_jsl(0x01F91E);
    }
    g_cpu.X = 0x0088; g_cpu.Y = 0x0062; CPU_SET_A16((uint16_t)(units + 0x00C2)); call_jsl(0x01F91E);
}

/* ---- $00:D5F5: the save/load screen's sprites: progress, the pointer
 * ($1980, unless negative), the disk, objects $16-$0E, a sound when $1ADD =
 * $100, the arrows. DB = 0 meanwhile. */
void mp_00D5F5(void) {
    uint8_t db = g_cpu.DB;
    g_cpu.DB = 0x00;
    call_jsr(0x00D659);
    uint16_t ptr = bus_read16(0x00, 0x1980);
    if (!(ptr & 0x8000)) { g_cpu.X = 0x0070; g_cpu.Y = 0x00CC; CPU_SET_A16(ptr); call_jsl(0x01F91E); }
    g_cpu.X = 0x0049; g_cpu.Y = 0x00AE; CPU_SET_A16(0x008C); call_jsl(0x01F91E);
    for (uint16_t o = 0x16; o >= 0x0E; o--) { CPU_SET_A16(o); call_jsl(0x01F84F); }
    if (bus_read16(0x00, 0x1ADD) == 0x0100) { CPU_SET_A16(0x007C); call_jsl(0x01D368); }
    CPU_SET_A16(0x0019); call_jsl(0x01F84F);
    g_cpu.X = 0x0080; g_cpu.Y = 0x007A; CPU_SET_A16(0x0045); call_jsl(0x01F91E);
    CPU_SET_A16(0x0017); call_jsl(0x01F84F);
    CPU_SET_A16(0x0018); call_jsl(0x01F84F);
    g_cpu.DB = db;
}

/* ---- $00:D824: objects $0D-$0A, $18 and $30. */
void mp_00D824(void) {
    for (uint16_t o = 0x0D; o >= 0x0A; o--) { CPU_SET_A16(o); call_jsl(0x01F84F); }
    CPU_SET_A16(0x0018); call_jsl(0x01F84F);
    CPU_SET_A16(0x0030); call_jsl(0x01F84F);
}

/* ---- $00:D590: the save/load screen's sprites as $D5F5 draws them, with
 * each object's display data loaded first ($F825); DB as the caller has it. */
void mp_00D590(void) {
    call_jsr(0x00D659);
    uint16_t ptr = bus_read16(g_cpu.DB, 0x1980);
    if (!(ptr & 0x8000)) { g_cpu.X = 0x0070; g_cpu.Y = 0x00CC; CPU_SET_A16(ptr); call_jsl(0x01F91E); }
    g_cpu.X = 0x0049; g_cpu.Y = 0x00AE; CPU_SET_A16(0x008C); call_jsl(0x01F91E);
    for (uint16_t o = 0x16; o >= 0x0E; o--) {
        CPU_SET_A16(o); call_jsl(0x01F825);
        CPU_SET_A16(o); call_jsl(0x01F84F);
    }
    CPU_SET_A16(0x0019); call_jsl(0x01F825);
    CPU_SET_A16(0x0019); call_jsl(0x01F84F);
    g_cpu.X = 0x0080; g_cpu.Y = 0x007A; CPU_SET_A16(0x0045); call_jsl(0x01F91E);
    CPU_SET_A16(0x0017); call_jsl(0x01F825);
    CPU_SET_A16(0x0017); call_jsl(0x01F84F);
    CPU_SET_A16(0x0018); call_jsl(0x01F84F);
}

/* ---- $00:D842: progress setup: total = $0555/$0557 / 100 into $0559/
 * $055B (one percent), step $0551/$0553 = -1, shown ($055D = -1). */
void mp_00D842(void) {
    uint8_t db = g_cpu.DB;
    push16(bus_read16(db, 0x0557));
    push16(bus_read16(db, 0x0555));
    push16(0x0000);
    push16(0x0064);
    call_jsl(0x01DD79);
    bus_write16(db, 0x0559, pull16());
    bus_write16(db, 0x055B, pull16());
    bus_write16(db, 0x0551, 0xFFFF);
    bus_write16(db, 0x0553, 0xFFFF);
    bus_write16(db, 0x055D, 0xFFFF);
    CPU_SET_A16(0xFFFF);
}

/* ---- $00:D73C: the 13-byte HDMA table at $00:D74E to $7E:4000. */
void mp_00D73C(void) {
    for (int x = 0x0C; x >= 0; x--) bus_write8(0x7E, (uint16_t)(0x4000 + x), bus_read8(g_cpu.DB, (uint16_t)(0xD74E + x)));
    g_cpu.X = 0xFFFF;
}

/* ---- $00:D52C: A = pointer (DB) to 11 bytes: the first ten become the
 * save slots' states $1A85 + 8k (byte << 8 | $FF, last slot first), the
 * eleventh $1ADD. */
void mp_00D52C(void) {
    uint16_t ptr = CPU_A16();
    uint32_t base = ((uint32_t)g_cpu.DB << 16) + ptr;
#define PB(y) bus_read8((uint8_t)((base + (y)) >> 16), (uint16_t)(base + (y)))
    uint16_t x = 0x0048;
    for (int y = 9; y >= 0; y--, x = (uint16_t)(x - 8))
        bus_write16(g_cpu.DB, (uint16_t)(0x1A85 + x), (uint16_t)((PB(y) << 8) | 0x00FF));
    bus_write16(g_cpu.DB, 0x1ADD, (uint16_t)((PB(10) << 8) | 0x00FF));
#undef PB
    g_cpu.X = x; g_cpu.Y = 0x000A;
    CPU_SET_A16(ptr);
}

/* $00:C6C6: A rounds of: sprite/OAM refresh ($8B48, $9378), $0446 = 4,
 * the objects $22 down to $1B ($01:F84F with the number on the stack),
 * the colour fade step ($01:E09B) and a frame. */
void mp_00C6C6(void) {
    uint16_t n = CPU_A16();
    do {
        push_a16(n);
        CPU_SET_A16(n);
        call_jsr(0x008B48);
        call_jsr(0x009378);
        bus_write16(g_cpu.DB, 0x0446, 0x0004);
        for (uint16_t i = 0x0022; ; i--) {
            push_a16(i);
            CPU_SET_A16(i);
            call_jsl(0x01F84F);
            g_cpu.S = (uint16_t)(g_cpu.S + 2);
            CPU_SET_A16((uint16_t)(i - 1));
            if ((int16_t)(uint16_t)(i - 1 - 0x001B) < 0) break;
        }
        call_jsl(0x01E09B);
        call_jsl(0x01E2CE);
        n = bus_wram_read16((uint16_t)(g_cpu.S + 1));
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
        n--;
        CPU_SET_A16(n);
    } while (n != 0);
}

/* $00:C873: with $19FA, the page $19C2's row of tiles into the buffer at
 * $0326 (tile, then $30B1 + index), its two object masks into $0436/$0438
 * and the $78 timer in $0545. Tables at $C8C2/$C8C8/$C8F4 (through DB). */
void mp_00C873(void) {
    uint8_t db = g_cpu.DB;
    if (bus_read16(db, 0x19FA) == 0) return;
    uint16_t pg = bus_read16(db, 0x19C2);
    uint16_t y = bus_read16(db, (uint16_t)(0xC8C2 + pg * 2));
    uint16_t src = (uint16_t)(bus_read16(db, (uint16_t)(0xC8C8 + pg * 2)) + 0xC8CE);
    for (uint16_t x = 0; ; x = (uint16_t)(x + 4)) {
        bus_write16(db, (uint16_t)(0x0326 + x), bus_read16(db, (uint16_t)(src + y)));
        bus_write16(db, (uint16_t)(0x0328 + x), (uint16_t)((y >> 1) + 0x30B1));
        y = (uint16_t)(y - 2);
        if (y & 0x8000) break;
    }
    bus_write16(db, 0x0436, bus_read16(db, (uint16_t)(0xC8F4 + pg * 4)) & bus_read16(db, 0x0436));
    bus_write16(db, 0x0438, bus_read16(db, (uint16_t)(0xC8F6 + pg * 4)) & bus_read16(db, 0x0438));
    bus_write16(db, 0x0545, 0x0078);
}

/* $00:BA69: the two sprite scripts $CEB2 and $CF16 ($01:CDE1). */
void mp_00BA69(void) {
    CPU_SET_A16(0xCEB2); call_jsl(0x01CDE1);
    CPU_SET_A16(0xCF16); call_jsl(0x01CDE1);
}

/* $00:8994 (JSL): back to the canvas: $87EE, the tool switch to the plain
 * canvas (tool 7, no words), $09AB with the stamp tool, palette record. */
void mp_008994(void) {
    call_jsr(0x0087EE);
    call_tool_switch(0x0007, 0, 0);
    if (bus_read16(g_cpu.DB, 0x04D0) == 0x0006)
        bus_write16(g_cpu.DB, 0x09AB, (uint16_t)(bus_read16(g_cpu.DB, 0x09AB) + 1));
    call_jsr(0x00A363);
}

/* $00:C490: copy a block of tile words into WRAM, skipping the $01FF
 * holes. Stack arguments (pushed by the caller, removed here like the ROM
 * does by moving the return address): rows (low 5 bits, x $40 bytes),
 * source in bank $02, destination in bank $7E. */
void mp_00C490(void) {
    uint16_t s0 = g_cpu.S, dp0 = g_cpu.DP;
    uint16_t d = (uint16_t)(s0 - 0x12);
    bus_wram_write16((uint16_t)(s0 - 1), dp0);              /* PHD; TSC; SBC #$10; TCS; TCD */
    g_cpu.S = d; g_cpu.DP = d;
    uint16_t bytes = (uint16_t)((bus_wram_read16((uint16_t)(s0 + 3)) & 0x001F) << 6);
    bus_wram_write16((uint16_t)(s0 + 3), bytes);
    uint16_t src = bus_wram_read16((uint16_t)(s0 + 5));
    uint16_t dst = bus_wram_read16((uint16_t)(s0 + 7));
    bus_wram_write16((uint16_t)(d + 1), src);
    bus_wram_write16((uint16_t)(d + 3), 0x0002);
    bus_wram_write16((uint16_t)(d + 5), dst);
    bus_wram_write16((uint16_t)(d + 7), 0x007E);
    uint16_t y = 0, a;
    do {
        uint32_t sa = 0x020000u + src + y, da = 0x7E0000u + dst + y;
        a = bus_read16((uint8_t)(sa >> 16), (uint16_t)sa);
        if (a != 0x01FF) bus_write16((uint8_t)(da >> 16), (uint16_t)da, a);
        y = (uint16_t)(y + 2);
    } while (y != bytes);
    g_cpu.DP = dp0;                                         /* PLD; PLY; S += 6; PHY */
    uint16_t ret = bus_wram_read16((uint16_t)(s0 + 1));
    bus_wram_write16((uint16_t)(s0 + 7), ret);
    g_cpu.S = (uint16_t)(s0 + 6);
    g_cpu.Y = ret;
    CPU_SET_A16((uint16_t)(s0 + 8));
}

/* $01:904A: a circle (midpoint method, eight octants) with stack
 * arguments radius, centre Y, centre X (pushed in that reverse order; the
 * radius slot is the running error term). The points go through $01:916E
 * (right half) and $01:9130 (left half); the X/Y offsets are squashed by
 * 3/16 for the screen's pixel aspect. Removes its arguments by moving the
 * return address, like the ROM. */
void mp_01904A(void) {
    uint16_t s0 = g_cpu.S;
#define ARG(o)     bus_wram_read16((uint16_t)(s0 + (o)))
#define SETARG(o,v) bus_wram_write16((uint16_t)(s0 + (o)), (uint16_t)(v))
    call_jsr(0x0191AC);
    uint16_t y = ARG(3), x = 0, cy = ARG(5), cx = ARG(7);
    uint16_t l = s0 - 8;                                   /* locals $01..$08,s */
    bus_wram_write16((uint16_t)(l + 7), y);
    bus_wram_write16((uint16_t)(l + 5), 0);
    bus_wram_write16((uint16_t)(l + 3), 0);
    bus_wram_write16((uint16_t)(l + 1), 0);
    g_cpu.S = l;
    while (y >= x) {
        uint16_t sy = (uint16_t)(y - (uint16_t)(((uint16_t)(y * 3)) >> 4));
        uint16_t sx = (uint16_t)(x - (uint16_t)(((uint16_t)(x * 3)) >> 4));
        bus_wram_write16((uint16_t)(l + 3), sy);
        bus_wram_write16((uint16_t)(l + 1), sx);
        const struct { int dxs; uint16_t dx; int dys; uint16_t dy; uint32_t f; } p[8] = {
            { +1, sy, +1, x, 0x01916E }, { +1, sy, -1, x, 0x01916E },
            { -1, sy, +1, x, 0x019130 }, { -1, sy, -1, x, 0x019130 },
            { +1, sx, +1, y, 0x01916E }, { +1, sx, -1, y, 0x01916E },
            { -1, sx, +1, y, 0x019130 }, { -1, sx, -1, y, 0x019130 },
        };
        for (int i = 0; i < 8; i++) {
            g_cpu.X = (uint16_t)(p[i].dxs > 0 ? cx + p[i].dx : cx - p[i].dx);
            g_cpu.Y = (uint16_t)(p[i].dys > 0 ? cy + p[i].dy : cy - p[i].dy);
            CPU_SET_A16(g_cpu.Y);
            call_jsr(p[i].f);
        }
        uint16_t d = (uint16_t)(ARG(3) - (uint16_t)((x << 1) - 1));
        x = (uint16_t)(x + 1);
        bus_wram_write16((uint16_t)(l + 5), x);
        SETARG(3, d);
        if (d & 0x8000) {
            y = (uint16_t)(y - 1);
            bus_wram_write16((uint16_t)(l + 7), y);
            SETARG(3, (uint16_t)(d + (uint16_t)(y << 1)));
        }
    }
    SETARG(7, ARG(1));                                     /* return address over centre X */
    g_cpu.S = (uint16_t)(s0 + 6);
    CPU_SET_A16(g_cpu.S);
#undef ARG
#undef SETARG
}

/* ---- The copy tool's buffer work (bank $01, DP = the canvas DP) -------
 * $7F:0000/0002 width/height-1 of the copied area, $7F:0004/0006 its left
 * and right pixel offsets in their 8-pixel columns, $7F:0008.. the copied
 * 2bpp tile rows ($80 bytes per row of columns). DP $0A/$0D are long
 * pointers into the canvas tile buffer (planes 0-1 and 2-3). */
#define CR(o)    bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define CW(o, v) bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
#define K16(o)   bus_read16(0x7F, (uint16_t)(o))
#define KW16(o, v) bus_write16(0x7F, (uint16_t)(o), (uint16_t)(v))

static uint32_t dp_long(uint16_t o) {
    uint16_t a = (uint16_t)(g_cpu.DP + o);
    return bus_read16(0x00, a) | ((uint32_t)bus_read8(0x00, (uint16_t)(a + 2)) << 16);
}
static uint16_t long_r16(uint32_t a) { return bus_read16((uint8_t)(a >> 16), (uint16_t)a); }
static void long_w16(uint32_t a, uint16_t v) { bus_write16((uint8_t)(a >> 16), (uint16_t)a, v); }

/* $01:D9A9: the tile-buffer offset $84 of the canvas pixel ($80, $82). */
void mp_01D9A9(void) {
    uint16_t v = (uint16_t)((CR(0x80) & 0x00F8) << 2);
    v = (uint16_t)(v + ((CR(0x82) & 0x00F8) << 7));
    v = (uint16_t)(v + ((CR(0x82) & 0x0007) << 1));
    v = (uint16_t)(v - 0x0800);
    CW(0x84, v);
    CPU_SET_A16(v);
}

/* $01:D7F0: one row of the area ($C2 + 1 columns) from the canvas at $84
 * into the copy buffer at $7F:0008 + X. X and Y are kept. */
void mp_01D7F0(void) {
    uint16_t x = g_cpu.X, y0 = g_cpu.Y;
    uint32_t base = dp_long(0x0A);
    CW(0xC0, CR(0xC2));
    uint16_t y = CR(0x84), xx = x;
    do {
        KW16(0x0008 + xx, long_r16(base + y));
        y = (uint16_t)(y + 0x10);
        xx = (uint16_t)(xx + 2);
        CW(0xC0, CR(0xC0) - 1);
    } while (!(CR(0xC0) & 0x8000));
    g_cpu.X = x; g_cpu.Y = y0;
}

/* $01:D8CE: the copied pixel (both plane pairs, each expanded to a full
 * byte mask in $C6 / $00:00C8) for target column $C0, row $C4, read
 * through the flip/turn mode $0997 (0-7). X and Y are kept. */
void mp_01D8CE(void) {
    uint16_t x0 = g_cpu.X, y0 = g_cpu.Y;
    uint16_t c0 = CR(0xC0), c4 = CR(0xC4), w = K16(0x0000), h = K16(0x0002);
    uint16_t col, row;
    switch (bus_read16(g_cpu.DB, 0x0997) & 0x00FF) {
    case 0: col = c0; row = c4; break;
    case 1: col = (uint16_t)(w - c0); row = c4; break;
    case 2: col = c0; row = (uint16_t)(h - c4); break;
    case 3: col = (uint16_t)(w - c0); row = (uint16_t)(h - c4); break;
    case 4: col = (uint16_t)(h - c4); row = c0; break;
    case 5: col = c4; row = (uint16_t)(w - c0); break;
    case 6: col = c4; row = c0; break;
    default: col = (uint16_t)(h - c4); row = (uint16_t)(w - c0); break;
    }
    uint16_t t = (uint16_t)(col + K16(0x0004));                       /* $D8E7 */
    uint16_t xi = (uint16_t)(((t & 0xFFF8) >> 1) + ((row & 0x00FF) << 7));
    uint16_t m = bus_read16(0x01, (uint16_t)(0xD9D1 + ((t & 7) << 1)));
    for (int k = 0; k < 2; k++) {
        uint16_t a = (uint16_t)(K16(0x0008 + 2 * k + xi) & m);
        if (a & 0x00FF) a |= 0x00FF;
        if (a & 0xFF00) a |= 0xFF00;
        if (k == 0) CW(0xC6, a); else bus_write16(0x00, 0x00C8, a);
        CPU_SET_A16(a);
    }
    g_cpu.X = x0; g_cpu.Y = y0;
}

/* $01:D874: one row of the copy laid onto the canvas from $84 on: column
 * $C0 from the left edge clip, while inside the canvas; each pixel merged
 * into both plane pairs ([$0A], [$0D]) through the pixel mask $01:D9D1. */
void mp_01D874(void) {
    uint16_t v = (uint16_t)(bus_read16(g_cpu.DB, 0x19B6) - CR(0x22));
    CW(0xC0, (v & 0x8000) ? 0 : v);
    uint16_t y = CR(0x84);
    uint32_t p0 = dp_long(0x0A), p1 = dp_long(0x0D);
    for (;;) {
        uint16_t a = (uint16_t)(CR(0x22) + CR(0xC0) - 1);
        if (a >= bus_read16(g_cpu.DB, 0x19B8)) break;
        uint16_t x = (uint16_t)((((uint16_t)(a + 1)) & 0x0007) << 1);
        g_cpu.X = x; g_cpu.Y = y;
        mp_01D8CE();
        if ((CR(0xC6) | CR(0xC8)) != 0) {
            uint16_t m = bus_read16(0x01, (uint16_t)(0xD9D1 + x));
            uint16_t d = long_r16(p0 + y);
            long_w16(p0 + y, (uint16_t)(((CR(0xC6) ^ d) & m) ^ d));
            d = long_r16(p1 + y);
            long_w16(p1 + y, (uint16_t)(((CR(0xC8) ^ d) & m) ^ d));
        }
        if (x == 0x000E) y = (uint16_t)(y + 0x0020);
        CW(0xC0, CR(0xC0) + 1);
        if (CR(0xC0) > K16(0x0000)) break;
    }
    g_cpu.Y = y;
}

/* $01:D75B (JSL): copy the selected area ($22,$24)-($26,$28) from the
 * canvas into the copy buffer; for the turned modes ($0997 >= 4) width and
 * height swap and the far corner moves with them. */
void mp_01D75B(void) {
    KW16(0x0000, CR(0x26) - CR(0x22));
    KW16(0x0002, CR(0x28) - CR(0x24));
    KW16(0x0004, CR(0x22) & 0x0007);
    KW16(0x0006, CR(0x26) & 0x0007);
    CW(0xC2, (uint16_t)((((uint16_t)((CR(0x26) & 0xFFF8) - (CR(0x22) & 0xFFF8))) >> 2) + 1));
    CW(0x80, CR(0x22));
    CW(0x82, CR(0x24));
    call_jsr(0x01D9A9);
    uint16_t x = 0, y = K16(0x0002);
    for (;;) {
        g_cpu.X = x; g_cpu.Y = y;
        call_jsr(0x01D7F0);
        uint16_t o = (uint16_t)(CR(0x84) + 2);
        if (!(o & 0x000F)) o = (uint16_t)(o + 0x03F0);
        CW(0x84, o);
        x = (uint16_t)(x + 0x0080);
        y = (uint16_t)(y - 1);
        if (y & 0x8000) break;
    }
    g_cpu.X = x; g_cpu.Y = y;
    if (bus_read16(g_cpu.DB, 0x0997) >= 0x0004) {
        uint16_t w = K16(0x0000);
        KW16(0x0000, K16(0x0002));
        KW16(0x0002, w);
        CW(0x26, CR(0x22) + K16(0x0000));
        CW(0x28, CR(0x24) + K16(0x0002));
    }
}

/* $01:D810 (JSL): lay the copy down at ($22,$24), clipped to the canvas,
 * row by row ($D874) until its height or the canvas bottom. */
void mp_01D810(void) {
    uint8_t db = g_cpu.DB;
    CW(0x0D, CR(0x0A) + 0x0010);
    uint16_t a = CR(0x22);
    if ((uint16_t)(a - bus_read16(db, 0x19B6)) & 0x8000) a = bus_read16(db, 0x19B6);
    CW(0x80, a);
    a = CR(0x24);
    if ((uint16_t)(a - bus_read16(db, 0x19BA)) & 0x8000) a = (uint16_t)(bus_read16(db, 0x19BA) + 1);
    CW(0x82, a);
    call_jsr(0x01D9A9);
    uint16_t v = (uint16_t)(bus_read16(db, 0x19BA) - CR(0x24));
    CW(0xC4, (v & 0x8000) ? 0 : (uint16_t)(v + 1));
    for (;;) {
        call_jsr(0x01D874);
        uint16_t o = (uint16_t)(CR(0x84) + 2);
        if (!(o & 0x000F)) o = (uint16_t)(o + 0x03F0);
        CW(0x84, o);
        if (o >= 0x6000) break;
        if ((uint16_t)(CR(0xC4) + CR(0x24) - 1) >= bus_read16(db, 0x19BC)) break;
        CW(0xC4, CR(0xC4) + 1);
        if (CR(0xC4) > K16(0x0002)) break;
    }
}
#undef CR
#undef CW
#undef K16
#undef KW16

/* $00:8AFF (JSL): the canvas save ($8B03) for long callers. */
void mp_008AFF(void) { call_jsr(0x008B03); }
/* $00:C410 (JSL): $C414 for long callers. */
void mp_00C410(void) { call_jsr(0x00C414); }

/* $01:B5D1: clear the 16 words $05B5..$05D4. */
void mp_01B5D1(void) {
    for (int x = 0x001E; x >= 0; x -= 2) bus_write16(g_cpu.DB, (uint16_t)(0x05B5 + x), 0);
    g_cpu.X = 0xFFFE;
}

/* $01:B5F4 / $01:B60F: dispatch on $05D7 through the jump tables at
 * $01:B5FB / $01:B616 (the ROM's $01:E393 inline-table trick; the handler
 * starts with A = its address and returns to this routine's caller). */
static void dispatch_05D7(uint16_t table) {
    uint16_t i = bus_read16(g_cpu.DB, 0x05D7) & 0x00FF;
    uint16_t h = bus_read16(0x01, (uint16_t)(table + (i << 1)));
    CPU_SET_A16(h);
    tail_jmp(0x010000u | h);
}
void mp_01B5F4(void) { dispatch_05D7(0xB5FB); }
void mp_01B60F(void) { dispatch_05D7(0xB616); }

/* $00:D514: A frames of the stamp-sprite refresh ($0446 = 4, $D5F5,
 * the palette step $01:E09B, a frame). */
void mp_00D514(void) {
    uint16_t n = CPU_A16();
    do {
        push_a16(n);
        bus_write16(g_cpu.DB, 0x0446, 0x0004);
        CPU_SET_A16(0x0004);
        call_jsl(0x00D5F5);
        call_jsl(0x01E09B);
        call_jsl(0x01E2CE);
        n = bus_wram_read16((uint16_t)(g_cpu.S + 1));
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
        n--;
        CPU_SET_A16(n);
    } while (n != 0 && !g_quit);
}

/* $00:FD06: copy the song $0C3C.. back over the score $09E4.. ($250
 * bytes). A and X are kept. */
void mp_00FD06(void) {
    uint16_t a = CPU_A16(), x0 = g_cpu.X;
    for (int x = 0x024C; x >= 0; x -= 2)
        bus_write16(g_cpu.DB, (uint16_t)(0x09E4 + x), bus_read16(g_cpu.DB, (uint16_t)(0x0C3C + x)));
    CPU_SET_A16(a);
    g_cpu.X = x0;
}

/* $00:A87E (JSL): queue the fixed 5-word DMA record at $A89E into the
 * record list $0182.. at $0204 and count it in $0202. */
void mp_00A87E(void) {
    uint8_t db = g_cpu.DB;
    bus_write16(db, 0x0202, 0);
    uint16_t x = bus_read16(db, 0x0204);
    for (uint16_t y = 0; y != 0x000A; y += 2, x += 2)
        bus_write16(db, (uint16_t)(0x0182 + x), bus_read16(db, (uint16_t)(0xA89E + y)));
    x--;
    bus_write16(db, 0x0204, x);
    g_cpu.X = x; g_cpu.Y = 0x000A;
    bus_write16(db, 0x0202, (uint16_t)(bus_read16(db, 0x0202) + 1));
}

/* $00:AAC7: sound $7B, $10 frames, then song $1012 + 2 (or $1D when
 * $1012 is 0) by $01:D308. Flags kept. */
void mp_00AAC7(void) {
    uint8_t p0 = cpu_get_p();
    g_cpu.flag_M = false;
    CPU_SET_A16(0x007B); call_jsl(0x01D368);
    for (uint16_t n = 0x0010; n; n--) {
        push_a16(n);
        call_jsl(0x01E2CE);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
        if (g_quit) return;
    }
    uint16_t v = bus_read16(g_cpu.DB, 0x1012);
    CPU_SET_A16(v ? (uint16_t)(v + 2) : 0x001D);
    call_jsl(0x01D308);
    cpu_set_p(p0);
}

/* $00:A982 (A 8-bit): layers $11 for a frame, the whole canvas buffer
 * [$0A] ($6000 bytes) cleared, six frames, then layers $13 and a frame. */
void mp_00A982(void) {
    uint8_t p0 = cpu_get_p();
    bus_write8(g_cpu.DB, 0x011A, 0x11);
    call_jsl(0x01E2CE);
    g_cpu.flag_M = false;
    uint16_t a = (uint16_t)(g_cpu.DP + 0x0A);
    uint32_t base = bus_read16(0x00, a) | ((uint32_t)bus_read8(0x00, (uint16_t)(a + 2)) << 16);
    for (int y = 0x5FFE; y >= 0; y -= 2) {
        uint32_t t = base + (uint32_t)y;
        bus_write16((uint8_t)(t >> 16), (uint16_t)t, 0);
    }
    g_cpu.Y = 0xFFFE;
    for (int i = 0; i < 6; i++) call_jsl(0x01E2CE);
    bus_write16(g_cpu.DB, 0x011A, 0x0013);
    CPU_SET_A16(0x0013);
    call_jsl(0x01E2CE);
    cpu_set_p(p0);
}

/* $00:FA35: the screen back on ($01:E2F3), then the tool switch to the
 * music page $0C32 + 1 (with a 0 before it when $0E8C is $0F). */
void mp_00FA35(void) {
    call_jsl(0x01E2F3);
    uint16_t page = (uint16_t)(bus_read16(g_cpu.DB, 0x0C32) + 1);
    uint16_t aa = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA));
    if (bus_read16(g_cpu.DB, 0x0E8C) != 0x000F) {
        call_tool_switch(aa, 1, &page);
    } else {
        uint16_t w[2] = { 0x0000, page };
        call_tool_switch(aa, 2, w);
    }
}

/* $00:EB3B: the pattern picture for the level $0EBC: 2 + the number of
 * thresholds passed ($20 $40 $80 $C0, or $20 $40 $50 $90 with $0EBA). */
void mp_00EB3B(void) {
    uint16_t a = bus_read16(g_cpu.DB, 0x0EBC);
    uint16_t y = bus_read16(g_cpu.DB, 0x0EBA);
    static const uint16_t t0[4] = { 0x20, 0x40, 0x80, 0xC0 }, t1[4] = { 0x20, 0x40, 0x50, 0x90 };
    const uint16_t *t = y ? t1 : t0;
    uint16_t x = 2;
    for (int i = 0; i < 4 && a >= t[i]; i++) x++;
    g_cpu.X = x; g_cpu.Y = y;
    CPU_SET_A16(x);
}

/* $01:CD86: queue the fixed 5-word DMA record at $01:CDCD into the record
 * list $0182.. at $0204 and count it in $0202. */
void mp_01CD86(void) {
    bus_write16(0x00, 0x0202, 0);
    uint16_t x = bus_read16(0x00, 0x0204);
    for (uint16_t y = 0; y != 0x000A; y += 2, x += 2)
        bus_write16(0x00, (uint16_t)(0x0182 + x), bus_read16(0x01, (uint16_t)(0xCDCD + y)));
    x--;
    bus_write16(0x00, 0x0204, x);
    bus_write16(0x00, 0x0202, 0x0001);
    g_cpu.X = x; g_cpu.Y = 0x000A;
    CPU_SET_A16(0x0001);
}

/* $00:A097 (JSL): the tool switch $9FC4 for long callers: its own return
 * address moves aside ($0992, bank $0994) so that $9FC4 finds its stack
 * arguments, then goes back on the stack for the RTL. */
void mp_00A097(void) {
    uint16_t s = g_cpu.S;
    uint16_t ret = bus_wram_read16((uint16_t)(s + 1));
    uint8_t bank = bus_wram_read8((uint16_t)(s + 3));
    bus_write16(g_cpu.DB, 0x0992, ret);
    bus_write8(g_cpu.DB, 0x0994, bank);
    g_cpu.S = (uint16_t)(s + 3);
    call_jsr_ret(0x009FC4, 0xA0A5);                         /* JSR $9FC4 at $A0A3 */
    bus_wram_write8(g_cpu.S, bank);
    g_cpu.S = (uint16_t)(g_cpu.S - 1);
    push_a16(ret);
    CPU_SET_A16(ret);
}

/* $01:EB81 (JSL): counts $0563 down; at 0 the top rows of $7E:2040 are
 * cleared, the screen updated, and the tool switch to $10 ($AA) with the
 * canvas saved ($8AFF); mode $09D6 = $0C. */
void mp_01EB81(void) {
    uint8_t db = g_cpu.DB;
    uint16_t c = (uint16_t)(bus_read16(db, 0x0563) - 1);
    bus_write16(db, 0x0563, c);
    if (c != 0) return;
    for (int x = 0x007E; x >= 0; x -= 2) bus_write16(0x7E, (uint16_t)(0x2040 + x), 0x21EE);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xAA), 0x0010);
    push_a16(0x0010);
    push_a16(0x0000);
    CPU_SET_A16(0x0000);
    call_jsl_ret(0x00A097, 0x01EBA9);
    call_jsl_ret(0x008AFF, 0x01EBAD);
    bus_write16(db, 0x09D6, 0x000C);
}

/* $01:ECAF: sound 9, the tool switch with word 4, $8B14, the switch back. */
void mp_01ECAF(void) {
    CPU_SET_A16(0x0009); call_jsl(0x01D368);
    uint16_t aa = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA));
    push_a16(0x0004); push_a16(aa); push_a16(0x0001);
    CPU_SET_A16(0x0001);
    call_jsl_ret(0x00A097, 0x01ECC4);
    call_jsl(0x008B14);
    aa = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA));
    push_a16(aa); push_a16(0x0000);
    CPU_SET_A16(0x0000);
    call_jsl_ret(0x00A097, 0x01ECD3);
}

/* $01:E93A (JSL): music page 1; three rows of 4 tile words from
 * $01:E980/E988/E990 into the screen ($7E:2654/2694/26D4); the score
 * $09E4.. ($250 bytes) from the sample song $0EB6 in bank $02 (end
 * offsets at $01:E978). */
void mp_01E93A(void) {
    uint8_t db = g_cpu.DB;
    bus_write16(db, 0x0C32, 0x0001);
    for (int x = 6; x >= 0; x -= 2) {
        bus_write16(0x7E, (uint16_t)(0x2654 + x), bus_read16(0x01, (uint16_t)(0xE980 + x)));
        bus_write16(0x7E, (uint16_t)(0x2694 + x), bus_read16(0x01, (uint16_t)(0xE988 + x)));
        bus_write16(0x7E, (uint16_t)(0x26D4 + x), bus_read16(0x01, (uint16_t)(0xE990 + x)));
    }
    uint16_t x = bus_read16(0x01, (uint16_t)(0xE978 + (bus_read16(db, 0x0EB6) << 1)));
    for (int y = 0x024E; y >= 0; y -= 2, x = (uint16_t)(x - 2))
        bus_write16(db, (uint16_t)(0x09E4 + y), bus_read16(0x02, (uint16_t)(0xF310 + x)));
    g_cpu.X = x; g_cpu.Y = 0xFFFE;
}

/* $01:F018: the free slot ($0A04 = 0) with the smallest $0A06 among the
 * 4-byte slots from 4 up to $1C0E: its offset into $1C08. */
void mp_01F018(void) {
    uint8_t db = g_cpu.DB;
    uint16_t x = 0x0004, y = 0xFFFF;
    do {
        if (bus_read16(db, (uint16_t)(0x0A04 + x)) == 0 && y >= bus_read16(db, (uint16_t)(0x0A06 + x))) {
            y = bus_read16(db, (uint16_t)(0x0A06 + x));
            bus_write16(db, 0x1C08, x);
        }
        x = (uint16_t)(x + 4);
    } while (x != bus_read16(db, 0x1C0E));
    g_cpu.X = x; g_cpu.Y = y;
    CPU_SET_A16(x);
}

void mp_register_misc(void) {
    func_table_register(0x00A097, mp_00A097);
    func_table_register(0x01EB81, mp_01EB81);
    func_table_register(0x01ECAF, mp_01ECAF);
    func_table_register(0x01E93A, mp_01E93A);
    func_table_register(0x01F018, mp_01F018);

    func_table_register(0x00AAC7, mp_00AAC7);
    func_table_register(0x00A982, mp_00A982);
    func_table_register(0x00FA35, mp_00FA35);
    func_table_register(0x00EB3B, mp_00EB3B);
    func_table_register(0x01CD86, mp_01CD86);

    func_table_register(0x008AFF, mp_008AFF);
    func_table_register(0x00C410, mp_00C410);
    func_table_register(0x01B5D1, mp_01B5D1);
    func_table_register(0x01B5F4, mp_01B5F4);
    func_table_register(0x01B60F, mp_01B60F);
    func_table_register(0x00D514, mp_00D514);
    func_table_register(0x00FD06, mp_00FD06);
    func_table_register(0x00A87E, mp_00A87E);

    func_table_register(0x01D9A9, mp_01D9A9);
    func_table_register(0x01D7F0, mp_01D7F0);
    func_table_register(0x01D8CE, mp_01D8CE);
    func_table_register(0x01D874, mp_01D874);
    func_table_register(0x01D75B, mp_01D75B);
    func_table_register(0x01D810, mp_01D810);

    func_table_register(0x00C490, mp_00C490);
    func_table_register(0x008994, mp_008994);
    func_table_register(0x00C6C6, mp_00C6C6);
    func_table_register(0x00C873, mp_00C873);
    func_table_register(0x00BA69, mp_00BA69);

    func_table_register(0x00D590, mp_00D590);
    func_table_register(0x00D842, mp_00D842);
    func_table_register(0x00D73C, mp_00D73C);
    func_table_register(0x00D52C, mp_00D52C);

    func_table_register(0x01DD79, mp_01DD79);
    func_table_register(0x00D659, mp_00D659);
    func_table_register(0x00D5F5, mp_00D5F5);
    func_table_register(0x00D824, mp_00D824);

    func_table_register(0x01EDDB, mp_01EDDB);
    func_table_register(0x019130, mp_019130);
    func_table_register(0x01916E, mp_01916E);
    func_table_register(0x0191AC, mp_0191AC);
    func_table_register(0x01ECD5, mp_01ECD5);
    func_table_register(0x01ED03, mp_01ED03);
    func_table_register(0x01EDAB, mp_01EDAB);
    func_table_register(0x01EDCA, mp_01EDCA);
    func_table_register(0x01EFB9, mp_01EFB9);

    func_table_register(0x009CC7, mp_009CC7);
    func_table_register(0x009E76, mp_009E76);
    func_table_register(0x009760, mp_009760);
    func_table_register(0x00E233, mp_00E233);
    /* $00:E6DA is no routine (a point inside $E64F, which has its C version). */
    func_table_register(0x0082A7, mp_0082A7);
    func_table_register(0x01904A, mp_01904A);
    func_table_register(0x01934F, mp_01934F);
}
