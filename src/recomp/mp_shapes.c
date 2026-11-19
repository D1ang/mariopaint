/*
 * Mario Paint — Shape drawing routines and remaining sub-calls.
 *
 * Line, rectangle, and ellipse drawing, plus pixel-level
 * plotting sub-routines and other remaining gap-fillers.
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
 * $00:AAFB — Line drawing (from point to point)
 *
 * Draws a line from ($22,$24) to ($26,$28) using Bresenham's
 * algorithm. Sets up shake animation on first click.
 * ======================================================================== */
void mp_00AAFB(void) {
    uint8_t draw = bus_wram_read8(0x0020);
    if (draw == 0) {
        /* First click — set up shake effect */
        op_sep(0x20);
        bus_wram_write8(0x1B21, 0x00);
        bus_wram_write8(0x1B1F, 0xFF);
        bus_wram_write8(0x1B20, 0xFF);
        op_rep(0x20);
    }

    /* Set up line endpoints for the line algorithm */
    bus_wram_write16(0x003E, bus_wram_read16(0x0022));
    bus_wram_write16(0x0040, bus_wram_read16(0x0024));
    bus_wram_write16(0x0042, bus_wram_read16(0x0026));
    bus_wram_write16(0x0044, bus_wram_read16(0x0028));

    /* Call the Bresenham line drawing at $00AF5A */
    func_table_call(0x00AF5A);

    bus_wram_write16(0x1B1F, 0x0000);
}

/* $00:AB26: the rectangle tool: the four edges of ($22,$24)-($26,$28)
 * by $AF5A; $1B1F/$1B20 $FF while drawing ($1B21 = 0 on the first press). */
void mp_00AB26(void) {
    uint8_t db = g_cpu.DB;
#define RD(o)     bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define RDW(o, v) bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
    if (RD(0x20) == 0) {
        bus_write8(db, 0x1B21, 0x00);
        bus_write8(db, 0x1B1F, 0xFF);
        bus_write8(db, 0x1B20, 0xFF);
    }
    static const uint8_t edge[4][4] = {
        { 0x22, 0x24, 0x26, 0x24 }, { 0x26, 0x24, 0x26, 0x28 },
        { 0x26, 0x28, 0x22, 0x28 }, { 0x22, 0x28, 0x22, 0x24 },
    };
    for (int i = 0; i < 4; i++) {
        RDW(0x3E, RD(edge[i][0])); RDW(0x40, RD(edge[i][1]));
        RDW(0x42, RD(edge[i][2])); RDW(0x44, RD(edge[i][3]));
        call_jsr(0x00AF5A);
    }
    bus_write16(db, 0x1B1F, 0x0000);
#undef RD
#undef RDW
}

/* ---- The ellipse tool ------------------------------------------------ */
#define ED(o)      bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define EDW(o, v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
#define ED8(o)     bus_read8(0x00, (uint16_t)(g_cpu.DP + (o)))
#define EDW8(o, v) bus_write8(0x00, (uint16_t)(g_cpu.DP + (o)), (uint8_t)(v))
#define ENEG(a, b) ((((uint16_t)((a) - (b))) & 0x8000) != 0)

/* $00:ACDC: the eight mirrored points of one step (offsets $64/$66 and
 * $6A/$68 around the centre $52/$54), each plotted by $B051 when inside
 * the canvas margins $19B6..$19BC set here; then the midpoint step:
 * error $5E, counter $5C up, radius $5A down when the error goes below 0. */
void mp_00ACDC(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    W16(0x19B6, R16(0x19AE) - 4);
    W16(0x19B8, R16(0x19B0) + 12);
    W16(0x19BA, R16(0x19B2) - 8);
    W16(0x19BC, R16(0x19B4) + 8);
    const struct { uint8_t dx, dy; int sx, sy; } pt[8] = {
        { 0x64, 0x66, +1, +1 }, { 0x64, 0x66, +1, -1 }, { 0x64, 0x66, -1, +1 }, { 0x64, 0x66, -1, -1 },
        { 0x6A, 0x68, +1, +1 }, { 0x6A, 0x68, +1, -1 }, { 0x6A, 0x68, -1, +1 }, { 0x6A, 0x68, -1, -1 },
    };
    for (int i = 0; i < 8; i++) {
        uint16_t x = (uint16_t)(pt[i].sx > 0 ? ED(0x52) + ED(pt[i].dx) : ED(0x52) - ED(pt[i].dx));
        if (pt[i].sx > 0 ? !ENEG(x, R16(0x19B8)) : ENEG(x, R16(0x19B6))) continue;
        EDW(0x86, x);
        uint16_t y = (uint16_t)(pt[i].sy > 0 ? ED(0x54) + ED(pt[i].dy) : ED(0x54) - ED(pt[i].dy));
        if (pt[i].sy > 0 ? !ENEG(y, R16(0x19BC)) : ENEG(y, R16(0x19BA))) continue;
        EDW(0x88, y);
        call_jsr(0x00B051);
    }
    uint16_t e = (uint16_t)(ED(0x5E) - ED(0x5C) - ED(0x5C) - 1);
    EDW(0x5E, e);
    EDW(0x5C, ED(0x5C) + 1);
    if (e & 0x8000) {
        EDW(0x5A, ED(0x5A) - 1);
        EDW(0x5E, (uint16_t)(ED(0x5E) + ED(0x5A) + ED(0x5A)));
    }
    CPU_SET_A16(ED(0x5E));
#undef R16
#undef W16
}

