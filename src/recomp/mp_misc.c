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
#include <stdio.h>
#include <stdlib.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;



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

/* $00:B780: pattern A (8 per row) from bank $0EBA + $0B: two blocks of
 * $20 words ($200 bytes apart) into $1B2A..$1BA9, then $01:D597. */
void mp_00B780(void) {
    uint8_t p0 = cpu_get_p();
    uint16_t a = CPU_A16();
    uint16_t y = (uint16_t)(((a & 0x0007) + ((a & 0xFFF8) << 1)) << 6);
    uint8_t bank = (uint8_t)(bus_read8(g_cpu.DB, 0x0EBA) + 0x0B);
    uint16_t x = 0;
    for (; x != 0x0040; x += 2, y += 2) bus_write16(0x00, (uint16_t)(0x1B2A + x), bus_read16(bank, (uint16_t)(0x8000 + y)));
    y = (uint16_t)(y + 0x01C0);
    for (; x != 0x0080; x += 2, y += 2) bus_write16(0x00, (uint16_t)(0x1B2A + x), bus_read16(bank, (uint16_t)(0x8000 + y)));
    g_cpu.X = x; g_cpu.Y = y;
    cpu_set_p(p0);
    call_jsl(0x01D597);
}

/* $01:EF36 (JSL): expand the run-length data at $7F:2000 into
 * $7E:4400..$7E:FE51: a negative word is a copy of (high & $7F) bytes from
 * (low byte) back, a positive word a run of that many literal bytes.
 * $01:EFB9 (the progress count) runs once per byte. */
void mp_01EF36(void) {
    uint8_t db0 = g_cpu.DB;
    uint16_t dp0 = g_cpu.DP, s0 = g_cpu.S;
    bus_wram_write8(s0, db0);                                /* PHB; DB = $7F; PHD */
    bus_wram_write16((uint16_t)(s0 - 2), dp0);
    g_cpu.DB = 0x7F;
    uint16_t d = (uint16_t)(s0 - 3 - 0x1E);
    g_cpu.S = d; g_cpu.DP = d;
#define LW(o, v) bus_write16(0x00, (uint16_t)(d + (o)), (uint16_t)(v))
#define LR(o)    bus_read16(0x00, (uint16_t)(d + (o)))
    uint16_t y = 0, x = 0;
    while (x < 0xBA52) {
        uint16_t w = bus_read16(0x7F, (uint16_t)(0x2000 + y));
        if (w & 0x8000) {
            LW(0x02, w & 0x00FF);
            LW(0x04, (uint16_t)((w & 0x7F00) >> 8));
            uint16_t src = (uint16_t)(x - LR(0x02));
            bool out = false;
            do {
                bus_write8(0x7E, (uint16_t)(0x4400 + x), bus_read8(0x7E, (uint16_t)(0x4400 + src)));
                g_cpu.X = x; g_cpu.Y = src;
                call_jsr(0x01EFB9);
                src++; x++;
                if (x >= 0xBA52) { out = true; break; }
                LW(0x04, LR(0x04) - 1);
            } while (LR(0x04) != 0);
            if (out) break;
            y = (uint16_t)(y + 2);
        } else {
            LW(0x04, w);
            y = (uint16_t)(y + 2);
            bool out = false;
            do {
                bus_write8(0x7E, (uint16_t)(0x4400 + x), bus_read8(0x7F, (uint16_t)(0x2000 + y)));
                g_cpu.X = x; g_cpu.Y = y;
                call_jsr(0x01EFB9);
                x++; y++;
                if (x >= 0xBA52) { out = true; break; }
                LW(0x04, LR(0x04) - 1);
            } while (LR(0x04) != 0);
            if (out) break;
        }
    }
#undef LW
#undef LR
    g_cpu.X = x; g_cpu.Y = y;
    g_cpu.S = s0; g_cpu.DP = dp0; g_cpu.DB = db0;
}

/* $01:F21D (JSL, length argument on the stack): Huffman-decode the save.
 * The tree (16 words a node pair at $7F:0200, copied from $7F:2000) and
 * the bit stream (moved up to end at $7F:FFFF); bits are taken by ROL of
 * each stream word through the carry, as the ROM does, so the stream
 * words end rotated. Bytes go out to $7F:2000 + $03. Returns A = 0 and
 * the decoded length in the argument slot, or A = $FFFF when the output
 * runs into the input. $01:EFB9 counts progress per stream word. */
void mp_01F21D(void) {
    uint8_t db0 = g_cpu.DB;
    uint16_t dp0 = g_cpu.DP, s0 = g_cpu.S;
    bus_wram_write8(s0, db0);
    bus_wram_write16((uint16_t)(s0 - 2), dp0);
    g_cpu.DB = 0x7F;
    uint16_t d = (uint16_t)(s0 - 3 - 0x10);
    g_cpu.S = d; g_cpu.DP = d;
#define LW(o, v) bus_write16(0x00, (uint16_t)(d + (o)), (uint16_t)(v))
#define LR(o)    bus_read16(0x00, (uint16_t)(d + (o)))
#define K(o)     bus_read16(0x7F, (uint16_t)(o))
#define KW(o, v) bus_write16(0x7F, (uint16_t)(o), (uint16_t)(v))
    for (int x = 0x07FE; x >= 0; x -= 2) KW(0x0200 + x, K(0x2000 + x));
    uint16_t arg = LR(0x17);
    bool c = arg >= 0x0800;                                  /* SEC; SBC #$0800 */
    uint16_t x = (uint16_t)(arg - 0x0800), y = 0xD800;
    do {
        KW((uint16_t)(0x27FE + y), K((uint16_t)(0x27FE + x)));
        y = (uint16_t)(y - 2); x = (uint16_t)(x - 2);
    } while (x != 0);
    LW(0x01, y);
    LW(0x03, 0);
    LW(0x07, 0x0011);
    uint16_t result;
    for (;;) {
        LW(0x05, 0);                                         /* $F257 */
        bool leaf = false;
        while (!leaf) {
            LW(0x07, LR(0x07) - 1);                          /* $F259 */
            if (LR(0x07) == 0) {
                LW(0x01, LR(0x01) + 2);
                call_jsr(0x01EFB9);
                if (LR(0x01) == 0xD800) { LW(0x17, LR(0x03)); result = 0x0000; goto done; }
                c = LR(0x01) >= 0xD800;
                LW(0x07, 0x0010);
            }
            uint16_t p = LR(0x01);                           /* $F273: ROL $2800,x */
            uint16_t w = K((uint16_t)(0x2800 + p));
            bool bit = (w & 0x8000) != 0;
            KW((uint16_t)(0x2800 + p), (uint16_t)((w << 1) | (c ? 1 : 0)));
            c = bit;
            uint16_t node = K((uint16_t)((bit ? 0x0202 : 0x0200) + LR(0x05)));
            LW(0x05, node);
            leaf = K((uint16_t)(0x0200 + node)) == 0;
        }
        KW((uint16_t)(0x2000 + LR(0x03)), K((uint16_t)(0x0202 + LR(0x05))));   /* $F298 */
        LW(0x03, LR(0x03) + 1);
        c = LR(0x03) >= LR(0x01);
        if (LR(0x03) == LR(0x01)) { result = 0xFFFF; goto done; }
    }
done:
#undef LW
#undef LR
#undef K
#undef KW
    g_cpu.S = s0; g_cpu.DP = dp0; g_cpu.DB = db0;
    CPU_SET_A16(result);
}

/* $01:EC4B (JSR from the menu loop $EBB5, A = pointer x): which of the
 * picture slots ($28 + n*$18, 15 wide) was clicked; then sound 3, the
 * tool switch ($A097: tool $AA, one argument, the slot) and the slot's
 * picture (long pointer at $01:EC9F + slot*4) copied to $7E:A000-$FFFF. */
void mp_01EC4B(void) {
    uint8_t db0 = g_cpu.DB;
    uint16_t dp0 = g_cpu.DP, s0 = g_cpu.S;
    bus_wram_write8(s0, db0);
    bus_wram_write16((uint16_t)(s0 - 2), dp0);
    g_cpu.S = (uint16_t)(s0 - 3);
    uint16_t a = (uint16_t)(CPU_A16() - 0x28), y = 0;
    while (a >= 0x18) { a = (uint16_t)(a - 0x18); y++; }
    g_cpu.Y = y;
    if (a < 0x0F) {
        CPU_SET_A16(0x0003);
        call_jsl(0x01D368);
        push_a16(y); push_a16(y);
        push_a16(bus_read16(0x00, (uint16_t)(dp0 + 0xAA)));
        push_a16(0x0001);
        call_jsl_ret(0x00A097, 0x01EC78);
        uint16_t slot = bus_wram_read16((uint16_t)(g_cpu.S + 1));
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
        uint16_t x = (uint16_t)(slot << 2);
        uint16_t hi = bus_read16(0x01, (uint16_t)(0xEC9F + x)), lo = bus_read16(0x01, (uint16_t)(0xECA1 + x));
        push_a16(hi); push_a16(lo);
        bus_wram_write16((uint16_t)(g_cpu.S - 1), 0x7E7E);  /* PEA $7E7E; PLB; PLB */
        uint32_t src = ((uint32_t)(hi & 0xFF) << 16) | lo;
        for (int i = 0x5FFE; i >= 0; i -= 2) {
            uint32_t q = (src + (uint32_t)i) & 0xFFFFFF;
            bus_write16(0x7E, (uint16_t)(0xA000 + i), bus_read16((uint8_t)(q >> 16), (uint16_t)q));
        }
        g_cpu.S = (uint16_t)(g_cpu.S + 4);
        g_cpu.X = x; g_cpu.Y = 0xFFFE;
        a = hi;
    }
    CPU_SET_A16(a);
    g_cpu.S = s0; g_cpu.DP = dp0; g_cpu.DB = db0;
}

/* $01:F03A (JSL, length argument on the stack): Huffman-encode the save.
 * The data ($7F:2000, length in the argument slot) is moved up to end at
 * $7F:E000; byte counts at $7F:0000, then the tree is built at $7F:0200
 * (node pairs) with weights at $7F:0A04 by repeatedly joining the two
 * lightest free nodes ($F018); each byte's code (length $1408, bits $1409,
 * through $1208) is found by walking up to the root, and the codes are
 * packed by ROL into $7F:2800 on. The tree goes to $7F:2000 for $F21D.
 * Returns A = 0 and the packed length, or A = $FFFF when the output runs
 * into the input. $EFB9 counts progress. */
static uint16_t f03a_rol(uint16_t addr, bool *c) {
    uint16_t w = bus_read16(0x7F, addr);
    bool out = (w & 0x8000) != 0;
    w = (uint16_t)((w << 1) | (*c ? 1 : 0));
    bus_write16(0x7F, addr, w);
    *c = out;
    return w;
}
void mp_01F03A(void) {
    uint8_t db0 = g_cpu.DB;
    uint16_t dp0 = g_cpu.DP, s0 = g_cpu.S;
    bus_wram_write8(s0, db0);
    bus_wram_write16((uint16_t)(s0 - 2), dp0);
    bus_wram_write16((uint16_t)(s0 - 1), 0x7F7F);           /* PEA $7F7F; PLB; PLB */
    bus_wram_write16((uint16_t)(s0 - 2), dp0);
    g_cpu.DB = 0x7F;
    uint16_t d = (uint16_t)(s0 - 3 - 0x10);
    g_cpu.S = d; g_cpu.DP = d;
#define LW(o, v) bus_write16(0x00, (uint16_t)(d + (o)), (uint16_t)(v))
#define LR(o)    bus_read16(0x00, (uint16_t)(d + (o)))
#define K(o)     bus_read16(0x7F, (uint16_t)(o))
#define KW(o, v) bus_write16(0x7F, (uint16_t)(o), (uint16_t)(v))
#define PROGRESS(xx, yy) do { g_cpu.X = (uint16_t)(xx); g_cpu.Y = (uint16_t)(yy); call_jsr(0x01EFB9); } while (0)
    uint16_t x = (uint16_t)((LR(0x17) + 1) & 0xFFFE), y = 0xE000;
    do {
        KW((uint16_t)(0x1FFE + y), K((uint16_t)(0x1FFE + x)));
        y = (uint16_t)(y - 2); x = (uint16_t)(x - 2);
    } while (x != 0);
    LW(0x01, y);
    for (int i = 0x0200; i >= 0; i -= 2) KW(i, 0);
    x = 0xFFFE;
    y = LR(0x01);
    do {                                                     /* byte counts */
        PROGRESS(x, y);
        x = (uint16_t)(bus_read8(0x7F, (uint16_t)(0x1FFF + y)) << 1);
        KW(x, K(x) + 1);
        y++;
    } while (y != 0xE000);
    x = 0x0004; y = 0;
    do {                                                     /* 256 leaves */
        KW(0x0200 + x, 0); KW(0x0A04 + x, 0);
        x += 2;
        KW(0x0200 + x, y); KW(0x0A04 + x, K((uint16_t)(y << 1)));
        y++; x += 2;
    } while (y != 0x0100);
    do {
        KW(0x0200 + x, 0); KW(0x0A04 + x, 0); x += 2;
        KW(0x0200 + x, 0); KW(0x0A04 + x, 0); x += 2;
    } while (x != 0x0800);
    KW(0x1C0E, 0x0404);
    for (;;) {                                               /* join the two lightest */
        PROGRESS(x, y);
        call_jsr(0x01F018);
        x = K(0x1C08);
        KW(0x0A04 + x, 1);
        uint16_t a = x;
        x = K(0x1C0E);
        KW(0x0200 + x, a);
        KW(0x0A06 + x, g_cpu.Y);
        call_jsr(0x01F018);
        y = g_cpu.Y;
        if (y == 0xFFFF) break;
        x = K(0x1C08);
        KW(0x0A04 + x, 1);
        a = x;
        x = K(0x1C0E);
        KW(0x0202 + x, a);
        KW(0x0A06 + x, (uint16_t)(y + K(0x0A06 + x)));
        KW(0x1C0E, K(0x1C0E) + 4);
    }
    y = 0;                                                   /* codes */
    KW(0x1C0A, 0);
    KW(0x1C0C, 0x0004);
    uint16_t a;
    do {
        PROGRESS(x, y);
        KW(0x1208 + y, K(0x1C0A));
        x = K(0x1C0A);
        KW(0x1408 + x, 0);
        KW(0x1409 + x, 0);
        a = K(0x1C0C);
        x = 0x0404;
        for (;;) {
            int bit = -1;
            if (a == K(0x0200 + x)) bit = 0;
            else if (a == K(0x0202 + x)) bit = 1;
            if (bit >= 0) {
                uint16_t k = K(0x1C0A);
                KW(0x1408 + k, K(0x1408 + k) + 1);
                bool c = bit != 0;
                f03a_rol((uint16_t)(0x1409 + k), &c);
                a = x;
                x = 0x0404;
                continue;
            }
            x = (uint16_t)(x + 4);
            if (x == K(0x1C0E)) break;
        }
        KW(0x1C0A, K(0x1C0A) + 3);
        a = (uint16_t)(K(0x1C0C) + 4);
        KW(0x1C0C, a);
        y = (uint16_t)(y + 2);
    } while (a != 0x0404);
    LW(0x05, 0x0010);                                        /* pack */
    KW(0x2800, 0);
    LW(0x03, 0);
    uint16_t result = 0x0000;
    bool c = false;
    do {
        x = LR(0x01);
        x = (uint16_t)(bus_read8(0x7F, (uint16_t)(0x2000 + x)) << 1);
        c = false;                                           /* ASL of a byte */
        x = K(0x1208 + x);
        y = K(0x1408 + x) & 0x00FF;
        a = K(0x1409 + x);
        c = a & 1; a = (uint16_t)(a >> 1);                   /* ROR, carry in 0 */
        x = LR(0x03);
        for (;;) {
            f03a_rol((uint16_t)(0x2800 + x), &c);
            LW(0x05, LR(0x05) - 1);
            if (LR(0x05) == 0) {
                LW(0x05, 0x0010);
                LW(0x03, LR(0x03) + 2);
                x = LR(0x03);
                c = x >= LR(0x01);
                if (x == LR(0x01)) { result = 0xFFFF; goto done; }
                KW(0x2800 + x, 0);
            }
            bool nc = a & 1;
            a = (uint16_t)((a >> 1) | (c ? 0x8000 : 0));
            c = nc;
            if (--y == 0) break;
        }
        LW(0x01, LR(0x01) + 1);
        PROGRESS(x, y);
    } while (LR(0x01) != 0xE000);
    x = LR(0x03);
    do { c = false; f03a_rol((uint16_t)(0x2800 + x), &c); LW(0x05, LR(0x05) - 1); } while (LR(0x05) != 0);
    x = (uint16_t)(K(0x1C0E) - 2);
    KW(0x2002, K(0x0200 + x));
    x = (uint16_t)(x - 2);
    KW(0x2000, K(0x0200 + x));
    for (x = 0x0004; x != 0x0800; x += 2) KW(0x2000 + x, K(0x0200 + x));
    LW(0x17, (LR(0x03) + 0x0801) & 0xFFFE);
done:
#undef LW
#undef LR
#undef K
#undef KW
#undef PROGRESS
    g_cpu.X = x; g_cpu.Y = y;
    g_cpu.S = s0; g_cpu.DP = dp0; g_cpu.DB = db0;
    CPU_SET_A16(result);
}

