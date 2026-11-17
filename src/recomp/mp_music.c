/*
 * Mario Paint — music composer, translated from the ROM.
 *
 *   $00:FB5C  draw every visible note
 *   $00:FC93  put one note sprite into the OAM buffer
 *   $00:FD83  draw one column of the staff (bar lines, measure ends)
 *   $00:FDFE    is column X the last of its group? (answer in carry)
 *   $00:F1FD  highlight the note under the eraser cursor
 *   $00:FE54  copy the visible staff to the BG2 map
 *   $01:E998  draw the control panel's button state
 *
 * The song buffer is at $09E4: per column three words, note byte low
 * (pitch), bit 13 = highlighted. $0C24 is the scroll position, $0C26 the
 * time-signature flag, $0C32 + 3 the beats per measure.
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- $00:FC93: A = note (0-$1F), X/Y = screen position. Registers and
 * flags come back as the CMP #$20 left them (PHP/PLP around the body). */
void mp_00FC93(void) {
    uint16_t a = CPU_A16(), sx = g_cpu.X, sy = g_cpu.Y;
    g_cpu.flag_C = a >= 0x0020;
    g_cpu.flag_Z = a == 0x0020;
    g_cpu.flag_N = ((uint16_t)(a - 0x0020) & 0x8000) != 0;
    if (g_cpu.flag_C) return;                               /* BCS $FCEF */
    uint16_t at = bus_wram_read16(0x0446);
    bus_wram_write8((uint16_t)(0x0226 + at), (uint8_t)sx);
    bus_wram_write8((uint16_t)(0x0227 + at), (uint8_t)sy);
    uint16_t tile = (uint16_t)((uint16_t)(((a & 0xFFF8) + a) << 1) + 0x2080);
    bus_wram_write16((uint16_t)(0x0228 + at), tile);
    bus_wram_write16(0x0446, (uint16_t)(at + 4));
    /* High table: large size, plus the 9th X bit when X >= $100. */
    uint16_t i = at >> 2;
    uint16_t hi = (uint16_t)(0x0426 + (i >> 2));
    uint8_t bits = (sx >> 8) ? 0xFF : 0xAA;
    uint8_t mask = bus_read8(0x00, (uint16_t)(0xFCF0 + (i & 3)));
    uint8_t h = bus_wram_read8(hi);
    bus_wram_write8(hi, (uint8_t)(((bits ^ h) & mask) ^ h));
}

/* ---- $00:FDFE: X = column; carry set if it is the last of its group. A
 * is returned unchanged (PHA ... PLA). */
static bool last_of_group(uint16_t x) {
    uint16_t n = (uint16_t)(bus_wram_read16(0x0C32) + 3);
    uint16_t v = (uint16_t)((uint16_t)((uint16_t)(x - 0x000A) >> 3) - 1 + n);
    for (;;) {
        if (v < n) return false;
        if (v == n) return true;
        v = (uint16_t)(v - n);
    }
}
void mp_00FDFE(void) {
    g_cpu.flag_C = last_of_group(g_cpu.X);
    cpu_update_nz16(CPU_A16());                             /* last PLA */
}

/* ---- $00:FD83: X = column kind/position, Y = where the column's 14 tile
 * words go (data bank). Tiles come from $02:C100 + X, $40 apart. */
void mp_00FD83(void) {
    uint16_t x0 = g_cpu.X, y0 = g_cpu.Y, x = x0;
    if (x < 0x0006) goto copy;
    if (x == 0x0006) { x = 0x000A; goto copy; }
    if (x < 0x000C) {
        if (bus_wram_read16(0x0C26) == 0) { x = 0x000A; goto copy; }
        x = (uint16_t)(x - 2);
        goto copy;
    }
    if ((x & 0xFFFC) == bus_wram_read16(0x0C24)) {          /* $FD9B */
        uint16_t a = x & 0x0002;
        if (bus_wram_read16(0x0C26) != 0) a = (uint16_t)(a + 4);
        if (last_of_group(x)) a = (uint16_t)(a + 8);
        x = (uint16_t)(a + 0x10);
    } else if (((uint16_t)(x - 0x000A) & 0x0007) != 0) {    /* $FDC7 */
        x = 0x000A;
    } else {
        uint16_t a = 0x000C;
        if (last_of_group(x)) a = (uint16_t)(a + 2);
        x = a;
    }
copy: {                                                     /* $FDE1 */
        uint16_t y = y0;
        do {
            bus_write16(g_cpu.DB, y, bus_read16(0x02, (uint16_t)(0xC100 + x)));
            x = (uint16_t)(x + 0x40);
            y = (uint16_t)(y + 2);
        } while ((y & 0x001F) != 0x001C);
    }
    g_cpu.X = x0;
    g_cpu.Y = y0;
}

