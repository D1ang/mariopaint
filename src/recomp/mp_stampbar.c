/*
 * Mario Paint — stamp bar thumbnails, translated from the ROM.
 *
 *   $00:DE8E  build all 16 thumbnails
 *   $00:E0D4  fetch one stamp's 16x16 graphics (4 bitplanes)
 *   $00:E167    copy one word pair of it
 *   $00:DF1C  shrink it into the bar, row by row
 *   $00:DF2C    gather one row and squeeze its 16 pixels
 *   $00:DF95      the bit squeeze for one bitplane byte pair
 *   $00:DFC4    shift the row into place and merge it into $1144
 *
 * $112C is the stamp index (0-15). With $A6 = $F0 the stamps are the
 * player's own, in SRAM at $70:0000; otherwise they come from ROM bank $0A.
 * The thumbnails go to the BG buffer at $1144 (copied from $05:F800 first).
 *
 * Table reads (abs,X) use the data bank, as in the ROM: for stamp 15 the
 * column table at $E07A is indexed past its end.
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- $00:E167: one word from each of the two long pointers at DP $01 and
 * DP $05 (+Y) into $101C+X and $102C+X; X and Y step by 2. ---- */
static void copy_plane_pair(void) {
    uint16_t dp = g_cpu.DP;
    uint32_t p1 = bus_read16(0x00, (uint16_t)(dp + 1)) | ((uint32_t)bus_read8(0x00, (uint16_t)(dp + 3)) << 16);
    uint32_t p2 = bus_read16(0x00, (uint16_t)(dp + 5)) | ((uint32_t)bus_read8(0x00, (uint16_t)(dp + 7)) << 16);
    uint16_t x = g_cpu.X, y = g_cpu.Y;
    bus_wram_write16((uint16_t)(0x101C + x), read_long16(p1 + y));   /* LDA [$01],y */
    uint16_t v = read_long16(p2 + y);
    bus_wram_write16((uint16_t)(0x102C + x), v);                     /* LDA [$05],y */
    CPU_SET_A16(v);
    g_cpu.Y = (uint16_t)(y + 2);
    g_cpu.X = (uint16_t)(x + 2);
}
void mp_00E167(void) { copy_plane_pair(); }

/* ---- $00:E0D4 ---- */
void mp_00E0D4(void) {
    /* PHD; TSC; SBC #$10; TCS; TCD: a 16-byte direct-page frame. */
    uint16_t s = g_cpu.S;
    bus_wram_write16((uint16_t)(s - 1), g_cpu.DP);
    s = (uint16_t)(s - 2 - 0x10);
    uint16_t old_dp = g_cpu.DP;
    g_cpu.S = s;
    g_cpu.DP = s;

    uint16_t a = bus_wram_read16(0x00A6), bank;
    if (a == 0x00F0) {                                      /* own stamps: SRAM */
        a = 0x0000;
        bank = 0x0070;
    } else {
        if (a >= 0x0070) a = (uint16_t)(a + 0x0010);
        a = (uint16_t)((uint16_t)((a << 8) | (a >> 8)) >> 1);   /* XBA; LSR */
        a = (uint16_t)(a + 0x8000);
        bank = 0x000A;
    }
    bus_wram_write16((uint16_t)(s + 1), a);                 /* $01: planes 0/1 */
    bus_wram_write16((uint16_t)(s + 5), (uint16_t)(a + 0x10));  /* $05: planes 2/3 */
    bus_wram_write16((uint16_t)(s + 3), bank);
    bus_wram_write16((uint16_t)(s + 7), bank);

    uint16_t idx = bus_wram_read16(0x112C);
    uint16_t hi = (uint16_t)((idx & 0x0008) << 7);          /* AND #8; XBA; LSR */
    uint16_t lo = (uint16_t)((idx & 0x0007) << 6);          /* AND #7; XBA; LSR x2 */
    g_cpu.Y = (uint16_t)(hi + lo);

    /* Four tiles of 8 word pairs: X runs 0-$10, $20-$30, $40-$50, $60-$70;
     * the source moves on $10, $1D0 and $10 bytes between them. */
    static const uint16_t step[4] = { 0x0000, 0x0010, 0x01D0, 0x0010 };
    for (int t = 0; t < 4; t++) {
        g_cpu.Y = (uint16_t)(g_cpu.Y + step[t]);
        g_cpu.X = (uint16_t)(t * 0x20);
        do copy_plane_pair(); while (g_cpu.X != (uint16_t)(t * 0x20 + 0x10));
    }

    s = (uint16_t)(s + 0x10);                               /* TSC; ADC #$10; TCS */
    CPU_SET_A16(s);
    g_cpu.S = (uint16_t)(s + 2);                            /* PLD */
    g_cpu.DP = old_dp;
}