/* $01:EBB5 (JSL): the save/load menu loop. Waits a frame at a time
 * ($E2CE) with the pointer kept below y $C8, until a click ($04CA bit 5):
 * x $08-$16 leaves (sound $41, $AA = $0C); x $28-$86 picks a picture slot
 * ($EC4B), x $D8-$E6 the page button ($ECAF); after either the animation
 * ($0206/$0208) is waited out and the tool switch $A097 runs with no
 * arguments. */
void mp_01EBB5(void) {
    uint8_t db = g_cpu.DB;
    bus_write16(db, 0x0446, 0x0004);
    call_jsl(0x01E09B);
    call_jsl(0x00C410);
    call_jsl(0x01DE97);
    bus_write16(db, 0x04DE, 0x00D0);
    for (;;) {
        call_jsl(0x01E2CE);
        call_jsl(0x008B3C);
        if (bus_read16(db, 0x04DE) < 0x00C8) bus_write16(db, 0x04DE, 0x00C8);
        call_jsl(0x008B44);
        if (!(bus_read16(db, 0x04CA) & 0x0020)) continue;
        uint16_t a = bus_read16(db, 0x04DC);
        if (a < 0x0008) continue;
        if (a < 0x0017) {
            CPU_SET_A16(0x0041);
            call_jsl(0x01D368);
            CPU_SET_A16(0x000C);
            bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xAA), 0x000C);
            return;
        }
        if (a < 0x0028) continue;
        if (a < 0x0087) {
            bus_write16(db, 0x0206, 0);
            CPU_SET_A16(a);
            call_jsr(0x01EC4B);
        } else {
            if (a < 0x00D8 || a >= 0x00E7) continue;
            bus_write16(db, 0x0206, 0);
            CPU_SET_A16(a);
            call_jsr(0x01ECAF);
        }
        bus_write16(db, 0x0208, 0);
        bus_write16(db, 0x0206, (uint16_t)(bus_read16(db, 0x0206) + 1));
        do call_jsl(0x01E2CE); while (bus_read16(db, 0x0208) != 0);
        push_a16(bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA)));
        push_a16(0x0000);
        call_jsl_ret(0x00A097, 0x01EC42);
    }
}

/* ---- The mode bar's buttons ($95EE ids from mode 7) and their helpers. */
#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))

/* PHP / PLP kept on the stack, as the ROM does: a body that waits for a
 * frame between them resumes on the genuine machine's stack, where its own
 * pushes must sit at the ROM's depth. */
static void mb_php(void) { bus_wram_write8(g_cpu.S, cpu_get_p()); g_cpu.S = (uint16_t)(g_cpu.S - 1); }
static void mb_plp(void) { g_cpu.S = (uint16_t)(g_cpu.S + 1); cpu_set_p(bus_wram_read8(g_cpu.S)); }

static void mb_sound(uint16_t n) { CPU_SET_A16(n); call_jsl(0x01D368); }
static void mb_cdE1(uint16_t a) { CPU_SET_A16(a); call_jsl(0x01CDE1); }

/* PHA; JSL $01F825; PLA; DEC; (CMP #stop;) BPL: frames of $F825 for
 * a, a-1, .. down to stop. */
static void mb_f825_down(uint16_t a, uint16_t stop) {
    do {
        push_a16(a);
        CPU_SET_A16(a);
        call_jsl(0x01F825);
        a = bus_wram_read16((uint16_t)(g_cpu.S + 1));
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
        a--;
    } while (!((uint16_t)(a - stop) & 0x8000));
    CPU_SET_A16(a);
}

static void mb_tiles(uint16_t dst, uint16_t src, uint16_t rows) {
    push_a16(dst); push_a16(src); push_a16(rows);
    call_jsr(0x00C490);
}

static void mb_tool(uint16_t t) {
    bus_write16(0x00, MB_DP(0xAA), t);
    call_tool_switch(t, 0, 0);
}

/* $00:E2CC: the tool's cursor colours: 8 rows of 4 words from the table at
 * $E2F5 + (tool high byte)*4 into $1CAA/$1CBA (skipping every other $10). */
void mp_00E2CC(void) {
    uint16_t x = (uint16_t)(((MB_R(0x04D0) & 0xFF00) >> 8) << 2);
    uint16_t y = 0x006E;
    do {
        MB_W(0x1CAA + y, bus_read16(0x00, (uint16_t)(0xE2F5 + x)));
        MB_W(0x1CBA + y, bus_read16(0x00, (uint16_t)(0xE2F7 + x)));
        y = (uint16_t)(y - 2);
        if (y & 0x0010) y = (uint16_t)(y - 0x10);
    } while (y != 0);
    g_cpu.X = x; g_cpu.Y = 0;
    CPU_SET_A16(0);
}

/* $00:DE46: the undo-dog screen's tiles ($02:B180 -> $7E:28C0), $101C
 * cleared, a frame, then the 10 sprite words from $DE7B into $0182. */
void mp_00DE46(void) {
    for (int x = 0x057E; x >= 0; x -= 2)
        bus_write16(0x7E, (uint16_t)(0x28C0 + x), bus_read16(0x02, (uint16_t)(0xB180 + x)));
    for (int x = 0x007E; x >= 0; x -= 2) MB_W(0x101C + x, 0);
    call_jsl(0x01DEB2);
    call_jsl(0x01E2CE);
    MB_W(0x0202, 0);
    for (int x = 0x0012; x >= 0; x -= 2) MB_W(0x0182 + x, bus_read16(0x00, (uint16_t)(0xDE7B + x)));
    MB_W(0x0202, MB_R(0x0202) + 1);
    g_cpu.X = 0xFFFE;
}

/* $00:D869 (mode bar 48-63): tool $0B (the old one kept in $101A, $A6 in
 * $1126), its tiles and cursor colours, bar mode $0A, the screen of $DE46,
 * three steps of $F825, state $15 (the old in $09D8). */
void mp_00D869(void) {
    mb_php();
    CPU_SET_A16(0x0006); call_jsl(0x01D2BF);
    mb_sound(0x0002);
    call_jsl(0x01E06F);
    MB_W(0x1992, 0);
    MB_W(0x101A, MB_R(0x04D0));
    MB_W(0x04D0, 0x000B);
    MB_W(0x1126, bus_read16(0x00, MB_DP(0xA6)));
    bus_write16(0x00, MB_DP(0xA6), 0x0100);
    MB_W(0x1124, 0);
    bus_write16(0x00, MB_DP(0x20), 0x0001);
    MB_W(0x1996, 0);
    mb_cdE1(0xCF16);
    call_jsr(0x00C414);
    call_jsl(0x01DE97);
    call_jsr(0x00E2CC);
    call_jsr(0x00A363);
    mb_tool(0x000A);
    bus_write8(g_cpu.DB, 0x010E, 0x66);
    CPU_SET_A16(0x3DFF); call_jsl(0x01E033);
    call_jsr(0x00DE46);
    mb_f825_down(0x003E, 0x003C);
    MB_W(0x09D8, MB_R(0x09D6));
    MB_W(0x09D6, 0x0015);
    CPU_SET_A16(0x0015);
    mb_plp();
}

/* $00:CE22 (mode bar 120-135): into the save/load menu: tool $11 (the old
 * in $1976), the save check $D6D3, its screen (tiles $02:B700 -> $7E:2100,
 * 20 rows; the row at $CEC0 -> $7E:2040), bar mode $0C, $F825 counts,
 * state 6. */
void mp_00CE22(void) {
    mb_sound(0x0006);
    MB_W(0x1976, MB_R(0x04D0));
    MB_W(0x04D0, 0x0011);
    call_jsr(0x009378);
    MB_W(0x09D8, MB_R(0x09D6));
    call_jsl(0x00D6D3);
    CPU_SET_A16(0x0018); call_jsl(0x01F825);
    if (MB_R(0x1982) != 0) { MB_W(0x1AD5, 0xFFFF); MB_W(0x1984, 0); MB_W(0x1986, 0); }
    MB_W(0x09AD, MB_R(0x1012));
    call_jsr(0x00C414);
    mb_cdE1(0xCF16);
    mb_cdE1(0xCE74);
    mb_tiles(0x2100, 0xB700, 0x0014);
    for (int x = 0x007E; x >= 0; x -= 2)
        bus_write16(0x7E, (uint16_t)(0x2040 + x), bus_read16(0x00, (uint16_t)(0xCEC0 + x)));
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    mb_tool(0x000C);
    mb_f825_down(0x000D, 0x000A);
    CPU_SET_A16(0x0030); call_jsl(0x01F825);
    MB_W(0x09D6, 0x0006);
    CPU_SET_A16(0x0006);
    g_cpu.X = 0xFFFE;
}

/* $00:C9E3 (mode bar 160-175): its frame (tiles $02:A7C0 -> $7E:2100,
 * 19 rows), $F825 for 9..0, three sprite rows around $04C4 hidden, bar
 * mode $0D, tool 9 (the old in $1B12), state 1. */
void mp_00C9E3(void) {
    mb_sound(0x0006);
    mb_cdE1(0xCFA9);
    mb_tiles(0x2100, 0xA7C0, 0x0013);
    mb_f825_down(0x0009, 0x0000);
    uint16_t k = MB_R(0x04C4);
    MB_W(0x1A15 + (uint16_t)((k + 1) << 3), 0xFFFF);
    MB_W(0x1A15 + (uint16_t)((k + 4) << 3), 0x01FF);
    uint16_t x = (uint16_t)((MB_R(0x04C4) + 7) << 3);
    MB_W(0x1A15 + x, 0xFFFF);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    MB_W(0x1B18, MB_R(0x1B18) + 1);
    mb_tool(0x000D);
    MB_W(0x09AB, MB_R(0x09AB) + 1);
    call_jsr(0x00A363);
    MB_W(0x1B12, MB_R(0x04D0));
    MB_W(0x04D0, 0x0009);
    MB_W(0x09D8, MB_R(0x09D6));
    MB_W(0x09D6, 0x0001);
    CPU_SET_A16(0x0001);
    g_cpu.X = x;
}

/* $00:9734 (mode bar 184-199): unless $19FA, layers $011A = 6 until a
 * click, then back to $13. */
void mp_009734(void) {
    mb_php();
    mb_sound(0x0006);
    if (MB_R(0x19FA) == 0) {
        op_sep(0x20);
        bus_write8(g_cpu.DB, 0x011A, 0x06);
        do call_jsl(0x01E2CE); while (!(bus_read8(g_cpu.DB, 0x04CA) & 0x20));
        CPU_SET_A8(0x06); call_jsl(0x01D368);
        bus_write8(g_cpu.DB, 0x011A, 0x13);
        CPU_SET_A8(0x13);
    }
    mb_plp();
}

/* $00:96A6 (mode bar 208-223): Gnat Attack ($01:9F6C). */
void mp_0096A6(void) { call_jsl(0x019F6C); }
#undef MB_R
#undef MB_W
#undef MB_DP

#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
/* $00:EB7A (mode bar 96-111): into the music editor (bar mode $0B): the music sound bank
 * ($1C, after the sound CPU answers), tool 3 (the old in $09DC; the copy
 * flip $0997 kept in $0EB0, $A6 in $09DE), its screen and staff ($FE9E,
 * $FD18, $FB5C, $FCF4, $FE54; tiles $02:BC00 -> $7E:2100, 20 rows; the
 * markers $FB47), state $1E, tool $0D, bar mode $0B with the song's
 * length + 1. */
void mp_00EB7A(void) {
    mb_php();
    mb_sound(0x0006);
    op_sep(0x20);
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    bus_write8(0x00, MB_DP(0xCC), 0x00);
    bus_write8(0x00, MB_DP(0xCD), 0x80);
    bus_write8(0x00, MB_DP(0xCE), 0x1C);
    bus_write8(g_cpu.DB, 0x2140, 0xFF);
    CPU_SET_A8(0xFF);
    call_jsl(0x01DF25);
    /* -: LDA #1 : STA $2140 : LDA $2140 : CMP #1 : BNE -  until the sound
     * CPU answers: in the ROM's own loop ($EBA1 to $EBAD) where it can run. */
    if (!recomp_rom_wait(0x00EBA1, 0x00EBAD))
        for (int n = 0; n < 24000; n++) {
            bus_write8(g_cpu.DB, 0x2140, 0x01);
            if (bus_read8(g_cpu.DB, 0x2140) == 0x01 || !recomp_timed_spin(64)) break;
        }
    op_rep(0x20);
    call_jsl(0x01E2F3);
    MB_W(0x0206, 0);
    call_jsl(0x01E06F);
    MB_W(0x1992, 0); MB_W(0x0EB6, 0); MB_W(0x09E0, 0);
    MB_W(0x09DC, MB_R(0x04D0));
    MB_W(0x04D0, 0x0003);
    MB_W(0x0E8C, 0x0003);
    MB_W(0x0EB4, 0x0003);
    MB_W(0x0EB0, MB_R(0x0997));
    MB_W(0x0997, 0);
    MB_W(0x09DE, bus_read16(0x00, MB_DP(0xA6)));
    bus_write16(0x00, MB_DP(0xA6), 0x0070);
    MB_W(0x0EB2, 0);
    call_jsr(0x00C414);
    mb_cdE1(0xCF65);
    bus_write8(g_cpu.DB, 0x010E, 0x66);
    MB_W(0x0EA2, 0x0004);
    call_jsr(0x00FE9E);
    MB_W(0x0446, 0x0004);
    call_jsr(0x00FD18);
    call_jsr(0x00FB5C);
    call_jsr(0x00FCF4);
    call_jsr(0x00FE54);
    mb_tiles(0x2100, 0xBC00, 0x0014);
    if (MB_R(0x0C26) != 0) { CPU_SET_A16(0x0C36); g_cpu.X = 0x0554; g_cpu.Y = 0x0003; call_jsr(0x00FB47); }
    CPU_SET_A16(0x0C30); g_cpu.X = 0x0544; g_cpu.Y = 0x0003; call_jsr(0x00FB47);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    MB_W(0x09D8, MB_R(0x09D6));
    MB_W(0x09D6, 0x001E);
    bus_write16(0x00, MB_DP(0xA8), 0);
    call_jsr(0x0096E1);
    call_jsr(0x00A363);
    MB_W(0x04D0, 0x000D);
    CPU_SET_A16(0x004D); call_jsl(0x01F825);
    MB_W(0x0E8E, MB_R(0x0E8E) + 1);
    uint16_t len = (uint16_t)(MB_R(0x0C32) + 1);
    bus_write16(0x00, MB_DP(0xAA), 0x000B);
    call_tool_switch(0x000B, 1, &len);
    mb_plp();
}

/* $00:E3F7 (mode bar 144-159): tool 9 (the old in $1010), its screen (tiles $02:AC80 -> $7E:2100, 20 rows), $F825 for
 * $4C..$3F, the frame-count sprites for $1012 frames (row from $E4B5),
 * $1014 = $20, state $1D, bar mode $0E; then $A363. */
void mp_00E3F7(void) {
    mb_sound(0x0006);
    call_jsl(0x01E06F);
    MB_W(0x1010, MB_R(0x04D0));
    MB_W(0x04D0, 0x0009);
    call_jsr(0x009378);
    mb_cdE1(0xCE9D);
    mb_tiles(0x2100, 0xAC80, 0x0014);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    mb_f825_down(0x004C, 0x003F);
    uint16_t x = (uint16_t)(((MB_R(0x1012) + 0x003F) << 3) & 0x00FF);
    MB_W(0x1A15 + x, 0x01FF);
    x = bus_read8(0x00, (uint16_t)(0xE4B5 + MB_R(0x1012)));
    MB_W(0x1A15 + x, 0xFFFF);
    MB_W(0x1A3D, 0xFFFF);
    if (x == 0x0048) {
        MB_W(0x1A65, 0x00FF);
        MB_W(0x1A6D, 0x00FF);
        MB_W(0x1A66, 0x000A);
        MB_W(0x1A6E, 0x0014);
    }
    if (MB_R(0x1012) != 0) { MB_W(0x1A45, 0x02FF); MB_W(0x1A4D, 0x02FF); MB_W(0x1A55, 0x02FF); }
    MB_W(0x1014, 0x0020);
    MB_W(0x09D8, MB_R(0x09D6));
    MB_W(0x09D6, 0x001D);
    mb_tool(0x000E);
    MB_W(0x09AB, MB_R(0x09AB) + 1);
    tail_jmp(0x00A363);
}
#undef MB_R
#undef MB_W
#undef MB_DP

#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
/* ---- Bar mode 8 (reached from the animation player bar's 64-79): $AC =
 * the slot clicked (1-based), $0EBA a page, $0EBC a byte whose high nibble
 * comes from the tables at $EA06 / $EA5C, $EB3B the tool word for it. */
static uint16_t pb_dp(uint16_t o) { return bus_read16(0x00, MB_DP(o)); }
static void pb_pattern(uint16_t table, uint16_t x) {
    uint16_t v = bus_read8(0x00, (uint16_t)(table + x));
    MB_W(0x0EBC, (uint16_t)((MB_R(0x0EBC) & 0x000F) | v));
}
static void pb_pick(void) {
    MB_W(0x04D0, 0x0003);
    MB_W(0x1992, 0);
}