/* (a x b) / c on the hardware multiplier and divider, 8-bit operands, as
 * the ROM does it (the quotient's two bytes into DP o, o+1). */
static void ell_muldiv(uint8_t a, uint8_t b, uint8_t c, uint8_t o) {
    uint8_t db = g_cpu.DB;
    bus_write8(db, 0x4202, a);
    bus_write8(db, 0x4203, b);
    bus_write8(db, 0x4204, bus_read8(db, 0x4216));
    bus_write8(db, 0x4205, bus_read8(db, 0x4217));
    bus_write8(db, 0x4206, c);
    EDW8(o, bus_read8(db, 0x4214));
    EDW8(o + 1, bus_read8(db, 0x4215));
}

/* $00:AB8A: the ellipse tool: corners $22/$24 and $26/$28 give the centre
 * $52/$54 and the half axes $56/$58; the longer axis is stepped by the
 * midpoint method ($ACDC), the other scaled by the axis ratio. $1B1F/$1B20
 * are $FF while it draws ($1B21 = 0 on the first press). */
void mp_00AB8A(void) {
    uint8_t db = g_cpu.DB;
    if (ED(0x20) == 0) {
        bus_write8(db, 0x1B21, 0x00);
        bus_write8(db, 0x1B1F, 0xFF);
        bus_write8(db, 0x1B20, 0xFF);
    }
    EDW(0x52, ED(0x22));
    EDW(0x54, ED(0x24));
    uint16_t d = (uint16_t)(ED(0x26) - ED(0x52));
    if (d & 0x8000) d = (uint16_t)-d;
    EDW(0x56, d);
    d = (uint16_t)(ED(0x28) - ED(0x54));
    if (d & 0x8000) d = (uint16_t)-d;
    EDW(0x58, d);
    if ((ED(0x56) | ED(0x58)) != 0) {
        bool wide = ED(0x56) >= ED(0x58);
        uint16_t r = wide ? ED(0x56) : ED(0x58);
        EDW(0x5E, r); EDW(0x5A, r); EDW(0x5C, 0);
        while (ED(0x5A) >= ED(0x5C)) {
            if (wide) {
                ell_muldiv(ED8(0x5A), ED8(0x58), ED8(0x56), 0x68);
                ell_muldiv(ED8(0x5C), ED8(0x58), ED8(0x56), 0x66);
                EDW(0x64, ED(0x5A)); EDW(0x6A, ED(0x5C));
            } else {
                ell_muldiv(ED8(0x5A), ED8(0x56), ED8(0x58), 0x64);
                ell_muldiv(ED8(0x5C), ED8(0x56), ED8(0x58), 0x6A);
                EDW(0x68, ED(0x5A)); EDW(0x66, ED(0x5C));
            }
            call_jsr(0x00ACDC);
            if (g_quit) return;
        }
    }
    bus_write16(db, 0x1B1F, 0x0000);                         /* $ABC6 */
}
#undef ED
#undef EDW
#undef ED8
#undef EDW8
#undef ENEG

/* $00:AF5A: a line from ($3E,$40) to ($42,$44), plotted by $B051 point
 * by point (Bresenham): $46 the minor step, $48 the major step, $4A/$4C
 * the absolute deltas, $4E the error. The start corner is moved along. */
void mp_00AF5A(void) {
#define LD(o)      bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define LDW(o, v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
#define LNEG(a, b) ((((uint16_t)((a) - (b))) & 0x8000) != 0)
    uint16_t x = 0x0001, y = 0xFFFF, t;
    if (LNEG(LD(0x3E), LD(0x42))) { t = x; x = y; y = t; }
    if (LNEG(LD(0x40), LD(0x44))) { t = x; x = y; y = t; }
    LDW(0x46, x);
    LDW(0x48, 0x0001);
    uint16_t d = (uint16_t)(LD(0x42) - LD(0x3E));
    if (d & 0x8000) d = (uint16_t)-d;
    LDW(0x4A, d);
    d = (uint16_t)(LD(0x44) - LD(0x40));
    if (d & 0x8000) d = (uint16_t)-d;
    LDW(0x4C, d);
    bool xmajor = !LNEG(LD(0x4A), LD(0x4C));
    /* major axis: X ($3E, delta $4A) or Y ($40, delta $4C) */
    uint8_t maj = xmajor ? 0x3E : 0x40, mnr = xmajor ? 0x40 : 0x3E;
    uint8_t dmaj = xmajor ? 0x4A : 0x4C, dmnr = xmajor ? 0x4C : 0x4A;
    uint8_t emaj = xmajor ? 0x42 : 0x44;
    if (!LNEG(LD(maj), LD(emaj))) {
        LDW(0x46, (uint16_t)-LD(0x46));
        LDW(0x48, 0xFFFF);
    }
    LDW(0x86, LD(0x3E));
    LDW(0x88, LD(0x40));
    call_jsr(0x00B051);
    LDW(0x4E, (uint16_t)(LD(dmaj) >> 1));
    for (uint16_t i = 1; i <= LD(dmaj); i++) {
        uint16_t e = (uint16_t)(LD(0x4E) - LD(dmnr));
        LDW(0x4E, e);
        if (e & 0x8000) {
            LDW(0x4E, (uint16_t)(LD(0x4E) + LD(dmaj)));
            LDW(mnr, (uint16_t)(LD(mnr) + LD(0x46)));
        }
        LDW(maj, (uint16_t)(LD(maj) + LD(0x48)));
        LDW(0x86, LD(0x3E));
        LDW(0x88, LD(0x40));
        call_jsr(0x00B051);
        g_cpu.X = (uint16_t)(i + 1);
        if (g_quit) return;
    }
#undef LD
#undef LDW
#undef LNEG
}

