/*
 * Mario Paint — pen plotting and a toolbar helper, translated from the ROM.
 *
 *   $00:B23C  fetch the pen's two plane pairs for one canvas row
 *   $00:B25E  shift the pen masks for a sub-tile X offset (and $B2F8)
 *   $00:B1C2  merge the pen into the canvas at [$0A] + Y + $84
 *   $00:A0B3  put one tool icon (2x3 tiles) into the toolbar tilemap
 *
 * The plotter works in direct page: $84 canvas offset, $8A-$8D shifted-out
 * bits, $92/$94 masks, $96 colour mask, $9A/$9C pen planes, [$0A] canvas.
 * Absolute table reads go through the data bank, as in the ROM.
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>
#include <stdint.h>
#include <stdbool.h>

#define DP(o) ((uint16_t)(g_cpu.DP + (o)))
static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

/* (offset & $400) >> 4 | (offset & $3F): the pen tile row for an offset. */
static inline uint16_t pen_row(uint16_t off, uint16_t *hi) {
    *hi = (uint16_t)((off & 0x0400) >> 4);
    return (uint16_t)((off & 0x003F) | *hi);
}

/* ---- $00:B23C ---- */
void mp_00B23C(void) {
    uint16_t hi;
    uint16_t x = pen_row((uint16_t)(g_cpu.Y + bus_wram_read16(DP(0x84))), &hi);
    bus_wram_write16(DP(0x9A), bus_read16(g_cpu.DB, (uint16_t)(0x1CAA + x)));
    bus_wram_write16(DP(0x9C), bus_read16(g_cpu.DB, (uint16_t)(0x1CBA + x)));
    g_cpu.X = x;
    CPU_SET_A16(hi);                                        /* PLA */
}

/* ---- $00:B25E: X = pixel offset within the byte (0 = aligned). Each pen
 * byte n becomes shift-table entries $02:E000/$E800 + n*8 + X: the bits
 * that stay ($92-$95) and the bits pushed into the next byte ($8A-$8D). */
void mp_00B25E(void) {
    uint16_t saved_x = g_cpu.X, saved_y = g_cpu.Y;
    uint16_t hi;
    uint16_t y = pen_row(saved_y, &hi);
    uint8_t db = g_cpu.DB;
    if (saved_x == 0) {                                     /* $B2F8 */
        bus_wram_write16(DP(0x92), bus_read16(db, (uint16_t)(0x1C2A + y)));
        bus_wram_write16(DP(0x94), bus_read16(db, (uint16_t)(0x1C3A + y)));
        g_cpu.X = saved_x; g_cpu.Y = saved_y;
        return;
    }
    bus_wram_write16(DP(0x92), 0); bus_wram_write16(DP(0x94), 0);
    bus_wram_write16(DP(0x8A), 0); bus_wram_write16(DP(0x8C), 0);
    static const struct { uint16_t src; uint8_t stay, out; } part[4] = {
        { 0x1C2A, 0x92, 0x8A }, { 0x1C2A, 0x93, 0x8B },
        { 0x1C3A, 0x94, 0x8C }, { 0x1C3A, 0x95, 0x8D },
    };
    for (int k = 0; k < 4; k++) {
        uint16_t w = bus_read16(db, (uint16_t)(part[k].src + y));
        uint8_t n = (k & 1) ? (uint8_t)(w >> 8) : (uint8_t)w;
        if (n == 0) continue;                               /* BEQ: leave 0 */
        uint16_t ix = (uint16_t)(((uint16_t)n << 3) | saved_x);
        bus_wram_write8(DP(part[k].stay), bus_read8(0x02, (uint16_t)(0xE000 + ix)));
        bus_wram_write8(DP(part[k].out),  bus_read8(0x02, (uint16_t)(0xE800 + ix)));
    }
    g_cpu.X = saved_x; g_cpu.Y = saved_y;
}

/* ---- $00:B1C2 ---- */
void mp_00B1C2(void) {
    uint16_t saved_y = g_cpu.Y;
    uint16_t m92 = bus_wram_read16(DP(0x92)), m94 = bus_wram_read16(DP(0x94));
    if (bus_wram_read16(0x1992) == 0) {
        bool solid = false;                                 /* $B1E4 path */
        if (bus_wram_read16(0x0EB8) == 0 && bus_wram_read16(0x00A6) >= 0x0070 &&
            (bus_wram_read16(0x04D0) & 0x00FF) == 0x0003)
            solid = true;
        if (solid) {
            /* Pattern colour: the pen becomes the mask, filled with $96. */
            bus_wram_write16(DP(0x9A), m92);
            bus_wram_write16(DP(0x9C), m94);
            uint16_t v = (uint16_t)(m92 | m94);
            uint16_t t = (uint16_t)((swap16(v) | v) & bus_wram_read16(DP(0x96)));
            bus_wram_write16(DP(0x92), t);
            bus_wram_write16(DP(0x94), t);
        } else {                                            /* $B1FD */
            uint16_t v = (uint16_t)(bus_wram_read16(DP(0x9A)) | bus_wram_read16(DP(0x9C)));
            uint16_t t = (uint16_t)(swap16(v) | v);
            bus_wram_write16(DP(0x92), (uint16_t)(t & m92));
            bus_wram_write16(DP(0x94), (uint16_t)(t & m94));
        }
    }
    uint16_t off = (uint16_t)(saved_y + bus_wram_read16(DP(0x84)));   /* $B210 */
    if (off < 0x6000) {
        uint32_t canvas = dp_long_ptr(g_cpu.DP, 0x0A);
        uint16_t mask = bus_wram_read16(0x19BE);
        for (int k = 0; k < 2; k++) {
            uint16_t plane = bus_wram_read16(DP(k ? 0x9C : 0x9A));
            uint16_t m = bus_wram_read16(DP(k ? 0x94 : 0x92));
            uint32_t at = canvas + (uint16_t)(off + k * 0x10);
            uint16_t c = read_long16(at);
            write_long16(at, (uint16_t)(((plane ^ c) & m & mask) ^ c));
        }
    }
    g_cpu.Y = saved_y;
}

/* ---- $00:A0B3: icon A from the table whose pointer the caller pushed
 * (at $05,s after our PHX and the return address): [X dest, first tile].
 * Tiles t, t+$10, t+$20 go down the left column, t+1.. the right. */
void mp_00A0B3(void) {
    uint16_t table = bus_wram_read16((uint16_t)(g_cpu.S + 3));   /* ($05,s) after PHX */
    uint16_t y = (uint16_t)(CPU_A16() << 2);
    uint16_t x = bus_read16(g_cpu.DB, (uint16_t)(table + y));
    uint16_t t = bus_read16(g_cpu.DB, (uint16_t)(table + y + 2));
    for (int row = 0; row < 3; row++) {
        bus_write16(0x7E, (uint16_t)(0x2640 + row * 0x40 + x), (uint16_t)(t + row * 0x10));
        bus_write16(0x7E, (uint16_t)(0x2642 + row * 0x40 + x), (uint16_t)(t + 1 + row * 0x10));
    }
    g_cpu.Y = (uint16_t)(y + 2);
    CPU_SET_A16((uint16_t)(t + 1 + 0x20));
}

void mp_register_plot(void) {
    func_table_register(0x00B23C, mp_00B23C);
    func_table_register(0x00B25E, mp_00B25E);
    func_table_register(0x00B1C2, mp_00B1C2);
    func_table_register(0x00A0B3, mp_00A0B3);
}