/* $00:E9A1 (8-47): a page button: sound $42 on the first, $0A else; when
 * the page changes, the pattern's high nibble for it; the tool switch with
 * the tool word and the page; $96E1, $A363. */
void mp_00E9A1(void) {
    uint16_t slot = (uint16_t)(pb_dp(0xAC) - 1);
    mb_sound(slot == 0 ? 0x0042 : 0x000A);
    pb_pick();
    MB_W(0x0EB8, 0x0001);
    if ((uint16_t)(pb_dp(0xAC) - 1) != MB_R(0x0EBA)) {
        call_jsr(0x00EB3B);
        uint16_t x = g_cpu.X;
        if (MB_R(0x0EBA) == 0) x = (uint16_t)(x + 5);
        pb_pattern(0xEA06, x);
    }
    MB_W(0x0EBA, (uint16_t)(pb_dp(0xAC) - 1));
    call_jsr(0x00EB3B);
    uint16_t w[2] = { CPU_A16(), MB_R(0x0EBA) };
    call_tool_switch(pb_dp(0xAA), 2, w);
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* $00:EA12 (104-183): a pattern slot: sound $0C, its high nibble ($EA5C,
 * +5 on page 1), the tool switch, $96E1, $A363. */
void mp_00EA12(void) {
    pb_pick();
    MB_W(0x0EB8, 0x0001);
    mb_sound(0x000C);
    uint16_t x = pb_dp(0xAC);
    if (MB_R(0x0EBA) != 0) x = (uint16_t)(x + 5);
    g_cpu.X = x;
    pb_pattern(0xEA5C, x);
    call_jsr(0x00EB3B);
    uint16_t w[2] = { CPU_A16(), MB_R(0x0EBA) };
    call_tool_switch(pb_dp(0xAA), 2, w);
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* $00:E848 (192-207): the plain pen: $0EB8 = -1, the tool switch with
 * three words (tool word, 7, page). */
void mp_00E848(void) {
    mb_sound(0x000C);
    pb_pick();
    MB_W(0x0EB8, 0xFFFF);
    call_jsr(0x00EB3B);
    uint16_t w[3] = { CPU_A16(), 0x0007, MB_R(0x0EBA) };
    call_tool_switch(pb_dp(0xAA), 3, w);
    call_jsr(0x0096E1);
    tail_jmp(0x00A363);
}

/* $00:EA69 (208-223): the eraser menu ($1992 = 1, tool 7, $B66C/$B6F4
 * cursor, $A525), tool switch with 8 and the page. */
void mp_00EA69(void) {
    mb_sound(0x0007);
    MB_W(0x1992, 0x0001);
    CPU_SET_A16(0x000F); call_jsr(0x00B66C);
    CPU_SET_A16((uint16_t)(MB_R(0x1994) + 8)); call_jsr(0x00B6F4);
    MB_W(0x04D0, 0x0007);
    call_jsr(0x00A525);
    uint16_t w[2] = { 0x0008, MB_R(0x0EBA) };
    call_tool_switch(pb_dp(0xAA), 2, w);
    tail_jmp(0x00A363);
}

/* $00:EAA4 (8-23 in the pattern bar): back: sound $41, the tool saved in
 * $0EC0 ($0EC2 to $B8); the eraser menu again for tool 7, else the
 * stamp/pattern size ($A8, $96E1); the tool switch (4 with the stamp
 * $19C0 for tool 6 when $19FA, 4 without, else 7), $A451, $A525, $A363. */
void mp_00EAA4(void) {
    mb_sound(0x0041);
    MB_W(0x0EB8, 0);
    MB_W(0x1992, 0);
    bus_write16(0x00, MB_DP(0xB8), MB_R(0x0EC2));
    MB_W(0x04D0, MB_R(0x0EC0));
    if ((MB_R(0x04D0) & 0x00FF) == 0x0007) {
        MB_W(0x1992, MB_R(0x1992) + 1);
        CPU_SET_A16(0x000F); call_jsr(0x00B66C);
        CPU_SET_A16((uint16_t)(MB_R(0x1994) + 8)); call_jsr(0x00B6F4);
    } else {
        bus_write16(0x00, MB_DP(0xA8), (uint16_t)(MB_R(0x04D0) & 0xFF00));
        call_jsr(0x0096E1);
    }
    g_cpu.Y = MB_R(0x19FA);
    if (g_cpu.Y != 0) {
        bus_write16(0x00, MB_DP(0xAA), 0x0004);
        if ((MB_R(0x04D0) & 0x00FF) == 0x0006) { uint16_t w = MB_R(0x19C0); call_tool_switch(0x0004, 1, &w); }
        else call_tool_switch(0x0004, 0, 0);
    } else {
        bus_write16(0x00, MB_DP(0xAA), 0x0007);
        call_tool_switch(0x0007, 0, 0);
    }
    call_jsr(0x00A451);
    call_jsr(0x00A525);
    if (MB_R(0x04D0) == 0x0006) MB_W(0x09AB, MB_R(0x09AB) + 1);
    tail_jmp(0x00A363);
}

/* $00:969C (mode 15): $01:D409. */
void mp_00969C(void) { call_jsl(0x01D409); }
#undef MB_R
#undef MB_W
#undef MB_DP

#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
/* ---- The music editor's bar (mode 11). Each acts on a click ($04CA bit
 * 5); $AC = the slot clicked (1-based). */
static bool mu_click(void) {
    uint16_t a = MB_R(0x04CA);
    CPU_SET_A16(a);
    return (a & 0x0020) != 0;
}
/* The marker for the song or the loop ($0C26), redrawn by $FB47. */
static void mu_marker(void) {
    CPU_SET_A16(MB_R(0x0C26) != 0 ? 0x0C36 : 0x0C16);
    g_cpu.X = 0x0554; g_cpu.Y = 0x0003;
    call_jsr(0x00FB47);
}

/* $00:F9A7 (40-55): to tool $0F (the old in $0EB4), $1C2A..$1D29
 * cleared, $E64F, the tool switch with the song length + 1 and 0 (and
 * $0EB6 + 2 when set). */
void mp_00F9A7(void) {
    call_jsl(0x01E2F3);
    if (!mu_click()) return;
    mb_sound(0x0002);
    uint16_t a = MB_R(0x0E8C);
    if (a == 0x000F) { CPU_SET_A16(a); return; }
    MB_W(0x0EB4, a);
    MB_W(0x0E8C, 0x000F);
    for (int x = 0x00FE; x >= 0; x -= 2) MB_W(0x1C2A + x, 0);
    call_jsr(0x00E64F);
    uint16_t sel = MB_R(0x0EB6);
    uint16_t len = (uint16_t)(MB_R(0x0C32) + 1);
    if (sel == 0) {
        uint16_t w[2] = { len, 0x0000 };
        call_tool_switch(bus_read16(0x00, MB_DP(0xAA)), 2, w);
    } else {
        uint16_t w[3] = { (uint16_t)(sel + 2), len, 0x0000 };
        call_tool_switch(bus_read16(0x00, MB_DP(0xAA)), 3, w);
    }
}

/* $00:FA08 (80-111): the song length = slot - 2 (sound 7 for 0, 8
 * else), then $FA35 and $FD18. */
void mp_00FA08(void) {
    if (!mu_click()) return;
    MB_W(0x0EB6, 0);
    uint16_t v = (uint16_t)(bus_read16(0x00, MB_DP(0xAC)) - 2);
    MB_W(0x0C32, v);
    mb_sound(v == 0 ? 0x0007 : 0x0008);
    call_jsr(0x00FA35);
    call_jsr(0x00FD18);
}

/* $00:FEF9 (136-199): $0EB6 = slot - 3 (sound $0D, $FCF4 first), $E93A,
 * the marker, $DE97, then $FD18. */
void mp_00FEF9(void) {
    if (!mu_click()) return;
    mb_sound(0x000D);
    call_jsr(0x00FCF4);
    MB_W(0x0EB6, (uint16_t)(bus_read16(0x00, MB_DP(0xAC)) - 3));
    call_jsl(0x01E93A);
    mu_marker();
    call_jsl(0x01DE97);
    tail_jmp(0x00FD18);
}

/* $00:FA65 (216-231): $0EB6 = 0, sound 4, the tool switch with 6, $FD06,
 * $FD18, a frame, the marker, $DE97, a frame, $FA35. */
void mp_00FA65(void) {
    call_jsl(0x01E2F3);
    if (!mu_click()) return;
    MB_W(0x0EB6, 0);
    mb_sound(0x0004);
    uint16_t w = 0x0006;
    call_tool_switch(bus_read16(0x00, MB_DP(0xAA)), 1, &w);
    call_jsr(0x00FD06);
    call_jsr(0x00FD18);
    call_jsl(0x01E2CE);
    mu_marker();
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    call_jsr(0x00FA35);
}
#undef MB_R
#undef MB_W
#undef MB_DP

#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
/* ---- The top bar while $1992 is set (the stamp editor, $A4B4): $9598's
 * table $95C2. The picture effects send their sound command ($D2BF) and wait
 * in the ROM's loop for the NMI to take it ($053A), then $8B03, the effect,
 * $AAC7, $09A7 = 0. */
static void er_push16(uint16_t v) { push_a16(v); }
static uint16_t er_pull16(void) {
    uint16_t v = bus_wram_read16((uint16_t)(g_cpu.S + 1));
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    return v;
}
static void er_start(uint16_t cmd, uint32_t spin) {
    MB_W(0x09A7, MB_R(0x09A7) + 1);
    CPU_SET_A16(cmd);
    call_jsl(0x01D2BF);
    if ((MB_R(0x053A) & 0x00FF) != 0 && !recomp_rom_wait(spin, spin + 8))
        while ((MB_R(0x053A) & 0x00FF) != 0 && !g_quit) call_jsl(0x01E2CE);
    call_jsr(0x008B03);
}
static void er_end(void) {
    call_jsr(0x00AAC7);
    MB_W(0x09A7, 0);
}

/* $00:A509 (slots 1-5): the stamp page: $1994 = $A8, its cursor ($B6F4
 * with +8), $A525, sound 4. */
void mp_00A509(void) {
    uint16_t a = bus_read16(0x00, MB_DP(0xA8));
    if (a >= 0x0006) { CPU_SET_A16(a); return; }
    MB_W(0x1994, a);
    CPU_SET_A16((uint16_t)(a + 8)); call_jsr(0x00B6F4);
    call_jsr(0x00A525);
    mb_sound(0x0004);
}

/* $00:A55B (category 2): clear in 4 x 8 steps: each step ANDs the picture's
 * bytes ([$0D] = the canvas at $0A + $A5F3[step]; 16 of every 32, up to
 * $6000) with four masks from $A5F7/$A5FF/$A607/$A60F, a frame each. */
void mp_00A55B(void) {
    mb_php();
    er_start(0x0011, 0x00A566);
    for (uint16_t ox = 0; ox != 4; ox++) {
        er_push16(ox);                                          /* PHX */
        uint16_t p = (uint16_t)((bus_read8(0x00, (uint16_t)(0xA5F3 + ox))) + bus_read16(0x00, MB_DP(0x0A)));
        bus_write16(0x00, MB_DP(0x0D), p);
        for (uint16_t x = 0; x != 8; x++) {
            op_sep(0x20);
            bus_write8(g_cpu.DB, 0x1944, bus_read8(0x00, (uint16_t)(0xA5F7 + x)));
            bus_write8(g_cpu.DB, 0x1945, bus_read8(0x00, (uint16_t)(0xA5FF + x)));
            bus_write8(g_cpu.DB, 0x1946, bus_read8(0x00, (uint16_t)(0xA607 + x)));
            bus_write8(g_cpu.DB, 0x1947, bus_read8(0x00, (uint16_t)(0xA60F + x)));
            uint32_t base = ((uint32_t)bus_read8(0x00, MB_DP(0x0F)) << 16) | bus_read16(0x00, MB_DP(0x0D));
            uint16_t y = 0;
            do {
                for (int k = 0; k < 4; k++) {
                    uint32_t q = (base + y) & 0xFFFFFF;
                    bus_write8((uint8_t)(q >> 16), (uint16_t)q,
                               bus_read8((uint8_t)(q >> 16), (uint16_t)q) & bus_read8(g_cpu.DB, (uint16_t)(0x1944 + k)));
                    y = (uint16_t)(y + 2);
                }
                if (!(y & 0x000F)) y = (uint16_t)(y + 0x10);
            } while (y < 0x6000);
            op_rep(0x20);
            g_cpu.Y = y;
            er_push16(x);                                       /* PHX; JSL; PLX */
            g_cpu.X = x;
            call_jsl(0x01E2CE);
            x = er_pull16();
        }
        ox = er_pull16();
    }
    g_cpu.X = 4;
    er_end();
    mb_plp();
}

/* $00:A637 (category 4): the shuffle: $200 frames; each frame swaps $21 -
 * (frames left / 16) random pairs of tile words in the picture map at
 * $7E:2800 (inside $C0-$63F; outside, the in-range one is blanked to
 * $1FFF), then $DEB2. Then $A982, layers $3FFF ($E033), $8A16. */
void mp_00A637(void) {
    mb_php();
    bus_wram_write8(g_cpu.S, g_cpu.DB); g_cpu.S = (uint16_t)(g_cpu.S - 1);   /* PHB */
    er_start(0x0013, 0x00A643);
    bus_wram_write8(g_cpu.S, 0x7E);                             /* PHA; PLB */
    g_cpu.DB = 0x7E;
    uint16_t cnt = 0x0200;
    do {
        er_push16(cnt);
        uint16_t n = (uint16_t)((uint16_t)~(cnt >> 4) + 0x0022);
        do {
            er_push16(n);
            call_jsl(0x01E20C);
            uint16_t r1 = CPU_A16() & 0x003E;
            er_push16(r1);
            call_jsl(0x01E20C);
            uint16_t x = (uint16_t)(((CPU_A16() & 0x00F8) << 3) + r1);
            er_pull16();
            call_jsl(0x01E20C);
            uint16_t r3 = CPU_A16() & 0x003E;
            er_push16(r3);
            call_jsl(0x01E20C);
            uint16_t y = (uint16_t)(((CPU_A16() & 0x00F8) << 3) + r3);
            er_pull16();
            g_cpu.X = x; g_cpu.Y = y;
            bool xin = x >= 0x00C0 && x < 0x0640, yin = y >= 0x00C0 && y < 0x0640;
            if (!xin) bus_write16(0x7E, (uint16_t)(0x2800 + y), 0x1FFF);
            else if (!yin) bus_write16(0x7E, (uint16_t)(0x2800 + x), 0x1FFF);
            else {
                uint16_t t = bus_read16(0x7E, (uint16_t)(0x2800 + x));
                er_push16(t);
                bus_write16(0x7E, (uint16_t)(0x2800 + x), bus_read16(0x7E, (uint16_t)(0x2800 + y)));
                bus_write16(0x7E, (uint16_t)(0x2800 + y), er_pull16());
            }
            n = (uint16_t)(er_pull16() - 1);
        } while (n != 0);
        call_jsl(0x01DEB2);
        call_jsl(0x01E2CE);
        cnt = (uint16_t)(er_pull16() - 1);
    } while (cnt != 0);
    op_sep(0x20);
    call_jsr(0x00A982);
    op_rep(0x20);
    CPU_SET_A16(0x3FFF); call_jsl(0x01E033);
    call_jsr(0x008A16);
    call_jsl(0x01E2CE);
    er_end();
    g_cpu.S = (uint16_t)(g_cpu.S + 1); g_cpu.DB = bus_wram_read8(g_cpu.S);   /* PLB */
    mb_plp();
}
#undef MB_R
#undef MB_W
#undef MB_DP

#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_R8(o)    bus_read8(g_cpu.DB, (uint16_t)(o))
#define MB_W8(o, v) bus_write8(g_cpu.DB, (uint16_t)(o), (uint8_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
static uint32_t er_long(uint16_t dp) {
    return ((uint32_t)bus_read8(0x00, MB_DP(dp + 2)) << 16) | bus_read16(0x00, MB_DP(dp));
}
static void er_and16(uint32_t base, uint16_t y, uint16_t m) {
    uint32_t q = (base + y) & 0xFFFFFF;
    bus_write16((uint8_t)(q >> 16), (uint16_t)q, bus_read16((uint8_t)(q >> 16), (uint16_t)q) & m);
}
static void er_push8(uint8_t v) { bus_wram_write8(g_cpu.S, v); g_cpu.S = (uint16_t)(g_cpu.S - 1); }
static uint8_t er_pull8(void) { g_cpu.S = (uint16_t)(g_cpu.S + 1); return bus_wram_read8(g_cpu.S); }

/* $00:A6F7: the dissolve: two passes of $7000 random picks of a column
 * pair ($E20C & $FE + pass); each column counts its pixels gone (to $C8,
 * $7F:0000); $B305 finds the pixel, cleared ($A782 masks) in both
 * planes [$0A] and [$0D] = [$0A] + $10. */
void mp_00A6F7(void) {
    er_start(0x0014, 0x00A701);
    bus_write16(0x00, MB_DP(0x0D), (uint16_t)(bus_read16(0x00, MB_DP(0x0A)) + 0x10));
    for (int x = 0x01FE; x >= 0; x -= 2) bus_write16(0x7F, (uint16_t)x, 0x0018);
    bus_write16(0x7F, 0x0200, 0x0001);
    int16_t pass;
    do {
        uint16_t cnt = 0x7000;
        do {
            er_push16(cnt);
            call_jsl(0x01E20C);
            uint16_t r = (uint16_t)((CPU_A16() & 0x00FE) + bus_read16(0x7F, 0x0200));
            bus_write16(0x00, MB_DP(0x80), r);
            uint16_t x = (uint16_t)(r << 1);
            uint16_t v = bus_read16(0x7F, x);
            bus_write16(0x00, MB_DP(0x82), v);
            v = (uint16_t)(v + 1);
            if (v != 0x00C8) bus_write16(0x7F, x, v);
            CPU_SET_A16(v); g_cpu.X = x;
            call_jsr(0x00B305);
            uint16_t x2 = g_cpu.X & 0x000E;
            g_cpu.X = x2;
            uint16_t y = bus_read16(0x00, MB_DP(0x84));
            g_cpu.Y = y;
            uint16_t m = MB_R(0xA782 + x2);
            er_and16(er_long(0x0A), y, m);
            er_and16(er_long(0x0D), y, m);
            cnt = (uint16_t)(er_pull16() - 1);
        } while (cnt != 0);
        pass = (int16_t)(bus_read16(0x7F, 0x0200) - 1);
        bus_write16(0x7F, 0x0200, (uint16_t)pass);
    } while (pass >= 0);
    op_sep(0x20);
    call_jsr(0x00A982);
    op_rep(0x20);
    er_end();
}

/* $00:A792: the scroll-away: the picture layer off ($E042 $3FFF), $DECD;
 * the row table at $7E:3000 (16 pairs of words, $4000 + two counters going
 * apart) every frame for $100 frames ($A87E), $0172 up every $40; the
 * layers back ($0108/$010B/$010C), $8A39. */
void mp_00A792(void) {
    mb_php();
    er_start(0x0015, 0x00A79D);
    CPU_SET_A16(0x3FFF); call_jsl(0x01E042);
    call_jsl(0x01DECD);
    call_jsl(0x01E2CE);
    for (int x = 0x007E; x >= 0; x -= 2) bus_write16(0x7E, (uint16_t)(0x3000 + x), 0);
    MB_W8(0x0108, 0x0A); MB_W8(0x010B, 0x35); MB_W8(0x010C, 0x3C);
    MB_W(0x0178, 0); MB_W(0x0176, 0);
    call_jsl(0x00A87E);
    call_jsl(0x01E2CE);
    bus_write16(0x7F, 0x0202, 0);
    bus_write16(0x7F, 0x0204, 0);
    uint16_t cnt = 0;
    do {
        er_push16(cnt);
        for (int x = 0x003C; x >= 0; x -= 4) {
            bus_write16(0x7E, (uint16_t)(0x3000 + x), (uint16_t)((bus_read16(0x7F, 0x0202) & 0x03FF) | 0x4000));
            bus_write16(0x7E, (uint16_t)(0x3002 + x), (uint16_t)((bus_read16(0x7F, 0x0204) & 0x03FF) | 0x4000));
        }
        g_cpu.X = 0xFFFC;
        call_jsl(0x00A87E);
        call_jsl(0x01E2CE);
        bus_write16(0x7F, 0x0202, (uint16_t)(bus_read16(0x7F, 0x0202) - 1));
        bus_write16(0x7F, 0x0204, (uint16_t)(bus_read16(0x7F, 0x0204) + 1));
        cnt = (uint16_t)(er_pull16() + 1);
        if (!(cnt & 0x003F)) MB_W(0x0172, MB_R(0x0172) + 1);
    } while (cnt != 0x0100);
    op_sep(0x20);
    call_jsr(0x00A982);
    MB_W8(0x0108, 0x09); MB_W8(0x010B, 0x34); MB_W8(0x010C, 0x38);
    op_rep(0x20);
    MB_W(0x0172, 0);
    call_jsr(0x008A39);
    call_jsl(0x01E2CE);
    er_end();
    mb_plp();
}

/* $00:A8A8: the mosaic: $40 frames of mosaic size ($0109 = (n*4 & $F0) | 2)
 * with $0172 = n/8, then off. */
void mp_00A8A8(void) {
    mb_php();
    er_start(0x0016, 0x00A8B3);
    op_sep(0x20);
    uint8_t a = 0;
    do {
        er_push8(a); er_push8(a);
        MB_W8(0x0109, ((uint8_t)(a << 2) & 0xF0) | 0x02);
        a = er_pull8();
        MB_W8(0x0172, (a >> 3) & 0x0F);
        MB_W8(0x0173, 0);
        call_jsl(0x01E2CE);
        a = (uint8_t)(er_pull8() + 1);
    } while (a != 0x40);
    call_jsr(0x00A982);
    MB_W8(0x0109, 0); MB_W8(0x0172, 0); MB_W8(0x0173, 0);
    call_jsl(0x01E2CE);
    op_rep(0x20);
    er_end();
    mb_plp();
}

/* $00:A8FF: the fade: colour math on ($011E/$011F), the fixed colour
 * ($0120 = $E0 | n/2) up over $40 frames, then back. */
void mp_00A8FF(void) {
    mb_php();
    er_start(0x0017, 0x00A90A);
    op_sep(0x20);
    MB_W8(0x011E, 0x00); MB_W8(0x011F, 0x02); MB_W8(0x0120, 0xE0);
    call_jsl(0x01E2CE);
    uint8_t a = 0;
    do {
        er_push8(a);
        MB_W8(0x0120, (a >> 1) | 0xE0);
        call_jsl(0x01E2CE);
        a = (uint8_t)(er_pull8() + 1);
    } while (a != 0x40);
    call_jsr(0x00A982);
    MB_W8(0x011E, 0x30); MB_W8(0x011F, 0x00); MB_W8(0x0120, 0xE0);
    call_jsl(0x01E2CE);
    op_rep(0x20);
    er_end();
    mb_plp();
}

/* $00:A95D: clear at once ($A982). */
void mp_00A95D(void) {
    mb_php();
    er_start(0x0018, 0x00A968);
    op_sep(0x20);
    call_jsr(0x00A982);
    op_rep(0x20);
    er_end();
    mb_plp();
}

/* $00:A9BE: the rocket: $10 frames; its sprites ($0426/$0428 from $AAB7,
 * for the stamp $19C2 + 1 when $19FA), five rows at $022A.. rising from y
 * $C8 to 8 with their tiles cycling ($1B24, $AAA1/$AAA3), the picture
 * cleared behind them row by row ($8A8E). The end is 8-bit. */
void mp_00A9BE(void) {
    mb_php();
    er_start(0x0019, 0x00A9C9);
    uint16_t y = 0x0010;
    do { er_push16(y); g_cpu.Y = y; call_jsl(0x01E2CE); y = (uint16_t)(er_pull16() - 1); } while (y != 0);
    MB_W(0x1B24, 0);
    MB_W(0x0206, 0);
    uint16_t a = MB_R(0x19FA) ? (uint16_t)(MB_R(0x19C2) + 1) : 0;
    uint16_t x = (uint16_t)(a << 2);
    MB_W(0x0426, (uint16_t)((MB_R(0x0426) & 0x0003) | MB_R(0xAAB7 + x)));
    MB_W(0x0428, (uint16_t)((MB_R(0x0428) & 0xFFC0) | MB_R(0xAAB9 + x)));
    op_sep(0x30);
    for (uint8_t xi = 0, yi = 0; xi != 0x0A; xi += 2, yi += 8) {
        MB_W8(0x022A + yi, MB_R8(0xAAAD + xi));
        MB_W8(0x022E + yi, MB_R8(0xAAAE + xi));
    }
    uint8_t yy = 0xC8;
    do {
        er_push8(yy);
        for (int k = 0; k < 5; k++) MB_W8(0x022B + 8 * k, yy);
        uint8_t lo = (uint8_t)(yy + 0x10);
        for (int k = 0; k < 5; k++) MB_W8(0x022F + 8 * k, lo);
        uint8_t t = (uint8_t)(MB_R8(0x1B24) + 1);
        MB_W8(0x1B24, t);
        if (t == 0x06) { t = 0; MB_W8(0x1B24, 0); }
        uint8_t xx = (uint8_t)((t & 0xFE) << 1);
        op_rep(0x20);
        uint16_t w1 = MB_R(0xAAA1 + xx), w2 = MB_R(0xAAA3 + xx);
        for (int k = 0; k < 5; k++) { MB_W(0x022C + 8 * k, w1); MB_W(0x0230 + 8 * k, w2); }
        op_sep(0x20);
        g_cpu.X = xx;
        if (yy < 0xB7) { g_cpu.X = (uint8_t)(yy + 1); call_jsr(0x008A8E); }
        yy = (uint8_t)(er_pull8() - 1);
    } while (yy != 0x07);
    g_cpu.Y = yy;
    MB_W8(0x0206, MB_R8(0x0206) + 1);
    call_jsr(0x00AAC7);
    MB_W8(0x09A7, 0);
    mb_plp();
}
#undef MB_R
#undef MB_W
#undef MB_R8
#undef MB_W8
#undef MB_DP

/* $00:8A8E (X = a picture row): clear that row of the picture ($40 words
 * of [$0A], 16 bytes apart, from row (X & $F8) * $80 + (X & 7) * 2), mark it
 * ($198C), $8AC2, a frame. $8A8A is its long entry. */
void mp_008A8E(void) {
    uint8_t p0 = cpu_get_p();
    bus_wram_write8(g_cpu.S, p0); g_cpu.S = (uint16_t)(g_cpu.S - 1);   /* PHP */
    op_rep(0x30);
    uint16_t x = g_cpu.X;
    uint16_t a = (uint16_t)((x & 0x00F8) << 7);
    bus_write16(g_cpu.DB, 0x198C, a);
    push_a16(a);
    uint16_t y = (uint16_t)(((x & 0x0007) << 1) + a);
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    uint32_t base = ((uint32_t)bus_read8(0x00, (uint16_t)(g_cpu.DP + 0x0C)) << 16) | bus_read16(0x00, (uint16_t)(g_cpu.DP + 0x0A));
    for (int n = 0; n < 0x40; n++) {
        uint32_t q = (base + y) & 0xFFFFFF;
        bus_write16((uint8_t)(q >> 16), (uint16_t)q, 0);
        y = (uint16_t)(y + 0x10);
    }
    g_cpu.X = 0; g_cpu.Y = y;
    call_jsr(0x008AC2);
    bus_write16(g_cpu.DB, 0x0202, (uint16_t)(bus_read16(g_cpu.DB, 0x0202) + 1));
    call_jsl(0x01E2CE);
    g_cpu.S = (uint16_t)(g_cpu.S + 1);                                  /* PLP */
    cpu_set_p(bus_wram_read8(g_cpu.S));
}
void mp_008A8A(void) { call_jsr(0x008A8E); }

/* $00:8AC2: queue the row $198C for the frame's DMA: the 5-word entry from
 * $8AF5 at $0182 + $0204 ($0204 += 9), its source ($198C + $0A at $0183)
 * and VRAM address ($198C / 2 at $0189). */
void mp_008AC2(void) {
    uint8_t db = g_cpu.DB;
    bus_write16(db, 0x0202, 0);
    uint16_t x = bus_read16(db, 0x0204), y = 0;
    do {
        bus_write16(db, (uint16_t)(0x0182 + x), bus_read16(db, (uint16_t)(0x8AF5 + y)));
        y = (uint16_t)(y + 2); x = (uint16_t)(x + 2);
    } while (y < 0x000A);
    bus_write16(db, 0x0204, (uint16_t)(bus_read16(db, 0x0204) + 9));
    bus_write16(db, 0x0183, (uint16_t)(bus_read16(db, 0x198C) + bus_read16(0x00, (uint16_t)(g_cpu.DP + 0x0A))));
    uint16_t a = (uint16_t)(bus_read16(db, 0x198C) >> 1);
    bus_write16(db, 0x0189, a);
    CPU_SET_A16(a); g_cpu.X = x; g_cpu.Y = y;
}

/* ---- $01:CFD5, the wave: the picture's rows shaken by two HDMA tables
 * ($7F:9200 and $7F:9600, two buffers each, $0222/$0224 the one in use)
 * while its colours step through the palettes at $02:FA00. */
#define WV_R(b, o)     bus_read16((b), (uint16_t)(o))
#define WV_W(b, o, v)  bus_write16((b), (uint16_t)(o), (uint16_t)(v))
#define WV_W8(b, o, v) bus_write8((b), (uint16_t)(o), (uint8_t)(v))
static void wv_phb(void) { bus_wram_write8(g_cpu.S, g_cpu.DB); g_cpu.S = (uint16_t)(g_cpu.S - 1); }
static void wv_plb(void) { g_cpu.S = (uint16_t)(g_cpu.S + 1); g_cpu.DB = bus_wram_read8(g_cpu.S); }
static void wv_db7f(void) { wv_phb(); bus_wram_write16((uint16_t)(g_cpu.S - 1), 0x7F7F); g_cpu.DB = 0x7F; }

/* $01:D287: queue the palette upload (the 8 bytes at $01:D2B7). */
void mp_01D287(void) {
    uint8_t p0 = cpu_get_p();
    bus_wram_write8(g_cpu.S, p0); g_cpu.S = (uint16_t)(g_cpu.S - 1);
    wv_phb();
    bus_wram_write8(g_cpu.S, 0x01);                     /* PHA; PLB */
    g_cpu.DB = 0x01;
    WV_W8(0x00, 0x0202, 0);
    uint8_t x = (uint8_t)(bus_read8(0x00, 0x0204) + 7);
    WV_W8(0x00, 0x0204, x);
    for (int y = 7; y >= 0; y--, x--) WV_W8(0x00, 0x0182 + x, bus_read8(0x01, (uint16_t)(0xD2B7 + y)));
    WV_W8(0x00, 0x0202, 1);
    wv_plb();
    g_cpu.S = (uint16_t)(g_cpu.S + 1);
    cpu_set_p(bus_wram_read8(g_cpu.S));
}

/* $01:D259 (A = n): palette n of $02:FA00 into $09AF, queued. */
void mp_01D259(void) {
    uint16_t a = CPU_A16();
    push_a16(a);
    int x = (int)(uint16_t)((a << 5) + 0x1E);
    for (int y = 0x1E; y >= 0; y -= 2, x -= 2) WV_W(g_cpu.DB, 0x09AF + y, bus_read16(0x02, (uint16_t)(0xFA00 + x)));
    call_jsr(0x01D287);
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    CPU_SET_A16(a);
}

/* $01:D279: the picture's own palette back ($02:FEE0), queued. */
void mp_01D279(void) {
    for (int x = 0x1E; x >= 0; x -= 2) WV_W(g_cpu.DB, 0x09AF + x, bus_read16(0x02, (uint16_t)(0xFEE0 + x)));
    call_jsr(0x01D287);
}

/* $01:D22B / $01:D242: the picture's tiles to palette 7 and back. */
void mp_01D22B(void) {
    for (int x = 0x057E; x >= 0; x -= 2) WV_W(0x7E, 0x28C0 + x, WV_R(0x7E, 0x28C0 + x) | 0x1C00);
    call_jsl(0x01DEB2);
}
void mp_01D242(void) {
    for (int x = 0x057E; x >= 0; x -= 2) WV_W(0x7E, 0x28C0 + x, WV_R(0x7E, 0x28C0 + x) & 0xE3FF);
    call_jsl(0x01DEB2);
}

/* $01:D08D: both tables cleared. */
void mp_01D08D(void) {
    wv_db7f();
    for (int x = 0x01FE; x >= 0; x--) {
        WV_W(0x7F, 0x9200 + x, 0); WV_W(0x7F, 0x9400 + x, 0);
        WV_W(0x7F, 0x9600 + x, 0); WV_W(0x7F, 0x9800 + x, 0);
    }
    wv_plb();
    WV_W(g_cpu.DB, 0x0222, 0);
    WV_W(g_cpu.DB, 0x0224, 0);
    g_cpu.X = 0xFFFF;
}

/* Next entry of a table: past line $FF a repeat byte $E1; at $1C2 done. */
static bool wv_next(uint16_t base, uint16_t *x) {
    uint16_t t = *x & 0x01FF;
    if (t == 0x01C2) return true;
    if (t == 0x00FF) { WV_W8(0x7F, base + *x, 0xE1); (*x)++; }
    return false;
}
static uint16_t wv_rand(void) { call_jsl(0x01E20C); return CPU_A16(); }

/* $01:D0AD: table 1, the shake: runs of 0-15 still lines, then 0-3
 * lines at -8..+7. */
void mp_01D0AD(void) {
    wv_db7f();
    uint16_t x = (bus_read16(0x00, 0x0222) & 1) ? 0x0200 : 0;
    WV_W8(0x7F, 0x9200 + x, 0xFF); x++;
    for (;;) {
        int16_t y = (int16_t)(wv_rand() & 0x000F);
        bool end = false;
        do {
            WV_W(0x7F, 0x9200 + x, 0); x = (uint16_t)(x + 2);
            if ((end = wv_next(0x9200, &x))) break;
        } while (--y >= 0);
        if (end) break;
        y = (int16_t)(wv_rand() & 0x0003);
        do {
            WV_W(0x7F, 0x9200 + x, (uint16_t)((wv_rand() & 0x000F) - 8)); x = (uint16_t)(x + 2);
            if ((end = wv_next(0x9200, &x))) break;
        } while (--y >= 0);
        if (end) break;
    }
    WV_W8(0x7F, 0x9200 + x, 0);
    WV_W(0x00, 0x0222, bus_read16(0x00, 0x0222) ^ 0x0003);
    g_cpu.X = x;
    wv_plb();
}

/* $01:D13B: table 2, the slant: each line 1-16 more than the last. */
void mp_01D13B(void) {
    wv_db7f();
    uint16_t x = (bus_read16(0x00, 0x0224) & 1) ? 0x0200 : 0;
    WV_W8(0x7F, 0x9600 + x, 0xFF); x++;
    uint16_t acc = 0;
    push_a16(acc);
    uint16_t r = (uint16_t)((wv_rand() & 0x000F) + 1);
    push_a16(r);
    for (;;) {
        WV_W(0x7F, 0x9600 + x, acc);
        acc = (uint16_t)(acc + r);
        bus_wram_write16((uint16_t)(g_cpu.S + 3), acc);
        x = (uint16_t)(x + 2);
        if (wv_next(0x9600, &x)) break;
    }
    g_cpu.S = (uint16_t)(g_cpu.S + 4);
    WV_W8(0x7F, 0x9600 + x, 0);
    WV_W(0x00, 0x0224, bus_read16(0x00, 0x0224) ^ 0x0003);
    g_cpu.X = x;
    wv_plb();
}

/* $01:D1A1 / $01:D1E6: table 1 / 2 still. */
static void wv_still(uint16_t base, uint16_t flag) {
    wv_db7f();
    uint16_t x = (bus_read16(0x00, flag) & 1) ? 0x0200 : 0;
    WV_W8(0x7F, base + x, 0x7F); x++;
    WV_W(0x7F, base + x, 0); x = (uint16_t)(x + 2);
    WV_W8(0x7F, base + x, 0x61); x++;
    WV_W(0x7F, base + x, 0); x = (uint16_t)(x + 2);
    WV_W8(0x7F, base + x, 0);
    WV_W(0x00, flag, bus_read16(0x00, flag) ^ 0x0003);
    g_cpu.X = x;
    wv_plb();
}
void mp_01D1A1(void) { wv_still(0x9200, 0x0222); }
void mp_01D1E6(void) { wv_still(0x9600, 0x0224); }

/* PHA; body; frame; PLA; DEC; BPL: n+1 frames from n down to 0. */
static void wv_frames(uint16_t n, void (*body)(void)) {
    int16_t a = (int16_t)n;
    do {
        push_a16((uint16_t)a);
        body();
        call_jsl(0x01E2CE);
        a = (int16_t)(bus_wram_read16((uint16_t)(g_cpu.S + 1)) - 1);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    } while (a >= 0);
}
static void wv_shake(void) { call_jsr(0x01D0AD); }
static void wv_rest(void) { call_jsr(0x01D1A1); }
static void wv_both(void) { call_jsr(0x01D0AD); call_jsr(0x01D13B); }
static void wv_shake_rest2(void) { call_jsr(0x01D0AD); call_jsr(0x01D1E6); }

/* $01:CFD5 (JSL): 16 rounds of shaking (longer each round) with rests,
 * then 16 rounds of shake and slant, then the picture rows cleared from
 * $B8 up to 9 ($8A8A) still shaking; all back. */
void mp_01CFD5(void) {
    CPU_SET_A16(0); call_jsr(0x01D259);
    call_jsr(0x01D22B);
    call_jsl(0x01E2CE);
    call_jsr(0x01D08D);
    WV_W(g_cpu.DB, 0x0222, WV_R(g_cpu.DB, 0x0222) + 1);
    WV_W(g_cpu.DB, 0x0224, WV_R(g_cpu.DB, 0x0224) + 1);
    for (uint16_t a = 0x0010; a != 0; a--) {
        push_a16(a);
        uint16_t k = (uint16_t)((uint16_t)~a + 0x0011);
        CPU_SET_A16(k); call_jsr(0x01D259);
        wv_frames(k, wv_shake);
        CPU_SET_A16((uint16_t)((((uint16_t)~a + 0x0011) >> 2) + 4)); call_jsr(0x01D259);
        wv_frames(a, wv_rest);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    }
    CPU_SET_A16(0x000F); call_jsr(0x01D259);
    for (uint16_t a = 0x0010; a != 0; a--) {
        push_a16(a);
        wv_frames((uint16_t)((uint16_t)~a + 0x0011), wv_both);
        wv_frames(a, wv_shake_rest2);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    }
    WV_W(g_cpu.DB, 0x0206, 0);
    for (uint16_t a = 0x00B8; a != 0x0008; a--) {
        push_a16(a);
        call_jsr(0x01D0AD);
        call_jsr(0x01D13B);
        g_cpu.X = a;
        call_jsl(0x008A8A);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    }
    WV_W(g_cpu.DB, 0x0206, WV_R(g_cpu.DB, 0x0206) + 1);
    WV_W(g_cpu.DB, 0x0222, 0xFFFF);
    WV_W(g_cpu.DB, 0x0224, 0xFFFF);
    call_jsr(0x01D242);
    call_jsr(0x01D279);
}
#undef WV_R
#undef WV_W
#undef WV_W8

/* $00:A617: the wave ($01:CFD5). */
void mp_00A617(void) {
    er_start(0x0012, 0x00A621);
    call_jsl(0x01CFD5);
    er_end();
}

#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
/* $01:D39F (JSL, A 8-bit): $40 frames. */
void mp_01D39F(void) {
    uint8_t a = 0x40;
    do {
        bus_wram_write8(g_cpu.S, a); g_cpu.S = (uint16_t)(g_cpu.S - 1);
        call_jsl(0x01E2CE);
        g_cpu.S = (uint16_t)(g_cpu.S + 1);
        a = (uint8_t)(bus_wram_read8(g_cpu.S) - 1);
    } while (a != 0);
    CPU_SET_A8(0);
}

#undef MB_R
#undef MB_W
#undef MB_DP



#define MB_R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MB_W(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define MB_DP(o)    ((uint16_t)(g_cpu.DP + (o)))
/* $00:EF6E (music bar 8-23, on a click): leave the music editor. Fade the
 * music ($D39F), the canvas sound bank ($1A), the leaving sound ($D3AB);
 * then it drops its caller's return address (PLA), so its RTS (at the end
 * of $A363) leaves the music editor's loop. The tool and its state back
 * ($09DC, $0EB0, $09DE, $09D8), the canvas screen ($C414, or $C3DE with
 * $19FA), tool switch 7, and the stamp editor or the tool's bar. */
void mp_00EF6E(void) {
    call_jsl(0x01E2F3);
    uint16_t c = MB_R(0x04CA);
    if (!(c & 0x0020)) { CPU_SET_A16(c); return; }
    mb_sound(0x0001);
    op_sep(0x20);
    call_jsl(0x01D39F);
    call_jsl(0x01E30E);
    bus_write8(0x00, MB_DP(0xCC), 0x00);
    bus_write8(0x00, MB_DP(0xCD), 0x80);
    bus_write8(0x00, MB_DP(0xCE), 0x1A);
    bus_write8(g_cpu.DB, 0x2140, 0xFF);
    CPU_SET_A8(0xFF);
    call_jsl(0x01DF25);
    op_rep(0x20);
    call_jsl(0x01E2F3);
    call_jsl(0x01D3AB);
    g_cpu.S = (uint16_t)(g_cpu.S + 2);                       /* PLA: the caller's return */
    mb_php();
    call_jsl(0x01E06F);
    MB_W(0x0997, MB_R(0x0EB0));
    uint16_t t = MB_R(0x09DC);
    MB_W(0x04D0, t);
    if (t == 0x0007) MB_W(0x1992, MB_R(0x1992) + 1);
    bus_write16(0x00, MB_DP(0xA6), MB_R(0x09DE));
    if (MB_R(0x19FA) == 0) call_jsr(0x00C414); else call_jsr(0x00C3DE);
    bus_write8(g_cpu.DB, 0x010E, 0x06);
    CPU_SET_A16(0x3FFF); call_jsl(0x01E033);
    MB_W(0x0172, 0);
    call_jsr(0x008A16);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    MB_W(0x0E8E, 0);
    mb_tool(0x0007);
    mb_cdE1(0xCF16);
    mb_cdE1(0xCE4B);
    call_jsr(0x00A451);
    call_jsr(0x00A525);
    MB_W(0x09D6, MB_R(0x09D8));
    MB_W(0x0206, MB_R(0x0206) + 1);
    mb_plp();
    if (MB_R(0x1992) != 0) {
        CPU_SET_A16(0x000F); call_jsr(0x00B66C);
        CPU_SET_A16((uint16_t)(MB_R(0x1994) + 8)); call_jsr(0x00B6F4);
    } else {
        bus_write16(0x00, MB_DP(0xA8), (uint16_t)(MB_R(0x04D0) & 0xFF00));
        call_jsr(0x0096E1);
        if (MB_R(0x04D0) == 0x0006) MB_W(0x09AB, MB_R(0x09AB) + 1);
    }
    tail_jmp(0x00A363);
}
#undef MB_R
#undef MB_W
#undef MB_DP

/* ---- Small helpers the tour runs on the ROM (their callers' C versions
 * do the same work inline). ---- */

static uint16_t st16(uint8_t o) { return bus_wram_read16((uint16_t)(g_cpu.S + o)); }
static void st16w(uint8_t o, uint16_t v) { bus_wram_write16((uint16_t)(g_cpu.S + o), v); }

/* $00:B610: queue a fill span (the fill's queue at $7F:6000, write index
 * DP $7E, read index DP $7C): the pixel x/row word at 5,S and the tile
 * offset at 3,S (pushed by the caller). Full: the entry is written but the
 * index stays. */
static void mp_00B610(void) {
    uint16_t dp = g_cpu.DP;
    uint16_t t = bus_read16(0x00, (uint16_t)(dp + 0x7E));
    g_cpu.X = t;
    bus_write16(0x7F, (uint16_t)(0x6000 + t), st16(5));
    bus_write16(0x7F, (uint16_t)(0x6002 + t), st16(3));
    uint16_t h = (uint16_t)((bus_read16(0x00, (uint16_t)(dp + 0x7C)) - 4) & 0x1FFF);
    if (h == t) { CPU_SET_A16(h); return; }
    t = (uint16_t)((t + 4) & 0x1FFF);
    bus_write16(0x00, (uint16_t)(dp + 0x7E), t);
    CPU_SET_A16(t);
}

/* $00:B637: the fill's next span. A button pressed during the fill
 * ($1A0C bit 5): leave the whole fill (S back to the one saved in DP $B0
 * by $B3FF, $1B1C cleared, PLP, RTS). Queue empty: carry set. Else the
 * entry into 5,S / 3,S, the read index on, carry clear. */
static void mp_00B637(void) {
    uint16_t dp = g_cpu.DP;
    if (bus_wram_read16(0x1A0C) & 0x0020) {
        g_cpu.X = bus_read16(0x00, (uint16_t)(dp + 0xB0));
        g_cpu.S = g_cpu.X;
        op_sep(0x20);
        bus_wram_write8(0x1B1C, 0x00);
        g_cpu.S = (uint16_t)(g_cpu.S + 1);
        cpu_set_p(bus_wram_read8(g_cpu.S));
        return;
    }
    uint16_t h = bus_read16(0x00, (uint16_t)(dp + 0x7C));
    CPU_SET_A16(h);
    if (h == bus_read16(0x00, (uint16_t)(dp + 0x7E))) { g_cpu.flag_C = true; return; }
    g_cpu.X = h;
    st16w(5, bus_read16(0x7F, (uint16_t)(0x6000 + h)));
    st16w(3, bus_read16(0x7F, (uint16_t)(0x6002 + h)));
    h = (uint16_t)((h + 4) & 0x1FFF);
    bus_write16(0x00, (uint16_t)(dp + 0x7C), h);
    CPU_SET_A16(h);
    g_cpu.flag_C = false;
}

/* $0F:C118: the demo's next entry ($04E6, 15 bits); at the wrap the other
 * bank pair ($04E8 ^ 3). */
static void mp_0FC118(void) {
    uint16_t i = (uint16_t)((bus_wram_read16(0x04E6) + 1) & 0x7FFF);
    if (i == 0) bus_wram_write16(0x04E8, (uint16_t)(bus_wram_read16(0x04E8) ^ 0x0003));
    bus_wram_write16(0x04E6, i);
    CPU_SET_A16(i);
}

/* $01:DB25: strobe the joypad latch ($4016 = 1), read $4016,X, release. */
static void mp_01DB25(void) {
    bus_write8(0x00, 0x4016, 0x01);
    CPU_SET_A8(bus_read8(0x00, (uint16_t)(0x4016 + g_cpu.X)));
    bus_write8(0x00, 0x4016, 0x00);
}

/* $00:E2A3: copy the 64 words at $101C to $109C. */
static void mp_00E2A3(void) {
    for (int x = 0x7E; x >= 0; x -= 2)
        bus_wram_write16((uint16_t)(0x109C + x), bus_wram_read16((uint16_t)(0x101C + x)));
    g_cpu.X = 0xFFFE;
}

/* $00:DA49: one button of a check chain: the cursor in the box x $04-$23,
 * y $88-$9B: $E2A3, sound $0F, $1AF5 = $FFFF, $09D6 = $16, and back to the
 * caller's caller (PLA, RTS). x at $24 or more: on to the next button
 * ($DA7B, which follows). */
static void mp_00DA49(void) {
    uint16_t x = bus_wram_read16(0x04DC);
    CPU_SET_A16(x);
    if (x < 0x0004) return;
    if (x >= 0x0024) { tail_jmp(0x00DA7B); return; }
    uint16_t y = bus_wram_read16(0x04DE);
    CPU_SET_A16(y);
    if (y < 0x0088 || y >= 0x009C) return;
    mp_00E2A3();
    CPU_SET_A16(0x000F);
    call_jsl(0x01D368);
    bus_wram_write16(0x1AF5, 0xFFFF);
    bus_wram_write16(0x09D6, 0x0016);
    CPU_SET_A16(st16(1));
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
}

/* $00:9F87: clamp the rectangle on the stack (x at 9,S and 7,S, y at 5,S
 * and 3,S) to the canvas $19B6..$19B8 / $19BA..$19BC, by the sign of the
 * difference as the ROM's CMP/BMI/BPL do. */
static void mp_009F87(void) {
    static const uint8_t xs[2] = { 9, 7 }, ys[2] = { 5, 3 };
    for (int k = 0; k < 2; k++) {
        uint16_t lo = bus_wram_read16(0x19B6);
        if (!((uint16_t)(lo - st16(xs[k])) & 0x8000)) st16w(xs[k], lo);
    }
    for (int k = 0; k < 2; k++) {
        uint16_t hi = bus_wram_read16(0x19B8);
        if ((uint16_t)(hi - st16(xs[k])) & 0x8000) st16w(xs[k], hi);
    }
    for (int k = 0; k < 2; k++) {
        uint16_t lo = bus_wram_read16(0x19BA);
        if (!((uint16_t)(lo - st16(ys[k])) & 0x8000)) st16w(ys[k], lo);
    }
    for (int k = 0; k < 2; k++) {
        uint16_t hi = bus_wram_read16(0x19BC);
        if ((uint16_t)(hi - st16(ys[k])) & 0x8000) st16w(ys[k], hi);
    }
    CPU_SET_A16(bus_wram_read16(0x19BC));
}

/* ---- Reached by the special start's attract loop ($01:9372). ---- */

/* $00:B98C: restore parts of the screen from the copy at $7F:A000: $8B03,
 * then the list for the stamp set $19C2 (its pointer at $00:B9EF): per
 * entry rows, width in bytes, source and target offsets (words; ends at
 * 0); each row copied word by word within bank $7F, the source one tile
 * row ($400) on. Its three counters live on the stack in the ROM. */
static void mp_00B98C(void) {
    call_jsr(0x008B03);
    uint8_t db = g_cpu.DB;
    uint16_t ptr = bus_read16(db, (uint16_t)(0xB9EF + (uint16_t)(bus_read16(db, 0x19C2) << 1)));
    uint16_t y = 0, x = g_cpu.X;
    for (;;) {
        uint16_t rows = bus_read16(db, (uint16_t)(ptr + y));
        if (rows == 0) break;
        uint16_t width = bus_read16(db, (uint16_t)(ptr + y + 2));
        x = bus_read16(db, (uint16_t)(ptr + y + 4));
        uint16_t t = bus_read16(db, (uint16_t)(ptr + y + 6));
        do {
            uint16_t n = width;
            do {
                bus_write16(0x7F, t, bus_read16(0x7F, (uint16_t)(0xA000 + x)));
                x += 2; t += 2; n -= 2;
            } while (n != 0);
            x = (uint16_t)(x + 0x0400 - width);
        } while (--rows != 0);
        y = (uint16_t)(y + 8);
    }
    g_cpu.X = x; g_cpu.Y = y;
    CPU_SET_A16(ptr);
}

/* $00:B988: JSL entry of $B98C. */
static void mp_00B988(void) { mp_00B98C(); }

/* $00:C7AF: the music's tempo $0EAE from $0C24 ((n - $10) / 2 * 3 / 2),
 * sound and music state 0 ($D328, $D348), the player's counters cleared. */
static void mp_00C7AF(void) {
    uint16_t h = (uint16_t)((bus_wram_read16(0x0C24) - 0x0010) >> 1);
    bus_wram_write16(0x0EAE, (uint16_t)((h >> 1) + h));
    CPU_SET_A16(0x0000); call_jsl(0x01D328);
    CPU_SET_A16(0x0000); call_jsl(0x01D348);
    bus_wram_write16(0x0C2E, 0); bus_wram_write16(0x0C30, 0);
    bus_wram_write16(0x0E92, 0); bus_wram_write16(0x0E90, 0);
}

/* $00:C7DB: one step of the saved song in the attract loop. When the
 * step flag $0E92 is set: at the end of the song ($0E90 = $0EAE) either
 * stop (no repeat, $0C26 = 0: $C865) or start over; then the three
 * channels' notes at $09E4 + $0E90 (negative: rest) go to $D328, $D348
 * and $D368 as (note + $100) / 16 | note, low byte; $0E90 moves on by 3
 * words. Then the beat counter $0C30:$0C2E += $0C2C:$0C2A, its carry the
 * next step flag. */
static void mp_00C7DB(void) {
    if (bus_wram_read16(0x0E92) != 0) {
        uint16_t x = bus_wram_read16(0x0E90);
        if (x == bus_wram_read16(0x0EAE)) {
            if (bus_wram_read16(0x0C26) == 0) {                     /* $C865 */
                bus_wram_write16(0x0EAC, 0);
                uint16_t sp = g_cpu.S;
                uint16_t aa = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA));
                bus_wram_write16((uint16_t)(sp - 1), aa);
                bus_wram_write16((uint16_t)(sp - 3), 0x0000);
                g_cpu.S = (uint16_t)(sp - 4);
                CPU_SET_A16(0x0000);
                call_jsr(0x009FC4);
                return;
            }
            bus_wram_write16(0x0E90, 0);
            x = 0;
        }
        static const uint32_t ch[3] = { 0x01D328, 0x01D348, 0x01D368 };
        for (int k = 0; k < 3; k++, x += 2) {
            uint16_t v = bus_wram_read16((uint16_t)(0x09E4 + x));
            if (v & 0x8000) continue;
            CPU_SET_A16((uint16_t)((((uint16_t)(v + 0x0100) >> 4) | v) & 0x00FF));
            call_jsl(ch[k]);
        }
        g_cpu.X = x;
        bus_wram_write16(0x0E90, x);
    }
    uint32_t lo = (uint32_t)bus_wram_read16(0x0C2A) + bus_wram_read16(0x0C2E);
    bus_wram_write16(0x0C2E, (uint16_t)lo);
    uint32_t hi = (uint32_t)bus_wram_read16(0x0C2C) + bus_wram_read16(0x0C30) + (lo >> 16);
    bus_wram_write16(0x0C30, (uint16_t)hi);
    bus_wram_write16(0x0E92, (uint16_t)(hi >> 16));
    CPU_SET_A16((uint16_t)hi);
}