/* $00:AF2C: clear sprite X's size/high-X bits in the OAM high table
 * ($0426.., masks at $AF4A). X and A as the ROM leaves them. */
static void oam_hi_clear(uint16_t x) {
    uint8_t db = g_cpu.DB;
    uint16_t n = (uint16_t)(x >> 2);
    uint16_t o = (uint16_t)(0x0426 + ((n >> 2) & 0xFFFE));
    uint16_t m = bus_read16(db, (uint16_t)(0xAF4A + ((n & 7) << 1)));
    bus_write16(db, o, (uint16_t)(bus_read16(db, o) & m));
    CPU_SET_A16(n);
}
void mp_00AF2C(void) { uint16_t x = g_cpu.X; oam_hi_clear(x); g_cpu.X = x; }

/* $00:ADFB: the copy tool's dashed frame as sprites: the corners $22/$26,
 * $24/$28 sorted into $2A..$30, kept inside the canvas $19B6..$19BC, then
 * the top and bottom edges in pieces of 8, 4, 2, 1 pixels (tiles $6B down)
 * and the left and right edges the same way (tiles $7B down), from OAM
 * slot $0446 on. */
void mp_00ADFB(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define DR(o)    bus_read16(0x00, (uint16_t)(dp + (o)))
#define DW(o,v)  bus_write16(0x00, (uint16_t)(dp + (o)), (uint16_t)(v))
#define NEG(a,b) ((((uint16_t)((a) - (b))) & 0x8000) != 0)
#define XBA(v)   ((uint16_t)(((v) << 8) | ((v) >> 8)))
    uint16_t a = DR(0x22), x = DR(0x26);
    if (NEG(a, x)) { DW(0x2A, a); DW(0x2E, x); } else { DW(0x2A, x); DW(0x2E, a); }
    a = DR(0x24); x = DR(0x28);
    if (NEG(a, x)) { DW(0x2C, a); DW(0x30, x); } else { DW(0x2C, x); DW(0x30, a); }
    DW(0x2C, DR(0x2C) - 1);
    DW(0x30, DR(0x30) - 1);
    uint16_t v = R16(0x19B6);
    if (!NEG(v, DR(0x2A))) DW(0x2A, v);
    if (!NEG(v, DR(0x2E))) DW(0x2E, v);
    v = R16(0x19B8);
    if (NEG(v, DR(0x2A))) DW(0x2A, v);
    if (NEG(v, DR(0x2E))) DW(0x2E, v);
    v = R16(0x19BA);
    if (!NEG(v, DR(0x2C))) DW(0x2C, v);
    if (!NEG(v, DR(0x30))) DW(0x30, v);
    v = R16(0x19BC);
    if (NEG(v, DR(0x2C))) DW(0x2C, v);
    if (NEG(v, DR(0x30))) DW(0x30, v);
    DW(0x32, DR(0x2E) - DR(0x2A) + 1);
    DW(0x34, DR(0x30) - DR(0x2C) + 1);
    DW(0x2C, XBA(DR(0x2C)));
    DW(0x30, XBA(DR(0x30)));
    DW(0x36, DR(0x2A));
    DW(0x38, 0x0008);
    DW(0x3A, 0xA06B);
    DW(0x3C, 0x206B);
    uint16_t ox = R16(0x0446);
    for (;;) {                                               /* $AE89: top and bottom */
        if (DR(0x32) < DR(0x38)) {
            DW(0x3C, DR(0x3C) - 1);
            DW(0x3A, DR(0x3A) - 1);
            DW(0x38, DR(0x38) >> 1);
            if (DR(0x38) == 0) break;
            continue;
        }
        DW(0x32, DR(0x32) - DR(0x38));
        W16(0x0226 + ox, (uint16_t)(((uint16_t)(DR(0x2C) - 0x0700) & 0xFF00) | DR(0x36)));
        W16(0x0228 + ox, DR(0x3A));
        oam_hi_clear(ox);
        ox = (uint16_t)(ox + 4);
        if (DR(0x34) != 0x0001) {
            W16(0x0226 + ox, (uint16_t)(DR(0x30) | DR(0x36)));
            W16(0x0228 + ox, DR(0x3C));
            oam_hi_clear(ox);
            ox = (uint16_t)(ox + 4);
        }
        DW(0x36, DR(0x38) + DR(0x36));
    }
    DW(0x36, DR(0x2C));                                      /* $AED9: left and right */
    DW(0x38, 0x0008);
    DW(0x3A, 0x207B);
    for (;;) {
        if (DR(0x34) < DR(0x38)) {
            DW(0x3A, DR(0x3A) - 1);
            DW(0x38, DR(0x38) >> 1);
            if (DR(0x38) == 0) break;
            continue;
        }
        DW(0x34, DR(0x34) - DR(0x38));
        W16(0x0226 + ox, (uint16_t)(DR(0x36) | DR(0x2A)));
        W16(0x0228 + ox, DR(0x3A));
        oam_hi_clear(ox);
        ox = (uint16_t)(ox + 4);
        W16(0x0226 + ox, (uint16_t)(DR(0x36) | DR(0x2E)));
        W16(0x0228 + ox, DR(0x3A));
        oam_hi_clear(ox);
        ox = (uint16_t)(ox + 4);
        DW(0x36, (uint16_t)(XBA(DR(0x38)) + DR(0x36)));
    }
    W16(0x0446, ox);
    g_cpu.X = ox;
#undef R16
#undef W16
#undef DR
#undef DW
#undef NEG
#undef XBA
}