/* ---- $00:F1FD ---- */
void mp_00F1FD(void) {
    for (int x = 0x023E; x >= 0; x -= 2)                    /* clear all marks */
        bus_wram_write8((uint16_t)(0x09E5 + x), (uint8_t)(bus_wram_read8((uint16_t)(0x09E5 + x)) & 0xDF));
    if (bus_wram_read16(0x04D0) != 0x000F) return;          /* eraser only */
    if (bus_wram_read16(0x016C) & 0x0008) return;
    uint16_t a = (uint16_t)(bus_wram_read16(0x04DC) + bus_wram_read16(0x09E0) - 0x0040);
    if (a & 0x8000) return;
    uint16_t col = a >> 5;
    uint16_t x = (uint16_t)(col * 6);
    uint16_t b = (uint16_t)((uint16_t)(bus_wram_read16(0x0C24) - 0x0010) >> 1);
    uint16_t c = (uint16_t)((b >> 1) + b);
    if (c <= x) return;                                     /* BEQ / not BCS */
    uint16_t row = (uint16_t)((uint16_t)(bus_wram_read16(0x04DE) - 0x002C) >> 3);
    if (row >= 0x000D) return;
    uint16_t pitch = (uint16_t)(0x000D - row);              /* EOR #$FFFF; ADC #$0E */
    for (int k = 0; k < 3; k++, x = (uint16_t)(x + 2)) {
        uint16_t w = bus_wram_read16((uint16_t)(0x09E4 + x));
        if ((w & 0x00FF) == pitch) {
            bus_wram_write16((uint16_t)(0x09E4 + x), (uint16_t)(w | 0x2000));
            break;
        }
    }
    g_cpu.X = x;
}

/* ---- $00:FE54: copy the visible 32 columns x 14 rows of the staff from
 * the song picture in bank $7F (starting at column $09E0 / 8) to the BG2
 * map at $7E:2940, and queue the BG2 upload. $0172 = fine scroll. */
void mp_00FE54(void) {
    uint16_t scroll = bus_wram_read16(0x09E0);
    bus_wram_write16(0x0172, (uint16_t)(scroll & 0x0007));
    uint16_t y = (uint16_t)((scroll & 0xFFF8) << 2), x = 0;
    for (;;) {
        bus_write16(0x7E, (uint16_t)(0x2940 + x), bus_read16(0x7F, y));
        y = (uint16_t)(y + 2);
        x = (uint16_t)(x + 0x40);
        if ((x & 0x07C0) != 0x0380) continue;               /* next row */
        x = (uint16_t)(x - 0x037E);                         /* next column */
        y = (uint16_t)(y + 4);
        if (x == 0x0040) break;
    }
    g_cpu.X = x;
    g_cpu.Y = y;
    call_jsl(0x01DEB2);
}

/* ---- $01:E998: control-panel buttons. $0EB6 (0 = idle, n = state) and,
 * when $0EB2 is set, $016C bit 3 pick one of the tile sets at $01:EA07;
 * its 3 rows of 8 tiles go to $7E:2662/26A2/26E2 and the three DMA
 * records at $01:EB65 are queued. */
void mp_01E998(void) {
    uint16_t state = bus_wram_read16(0x0EB6), i = 0;
    if (state != 0) {
        bool lit = bus_wram_read16(0x0EB2) != 0 && (bus_wram_read16(0x016C) & 0x0008);
        i = (uint16_t)(((uint16_t)((state - 1) << 1) + (lit ? 1 : 0) + 1) << 1);   /* ROL; INC; ASL */
    }
    uint16_t set = bus_read16(0x01, (uint16_t)(0xEA07 + i));
    for (int x = 0x0E; x >= 0; x -= 2) {
        bus_write16(0x7E, (uint16_t)(0x2662 + x), bus_read16(0x01, (uint16_t)(set + x)));
        bus_write16(0x7E, (uint16_t)(0x26A2 + x), bus_read16(0x01, (uint16_t)(set + 0x10 + x)));
        bus_write16(0x7E, (uint16_t)(0x26E2 + x), bus_read16(0x01, (uint16_t)(set + 0x20 + x)));
    }
    bus_wram_write16(0x0202, 0x0000);
    uint16_t at = bus_wram_read16(0x0204);
    for (uint16_t y = 0; y < 0x001C; y += 2)
        bus_wram_write16((uint16_t)(0x0182 + at + y), bus_read16(0x01, (uint16_t)(0xEB65 + y)));
    bus_wram_write16(0x0204, (uint16_t)(at + 0x001B));      /* DEX: onto the terminator */
    bus_wram_write16(0x0202, 0x0001);
    g_cpu.X = (uint16_t)(at + 0x001B);
    g_cpu.Y = 0x001C;
    CPU_SET_A16(0x0001);
}