/* $00:BBD8: one frame of the attract loop's animation playback. The
 * frame counter sprite and its blinking marker ($F91E), then the next
 * frame of the saved animation from the stream at $7E:3800 ($19EA
 * position; flag bytes $19EE/$19F6 say which entries carry a hold count
 * $19F2 and a sign for x), drawn at its position by $01:F9BB; the frame
 * advance through the speed accumulator $1A00 += $1A02 (a carry, or speed
 * 0, steps $19FE, wrapping at the set's length $00:BE37); the saved song's
 * step ($C7DB) while it plays ($0EAC). A click goes on in the ROM ($BCDB:
 * the attract loop's buttons), with the PHP still on the stack. */
static void mp_00BBD8(void) {
    uint8_t db = g_cpu.DB;
    uint8_t p0 = cpu_get_p();
    uint16_t sp = g_cpu.S;
    bus_wram_write8(sp, p0);                                     /* PHP */
    g_cpu.S = (uint16_t)(sp - 1);
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    op_rep(0x20);
    uint16_t set2 = (uint16_t)(R16(0x19C2) << 1);
    uint8_t y8 = (uint8_t)set2;
    bus_write8(db, 0x0105, R16(0x19FE) >= R16(0xBE31 + y8) ? 0x0A : 0x02);
    op_rep(0x10);
    g_cpu.X = 0x0028; g_cpu.Y = 0x0007;
    CPU_SET_A16((uint16_t)(R16(0x1A08) + 0x0096));
    call_jsl(0x01F91E);
    uint16_t blink = R16(0x1A0A);
    W16(0x1A0A, blink + 1);
    if (!(blink & 0x0008)) {
        uint16_t f = R16(0x1A08);
        g_cpu.X = (uint16_t)((uint16_t)(f << 4) - f + 0x0038);
        g_cpu.Y = 0x0007;
        CPU_SET_A16(0x00A3);
        call_jsl(0x01F91E);
    }
    uint16_t base = (uint16_t)(R16(0xBE3D + set2) + R16(0x19FE));
    uint16_t x = R16(0x19EA);
    if (R16(0x19F2) != 0) {
        W16(0x19F2, R16(0x19F2) - 1);
    } else {
        x += 2;
        if (x == R16(0x19EC)) { W16(0x19EE, 0); x = 0; }
        W16(0x19F6, R16(0x19F6) >> 1);
        W16(0x19EE, R16(0x19EE) >> 1);
        if (!(R16(0x19EE) & 0x0100)) {
            W16(0x19EE, (uint16_t)(bus_read8(0x7E, (uint16_t)(0x3800 + x)) | 0xFF00));
            x++;
            W16(0x19F6, bus_read16(0x7E, (uint16_t)(0x3800 + x)));
            x++;
        }
        if (R16(0x19EE) & 0x0001) {
            W16(0x19F2, bus_read8(0x7E, (uint16_t)(0x3800 + x)));
            x++;
        }
        W16(0x19EA, x);
    }
    uint16_t sign = (R16(0x19F6) & 0x0001) ? 0xFF00 : 0x0000;
    g_cpu.Y = bus_read8(0x7E, (uint16_t)(0x3800 + x));
    g_cpu.X = (uint16_t)(bus_read8(0x7E, (uint16_t)(0x3801 + x)) | sign);
    CPU_SET_A16(base);
    call_jsl(0x01F9BB);
    bool step = true;
    uint16_t spd = R16(0x1A02);
    if (spd != 0) {
        uint32_t t = (uint32_t)spd + R16(0x1A00);
        W16(0x1A00, (uint16_t)t);
        step = (t >> 16) != 0;
    }
    if (step) {
        W16(0x19FE, R16(0x19FE) + 1);
        if (R16(0x19FE) == R16(0xBE37 + set2)) W16(0x19FE, 0);
    }
    if (R16(0x0EAC) != 0) call_jsr(0x00C7DB);
    if (R16(0x04CA) & 0x0020) {
        /* $BCDB: the buttons, in the ROM (S keeps the PHP). */
        recomp_set_redirect(0x00BCDB);
        return;
    }
    g_cpu.S = sp;                                                /* PLP */
    cpu_set_p(p0);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
#undef R16
#undef W16
}