/* ========================================================================
 * Data tables for pixel masking (from ROM $00:B1A2 / $00:B1B2 / $00:B600)
 * ======================================================================== */

/* Right-side pixel mask: masks bits 7..X within a byte pair */
static const uint16_t DATA_00B1A2[8] = {
    0xFFFF, 0x7F7F, 0x3F3F, 0x1F1F, 0x0F0F, 0x0707, 0x0303, 0x0101
};

/* Left-side pixel mask: masks bits X..0 within a byte pair */
static const uint16_t DATA_00B1B2[8] = {
    0x0000, 0x8080, 0xC0C0, 0xE0E0, 0xF0F0, 0xF8F8, 0xFCFC, 0xFEFE
};

/* Single-pixel bitmask for each X position within a tile */
static const uint16_t DATA_00B600[8] = {
    0x8080, 0x4040, 0x2020, 0x1010, 0x0808, 0x0404, 0x0202, 0x0101
};

/* ========================================================================
 * $00:B25E — Get pen mask (static helper)
 *
 * Reads pen pattern data from $1C2A/$1C3A and produces bitplane masks
 * in dp $92/$94 (pen mask) and $8A/$8C (overflow for next tile column).
 * Uses DATA_02E000/02E800 ROM lookup tables for bit shifting.
 *
 * y_reg: pen tile row offset (from B051's Y register)
 * x_bit: bit position within byte (0-7)
 * ======================================================================== */
static void inner_B25E(uint16_t y_reg, uint16_t x_bit) {
    /* Clear outputs */
    bus_wram_write16(0x0092, 0x0000);  /* pen mask bp0-1 */
    bus_wram_write16(0x0094, 0x0000);  /* pen mask bp2-3 */
    bus_wram_write16(0x008A, 0x0000);  /* overflow bp0-1 */
    bus_wram_write16(0x008C, 0x0000);  /* overflow bp2-3 */

    /* Convert Y register to pen buffer index:
     * index = (y & 0x3F) | ((y & 0x400) >> 4) */
    uint16_t index = (y_reg & 0x003F) | ((y_reg & 0x0400) >> 4);

    if (x_bit == 0) {
        /* Fast path: byte-aligned, just copy raw pen data */
        bus_wram_write16(0x0092, bus_wram_read16(0x1C2A + index));
        bus_wram_write16(0x0094, bus_wram_read16(0x1C3A + index));
        return;
    }

    /* Slow path: pen is at sub-byte offset, use shift lookup tables.
     * For each byte of pen data, look up shifted versions from ROM:
     *   DATA_02E000[byte*8 + bit_pos] = byte shifted right by bit_pos (kept bits)
     *   DATA_02E800[byte*8 + bit_pos] = byte shifted left by (8-bit_pos) (overflow) */

    /* Read pen data bp0-1 from $1C2A */
    uint16_t pen01 = bus_wram_read16(0x1C2A + index);
    uint8_t pen01_lo = pen01 & 0xFF;
    uint8_t pen01_hi = (pen01 >> 8) & 0xFF;

    /* Process low byte of bp0-1 */
    if (pen01_lo != 0) {
        uint16_t lut_idx = (uint16_t)pen01_lo * 8 + x_bit;
        bus_wram_write8(0x0092, bus_read8(0x02, 0xE000 + lut_idx));
        bus_wram_write8(0x008A, bus_read8(0x02, 0xE800 + lut_idx));
    }
    /* Process high byte of bp0-1 */
    if (pen01_hi != 0) {
        uint16_t lut_idx = (uint16_t)pen01_hi * 8 + x_bit;
        bus_wram_write8(0x0093, bus_read8(0x02, 0xE000 + lut_idx));
        bus_wram_write8(0x008B, bus_read8(0x02, 0xE800 + lut_idx));
    }

    /* Read pen data bp2-3 from $1C3A */
    uint16_t pen23 = bus_wram_read16(0x1C3A + index);
    uint8_t pen23_lo = pen23 & 0xFF;
    uint8_t pen23_hi = (pen23 >> 8) & 0xFF;

    /* Process low byte of bp2-3 */
    if (pen23_lo != 0) {
        uint16_t lut_idx = (uint16_t)pen23_lo * 8 + x_bit;
        bus_wram_write8(0x0094, bus_read8(0x02, 0xE000 + lut_idx));
        bus_wram_write8(0x008C, bus_read8(0x02, 0xE800 + lut_idx));
    }
    /* Process high byte of bp2-3 */
    if (pen23_hi != 0) {
        uint16_t lut_idx = (uint16_t)pen23_hi * 8 + x_bit;
        bus_wram_write8(0x0095, bus_read8(0x02, 0xE000 + lut_idx));
        bus_wram_write8(0x008D, bus_read8(0x02, 0xE800 + lut_idx));
    }
}