/* ---- $00:DF95: squeeze one bitplane byte pair (X = 0-3). The four bytes
 * $1138,x : $113C,x : $111C,x : $1120,x form one 32-bit shift register. ---- */
static void squeeze(uint16_t x) {
#define B(a) bus_wram_read8((uint16_t)((a) + x))
#define W(a, v) bus_wram_write8((uint16_t)((a) + x), (uint8_t)(v))
    for (int i = 0; i < 2; i++) {                           /* ASL $1120; ROL $111C */
        uint8_t c = B(0x1120) >> 7;
        W(0x1120, B(0x1120) << 1);
        W(0x111C, (B(0x111C) << 1) | c);
    }
    for (int i = 2; i < 0x0D; i++) {                        /* 11 x through all four */
        uint8_t c1 = B(0x1120) >> 7; W(0x1120, B(0x1120) << 1);
        uint8_t c2 = B(0x111C) >> 7; W(0x111C, (B(0x111C) << 1) | c1);
        uint8_t c3 = B(0x113C) >> 7; W(0x113C, (B(0x113C) << 1) | c2);
        W(0x1138, (B(0x1138) << 1) | c3);
    }
    for (int i = 0; i < 5; i++) {                           /* 5 x ASL $113C; ROL $1138 */
        uint8_t c = B(0x113C) >> 7;
        W(0x113C, B(0x113C) << 1);
        W(0x1138, (B(0x1138) << 1) | c);
    }
#undef B
#undef W
}
void mp_00DF95(void) { squeeze(g_cpu.X); g_cpu.X = g_cpu.X; }

/* ---- $00:DF2C: gather row X-1 of the stamp (offsets at $DF89) and squeeze. */
void mp_00DF2C(void) {
    uint16_t saved_x = g_cpu.X;
    uint16_t off = bus_read8(g_cpu.DB, (uint16_t)(0xDF89 + saved_x - 1));
    bus_wram_write16(0x111C, bus_wram_read16((uint16_t)(0x101C + off)));
    bus_wram_write16(0x111E, bus_wram_read16((uint16_t)(0x102C + off)));
    bus_wram_write16(0x1120, bus_wram_read16((uint16_t)(0x103C + off)));
    bus_wram_write16(0x1122, bus_wram_read16((uint16_t)(0x104C + off)));
    for (int x = 3; x >= 0; x--) squeeze((uint16_t)x);

    /* Pixels set in any plane become opaque: OR both bytes of the planes,
     * invert, and fold that into $113A (and, masked, into $113E). */
    uint16_t v = (uint16_t)(bus_wram_read16(0x1138) | bus_wram_read16(0x113A));
    uint16_t t = (uint16_t)(~((uint16_t)((v << 8) | (v >> 8)) | v));
    bus_wram_write16(0x113A, (uint16_t)(t | bus_wram_read16(0x113A)));
    v = (uint16_t)(bus_wram_read16(0x113C) | bus_wram_read16(0x113E));
    t = (uint16_t)(~((uint16_t)((v << 8) | (v >> 8)) | v));
    bus_wram_write16(0x113E, (uint16_t)((t | bus_wram_read16(0x113E)) & 0xE0E0));
    bus_wram_write16(0x1140, 0x0000);
    bus_wram_write16(0x1142, 0x0000);
    g_cpu.X = saved_x;
}

/* ---- $00:DFC4: shift the squeezed row to the stamp's column and merge it
 * into the thumbnails at $1144 / $1154 with the column masks. ---- */