/* ---- Main states ($09D6, table $87B0). ---- */

/* $00:D505: state $0B: $D824, then $01:EB81. */
static void mp_00D505(void) {
    call_jsr(0x00D824);
    call_jsl(0x01EB81);
}

/* $00:D03E / $00:D2F6: states $07 and $09: $D824, and once the counter
 * ($1A76 / $1A7E, low byte) reaches 8, four colour words from $00:CF40 /
 * $00:CF48 into $7E:208A, uploaded ($01:DE97), a frame, and the next
 * state ($08 / $0A). */
static void state_wait8(uint16_t counter, uint16_t colours, uint16_t next) {
    call_jsr(0x00D824);
    uint16_t a = (uint16_t)(bus_wram_read16(counter) & 0x00FF);
    CPU_SET_A16(a);
    if (a != 0x0008) return;
    for (int x = 6; x >= 0; x -= 2)
        bus_write16(0x7E, (uint16_t)(0x208A + x), bus_read16(0x00, (uint16_t)(colours + x)));
    g_cpu.X = 0xFFFE;
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    bus_wram_write16(0x09D6, next);
    CPU_SET_A16(next);
}
static void mp_00D03E(void) { state_wait8(0x1A76, 0xCF40, 0x0008); }
static void mp_00D2F6(void) { state_wait8(0x1A7E, 0xCF48, 0x000A); }