/* ========================================================================
 * $00:B23C — Canvas tile color lookup (static helper)
 *
 * Computes palette color values for the canvas tile at offset (Y + $84).
 * Reads from $1CAA (bp0-1) and $1CBA (bp2-3) and stores to $9A/$9C.
 *
 * y_reg: pen tile row offset
 * ======================================================================== */
static void inner_B23C(uint16_t y_reg) {
    /* Compute index: (y + $84) → tile half flag + position */
    uint16_t sum = y_reg + bus_wram_read16(0x0084);
    uint16_t half = (sum & 0x0400) >> 4;  /* $40 if in second tile half */
    uint16_t pos = sum & 0x003F;
    uint16_t idx = pos | half;

    bus_wram_write16(0x009A, bus_wram_read16(0x1CAA + idx));
    bus_wram_write16(0x009C, bus_wram_read16(0x1CBA + idx));
}

/* ========================================================================
 * $00:B1C2 — Canvas VRAM write (static helper)
 *
 * Applies pen mask ($92/$94) with color ($9A/$9C) to the canvas buffer
 * using XOR-AND-XOR selective bit replacement. Edge masking via $19BE.
 *
 * y_reg: pen tile row offset (used to compute canvas buffer address)
 * ======================================================================== */
static void inner_B1C2(uint16_t y_reg) {
    uint16_t erase = bus_wram_read16(0x1992);
    uint16_t pen92 = bus_wram_read16(0x0092);
    uint16_t pen94 = bus_wram_read16(0x0094);
    uint16_t color9a = bus_wram_read16(0x009A);
    uint16_t color9c = bus_wram_read16(0x009C);
    uint16_t mask96 = bus_wram_read16(0x0096);

    if (erase == 0) {
        /* Not erasing — apply color logic */
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if (eb8 != 0) {
            color9a = bus_wram_read16(0x0EBC);
            /* Fall through to normal draw with alternate color */
        } else {
            uint16_t pal = bus_wram_read16(0x00A6);
            if (pal >= 0x0070) {
                uint16_t tool = bus_wram_read16(0x04D0) & 0x00FF;
                if (tool == 0x0003) {
                    /* Special color mode: pen pattern IS the color */
                    color9a = pen92;
                    color9c = pen94;
                    uint16_t combined = pen92 | pen94;
                    combined = ((combined >> 8) | combined) & 0x00FF;
                    combined |= (combined << 8);
                    combined &= mask96;
                    pen92 = combined;
                    pen94 = combined;
                    goto do_write;
                }
            }
        }

        /* Normal drawing: mask pen bits where color is set */
        uint16_t color_any = color9a | color9c;
        /* Merge high and low bytes of color presence */
        color_any = ((color_any >> 8) | color_any);
        color_any = (color_any & 0x00FF) | ((color_any & 0x00FF) << 8);
        pen92 &= color_any;
        pen94 &= color_any;
    }

do_write:;
    /* Compute canvas buffer address */
    uint16_t addr = y_reg + bus_wram_read16(0x0084);
    if (addr >= 0x6000) return;  /* Bounds check */

    /* Read canvas buffer pointer from DP $0A-$0C ($7E:A000) */
    uint8_t buf_lo = bus_wram_read8(0x000A);
    uint8_t buf_hi = bus_wram_read8(0x000B);
    uint8_t buf_bank = bus_wram_read8(0x000C);
    uint16_t buf_base = ((uint16_t)buf_hi << 8) | buf_lo;
    uint16_t edge_mask = bus_wram_read16(0x19BE);

    /* Bitplane 0-1: XOR-AND-XOR selective replacement */
    uint16_t old_01 = bus_read16(buf_bank, buf_base + addr);
    uint16_t new_01 = ((color9a ^ old_01) & pen92 & edge_mask) ^ old_01;
    bus_write16(buf_bank, buf_base + addr, new_01);

    /* Bitplane 2-3: offset +$10 in tile */
    uint16_t addr23 = addr + 0x0010;
    uint16_t old_23 = bus_read16(buf_bank, buf_base + addr23);
    uint16_t new_23 = ((color9c ^ old_23) & pen94 & edge_mask) ^ old_23;
    bus_write16(buf_bank, buf_base + addr23, new_23);
}