void mp_00DFC4(void) {
    uint16_t row = g_cpu.X;                                 /* DF1C's counter */
    uint8_t saved_p = cpu_get_p();
    uint16_t idx = bus_wram_read16(0x112C);
    uint8_t n = bus_read8(g_cpu.DB, (uint16_t)(0xE05E + (idx & 3)));
    do {                                                    /* 4 x 24-bit LSR/ROR */
        for (int k = 0; k < 4; k++) {
            uint16_t a = (uint16_t)(0x1138 + k), b = (uint16_t)(0x113C + k), c = (uint16_t)(0x1140 + k);
            uint8_t va = bus_wram_read8(a), vb = bus_wram_read8(b), vc = bus_wram_read8(c);
            bus_wram_write8(a, (uint8_t)(va >> 1));
            bus_wram_write8(b, (uint8_t)((vb >> 1) | ((va & 1) << 7)));
            bus_wram_write8(c, (uint8_t)((vc >> 1) | ((vb & 1) << 7)));
        }
    } while (--n != 0);

    uint16_t x = (uint16_t)((idx & 3) << 1);
    bus_wram_write16(0x112E, bus_read16(g_cpu.DB, (uint16_t)(0xE062 + x)));
    bus_wram_write16(0x1132, bus_read16(g_cpu.DB, (uint16_t)(0xE06A + x)));
    bus_wram_write16(0x1136, bus_read16(g_cpu.DB, (uint16_t)(0xE072 + x)));

    x = (uint16_t)(idx * 6);
    for (uint16_t y = 0; y != 0x000C; y = (uint16_t)(y + 4), x = (uint16_t)(x + 2)) {
        uint16_t r = (uint16_t)(row + 1);
        if (r >= 0x0008) r = (uint16_t)(r + 0x00F8);
        uint16_t pos = (uint16_t)((uint16_t)(r << 1) + bus_read16(g_cpu.DB, (uint16_t)(0xE07A + x)));
        if (pos & 0x8000) continue;                         /* BMI: no column */
        /* LDA/STA abs,X go through the data bank: for stamp 15 the bogus
         * column offset points outside WRAM ($00:4C53, I/O space), where
         * the ROM's writes land nowhere. */
        uint8_t db = g_cpu.DB;
        uint16_t m = bus_wram_read16((uint16_t)(0x112E + y));
        uint16_t a1 = (uint16_t)(0x1144 + pos), a2 = (uint16_t)(0x1154 + pos);
        bus_write16(db, a1, (uint16_t)((bus_read16(db, a1) & m) | bus_wram_read16((uint16_t)(0x1138 + y))));
        bus_write16(db, a2, (uint16_t)((bus_read16(db, a2) & m) | bus_wram_read16((uint16_t)(0x113A + y))));
    }
    cpu_set_p(saved_p);
    g_cpu.X = row;
}

/* ---- $00:DF1C ---- */
void mp_00DF1C(void) {
    for (g_cpu.X = 1; g_cpu.X != 0x000D; g_cpu.X++) {
        call_jsr(0x00DF2C);
        call_jsr(0x00DFC4);
    }
}

/* ---- $00:DE8E ---- */
void mp_00DE8E(void) {
    for (int x = 0x07FE; x >= 0; x -= 2)
        bus_wram_write16((uint16_t)(0x1144 + x), read_long16(0x05F800u + (uint16_t)x));
    bus_wram_write16(0x00A6, 0x00F0);
    for (int i = 0x000F; i >= 0; i--) {
        bus_wram_write16(0x112C, (uint16_t)i);
        CPU_SET_A16((uint16_t)i);
        g_cpu.S = (uint16_t)(g_cpu.S - 2);                  /* PHA */
        call_jsr(0x00E0D4);
        call_jsr(0x00DF1C);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);                  /* PLA */
    }
    CPU_SET_A16(0xFFFF);
}

/* ---- $01:D421: load the stamp page's graphics: for the current tool
 * ($AA) the list at $01:DB53 + $01:DB31[tool] of source offsets (ends with
 * a negative one; $5400/$5A00 are moved by the page offset $0999 except
 * for tool $0F) - each 32 words from $03:8000/$8200/$8400 to $7F:8000/
 * $8200/$8400 + Y, 8 per row with a $400 gap after each row. Then the
 * five DMA words at $01:D4AD, waiting a frame first if the queue is busy,
 * and one frame. */