/* Arithmetic shift right (CMP #$8000; ROR). */
static inline uint16_t asr(uint16_t v) { return (uint16_t)((v >> 1) | (v & 0x8000)); }

/* ---- $00:FB5C: draw every visible note. Columns are 32 px apart from
 * the scroll position; each holds three note slots (6 bytes in $09E4).
 * Notes on the same line within a column are pushed right/up by $0C38 so
 * they do not cover each other; the note at the play cursor ($0EA6)
 * bounces by the offsets at $FC73 (not in mode $0EA2 = 4). */
void mp_00FB5C(void) {
    uint16_t a = (uint16_t)((bus_wram_read16(0x09E0) - 0x0040) & 0xFFE0);
    uint16_t t = asr(asr(asr(a)));
    uint16_t y = (uint16_t)(asr(t) + t);                    /* column * 6 */
    uint16_t limit = 0x0100;
    uint16_t right = (uint16_t)((uint16_t)(bus_wram_read16(0x0C24) << 2) - bus_wram_read16(0x09E0));
    if ((uint16_t)(right - 0x0100) & 0x8000) limit = (uint16_t)(right + 1);
    bus_wram_write16(0x0E8A, limit);
    uint16_t x = (uint16_t)(0x0008 - (bus_wram_read16(0x09E0) & 0x001F));
    while ((uint16_t)(x - bus_wram_read16(0x0E8A)) & 0x8000) {   /* $FC6A */
        bus_wram_write16(0x0C3A, 0x0000);                   /* $FBA3 */
        for (int n = 3; n > 0; n--, y = (uint16_t)(y + 2)) {
            if (y & 0x8000) continue;                       /* before the song */
            if ((uint16_t)(x - 0xFFF0) & 0x8000) continue;  /* off the left */
            uint16_t attr = bus_wram_read8((uint16_t)(0x09E5 + y));
            uint16_t pitch = bus_wram_read8((uint16_t)(0x09E4 + y));
            uint16_t sy = (uint16_t)((uint16_t)((uint16_t)(0x000D - pitch) << 3) + 0x0028);
            uint16_t k = (uint16_t)(bus_wram_read16(0x0C3A) + 1);
            bus_wram_write16(0x0C3A, k);
            if (k == 3) {                                   /* $FC08 */
                if (sy == bus_wram_read16(0x0C34) || sy == bus_wram_read16(0x0C36))
                    bus_wram_write16(0x0C38, (uint16_t)(bus_wram_read16(0x0C38) + 2));
                else
                    bus_wram_write16(0x0C38, 0x0000);
            } else if (k == 2) {                            /* $FBF6 */
                bus_wram_write16(0x0C36, sy);
                if (sy == bus_wram_read16(0x0C34)) bus_wram_write16(0x0C38, 0x0002);
            } else {
                bus_wram_write16(0x0C34, sy);
                bus_wram_write16(0x0C36, 0x0000);
                bus_wram_write16(0x0C38, 0x0000);
            }
            if (bus_wram_read16(0x0EA2) != 0x0004) {        /* $FC23: bounce */
                uint16_t d = (uint16_t)(bus_wram_read16(0x0EA6) - x);
                if (!(d & 0x8000) && d < 0x0020)
                    sy = (uint16_t)(sy + bus_read8(0x00, (uint16_t)(0xFC73 + d)));
            }
            uint16_t shift = bus_wram_read16(0x0C38);       /* $FC45 */
            CPU_SET_A16(attr);
            g_cpu.X = (uint16_t)(x + shift + shift);
            g_cpu.Y = (uint16_t)(sy - shift);
            call_jsr(0x00FC93);
        }
        x = (uint16_t)(x + 0x0020);                         /* $FC64 */
    }
    g_cpu.X = x;
    g_cpu.Y = y;
}

void mp_register_music(void) {
    func_table_register(0x00FC93, mp_00FC93);
    func_table_register(0x00FDFE, mp_00FDFE);
    func_table_register(0x00FD83, mp_00FD83);
    func_table_register(0x00F1FD, mp_00F1FD);
    func_table_register(0x00FE54, mp_00FE54);
    func_table_register(0x00FB5C, mp_00FB5C);
    func_table_register(0x01E998, mp_01E998);
}