/* ========================================================================
 * $00:B0D3 — Plot one pixel row (inner loop of B051)
 *
 * Plots pixels for a single Y row across the pen width.
 * Handles up to 3 tile columns (left, middle, right) since the
 * 16-pixel wide pen can straddle tile boundaries.
 *
 * Receives pen tile row offset via g_cpu.Y (set by B051).
 * ======================================================================== */
void mp_00B0D3(void) {
    uint16_t y_reg = g_cpu.Y;
    uint16_t x_draw = bus_wram_read16(0x0086);
    uint16_t x_bit = x_draw & 0x0007;

    /* === Left tile column === */
    inner_B25E(y_reg, x_bit);
    inner_B23C(y_reg);

    bus_wram_write16(0x0096, DATA_00B1A2[x_bit]);

    /* Edge masking for left tile */
    uint16_t x_tile = x_draw & 0xFFF8;
    uint16_t x_left = bus_wram_read16(0x19AE);
    uint16_t x_right = bus_wram_read16(0x19B0);

    if (x_tile == x_left) {
        bus_wram_write16(0x19BE, 0x0F0F);
    } else if ((int16_t)x_tile < (int16_t)x_left) {
        goto skip_left;
    } else if (x_tile == x_right) {
        bus_wram_write16(0x19BE, 0xF0F0);
    } else if ((int16_t)x_tile > (int16_t)x_right) {
        goto skip_left;
    } else {
        bus_wram_write16(0x19BE, 0xFFFF);
    }
    inner_B1C2(y_reg);

skip_left:;
    /* Save overflow from left tile */
    uint16_t overflow_8A = bus_wram_read16(0x008A);
    uint16_t overflow_8C = bus_wram_read16(0x008C);
    bus_wram_write16(0x008E, overflow_8A);
    bus_wram_write16(0x0090, overflow_8C);

    /* === Middle tile column === */
    uint16_t y_mid = y_reg + 0x0020;
    inner_B25E(y_mid, x_bit);

    /* OR overflow from left into middle pen mask */
    bus_wram_write16(0x0092, bus_wram_read16(0x0092) | bus_wram_read16(0x008E));
    bus_wram_write16(0x0094, bus_wram_read16(0x0094) | bus_wram_read16(0x0090));

    inner_B23C(y_mid);

    bus_wram_write16(0x0096, 0xFFFF);  /* Full mask for middle tile */

    uint16_t x_mid = (x_draw + 0x0008) & 0xFFF8;
    if (x_mid == x_left) {
        bus_wram_write16(0x19BE, 0x0F0F);
    } else if ((int16_t)x_mid < (int16_t)x_left) {
        goto skip_mid;
    } else if (x_mid == x_right) {
        bus_wram_write16(0x19BE, 0xF0F0);
    } else if ((int16_t)x_mid > (int16_t)x_right) {
        goto skip_mid;
    } else {
        bus_wram_write16(0x19BE, 0xFFFF);
    }
    inner_B1C2(y_mid);

skip_mid:;
    /* === Right tile column (if overflow from middle) === */
    overflow_8A = bus_wram_read16(0x008A);
    overflow_8C = bus_wram_read16(0x008C);
    if ((overflow_8A | overflow_8C) == 0) return;

    bus_wram_write16(0x0092, overflow_8A);
    bus_wram_write16(0x0094, overflow_8C);

    uint16_t y_right = y_mid + 0x0020;
    inner_B23C(y_right);

    bus_wram_write16(0x0096, DATA_00B1B2[x_bit]);

    uint16_t x_rt = (x_draw + 0x0010) & 0xFFF8;
    if (x_rt == x_left) {
        bus_wram_write16(0x19BE, 0x0F0F);
    } else if ((int16_t)x_rt < (int16_t)x_left) {
        return;
    } else if (x_rt == x_right) {
        bus_wram_write16(0x19BE, 0xF0F0);
    } else if ((int16_t)x_rt > (int16_t)x_right) {
        return;
    } else {
        bus_wram_write16(0x19BE, 0xFFFF);
    }
    inner_B1C2(y_right);
}

/* ========================================================================
 * $00:B32D — Alternate draw (with mask flag $50)
 *
 * Single-pixel draw mode used when $20 AND $50 is set.
 * Draws one pixel at ($86,$88) using the current color.
 * ======================================================================== */