/* $00:C94B: the sound-test row picked (state $01, the row in $1B1A):
 * only for a click between x $40 and $BF and a row other than the current
 * one ($04C4): its sound ($17 for row 0, else $44 + row), the old row's
 * three marks restored (the first from $0D:EB27, then $06FF, $05FF), the
 * new row's three marks off ($FFFF), at $1A15 + 8 * (row + 1/4/7). */
static void mp_00C94B(void) {
    uint8_t db = g_cpu.DB;
    uint16_t x = bus_wram_read16(0x04DC);
    if (x < 0x0040 || x >= 0x00C0) return;
    uint16_t row = bus_read16(db, 0x1B1A), cur = bus_read16(db, 0x04C4);
    if (row == cur) return;
    CPU_SET_A16(row == 0 ? 0x0017 : (uint16_t)(row + 0x0044));
    call_jsl(0x01D368);
    cur = bus_read16(db, 0x04C4);
    uint16_t o = (uint16_t)((cur + 1) << 3);
    bus_write16(db, (uint16_t)(0x1A15 + o), bus_read16(0x0D, (uint16_t)(0xEB27 + o)));
    bus_write16(db, (uint16_t)(0x1A15 + (uint16_t)((cur + 4) << 3)), 0x06FF);
    bus_write16(db, (uint16_t)(0x1A15 + (uint16_t)((cur + 7) << 3)), 0x05FF);
    row = bus_read16(db, 0x1B1A);
    bus_write16(db, 0x04C4, row);
    for (int k = 1; k <= 7; k += 3) {
        o = (uint16_t)((row + k) << 3);
        bus_write16(db, (uint16_t)(0x1A15 + o), 0xFFFF);
    }
    g_cpu.X = o;
    CPU_SET_A16(0xFFFF);
}

/* $00:C93C: JMP ($C941,X) by A (0..4): $C94B for 1-3, $CA76 for 0 and 4. */
static void mp_00C93C(void) {
    uint16_t x = (uint16_t)(CPU_A16() << 1);
    g_cpu.X = x;
    uint16_t t = bus_read16(0x00, (uint16_t)(0xC941 + x));
    if (t == 0xC94B) { mp_00C94B(); return; }
    tail_jmp((uint32_t)t);
}

/* $00:C900: state $01, the sound test: a click below y $30 picks a row
 * ((y - $30) / 32, at most 4, and 4 only from y $C8): $1B1A = row - 1 and
 * $C93C with the row; then the ten objects 9..0 drawn ($01:F84F). */
static void mp_00C900(void) {
    if (bus_wram_read16(0x04CA) & 0x0020) {
        uint16_t y = bus_wram_read16(0x04DE);
        if (y >= 0x0030) {
            uint16_t row = (uint16_t)((y - 0x0030) >> 5);
            bool go = true;
            if (row >= 0x0004) {
                if (y < 0x00C8) go = false;
                else row = 0x0004;
            }
            if (go) {
                bus_write16(g_cpu.DB, 0x1B1A, (uint16_t)(row - 1));
                CPU_SET_A16(row);
                call_jsr(0x00C93C);
            }
        }
    }
    for (int16_t n = 9; n >= 0; n--) {
        CPU_SET_A16((uint16_t)n);
        call_jsl(0x01F84F);
    }
    CPU_SET_A16(0xFFFF);
}

/* $00:CF50: leave the save/load menu (its exit button): sound $41, the
 * screen faded ($0446 = 4, $01:E09B), the canvas back ($01:CDE1 with
 * $CE4B, the colour $1976, $A451, $A525, $89C3, $C414), uploaded and a
 * frame; then the tool $07 ($9FC4 with DP $AA = 7 and 0 on the stack) and
 * the state from before ($09D8). */
static void mp_00CF50(void) {
    CPU_SET_A16(0x0041);
    call_jsl(0x01D368);
    bus_wram_write16(0x0446, 0x0004);
    call_jsl(0x01E09B);
    CPU_SET_A16(0xCE4B);
    call_jsl(0x01CDE1);
    bus_wram_write16(0x04D0, bus_wram_read16(0x1976));
    call_jsr(0x00A451);
    call_jsr(0x00A525);
    call_jsr(0x0089C3);
    call_jsr(0x00C414);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xAA), 0x0007);
    uint16_t sp = g_cpu.S;
    bus_wram_write16((uint16_t)(sp - 1), 0x0007);
    bus_wram_write16((uint16_t)(sp - 3), 0x0000);
    g_cpu.S = (uint16_t)(sp - 4);
    CPU_SET_A16(0x0000);
    call_jsr(0x009FC4);
    uint16_t st = bus_wram_read16(0x09D8);
    bus_wram_write16(0x09D6, st);
    CPU_SET_A16(st);
}

/* $00:CF9B: state $06, the save/load menu: a click on its boxes (x $48 to
 * $AF; y $48-$67 the first: state $07, sound $0D; with a saved game
 * ($1982), y $70-$8F the second: state $09, sound $0E; y $98-$B7 the
 * third: state $0B, $0563 = $40, sound $0E), or on the exit button
 * (y from $C8, x $08-$16: $CF50); then $D824. */
void mp_00CF9B(void) {
    if (bus_wram_read16(0x04CA) & 0x0020) {
        uint16_t x = bus_wram_read16(0x04DC), y = bus_wram_read16(0x04DE);
        bool box = false;
        if (x >= 0x0048 && x < 0x00B0 && y >= 0x0048) {
            if (y < 0x0068) {
                bus_wram_write16(0x1A65, 0xFFFF);
                bus_wram_write16(0x1A75, 0xFFFF);
                bus_wram_write16(0x09D6, 0x0007);
                CPU_SET_A16(0x000D);
                call_jsl(0x01D368);
                box = true;
            } else {
                bool third = false;
                if (bus_wram_read16(0x1982) == 0) third = true;
                else if (y >= 0x0070) {
                    if (y < 0x0090) {
                        bus_wram_write16(0x1A6D, 0xFFFF);
                        bus_wram_write16(0x1A7D, 0xFFFF);
                        bus_wram_write16(0x09D6, 0x0009);
                        CPU_SET_A16(0x000E);
                        call_jsl(0x01D368);
                        box = true;
                    } else third = true;
                }
                if (third && !box && y >= 0x0098 && y < 0x00B8) {
                    bus_wram_write16(0x1A95, 0xFFFF);
                    bus_wram_write16(0x0563, 0x0040);
                    bus_wram_write16(0x09D6, 0x000B);
                    CPU_SET_A16(0x000E);
                    call_jsl(0x01D368);
                    box = true;
                }
            }
        }
        if (!box && y >= 0x00C8 && x >= 0x0008 && x < 0x0017) {
            mp_00CF50();
            return;
        }
    }
    call_jsr(0x00D824);
}

/* ---- The music editor's scroll bar ($0C28, 0..$9F). ---- */

/* $F8B5: the scroll position moved by A, kept within 0..$9F (by the sign
 * of the sum, as the ROM's BPL/BMI), then the view redrawn ($F921). */
static void music_scroll_by(uint16_t a) {
    a = (uint16_t)(a + bus_wram_read16(0x0C28));
    if (a & 0x8000) a = 0;
    if (!((uint16_t)(a - 0x009F) & 0x8000)) a = 0x009F;
    bus_wram_write16(0x0C28, a);
    CPU_SET_A16(a);                       /* $F921 takes the position in A */
    call_jsr(0x00F921);
}

/* $00:F827: a click on the scroll bar (x $68-$A7): its arrows (x below
 * $70 or from $A0) step by one ($FCF4, $FEB1, $FF5F first); on the bar,
 * left or right of the thumb ((pos / 4) + $71, 5 wide) a page of $10,
 * on the thumb the drag starts ($0E96 / $0E9A = the cursor). Only with
 * the click bit ($04CA bit 6); elsewhere the drag ends ($0E96 = 0). */