static void stamp_page_tiles(void) {
    uint16_t aa = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA));
    uint16_t x = bus_read16(0x01, (uint16_t)(0xDB31 + (uint16_t)(aa << 1)));
    uint16_t y = 0, n = 0;
    for (;;) {
        uint16_t v = bus_read16(0x01, (uint16_t)(0xDB53 + x));
        if (v & 0x8000) break;
        if ((v == 0x5400 || v == 0x5A00) && bus_wram_read16(0x00AA) != 0x000F)
            v = (uint16_t)(v + bus_wram_read16(0x0999));
        uint16_t sx = v;
        do {
            bus_write16(0x7F, (uint16_t)(0x8000 + y), read_long16(0x038000u + sx));
            bus_write16(0x7F, (uint16_t)(0x8200 + y), read_long16(0x038200u + sx));
            bus_write16(0x7F, (uint16_t)(0x8400 + y), read_long16(0x038400u + sx));
            sx = (uint16_t)(sx + 2);
            y = (uint16_t)(y + 2);
        } while (y & 0x003F);
        x = (uint16_t)(x + 2);
        n++;
        if (!(n & 7)) y = (uint16_t)(y + 0x0400);
    }
    uint8_t db = g_cpu.DB;
    if (bus_read16(db, 0x0204) != 0) call_jsl(0x01E2CE);
    for (int i = 8; i >= 0; i -= 2) bus_write16(db, (uint16_t)(0x0182 + i), bus_read16(0x01, (uint16_t)(0xD4AD + i)));
    bus_write16(db, 0x0202, bus_read16(db, 0x0202) + 1);
    call_jsl(0x01E2CE);
}

/* ---- $01:D409: show stamp page $0997 (table at $01:D411: every entry
 * sets the page, its offset $40 * page, redraws $D56D/$D597 and loads the
 * graphics). */
void mp_01D409(void) {
    uint8_t db = g_cpu.DB;
    uint16_t k = bus_read16(db, 0x0997);
    uint16_t h = bus_read16(0x01, (uint16_t)(0xD411 + (uint16_t)(k << 1)));
    if (k >= 8 || h != (uint16_t)(0xD4BB + (k ? 0x11 + 0x17 * (k - 1) : 0))) {   /* not one of the eight */
        tail_jmp(0x010000u | h);
        return;
    }
    bus_write16(db, 0x0997, k);
    bus_write16(db, 0x0999, (uint16_t)(k << 6));
    call_jsl(0x01D56D);
    call_jsl(0x01D597);
    stamp_page_tiles();
}

/* ---- $01:D3BA: the stamp page arrows (left/right button = page -/+, 4 or
 * 8 pages with $099F), sound 6, $058B set while the page loads; the palette
 * ($00:A44D) and $00:E64B unless the tool is 6 or 7. */
void mp_01D3BA(void) {
    uint8_t db = g_cpu.DB;
    uint16_t b = bus_read16(db, 0x04CA);
    if (!(b & 0x0044)) return;
    uint16_t pg = bus_read16(db, 0x0997);
    pg = (uint16_t)((b & 0x0040) ? pg + 1 : pg - 1);
    bus_write16(db, 0x0997, pg);
    CPU_SET_A16(0x0006); call_jsl(0x01D368);
    uint16_t m = bus_read16(db, 0x099F) ? 0x0007 : 0x0003;
    bus_write16(db, 0x0997, (uint16_t)(m & bus_read16(db, 0x0997)));
    bus_write16(db, 0x058B, 0x0001);
    call_jsr(0x01D409);
    bus_write16(db, 0x058B, 0x0000);
    uint16_t t = bus_read16(db, 0x04D0);
    if (t == 0x0006 || t == 0x0007) return;
    call_jsl(0x00A44D);
    func_table_call(0x00E64B);                              /* JML: it returns for us */
}

/* ---- $01:D3AB: queue sound $1012 + 2, or $1D when $1012 is 0, on track 0
 * (JMP $D2BF). */
void mp_01D3AB(void) {
    uint16_t a = bus_read16(g_cpu.DB, 0x1012);
    CPU_SET_A16(a ? (uint16_t)(a + 2) : 0x001D);
    tail_jmp(0x01D2BF);
}

void mp_register_stampbar(void) {
    func_table_register(0x01D409, mp_01D409);
    func_table_register(0x01D3BA, mp_01D3BA);
    func_table_register(0x01D3AB, mp_01D3AB);

    func_table_register(0x00DE8E, mp_00DE8E);
    func_table_register(0x00E0D4, mp_00E0D4);
    func_table_register(0x00E167, mp_00E167);
    func_table_register(0x00DF1C, mp_00DF1C);
    func_table_register(0x00DF2C, mp_00DF2C);
    func_table_register(0x00DF95, mp_00DF95);
    func_table_register(0x00DFC4, mp_00DFC4);
}