void mp_00B32D(void) {
    /* Set up coordinates */
    bus_wram_write16(0x0080, bus_wram_read16(0x0086));
    bus_wram_write16(0x0082, bus_wram_read16(0x0088));

    func_table_call(0x00B305);

    /* Get single-pixel mask for X bit position */
    uint16_t x_bit = bus_wram_read16(0x0086) & 0x0007;
    uint16_t pixel_mask = DATA_00B600[x_bit];

    /* Set pen mask to single pixel */
    bus_wram_write16(0x0092, pixel_mask);
    bus_wram_write16(0x0094, pixel_mask);

    /* Get canvas color at this position */
    inner_B23C(0x0000);

    /* Full edge mask */
    bus_wram_write16(0x19BE, 0xFFFF);

    /* Apply color: mask pen by color presence */
    uint16_t color9a = bus_wram_read16(0x009A);
    uint16_t color9c = bus_wram_read16(0x009C);
    uint16_t color_any = color9a | color9c;
    color_any = ((color_any >> 8) | color_any);
    color_any = (color_any & 0x00FF) | ((color_any & 0x00FF) << 8);
    bus_wram_write16(0x0092, pixel_mask & color_any);
    bus_wram_write16(0x0094, pixel_mask & color_any);

    /* Read canvas buffer pointer */
    uint8_t buf_lo = bus_wram_read8(0x000A);
    uint8_t buf_hi = bus_wram_read8(0x000B);
    uint8_t buf_bank = bus_wram_read8(0x000C);
    uint16_t buf_base = ((uint16_t)buf_hi << 8) | buf_lo;

    uint16_t addr = bus_wram_read16(0x0084);

    /* Bitplane 0-1 */
    uint16_t pen92 = bus_wram_read16(0x0092);
    uint16_t old_01 = bus_read16(buf_bank, buf_base + addr);
    uint16_t new_01 = ((color9a ^ old_01) & pen92 & 0xFFFF) ^ old_01;
    bus_write16(buf_bank, buf_base + addr, new_01);

    /* Bitplane 2-3 */
    uint16_t pen94 = bus_wram_read16(0x0094);
    uint16_t addr23 = addr + 0x0010;
    uint16_t old_23 = bus_read16(buf_bank, buf_base + addr23);
    uint16_t new_23 = ((color9c ^ old_23) & pen94 & 0xFFFF) ^ old_23;
    bus_write16(buf_bank, buf_base + addr23, new_23);
}

/* $00:B610 (the fill queue push) lives in mp_draw.c, inside $B3FF. */

/* $00:B910 / $00:B926 (A 8-bit): sine and cosine of the angle A (0..$FF
 * a full turn) from the quarter-wave table at $00:B947; the value is the
 * magnitude, $0102 is set when it is negative. X is kept. */
static uint8_t quarter_wave(uint8_t i) { return bus_read8(0x00, (uint16_t)(0xB947 + (i & 0x7F))); }
void mp_00B910(void) {
    uint8_t a = CPU_A8();
    bus_write8(g_cpu.DB, 0x0102, a >= 0x80 ? 1 : 0);
    if (a & 0x40) a = (uint8_t)-a;
    CPU_SET_A8(quarter_wave(a));
}
void mp_00B926(void) {
    uint8_t a = CPU_A8();
    bus_write8(g_cpu.DB, 0x0102, (a >= 0x40 && a < 0xC0) ? 1 : 0);
    if (!(a & 0x40)) a = (uint8_t)-a;
    a ^= 0x40;
    CPU_SET_A8(quarter_wave(a));
}

/* $00:B7D6: the spray can's dot pattern, every other frame (or at once
 * while the mouse moves or $50 is set): the 8x8-tile buffer $1C2A..$1CA9
 * cleared, then rings 1..7 of dots at random angles. The dot count per
 * ring comes from one of three tables (\$B8E0, or for the first $10 calls
 * $B8F0 / $B900 by turn), the radius from the ring number, the offset from
 * the hardware multiplier ($4202 x $4203). */
void mp_00B7D6(void) {
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
#define DR(o)    bus_read16(0x00, (uint16_t)(dp + (o)))
#define DW(o, v) bus_write16(0x00, (uint16_t)(dp + (o)), (uint16_t)(v))
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    DW(0xBA, DR(0xBA) - 1);
    if (DR(0xBA) != 0 && (((R16(0x04C6) | R16(0x04C8)) & 0x00FF) | DR(0x50)) == 0) return;
    DW(0xBA, 0x0002);
    for (int x = 0x001E; x >= 0; x -= 2) {
        W16(0x1C2A + x, 0); W16(0x1C4A + x, 0); W16(0x1C6A + x, 0); W16(0x1C8A + x, 0);
    }
    for (uint16_t ring = 1; ring != 8; ring++) {
        uint16_t bc = DR(0xBC);
        DW(0xBC, (uint16_t)(bc + 1));
        if ((uint16_t)(bc + 1) == 0) DW(0xBE, 0x0001);
        uint16_t table = 0xB8E0;
        if (DR(0xBE) == 0 && bc < 0x0010) table = (bc & 1) ? 0xB900 : 0xB8F0;
        uint16_t dots = R16((uint16_t)(table + ((ring - 1) << 1)));
        while ((int16_t)(--dots) >= 0) {
            call_jsl(0x01E20C);
            uint8_t angle = (uint8_t)CPU_A16();
            uint16_t off[2];
            for (int k = 0; k < 2; k++) {                  /* $B2: cosine, $B4: sine */
                g_cpu.flag_M = true; g_cpu.flag_X = true;
                CPU_SET_A8(angle);
                call_jsr(k == 0 ? 0x00B926 : 0x00B910);
                bus_write8(db, 0x4202, CPU_A8());
                bus_write8(db, 0x4203, (uint8_t)ring);
                g_cpu.flag_M = false; g_cpu.flag_X = false;
                uint16_t p = bus_read16(db, 0x4216);
                if (bus_read8(db, 0x0102)) p = (uint16_t)-p;
                off[k] = (uint16_t)((int16_t)(int8_t)(p >> 8) + 8);
            }
            DW(0xB2, off[0]);
            DW(0xB4, off[1]);
            uint16_t b6 = (uint16_t)(((off[0] & 0xFFF8) << 2) + ((off[1] & 0xFFF8) << 3) + ((off[1] & 7) << 1));
            DW(0xB6, b6);
            uint16_t bits = R16((uint16_t)(0xB600 + ((off[0] & 7) << 1)));
            if (b6 >= 0x0070) { while (!g_quit) call_jsl(0x01E2CE); return; }   /* the ROM hangs (BRA *) */
            W16(0x1C2A + b6, bits | R16(0x1C2A + b6));
            W16(0x1C3A + b6, bits | R16(0x1C3A + b6));
        }
    }
    g_cpu.Y = 0x0008;
#undef DR
#undef DW
#undef R16
#undef W16
}