static void mp_00F827(void) {
    bus_wram_write16(0x0E94, 0);
    uint16_t x = bus_wram_read16(0x04DC);
    bool arrows = (x >= 0x0068 && x < 0x0070) || (x >= 0x00A0 && x < 0x00A8);
    bool bar = x >= 0x0070 && x < 0x00A0;
    if ((!arrows && !bar) || !(bus_wram_read16(0x04CA) & 0x0040)) {
        bus_wram_write16(0x0E96, 0);
        return;
    }
    call_jsr(0x00FCF4);
    call_jsr(0x00FEB1);
    call_jsr(0x00FF5F);
    x = bus_wram_read16(0x04DC);
    if (arrows) { music_scroll_by(x < 0x0070 ? 0xFFFF : 0x0001); return; }
    uint16_t t = (uint16_t)((bus_wram_read16(0x0C28) >> 2) + 0x0071);
    if (t >= x) { bus_wram_write16(0x0E96, 0); music_scroll_by(0xFFF0); return; }
    if ((uint16_t)(t + 5) < x) { bus_wram_write16(0x0E96, 0); music_scroll_by(0x0010); return; }
    bus_wram_write16(0x0E96, x);
    bus_wram_write16(0x0E9A, bus_wram_read16(0x04DE));
}

/* $00:F8CC: dragging the scroll bar's thumb: the cursor's move since the
 * last frame ($0E96) times 4 added to the position; past either end the
 * cursor is pulled back by the overshoot / 4; then the view ($F921), the
 * cursor's x kept as the drag's origin and its y held at $0E9A. */
static void mp_00F8CC(void) {
    uint16_t cx = bus_wram_read16(0x04DC);
    uint16_t a = (uint16_t)(-(uint16_t)((uint16_t)(bus_wram_read16(0x0E96) - cx) << 2));
    bus_wram_write16(0x0E96, cx);
    a = (uint16_t)(a + bus_wram_read16(0x0C28));
    if (a & 0x8000) {
        a = (uint16_t)(-a);
        a >>= 2;
        bus_wram_write16(0x04DC, (uint16_t)(a + bus_wram_read16(0x04DC)));
        a = 0;
    }
    if (!((uint16_t)(a - 0x009F) & 0x8000)) {
        a = (uint16_t)((uint16_t)(a - 0x009F) >> 2);
        a = (uint16_t)(-a);
        bus_wram_write16(0x04DC, (uint16_t)(a + bus_wram_read16(0x04DC)));
        a = 0x009F;
    }
    bus_wram_write16(0x0C28, a);
    CPU_SET_A16(a);
    call_jsr(0x00F921);
    bus_wram_write16(0x0E96, bus_wram_read16(0x04DC));
    bus_wram_write16(0x04DE, bus_wram_read16(0x0E9A));
}

/* ---- The music editor's song position bar ($09E0, 0..$B5F). ---- */

/* $F331: the song position moved by A, kept within 0..$B5F (by the sign of
 * the sum, as the ROM's BPL/BMI). */
static void song_pos_by(uint16_t a) {
    a = (uint16_t)(a + bus_wram_read16(0x09E0));
    if (a & 0x8000) a = 0;
    if (!((uint16_t)(a - 0x0B5F) & 0x8000)) a = 0x0B5F;
    bus_wram_write16(0x09E0, a);
    CPU_SET_A16(a);
}

/* $00:F289: the song position bar (y $A0-$A7): its arrows (x $B8-$BE and
 * $F1-$F7) move the song 8 times by 4, redrawn ($ED76) and faded
 * ($0446 = 4, $01:E09B) a frame each but the last; on the bar a click left
 * or right of the thumb ((pos / 64) + $BF, 5 wide) pages by $E0, on the
 * thumb the drag starts ($0E94 / $0E98). The step and count stay on the
 * stack across the frames, as in the ROM. */
static void mp_00F289(void) {
    call_jsl(0x01E2F3);
    bus_wram_write16(0x0E96, 0);
    if (!(bus_wram_read16(0x04CA) & 0x0010)) return;
    uint16_t x = bus_wram_read16(0x04DC);
    if (x < 0x00B8) return;
    if (x < 0x00BF || (x >= 0x00F1 && x < 0x00F8)) {
        uint16_t step = x >= 0x00C0 ? 0x0004 : 0xFFFC;
        for (uint16_t y = 8; y != 0 && !g_quit; ) {
            uint16_t sp = g_cpu.S;
            bus_wram_write16((uint16_t)(sp - 1), step);
            bus_wram_write16((uint16_t)(sp - 3), y);
            g_cpu.S = (uint16_t)(sp - 4);
            song_pos_by(step);
            if (y != 1) {
                bus_wram_write16(0x0446, 0x0004);
                call_jsr(0x00ED76);
                call_jsl(0x01E09B);
                call_jsl(0x01E2CE);
            }
            g_cpu.S = sp;
            y = (uint16_t)(bus_wram_read16((uint16_t)(sp - 3)) - 1);
            step = bus_wram_read16((uint16_t)(sp - 1));
            g_cpu.Y = y;
        }
        bus_wram_write16(0x0446, 0x0004);
        CPU_SET_A16(0x0004);
        return;
    }
    if (x >= 0x00F8) return;
    if (!(bus_wram_read16(0x04CA) & 0x0040)) return;
    uint16_t t = (uint16_t)((bus_wram_read16(0x09E0) >> 6) + 0x00BF);
    if (t >= x) { song_pos_by(0xFF20); return; }
    if ((uint16_t)(t + 5) < x) { song_pos_by(0x00E0); return; }
    bus_wram_write16(0x0E94, x);
    bus_wram_write16(0x0E98, bus_wram_read16(0x04DE));
}

/* $00:F346: dragging the song position bar's thumb: the cursor's move
 * since the last frame ($0E94) times 64 added to the position; past either
 * end the cursor is pulled back by the overshoot / 64; the cursor's x kept
 * as the drag's origin and its y held at $0E98. */
static void mp_00F346(void) {
    uint16_t cx = bus_wram_read16(0x04DC);
    uint16_t a = (uint16_t)(-(uint16_t)((uint16_t)(bus_wram_read16(0x0E94) - cx) << 6));
    a = (uint16_t)(a + bus_wram_read16(0x09E0));
    if (a & 0x8000) {
        a = (uint16_t)((uint16_t)(-a) >> 6);
        bus_wram_write16(0x04DC, (uint16_t)(a + bus_wram_read16(0x04DC)));
        a = 0;
    }
    if (!((uint16_t)(a - 0x0B5F) & 0x8000)) {
        a = (uint16_t)(-(uint16_t)((uint16_t)(a - 0x0B5F) >> 6));
        bus_wram_write16(0x04DC, (uint16_t)(a + bus_wram_read16(0x04DC)));
        a = 0x0B5F;
    }
    bus_wram_write16(0x09E0, a);
    bus_wram_write16(0x0E94, bus_wram_read16(0x04DC));
    bus_wram_write16(0x04DE, bus_wram_read16(0x0E98));
}

/* $00:EC88: state $1E, the music editor. $01:E30E; when the PPU flags a
 * sprite overflow ($213E bits 6-7) the sprite priority rotates ($0561,
 * $2102 from $00:ED6E); the cursor shape ($04D0: $0D, or the note $0E8C
 * over the staff when no bar is dragged). Without the button the drags
 * end; with it, by place: a bar's drag ($F346 / $F8CC), the top row
 * ($F054), the bottom bar ($8E0B, then $95E7 with its slot and tool in DP
 * $A4/$AC), the staff ($F0A4), the song position bar ($F289), and in the
 * row y $A8-$BB the buttons ($F3DE, $F7D4, the scroll bar $F827, $FAB5).
 * Then the view ($ED76) and $01:E2F3. */
static void mp_00EC88(void) {
    call_jsl(0x01E30E);
    op_sep(0x20);
    uint8_t st = bus_read8(0x00, 0x213E);
    if (st & 0xC0) {
        uint8_t n = (uint8_t)(bus_wram_read8(0x0561) + 1);
        bus_wram_write8(0x0561, n);
        g_cpu.X = (uint16_t)(n & 0x07);
        bus_write8(0x00, 0x2103, 0x80);
        bus_write8(0x00, 0x2102, bus_read8(g_cpu.DB, (uint16_t)(0xED6E + (n & 0x07))));
    }
    op_rep(0x20);
    bus_wram_write16(0x04D0, 0x000D);
    if ((bus_wram_read16(0x0E94) | bus_wram_read16(0x0E96)) == 0 && bus_wram_read16(0x04DE) < 0x0098)
        bus_wram_write16(0x04D0, bus_wram_read16(0x0E8C));
    bool end_drags = false;
    if (!(bus_wram_read16(0x04CA) & 0x0011)) {
        end_drags = true;
    } else if (bus_wram_read16(0x0E94)) {
        call_jsr(0x00F346);
    } else if (bus_wram_read16(0x0E96)) {
        call_jsr(0x00F8CC);
    } else {
        uint16_t y = bus_wram_read16(0x04DE);
        if (y < 0x0018) call_jsr(0x00F054);
        else if (y >= 0x00C8) {
            call_jsr(0x008E0B);
            bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xA4), g_cpu.X);
            bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xAC), CPU_A16());
            uint16_t sp = g_cpu.S;
            call_jsr(0x0095E7);
            /* Leaving the editor ($EF6E) pulls the return address of this
             * JSR (PLA), so its RTS returns from $EC88 itself, to the
             * editor's loop: the rest of $EC88 is skipped. Here that JSR's
             * frame was never on the stack, so S is 2 too high: undo the
             * PLA and return. */
            if (g_cpu.S != sp) { g_cpu.S = (uint16_t)(g_cpu.S - 2); return; }
            end_drags = true;
        } else if (y < 0x002C) {
        } else if (y < 0x0098) call_jsr(0x00F0A4);
        else if (y < 0x00A0) {
        } else if (y < 0x00A8) call_jsr(0x00F289);
        else if (y < 0x00BC) {
            uint16_t x = bus_wram_read16(0x04DC);
            if (x < 0x0030) {
            } else if (x < 0x0048) call_jsr(0x00F3DE);
            else if (x < 0x0050) {
            } else if (x < 0x0068) call_jsr(0x00F7D4);
            else if (x < 0x00A8) call_jsr(0x00F827);
            else if (x >= 0x00C8 && x < 0x00E8) call_jsr(0x00FAB5);
        }
    }
    if (end_drags) {
        bus_wram_write16(0x0E94, 0);
        bus_wram_write16(0x0E96, 0);
    }
    call_jsr(0x00ED76);
    call_jsl(0x01E2F3);
}

/* $00:E5F4: leave state $10 by its exit button: sound $41, a fade
 * ($01:E06F), the tool back ($1010 into $04D0, $9378), the canvas
 * ($01:CDE1 with $CE4B, $C414 or $C3DE with $19FA), uploaded and a frame,
 * the previous state ($09D8), $A451, $A525, tool switch 7 ($9FC4 with DP
 * $AA = 7 and 7, 0 on the stack), $09AB counted for the stamp tool, and
 * on at $A363. */
static void mp_00E5F4(void) {
    uint8_t db = g_cpu.DB;
    CPU_SET_A16(0x0041); call_jsl(0x01D368);
    call_jsl(0x01E06F);
    bus_write16(db, 0x04D0, bus_read16(db, 0x1010));
    call_jsr(0x009378);
    CPU_SET_A16(0xCE4B); call_jsl(0x01CDE1);
    if (bus_read16(db, 0x19FA) == 0) call_jsr(0x00C414); else call_jsr(0x00C3DE);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    bus_write16(db, 0x09D6, bus_read16(db, 0x09D8));
    call_jsr(0x00A451);
    call_jsr(0x00A525);
    bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xAA), 0x0007);
    uint16_t sp = g_cpu.S;
    bus_wram_write16((uint16_t)(sp - 1), 0x0007);
    bus_wram_write16((uint16_t)(sp - 3), 0x0000);
    g_cpu.S = (uint16_t)(sp - 4);
    CPU_SET_A16(0x0000);
    call_jsr(0x009FC4);
    if (bus_read16(db, 0x04D0) == 0x0006) bus_write16(db, 0x09AB, bus_read16(db, 0x09AB) + 1);
    tail_jmp(0x00A363);
}

/* $00:E4B9: state $10: the objects $4C..$3F drawn ($01:F84F); with a row
 * picked ($1012) four marks at $1A3A slide by $1014, which turns at $9900
 * and $3000; a click on a row (x $58-$CF, y $3C-$BB: rows 1, 2, 3, 0 by 32)
 * other than the current one picks it (sound $01:D3AB, the marks at $1A15
 * reset, the new row's two marks off, the row-dependent ones), or on the
 * exit button (y from $CC, x $08-$16: $E5F4). */
static void mp_00E4B9(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    uint16_t a = 0x004C;
    do {
        CPU_SET_A16(a);
        call_jsl(0x01F84F);
        a = (uint16_t)(a - 1);
    } while (!((uint16_t)(a - 0x003F) & 0x8000));
    if (R16(0x1012) != 0) {
        for (int x = 0x18; x >= 0; x -= 8) W16(0x1A3A + x, R16(0x1A3A + x) + R16(0x1014));
        if (R16(0x1A3A) == 0x9900) W16(0x1014, 0xFFE0);
        if (R16(0x1A3A) == 0x3000) W16(0x1014, 0x0020);
    }
    if (!(R16(0x04CA) & 0x0020)) return;
    uint16_t x = R16(0x04DC), y = R16(0x04DE);
    uint16_t row = 0xFFFF;
    if (x >= 0x0058 && x < 0x00D0) {
        if (y < 0x003C) return;
        if (y < 0x005C) row = 1;
        else if (y < 0x007C) row = 2;
        else if (y < 0x009C) row = 3;
        else if (y < 0x00BC) row = 0;
    }
    if (row == 0xFFFF) {
        if (y < 0x00CC || x < 0x0008 || x >= 0x0017) return;
        mp_00E5F4();
        return;
    }
    if (row == R16(0x1012)) return;
    W16(0x1012, row);
    call_jsl(0x01D3AB);
    W16(0x1B0D, 0x07FF); W16(0x1A15, 0x07FF); W16(0x1A1D, 0x07FF); W16(0x1A25, 0x07FF);
    W16(0x1A2D, 0x03FF); W16(0x1A35, 0x03FF);
    W16(0x1A3D, 0x04FF);
    W16(0x1A45, 0xFFFF); W16(0x1A4D, 0xFFFF); W16(0x1A55, 0xFFFF);
    W16(0x1A5D, 0x21FF); W16(0x1A65, 0x24FF); W16(0x1A6D, 0x27FF);
    W16(0x1A75, 0x04FF);
    uint16_t o = (uint16_t)(((uint16_t)(R16(0x1012) + 0x003F) << 3) & 0x00FF);
    W16(0x1A15 + o, 0xFFFF);
    o = (uint16_t)(bus_read8(db, (uint16_t)(0xE4B5 + R16(0x1012))));
    W16(0x1A15 + o, 0xFFFF);
    if (o == 0x0048) {
        W16(0x1A65, 0x00FF); W16(0x1A6D, 0x00FF);
        W16(0x1A66, 0x000A); W16(0x1A6E, 0x0014);
    }
    g_cpu.X = o;
    if (R16(0x1012) != 0) {
        W16(0x1A45, 0x02FF); W16(0x1A4D, 0x02FF); W16(0x1A55, 0x02FF);
        W16(0x1A3D, 0xFFFF);
    }
#undef R16
#undef W16
}

/* $00:E19B: the colour of a cell of the 16x16 pattern grid (Y: bits
 * 0-2 the pixel, 3 the half, 4-6 the row, 7 the second tile): its four
 * bitplane bits from $102C/$101C (masked by $00:E203) into $1134, and
 * the colour from $00:E213. X and Y kept. */
static void mp_00E19B(void) {
    uint8_t db = g_cpu.DB;
    uint16_t y = g_cpu.Y;
    uint16_t x = (uint16_t)(((y & 0x0080) >> 1) + ((y & 0x0070) >> 3) + ((y & 0x0008) << 2));
    uint16_t m = bus_read16(db, (uint16_t)(0xE203 + ((y & 0x0007) << 1)));
    uint16_t v = 0;
    uint16_t a = (uint16_t)(bus_read16(db, (uint16_t)(0x102C + x)) & m);
    if (a & 0xFF00) v |= 1;
    v <<= 1;
    if (a & 0x00FF) v |= 1;
    v <<= 1;
    a = (uint16_t)(bus_read16(db, (uint16_t)(0x101C + x)) & m);
    if (a & 0xFF00) v |= 1;
    v <<= 1;
    if (a & 0x00FF) v |= 1;
    bus_write16(db, 0x1134, v);
    CPU_SET_A16(bus_read16(db, (uint16_t)(0xE213 + (v << 1))));
}

/* $00:E335: in state $15, a click on the pattern grid (x and y $30-$AF)
 * picks that cell's colour ($E19B; $1124, the cursor $04D0 = colour << 8
 * | $0B), the cursor colours ($E2CC), $E64F and sound 4. */
static void mp_00E335(void) {
    uint8_t db = g_cpu.DB;
    uint16_t x = bus_read16(db, 0x04DC), y = bus_read16(db, 0x04DE);
    if (x < 0x0030 || x >= 0x00B0 || y < 0x0030 || y >= 0x00B0) return;
    uint16_t row = (uint16_t)(((uint16_t)(y - 0x0030) & 0xFFF8) << 1);
    g_cpu.Y = (uint16_t)(((uint16_t)(x - 0x0030) >> 3) + row);
    CPU_SET_A16(row);
    mp_00E19B();
    uint16_t c = (uint16_t)(CPU_A16() & 0x000F);
    bus_write16(db, 0x1124, c);
    bus_write16(db, 0x04D0, (uint16_t)((c << 8) | 0x000B));
    call_jsr(0x00E2CC);
    call_jsr(0x00E64F);
    CPU_SET_A16(0x0004);
    call_jsl(0x01D368);
}