/* ========================================================================
 * $00:9C1B — Drawing completion handler
 *
 * Called after a drawing operation completes.
 * Triggers VRAM transfer for the modified canvas area.
 * ======================================================================== */
void mp_009C1B(void) {
    /* $00:9C1B: wait A frames (A = 0 waits 65536). */
    uint16_t n = CPU_A16();
    do {
        call_jsl(0x01E2CE);
        n = (uint16_t)(n - 1);
        CPU_SET_A16(n);
    } while (n != 0);
}

/* ========================================================================
 * $00:9F50 — Stamp commit
 *
 * Commits the stamp placement to the canvas.
 * ======================================================================== */
void mp_009F50(void) {
    /* Commit the stamp preview to canvas permanently */
    func_table_call(0x00B051);  /* Final pixel write */
}

/* ========================================================================
 * $00:A0EB — Tool-specific initialization dispatch
 *
 * Jump table for tool-specific setup after tool change.
 * Called from $009FC4 when flags indicate setup needed.
 * ======================================================================== */
void mp_00A0EB(void) {
    /* Read the tool index and dispatch.
     * The dispatch table at $00:A0EB has entries for each tool.
     * For now, the individual tool setups are complex — dispatch. */
    uint16_t tool = bus_wram_read16(0x00AA);
    /* Each tool has specific graphics/state initialization */
}

/* ========================================================================
 * $00:82D5 — Reset delay + SPC700 reset
 *
 * Hardware delay loop used during soft reset. Sends reset
 * commands to the SPC700 with proper timing.
 * ======================================================================== */
void mp_0082D5(void) {
    mp_01E30E();  /* Disable NMI */

    /* Long delay (original: nested loops reading $7FFFFF) */
    bus_run_cycles(65536 * 4);

    /* SPC700 reset command */
    bus_write8(0x00, 0x2142, 0x02);

    /* Another long delay */
    bus_run_cycles(65536 * 4);

    /* Clear APU ports */
    bus_write8(0x00, 0x2140, 0xFF);
    bus_write8(0x00, 0x2141, 0x00);
    bus_write8(0x00, 0x2142, 0x00);
    bus_write8(0x00, 0x2143, 0x00);
}

/* ========================================================================
 * $01:8D35 — Title screen: check for logo click
 *
 * Checks if the user clicked in the toolbar area during
 * title screen state 1.
 * ======================================================================== */
void mp_018D35(void) {
    uint16_t buttons = bus_wram_read16(0x04CA);
    if (!(buttons & 0x0020)) {
        mp_01E2CE();
        return;
    }

    /* Check click Y range */
    uint16_t cy = bus_wram_read16(0x04DE);
    if (cy < 0x0040 || cy >= 0x0050) return;

    /* Click is in toolbar area — advance state */
    func_table_call(0x018D4D);
}

/* ========================================================================
 * Register all shape and remaining functions.
 * ======================================================================== */
void mp_register_shapes(void) {
    func_table_register(0x00AAFB, mp_00AAFB);
    func_table_register(0x00AB26, mp_00AB26);
    func_table_register(0x00AB8A, mp_00AB8A);
    func_table_register(0x00ACDC, mp_00ACDC);
    func_table_register(0x00ADFB, mp_00ADFB);
    func_table_register(0x00AF5A, mp_00AF5A);
    func_table_register(0x00B0D3, mp_00B0D3);
    func_table_register(0x00B32D, mp_00B32D);
    func_table_register(0x00B7D6, mp_00B7D6);
    func_table_register(0x00AF2C, mp_00AF2C);
    func_table_register(0x00B910, mp_00B910);
    func_table_register(0x00B926, mp_00B926);
    func_table_register(0x009C1B, mp_009C1B);
    func_table_register(0x009F50, mp_009F50);
    func_table_register(0x00A0EB, mp_00A0EB);
    func_table_register(0x0082D5, mp_0082D5);
    func_table_register(0x018D35, mp_018D35);
}