/* $00:CDCC: in state $02, the size buttons (right or left click, $04CA
 * bits $40/$04): the size in DP $A6 steps by $10 up or down (skipping
 * $60, kept to $F0 bits), its sound ($62 + size/16, one less from 6), the
 * tool redrawn with it ($B66C below $70, else $B6F4, with DP $A8 | $A6),
 * $A363; then it drops its caller's return address (PLA), so its RTS
 * leaves $CC10 too. */
static void mp_00CDCC(void) {
    uint16_t y = 0x0010;
    uint16_t a = bus_wram_read16(0x04CA);
    g_cpu.Y = y;
    if (!(a & 0x0044)) { CPU_SET_A16((uint16_t)(a & 0x0044)); return; }
    if (!(a & 0x0040)) y = 0xFFF0;
    uint16_t dp = g_cpu.DP;
    uint16_t t = (uint16_t)(bus_read16(0x00, (uint16_t)(dp + 0xA6)) + y);
    if (t == 0x0060) t = (uint16_t)(t + y);
    t &= 0x00F0;
    bus_write16(0x00, (uint16_t)(dp + 0xA6), t);
    uint16_t n = (uint16_t)(t >> 4);
    if (n >= 0x0006) n--;
    CPU_SET_A16((uint16_t)(n + 0x0062));
    call_jsl(0x01D368);
    g_cpu.Y = y;
    uint16_t v = (uint16_t)(bus_read16(0x00, (uint16_t)(dp + 0xA8)) | bus_read16(0x00, (uint16_t)(dp + 0xA6)));
    CPU_SET_A16(v);
    if (bus_read16(0x00, (uint16_t)(dp + 0xA6)) < 0x0070) call_jsr(0x00B66C); else call_jsr(0x00B6F4);
    call_jsr(0x00A363);
    CPU_SET_A16(bus_wram_read16((uint16_t)(g_cpu.S + 1)));           /* PLA: the caller's return */
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
}

/* $00:CB2E: state $02's exit button (on a click): sound $41, back to the
 * tool from before (tool switch $9FC4: 7 without a stamp set ($19FA),
 * 4 with one, and for the stamp tool 4 with $19C0 and a flag), the
 * previous state ($09D8), the size ($1B14 into DP $A6) and tool ($1990
 * into $04D0) back and redrawn as each tool shows them; on at $A363. */
static void mp_00CB2E(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define D16(o)   bus_read16(0x00, (uint16_t)(dp + (o)))
#define DW(o,v)  bus_write16(0x00, (uint16_t)(dp + (o)), (uint16_t)(v))
    if (!(R16(0x04CA) & 0x0020)) return;
    CPU_SET_A16(0x0041);
    call_jsl(0x01D368);
    uint16_t sp = g_cpu.S;
    if (R16(0x19FA) != 0 && (R16(0x1990) & 0x00FF) == 0x0006) {
        bus_wram_write16((uint16_t)(sp - 1), R16(0x19C0));
        DW(0xAA, 0x0004);
        bus_wram_write16((uint16_t)(sp - 3), 0x0004);
        bus_wram_write16((uint16_t)(sp - 5), 0x0001);
        g_cpu.S = (uint16_t)(sp - 6);
        CPU_SET_A16(0x0001);
    } else {
        uint16_t t = R16(0x19FA) != 0 ? 0x0004 : 0x0007;
        DW(0xAA, t);
        bus_wram_write16((uint16_t)(sp - 1), t);
        bus_wram_write16((uint16_t)(sp - 3), 0x0000);
        g_cpu.S = (uint16_t)(sp - 4);
        CPU_SET_A16(0x0000);
    }
    call_jsr(0x009FC4);
    W16(0x09D6, R16(0x09D8));
    DW(0xA6, R16(0x1B14));
    W16(0x04D0, R16(0x1990));
    uint16_t c = (uint16_t)(R16(0x1990) & 0x00FF);
    if (c == 0x0007) {
        W16(0x1992, 0x0001);
        CPU_SET_A16(0x000F); call_jsr(0x00B66C);
        CPU_SET_A16((uint16_t)(R16(0x1994) + 0x0008)); call_jsr(0x00B6F4);
        W16(0x04D0, 0x0007);
        call_jsr(0x00A525);
        DW(0xAE, 0x0000);
        if (R16(0x04D0) == 0x0006) W16(0x09AB, R16(0x09AB) + 1);
        tail_jmp(0x00A363);
        return;
    }
    if (c == 0x0003 && D16(0xA6) >= 0x0070) {
        CPU_SET_A16((uint16_t)((R16(0x04D0) >> 8) | D16(0xA6))); call_jsr(0x00B6F4);
        CPU_SET_A16(0x000D); call_jsr(0x00B66C);
        call_jsr(0x00A363);
        tail_jmp(0x00A451);
        return;
    }
    CPU_SET_A16((uint16_t)((R16(0x04D0) >> 8) | D16(0xA6))); call_jsr(0x00B66C);
    uint16_t lo = (uint16_t)(R16(0x04D0) & 0x00FF);
    CPU_SET_A16(lo == 0x0008 ? R16(0x198A) : lo);
    call_jsr(0x00B6F4);
    if (R16(0x04D0) == 0x0006) W16(0x09AB, R16(0x09AB) + 1);
    tail_jmp(0x00A363);
#undef R16
#undef W16
#undef D16
#undef DW
}

/* $00:CCE1: the colour a pixel word becomes when the stamp set's colour
 * table is applied (the set $19C2 + 1, or 0 without one ($19FA); its
 * table at $00:CD3C: a list of [low, high) ranges for the word's bits
 * $FC0F and of colour entries for its bits $03E0). No range: carry set.
 * A range but no entry: $FFFF, carry clear. Else the entry's colour,
 * carry clear. X and Y kept. */
static void mp_00CCE1(void) {
    uint8_t db = g_cpu.DB;
    uint16_t px = g_cpu.X, py = g_cpu.Y;
    uint16_t k = bus_read16(db, 0x19FA) ? (uint16_t)(bus_read16(db, 0x19C2) + 1) : 0;
    uint16_t x = (uint16_t)(k * 6);
    uint16_t ptr = bus_read16(db, (uint16_t)(0xCD3C + x));
    uint16_t y = bus_read16(db, (uint16_t)(0xCD3E + x));
    uint16_t v = (uint16_t)(py & 0xFC0F);
    bool found = false;
    for (;;) {
        bool ge = v >= bus_read16(db, (uint16_t)(ptr + y));
        y = (uint16_t)(y - 2);
        if (!ge && v >= bus_read16(db, (uint16_t)(ptr + y))) { found = true; break; }
        y = (uint16_t)(y - 2);
        if (y & 0x8000) break;
    }
    g_cpu.X = px; g_cpu.Y = py;
    if (!found) { g_cpu.flag_C = true; return; }
    uint16_t w = (uint16_t)(py & 0x03E0);
    y = bus_read16(db, (uint16_t)(0xCD40 + x));
    for (;;) {
        if (w == bus_read16(db, (uint16_t)(ptr + y))) {
            CPU_SET_A16(bus_read16(db, (uint16_t)(ptr + y + 2)));
            g_cpu.flag_C = false;
            return;
        }
        y = (uint16_t)(y - 4);
        if ((uint16_t)(y - bus_read16(db, (uint16_t)(0xCD3E + x))) & 0x8000) break;
    }
    CPU_SET_A16(0xFFFF);
    g_cpu.flag_C = false;
}

/* $00:CC5B: state $02's apply button (A = DP $A8 from $8D2C, on a click):
 * sound 4, $8B03, the tool redrawn, then every pixel word of the canvas
 * copy ($5FFE down to 0, X cycling $3E..0) mapped through $CCE1: a found
 * colour masks the word from the tool's table (at $1CAA or $1C2A, row
 * from bit $400 of Y) into the long pointer DP $0A; sound $72, the upload
 * ($0206/$0208) waited out frame by frame. */
static void mp_00CC5B(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    bus_write16(0x00, (uint16_t)(dp + 0xA8), CPU_A16());
    if (!(bus_read16(db, 0x04CA) & 0x0020)) return;
    CPU_SET_A16(0x0004); call_jsl(0x01D368);
    call_jsr(0x008B03);
    bus_write16(db, 0x09A7, bus_read16(db, 0x09A7) + 1);
    uint16_t a6 = bus_read16(0x00, (uint16_t)(dp + 0xA6));
    uint16_t v = (uint16_t)(bus_read16(0x00, (uint16_t)(dp + 0xA8)) | a6);
    CPU_SET_A16(v);
    uint16_t base;
    if (a6 < 0x0070) { call_jsr(0x00B66C); call_jsr(0x00E64F); base = 0x1CAA; }
    else             { call_jsr(0x00B6F4); call_jsr(0x00E64F); base = 0x1C2A; }
    uint16_t sp = g_cpu.S;
    bus_wram_write16((uint16_t)(sp - 1), base);
    g_cpu.S = (uint16_t)(sp - 2);
    bus_write16(db, 0x0206, 0);
    uint32_t lp = bus_read16(0x00, (uint16_t)(dp + 0x0A)) | ((uint32_t)bus_read8(0x00, (uint16_t)(dp + 0x0C)) << 16);
    uint16_t y = 0x5FFE, x = 0x003E;
    for (;;) {
        g_cpu.X = x; g_cpu.Y = y;
        mp_00CCE1();
        if (!g_cpu.flag_C) {
            uint16_t r = CPU_A16();
            uint16_t yy = (uint16_t)(((y & 0x0400) >> 4) | x);
            uint16_t val = (uint16_t)(bus_read16(db, (uint16_t)(base + yy)) & r);
            uint32_t t = lp + y;
            bus_write16((uint8_t)(t >> 16), (uint16_t)t, val);
            CPU_SET_A16(r);
        }
        x = (uint16_t)((x - 2) & 0x003F);
        y = (uint16_t)(y - 2);
        if (y & 0x8000) break;
    }
    g_cpu.X = x; g_cpu.Y = y;
    g_cpu.S = sp;
    CPU_SET_A16(0x0072); call_jsl(0x01D368);
    bus_write16(db, 0x0208, 0);
    bus_write16(db, 0x0206, bus_read16(db, 0x0206) + 1);
    do { call_jsl(0x01E2CE); } while (bus_read16(db, 0x0208) != 0 && !g_quit);
    bus_write16(db, 0x09A7, 0);
}

/* $00:CC10: state $02: on a press, the top row's buttons ($8D2C: 1 the
 * apply button $CC5B, 2 the size buttons $CDCC), and on a click in the
 * bottom bar its exit ($CB2E) or the button at x $D8-$E6 ($9760). */
static void mp_00CC10(void) {
    uint8_t db = g_cpu.DB;
    if (!(bus_read16(db, 0x04CA) & 0x0011)) return;
    uint16_t y = bus_read16(db, 0x04DE);
    if (y < 0x0018) {
        call_jsr(0x008D2C);
        if (g_cpu.X == 0x0001) { mp_00CC5B(); return; }
        if (g_cpu.X == 0x0002) {
            uint16_t sp = g_cpu.S;
            call_jsr(0x00CDCC);
            /* $CDCC pulls this JSR's return address (PLA) so its RTS
             * leaves $CC10; here that frame was never on the stack. */
            if (g_cpu.S != sp) g_cpu.S = (uint16_t)(g_cpu.S - 2);
        }
        return;
    }
    if (y < 0x00C8 || !(bus_read16(db, 0x04CA) & 0x0020)) return;
    uint16_t x = bus_read16(db, 0x04DC);
    if (x < 0x0008) return;
    if (x < 0x0017) { mp_00CB2E(); return; }
    if (x < 0x00D8 || x >= 0x00E7) return;
    tail_jmp(0x009760);
}

void mp_register_misc(void) {
    func_table_register(0x00CC10, mp_00CC10);
    func_table_register(0x00CB2E, mp_00CB2E);
    func_table_register(0x00CCE1, mp_00CCE1);
    func_table_register(0x00CC5B, mp_00CC5B);
    func_table_register(0x00CDCC, mp_00CDCC);
    func_table_register(0x00E19B, mp_00E19B);
    func_table_register(0x00E335, mp_00E335);
    func_table_register(0x00E4B9, mp_00E4B9);
    func_table_register(0x00E5F4, mp_00E5F4);
    func_table_register(0x00EC88, mp_00EC88);
    func_table_register(0x00F289, mp_00F289);
    func_table_register(0x00F346, mp_00F346);
    func_table_register(0x00F827, mp_00F827);
    func_table_register(0x00F8CC, mp_00F8CC);
    func_table_register(0x00CF9B, mp_00CF9B);
    func_table_register(0x00CF50, mp_00CF50);
    func_table_register(0x00C900, mp_00C900);
    func_table_register(0x00C93C, mp_00C93C);
    func_table_register(0x00C94B, mp_00C94B);
    func_table_register(0x00D505, mp_00D505);
    func_table_register(0x00D03E, mp_00D03E);
    func_table_register(0x00D2F6, mp_00D2F6);
    func_table_register(0x00BBD8, mp_00BBD8);
    func_table_register(0x00C7DB, mp_00C7DB);
    func_table_register(0x00B98C, mp_00B98C);
    func_table_register(0x00B988, mp_00B988);
    func_table_register(0x00C7AF, mp_00C7AF);
    func_table_register(0x00B610, mp_00B610);
    func_table_register(0x00B637, mp_00B637);
    func_table_register(0x0FC118, mp_0FC118);
    func_table_register(0x01DB25, mp_01DB25);
    func_table_register(0x00E2A3, mp_00E2A3);
    func_table_register(0x00DA49, mp_00DA49);
    func_table_register(0x009F87, mp_009F87);
    func_table_register(0x00EF6E, mp_00EF6E);


    func_table_register(0x01D39F, mp_01D39F);

    func_table_register(0x01D287, mp_01D287);
    func_table_register(0x01D259, mp_01D259);
    func_table_register(0x01D279, mp_01D279);
    func_table_register(0x01D22B, mp_01D22B);
    func_table_register(0x01D242, mp_01D242);
    func_table_register(0x01D08D, mp_01D08D);
    func_table_register(0x01D0AD, mp_01D0AD);
    func_table_register(0x01D13B, mp_01D13B);
    func_table_register(0x01D1A1, mp_01D1A1);
    func_table_register(0x01D1E6, mp_01D1E6);
    func_table_register(0x01CFD5, mp_01CFD5);
    func_table_register(0x00A617, mp_00A617);

    func_table_register(0x008AC2, mp_008AC2);

    func_table_register(0x008A8E, mp_008A8E);
    func_table_register(0x008A8A, mp_008A8A);

    func_table_register(0x00A6F7, mp_00A6F7);
    func_table_register(0x00A792, mp_00A792);
    func_table_register(0x00A8A8, mp_00A8A8);
    func_table_register(0x00A8FF, mp_00A8FF);
    func_table_register(0x00A95D, mp_00A95D);
    func_table_register(0x00A9BE, mp_00A9BE);

    func_table_register(0x00A509, mp_00A509);
    func_table_register(0x00A55B, mp_00A55B);
    func_table_register(0x00A637, mp_00A637);

    func_table_register(0x00F9A7, mp_00F9A7);
    func_table_register(0x00FA08, mp_00FA08);
    func_table_register(0x00FEF9, mp_00FEF9);
    func_table_register(0x00FA65, mp_00FA65);

    func_table_register(0x00E9A1, mp_00E9A1);
    func_table_register(0x00EA12, mp_00EA12);
    func_table_register(0x00E848, mp_00E848);
    func_table_register(0x00EA69, mp_00EA69);
    func_table_register(0x00EAA4, mp_00EAA4);
    func_table_register(0x00969C, mp_00969C);

    func_table_register(0x00EB7A, mp_00EB7A);
    func_table_register(0x00E3F7, mp_00E3F7);

    func_table_register(0x00E2CC, mp_00E2CC);
    func_table_register(0x00DE46, mp_00DE46);
    func_table_register(0x00D869, mp_00D869);
    func_table_register(0x00CE22, mp_00CE22);
    func_table_register(0x00C9E3, mp_00C9E3);
    func_table_register(0x009734, mp_009734);
    func_table_register(0x0096A6, mp_0096A6);

    func_table_register(0x01EBB5, mp_01EBB5);

    func_table_register(0x01F03A, mp_01F03A);

    func_table_register(0x01EC4B, mp_01EC4B);

    func_table_register(0x00B780, mp_00B780);
    func_table_register(0x01EF36, mp_01EF36);
    func_table_register(0x01F21D, mp_01F21D);

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

    func_table_register(0x009760, mp_009760);
    func_table_register(0x00E233, mp_00E233);
    /* $00:E6DA is no routine (a point inside $E64F, which has its C version). */
    func_table_register(0x0082A7, mp_0082A7);
    func_table_register(0x01904A, mp_01904A);
    func_table_register(0x01934F, mp_01934F);
}
