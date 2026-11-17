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

/* ---- $00:EF21 / $00:EEC9: draw the 4-tile marker (two rows, $40 apart)
 * at X in the staff map at $7E:2C00, clipped at the right edge. The
 * last-of-group column uses tiles $0C76/$0C77 for its third pair. */
/* CMP/CPX: carry = no borrow, N/Z from the difference. */
static inline void cmp16(uint16_t a, uint16_t b) {
    g_cpu.flag_C = a >= b;
    cpu_update_nz16((uint16_t)(a - b));
}

static void staff_marker(uint16_t x, bool last) {
    cmp16(x, 0x0040);
    if (x >= 0x0040) return;
    write_long16(0x7E2C00 + x, 0x0C74);
    write_long16(0x7E2C40 + x, 0x8C74);
    cmp16(x, 0x003E);
    if (x >= 0x003E) return;
    write_long16(0x7E2C02 + x, 0x0C6D);
    write_long16(0x7E2C42 + x, 0x8C6D);
    cmp16(x, 0x003C);
    if (x >= 0x003C) return;
    write_long16(0x7E2C04 + x, last ? 0x0C76 : 0x0C70);
    write_long16(0x7E2C44 + x, last ? 0x0C77 : 0x0C71);
    cmp16(x, 0x003A);
    if (x >= 0x003A) return;
    write_long16(0x7E2C06 + x, 0x4C74);
    write_long16(0x7E2C46 + x, 0xCC74);
}

/* $EE0C/$EE3A: one row of staff sprites, X = right end in song pixels,
 * Y = count - 1, $60 or $80 apart; drawn when on screen (0..$F7). */
static void staff_sprites(uint16_t x, uint16_t y, uint16_t step) {
    do {
        uint16_t scroll = bus_wram_read16(0x09E0);
        if (x >= scroll && (uint16_t)(x - scroll) < 0x00F8) {
            uint16_t sx = (uint16_t)(x - scroll), n = y;
            CPU_SET_A16((uint16_t)(n + 0x01A0));
            g_cpu.X = sx;
            g_cpu.Y = 0x0020;
            call_jsl(0x01F91E);
        }
        x = (uint16_t)(x - step);
        y = (uint16_t)(y - 1);
    } while (!(y & 0x8000));
    g_cpu.X = x;
    g_cpu.Y = y;
}

/* ---- $00:ED76: one frame of the music composer's screen. Marks the note
 * under the eraser, puts Mario (or, in state 5, the fixed sprite set)
 * and the two scroll knobs into OAM, draws the notes, the staff map and
 * the control panel. When not playing ($0EB2 = 0) it also draws the
 * measure sprites ($EE01) and, with the cursor over the measure bar, the
 * marker of the measure under it ($EE60..$EF6D). */
void mp_00ED76(void) {
    call_jsr(0x00F1FD);
    if ((bus_wram_read16(0x1A7E) & 0x00FF) != 0x0005) {
        CPU_SET_A16(0x004D);
        call_jsl(0x01F84F);
    } else {                                                /* $ED8D */
        bus_wram_write16(0x022A, 0xB0C8);
        bus_wram_write16(0x022C, 0x3DE8);
        bus_wram_write16(0x022E, 0xB0D8);
        bus_wram_write16(0x0230, 0x3DEA);
        bus_wram_write16(0x0232, 0xB8E8);
        bus_wram_write16(0x0234, 0x3DFC);
    }
    bus_wram_write16(0x0236, (uint16_t)((uint16_t)((bus_wram_read16(0x0C28) >> 2) + 0x0071) | 0xAC00));
    bus_wram_write16(0x0238, 0x3BDF);
    bus_wram_write16(0x023A, (uint16_t)((uint16_t)((bus_wram_read16(0x09E0) >> 6) + 0x00BF) | 0x9F00));
    bus_wram_write16(0x023C, 0x37EC);
    bus_wram_write16(0x0426, (uint16_t)((bus_wram_read16(0x0426) & 0xF003) | 0x0028));
    bus_wram_write16(0x0446, 0x0018);
    call_jsr(0x00FB5C);
    call_jsr(0x00FE54);
    call_jsl(0x01E998);
    uint16_t a = bus_wram_read16(0x0EB2);
    CPU_SET_A16(a);
    cpu_update_nz16(a);
    if (a != 0) return;

    if (bus_wram_read16(0x0C32) == 0) staff_sprites(0x0BF0, 0x001F, 0x0060);   /* $EE01 */
    else staff_sprites(0x0BD0, 0x0017, 0x0080);                                 /* $EE34 */

    /* $EE60: the cursor must be over the measure bar, not holding the
     * eraser or tool $0E. */
    a = bus_wram_read16(0x04D0);
    CPU_SET_A16(a);
    if (a == 0x000E || a == 0x000F) { cmp16(a, a); return; }
    uint16_t cy = bus_wram_read16(0x04DE), cx = bus_wram_read16(0x04DC);
    if (cy < 0x0085) { CPU_SET_A16(cy); cmp16(cy, 0x0085); return; }
    if (cy >= 0x0093) { CPU_SET_A16(cy); cmp16(cy, 0x0093); return; }
    CPU_SET_A16(cx);
    if (cx & 0x8000) { cpu_update_nz16(cx); return; }
    if (cx >= 0x0100) { cmp16(cx, 0x0100); return; }
    uint16_t scroll = bus_wram_read16(0x09E0);
    uint16_t sum = (uint16_t)(scroll + cx);
    a = (uint16_t)(sum - 0x0040);
    if (a & 0x8000) { CPU_SET_A16(a); cmp16(sum, 0x0040); return; }
    a = (uint16_t)(((a >> 2) & 0xFFF8) + 0x0010);
    CPU_SET_A16(a);
    if (a >= bus_wram_read16(0x0C24)) { cmp16(a, bus_wram_read16(0x0C24)); return; }
    uint16_t col = (uint16_t)(a + 2);                       /* PHA */
    uint16_t fine = scroll & 0x001F;                        /* PHA */
    uint16_t x = (uint16_t)(((uint16_t)((uint16_t)(((uint16_t)(fine + cx) & 0xFFE0) - fine) + 7) & 0xFFF8) >> 2);
    g_cpu.X = x;
    a = read_long16(0x7E2C00 + x);
    if (a == 0x0C74) {                                      /* already drawn: PLA */
        CPU_SET_A16(col);
        g_cpu.flag_C = true;
        cpu_update_nz16(col);
        return;
    }
    bool last = last_of_group(col);                         /* $EEC9: JSR $FDFE */
    CPU_SET_A16(col);
    staff_marker(x, last);
}

/* ---- $00:F6AC: the play cursor / conductor sprite. While playing ($0EA2
 * bit 0 clear) it walks with $0E9E (+$20 per beat, $0E92); otherwise the
 * staff scrolls ($09E2). $0EA6/$0EA4 = its column / screen x. Jumping
 * ($0EAA) follows the arc at $00:F7B4; else the walk frames $14A/$14B
 * (+$2D in mode 4) every $0C30 bit 12. Its OAM entries get priority 3 and
 * their first two words swapped. */
void mp_00F6AC(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    uint16_t oam = R16(0x0446);
    uint16_t x = R16(0x0E9E);
    uint16_t beat = (uint16_t)(((R16(0x0C30) & 0xFF00) >> 8) >> 3);
    if (!(R16(0x0EA2) & 0x0001)) {
        if (R16(0x0E92) != 0) W16(0x0E9E, R16(0x0E9E) + 0x20);
        x = (uint16_t)(beat + R16(0x0E9E));
    } else {
        if (R16(0x0E92) != 0) W16(0x09E2, R16(0x09E2) + 0x20);
        W16(0x09E0, (uint16_t)(beat + R16(0x09E2)));
    }
    W16(0x0EA6, (uint16_t)(x - 8));
    W16(0x0EA4, (uint16_t)(x + R16(0x09E0) - 0x68));
    uint16_t y = 0x0028, a;
    if (R16(0x0EAA) != 0) {
        uint16_t t;
        if (R16(0x0EAA) != 0x0002) {
            W16(0x0EAA, 0x0002);
            W16(0x0EA8, R16(0x0EA4));
            t = 0;
        } else {
            t = (uint16_t)(R16(0x0EA4) - R16(0x0EA8));
            if (t >= 0x001F) { W16(0x0EAA, 0); t = 0x001F; }
        }
        y = bus_read8(db, (uint16_t)(0xF7B4 + t));
        a = 0x014D;
    } else {
        if (R16(0x0C30) & 0x1000) {
            uint16_t f = (uint16_t)(R16(0x0EA0) + 1);
            if (f >= 0x0009) f = 0;
            W16(0x0EA0, f);
        }
        a = R16(0x0EA0) < 0x0004 ? 0x014A : 0x014B;
        if (R16(0x0EA2) == 0x0004) a = (uint16_t)(a + 0x2D);
    }
    CPU_SET_A16(a);
    g_cpu.X = x;
    g_cpu.Y = y;
    call_jsl(0x01F91E);
    for (int k = 0; k < 3; k++)
        W16(0x0228 + oam + 4 * k, R16(0x0228 + oam + 4 * k) | 0x7000);
    uint16_t w = R16(0x0228 + oam);
    W16(0x0228 + oam, R16(0x022C + oam));
    W16(0x022C + oam, w);
    g_cpu.X = oam;
    CPU_SET_A16(w);
#undef R16
#undef W16
}

/* ---- $00:F623: one frame of the composer while it plays: mouse, the
 * panel under the cursor (stop $10-$27, tempo $50-$67, $68-$A7), screen
 * ($9378, $ED76), conductor, frame. Carry set = stop was clicked. */
void mp_00F623(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
    bus_write16(db, 0x0446, 0x0004);
    call_jsr(0x008B48);
    bool stop = false;
    if (!(R16(0x04CA) & 0x0010)) {
        bus_write16(db, 0x0E96, 0);
    } else if (R16(0x0E96) != 0) {
        call_jsr(0x00F8CC);
    } else {
        uint16_t cy = R16(0x04DE), cx = R16(0x04DC);
        if (cy >= 0x00A8 && cy < 0x00BC && cx >= 0x0010) {
            if (cx < 0x0028) {
                if (R16(0x04CA) & 0x0020) {
                    CPU_SET_A16(0x0006);
                    call_jsl(0x01D368);
                    bus_write16(db, 0x0E96, 0);
                    stop = true;
                }
            } else if (cx >= 0x0050) {
                if (cx < 0x0068) call_jsr(0x00F7D4);
                else if (cx < 0x00A8) call_jsr(0x00F827);
            }
        }
    }
    call_jsr(0x009378);
    call_jsr(0x00ED76);
    call_jsr(0x00F6AC);
    call_jsl(0x01E09B);
    call_jsl(0x01E2CE);
    g_cpu.flag_C = stop;
#undef R16
}

/* ---- $00:FB47: Y tiles from A up, as tile pairs (t, t + $10) at
 * $7E:2000 + X and $7E:2040 + X. */
void mp_00FB47(void) {
    uint16_t a = CPU_A16(), x = g_cpu.X, y = g_cpu.Y;
    do {
        write_long16(0x7E2000u + x, a);
        write_long16(0x7E2040u + x, (uint16_t)(a + 0x10));
        x = (uint16_t)(x + 2);
        a++;
        y--;
    } while (y != 0);
    CPU_SET_A16(a); g_cpu.X = x; g_cpu.Y = 0;
    g_cpu.flag_Z = true; g_cpu.flag_N = false;
}

/* ---- $00:FCF4: keep a copy of the song ($09E4-$0C31 to $0C3C). */
void mp_00FCF4(void) {
    for (int x = 0x024C; x >= 0; x -= 2)
        bus_write16(g_cpu.DB, (uint16_t)(0x0C3C + x), bus_read16(g_cpu.DB, (uint16_t)(0x09E4 + x)));
}

/* ---- $00:FE21: X = column, Y = its map offset (DB = $7F): the two
 * end-of-measure marker tiles from $02:C3E0/$02:C420 (+8 for the last
 * column of a group), from the third row of a column on. X, Y kept. */
void mp_00FE21(void) {
    uint16_t x = g_cpu.X, y = g_cpu.Y;
    uint16_t base = last_of_group(x) ? 0x0008 : 0x0000;
    if (y >= 0x0100) {
        uint16_t i = (uint16_t)((((uint16_t)(y - 0x0100) & 0x0060) >> 4) + base);
        bus_write16(g_cpu.DB, (uint16_t)(0x0016 + y), bus_read16(0x02, (uint16_t)(0xC3E0 + i)));
        bus_write16(g_cpu.DB, (uint16_t)(0x0018 + y), bus_read16(0x02, (uint16_t)(0xC420 + i)));
    }
    CPU_SET_A16(base);
    g_cpu.X = x; g_cpu.Y = y;
}

/* ---- $00:FD18: draw the whole song staff into the map in bank $7F:
 * $18D columns ($FD83), and the measure-end marker ($FE21) on columns of
 * the song (below $0C24) whose three notes are not all set (>= 3). */
void mp_00FD18(void) {
    uint8_t db = g_cpu.DB;
    g_cpu.DB = 0x7F;
    uint16_t x = 0, y = 0x0020;
    for (uint16_t n = 0x018D; n; n--) {
        g_cpu.X = x; g_cpu.Y = y;
        CPU_SET_A16(n);
        call_jsr(0x00FD83);
        if ((uint16_t)(x + 2) < bus_wram_read16(0x0C24) && y >= 0x0100) {
            uint16_t t = (uint16_t)(((uint16_t)(y - 0x0100) & 0xFF80) >> 5);
            uint16_t i = (uint16_t)((t >> 1) + t);
            if ((bus_wram_read8((uint16_t)(0x09E4 + i)) < 3) || (bus_wram_read8((uint16_t)(0x09E6 + i)) < 3) ||
                (bus_wram_read8((uint16_t)(0x09E8 + i)) < 3)) {
                g_cpu.X = x; g_cpu.Y = y;
                mp_00FE21();
            }
        }
        y = (uint16_t)(y + 0x20);
        x = (uint16_t)(x + 2);
    }
    g_cpu.X = x; g_cpu.Y = y;
    CPU_SET_A16(0);
    g_cpu.DB = db;
}

#define MR16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define MW16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
static void sound(uint32_t fn, uint16_t a) { CPU_SET_A16(a); call_jsl(fn); }

/* ---- $00:FE9E: clear the BG map ($01:E033 with $3DFF) and the staff
 * picture in bank $7F ($4000 bytes) with what A is then. */
void mp_00FE9E(void) {
    CPU_SET_A16(0x3DFF);
    call_jsl(0x01E033);
    uint16_t a = CPU_A16();
    for (int x = 0x3FFE; x >= 0; x -= 2) write_long16(0x7F0000u + (uint16_t)x, a);
    g_cpu.X = 0xFFFE;
}

/* ---- $00:FF36: the 2x3 tile block at $7E:264A, queue BG upload. */
void mp_00FF36(void) {
    write_long16(0x7E264A, 0x2540); write_long16(0x7E264C, 0x2541);
    write_long16(0x7E268A, 0x2550); write_long16(0x7E268C, 0x2551);
    write_long16(0x7E26CA, 0x2560); write_long16(0x7E26CC, 0x2561);
    CPU_SET_A16(0x2561);
    call_jsl(0x01DE97);
}

/* ---- $00:F3C9: silence: tracks 0 of both sound queues. */
void mp_00F3C9(void) {
    MW16(0x0E94, 0); MW16(0x0E96, 0);
    sound(0x01D328, 0); sound(0x01D348, 0);
}

/* ---- $00:F054: the composer's instrument row ($8D2C picks the slot):
 * 0 nothing, 2 the eraser-like palette swap ($F954), otherwise a click
 * selects instrument Y (its sound, tool $0E8C/$0EB4/$04D0 = Y << 8 | 3). */
void mp_00F054(void) {
    call_jsl(0x01E2F3);
    MW16(0x0E94, 0); MW16(0x0E96, 0);
    call_jsr(0x008D2C);
    uint16_t x = g_cpu.X, a = CPU_A16();
    if (x == 0) return;
    if (x == 2) {                                           /* $F954 */
        call_jsl(0x01E2F3);
        if (!(MR16(0x04CA) & 0x0020)) return;
        sound(0x01D368, 0x0003);
        MW16(0x0E8C, 0x000E); MW16(0x0EB4, 0x000E);
        uint16_t sx = 0, y = 0;
        for (; y != 0x0040; y += 2, sx += 2) {
            uint16_t v = read_long16(0x089C00u + sx);
            MW16(0x1C2A + y, v); MW16(0x1CAA + y, v);
        }
        sx = (uint16_t)(sx + 0x01C0);
        for (; y != 0x0080; y += 2, sx += 2) {
            uint16_t v = read_long16(0x089C00u + sx);
            MW16(0x1C2A + y, v); MW16(0x1CAA + y, v);
        }
        call_jsr(0x00E64F);
        call_jsr(0x00FF36);
        return;
    }
    uint16_t y = a;
    if (!(MR16(0x04CA) & 0x0020)) return;
    sound(0x01D328, (uint16_t)((uint16_t)((y + 1) << 4) | 0x0005));
    uint16_t t = (uint16_t)((((y << 8) | (y >> 8)) & 0xFF00) | 0x0003);
    MW16(0x0E8C, t); MW16(0x0EB4, t); MW16(0x04D0, t);
    bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xA8), (uint16_t)(t & 0xFF00));
    call_jsr(0x0096E1);
    call_jsr(0x00E64F);
    call_jsr(0x00FF36);
}

/* The three notes of song column X ($09E4 + X): each note's sound on its
 * own channel ($D328/$D348/$D368), (note + $100) >> 4 | note. */
static uint16_t play_column(uint16_t x) {
    static const uint32_t ch[3] = { 0x01D328, 0x01D348, 0x01D368 };
    for (int k = 0; k < 3; k++, x = (uint16_t)(x + 2)) {
        uint16_t v = MR16(0x09E4 + x);
        if (v & 0x8000) continue;
        sound(ch[k], (uint16_t)(((uint16_t)((uint16_t)(v + 0x0100) >> 4) | v) & 0x00FF));
    }
    return x;
}
static void walk_step(uint16_t add, uint16_t frame_mod) {
    uint16_t f = (uint16_t)(MR16(0x0EA0) + 1);
    if (f >= frame_mod) f = 0;
    MW16(0x0EA0, f);
    MW16(0x0E9E, (uint16_t)(MR16(0x0E9E) + add));
}

/* ---- $00:F3DE: the play button. Mario walks in, the song plays column by
 * column at the tempo ($0C2A/$0C2C added to $0C2E/$0C30; each overflow of
 * the high byte is a beat, $0E92), the staff scrolls once he reaches the
 * middle ($0EA2 1, 2 at the end), looping when $0C26 is set, until the
 * end or the stop button ($F623 carry); then he walks off. */
void mp_00F3DE(void) {
    call_jsl(0x01E2F3);
    MW16(0x0E94, 0); MW16(0x0E96, 0);
    if (!(MR16(0x04CA) & 0x0020)) return;
    call_jsr(0x00FEB1);
    call_jsr(0x00FF5F);
    MW16(0x0EB2, MR16(0x0EB2) + 1);
    sound(0x01D368, 0x0006);
    CPU_SET_A16(0x0C10); g_cpu.X = 0x0544; g_cpu.Y = 3; mp_00FB47();
    CPU_SET_A16(0x0C33); g_cpu.X = 0x054C; g_cpu.Y = 3; mp_00FB47();
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    uint16_t t = (uint16_t)((uint16_t)(MR16(0x0C24) - 0x10) >> 1);
    MW16(0x0EAE, (uint16_t)((t >> 1) + t));
    sound(0x01D328, 0); sound(0x01D348, 0);
    MW16(0x0C2E, 0); MW16(0x0C30, 0); MW16(0x0E92, 0); MW16(0x0E90, 0);
    MW16(0x0E9E, 0xFFF0); MW16(0x0EA6, 0xFFF0);
    MW16(0x0EA0, 0); MW16(0x0EA2, 0); MW16(0x09E0, 0); MW16(0x09E2, 0);
    bool stop = false;
    do {                                                    /* walk in */
        call_jsr(0x00F623);
        if (g_cpu.flag_C) { stop = true; break; }
        walk_step(2, 9);
    } while ((uint16_t)(MR16(0x0E9E) - 0x0030) & 0x8000);
    if (!stop) {
        if (!((MR16(0x09E4) & MR16(0x09E6) & MR16(0x09E8)) & 0x8000)) MW16(0x0EAA, 0x0001);
        for (;;) {                                          /* $F497 */
            if (MR16(0x0E92) != 0) {
                uint16_t x;
                bool go = true;
                if (MR16(0x0E90) == MR16(0x0EAE)) {
                    if (MR16(0x0C26) == 0) { stop = true; break; }
                    MW16(0x0E90, 0); MW16(0x09E0, 0); MW16(0x09E2, 0); MW16(0x0EA2, 0);
                    MW16(0x0E9E, 0x0050);
                    x = 0;
                } else x = MR16(0x0E90);
                x = play_column(x);
                MW16(0x0E90, x);
                if (x == MR16(0x0EAE)) {
                    if (MR16(0x0C26) == 0) go = false;
                    else x = 0;
                }
                if (go && !((MR16(0x09E4 + x) & MR16(0x09E6 + x) & MR16(0x09E8 + x)) & 0x8000))
                    MW16(0x0EAA, 0x0001);
            }
            MW16(0x0E92, 0);                                /* $F53B */
            uint32_t lo = (uint32_t)MR16(0x0C2A) + MR16(0x0C2E);
            MW16(0x0C2E, (uint16_t)lo);
            uint32_t hi = (uint32_t)MR16(0x0C2C) + MR16(0x0C30) + (lo >> 16);
            MW16(0x0C30, (uint16_t)hi);
            MW16(0x0E92, (uint16_t)(hi >> 16));             /* ROL */
            if (MR16(0x0EA2) == 0) {
                uint16_t a = (uint16_t)((((MR16(0x0C30) & 0xFF00) >> 8) >> 3) + MR16(0x0E9E));
                if (a >= 0x0080) {
                    MW16(0x0E9E, a);
                    MW16(0x09E2, (uint16_t)(-(uint16_t)(a - 0x70)));
                    MW16(0x0EA2, MR16(0x0EA2) + 1);
                }
            } else if (MR16(0x0EA2) != 0x0002) {
                if ((uint16_t)((uint16_t)(MR16(0x09E0) + 0xE2) >> 2) >= MR16(0x0C24)) {
                    MW16(0x0E9E, (uint16_t)(MR16(0x0E9E) - 0x80 + 0x70));
                    MW16(0x0EA2, MR16(0x0EA2) + 1);
                }
            }
            call_jsr(0x00F623);                             /* $F5A6 */
            if (g_cpu.flag_C) { stop = true; break; }
        }
    }
    /* $F5AE: walk off */
    CPU_SET_A16(0x0C30); g_cpu.X = 0x0544; g_cpu.Y = 3; mp_00FB47();
    CPU_SET_A16(0x0C13); g_cpu.X = 0x054C; g_cpu.Y = 3; mp_00FB47();
    call_jsl(0x01DE97);
    if (MR16(0x0EA2) != 0x0001)
        MW16(0x0E9E, (uint16_t)((((MR16(0x0C30) & 0xFF00) >> 8) >> 3) + MR16(0x0E9E)));
    MW16(0x0C2E, 0); MW16(0x0C30, 0); MW16(0x0E92, 0);
    MW16(0x0EA2, 0x0004);
    do {
        call_jsr(0x00F623);
        walk_step(4, 9);
    } while ((uint16_t)(MR16(0x0E9E) - 0x0110) & 0x8000);
    MW16(0x0446, 0x0004);
    mp_00F3C9();
    MW16(0x0EB2, 0);
}
#undef MR16
#undef MW16

static void mpush16(uint16_t v) {
    bus_wram_write8(g_cpu.S, (uint8_t)(v >> 8));
    bus_wram_write8((uint16_t)(g_cpu.S - 1), (uint8_t)v);
    g_cpu.S = (uint16_t)(g_cpu.S - 2);
}
static uint16_t mpull16(void) {
    uint16_t v = bus_wram_read16((uint16_t)(g_cpu.S + 1));
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    return v;
}
/* NMI on for the duration (it stays as it was): PHA of $0122 bit 7. */
static uint16_t nmi_on(void) {
    uint16_t was = (uint16_t)(bus_read16(g_cpu.DB, 0x0122) & 0x0080);
    mpush16(was);
    if (!was) call_jsl(0x01E2F3);
    return was;
}
static void nmi_restore(void) { if (mpull16() == 0) call_jsl(0x01E30E); }

/* Palette for the eraser (tool $0E) from $08:9C00: 32 words, then the 32
 * words $200 bytes on, into both palette buffers. */
static void eraser_palette(void) {
    uint16_t sx = 0, y = 0;
    for (; y != 0x0040; y += 2, sx += 2) {
        uint16_t v = read_long16(0x089C00u + sx);
        bus_write16(g_cpu.DB, (uint16_t)(0x1C2A + y), v); bus_write16(g_cpu.DB, (uint16_t)(0x1CAA + y), v);
    }
    sx = (uint16_t)(sx + 0x01C0);
    for (; y != 0x0080; y += 2, sx += 2) {
        uint16_t v = read_long16(0x089C00u + sx);
        bus_write16(g_cpu.DB, (uint16_t)(0x1C2A + y), v); bus_write16(g_cpu.DB, (uint16_t)(0x1CAA + y), v);
    }
    g_cpu.X = sx; g_cpu.Y = y;
}

/* ---- $00:FEB1: before playing: if the eraser is out, back to the tool
 * in $0EB4 through $9FC4 (args: $0C32 + 1 and, with a panel state, $0EB6
 * + 2), with NMI on meanwhile. */
void mp_00FEB1(void) {
    uint8_t db = g_cpu.DB;
    if (bus_read16(db, 0x0E8C) != 0x000F) return;
    bus_write16(db, 0x0E8C, bus_read16(db, 0x0EB4));
    nmi_on();
    uint16_t aa = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA));
    if (bus_read16(db, 0x0EB6) != 0) {
        mpush16((uint16_t)(bus_read16(db, 0x0EB6) + 2));
        mpush16((uint16_t)(bus_read16(db, 0x0C32) + 1));
        mpush16(aa);
        mpush16(0x0002);
    } else {
        mpush16((uint16_t)(bus_read16(db, 0x0C32) + 1));
        mpush16(aa);
        mpush16(0x0001);
    }
    call_jsr(0x009FC4);
    nmi_restore();
}

/* ---- $00:FF5F: the current instrument's palette ($96E1 with $A8 = the
 * tool's high byte), or the eraser's; with NMI on meanwhile. */
void mp_00FF5F(void) {
    nmi_on();
    uint16_t t = bus_read16(g_cpu.DB, 0x0E8C);
    if (t != 0x000E) {
        bus_write16(0x00, (uint16_t)(g_cpu.DP + 0xA8), (uint16_t)(t & 0xFF00));
        call_jsr(0x0096E1);
    } else {
        eraser_palette();
    }
    call_jsr(0x00E64F);
    nmi_restore();
}

#define MR(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define MW(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
static void loop_tiles(void) {
    CPU_SET_A16(MR(0x0C26) == 0 ? 0x0C16 : 0x0C36);
    g_cpu.X = 0x0554; g_cpu.Y = 3;
    mp_00FB47();
}

/* ---- $00:F7D4: the loop button: toggle $0C26 (undo copy kept), its
 * tiles, redraw the staff, one frame. */
void mp_00F7D4(void) {
    MW(0x0E94, 0); MW(0x0E96, 0);
    if (!(MR(0x04CA) & 0x0020)) return;
    mp_00FCF4();
    call_jsr(0x00FEB1);
    call_jsr(0x00FF5F);
    CPU_SET_A16(0x0006); call_jsl(0x01D368);
    mp_00FCF4();
    MW(0x0C26, MR(0x0C26) ^ 0x0001);
    loop_tiles();
    call_jsl(0x01DE97);
    call_jsr(0x00FD18);
    call_jsl(0x01E2F3);
    call_jsl(0x01E2CE);
}

/* ---- $00:F0A4: a click on the staff. The end-marker tool ($0E) moves the
 * song's end ($0C24); the eraser ($0F) removes the note under the cursor
 * (the others in the column move up) or, on the end column, resets the
 * end to $310; any other tool puts its note there (sound first), unless the
 * column's three slots are taken. Right click in the panel state $099F:
 * clear button ($FA65). Each change keeps an undo copy ($FCF4). */
void mp_00F0A4(void) {
    MW(0x0E94, 0); MW(0x0E96, 0); MW(0x0EB6, 0);
    if (MR(0x099F) != 0 && (MR(0x04CA) & 0x0002)) {
        MW(0x04CA, 0x0020);
        call_jsl(0x01E2F3);                                 /* $FA65 */
        if (!(MR(0x04CA) & 0x0020)) return;
        MW(0x0EB6, 0);
        CPU_SET_A16(0x0004); call_jsl(0x01D368);
        mpush16(0x0006);
        mpush16(bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA)));
        mpush16(0x0001);
        call_jsr(0x009FC4);
        call_jsr(0x00FD06);
        call_jsr(0x00FD18);
        call_jsl(0x01E2CE);
        loop_tiles();
        call_jsl(0x01DE97);
        call_jsl(0x01E2CE);
        call_jsr(0x00FA35);
        return;
    }
    if (!(MR(0x04CA) & 0x0040)) return;
    if (MR(0x04D0) == 0x000E) {
        uint16_t a = (uint16_t)(((uint16_t)(MR(0x04DC) + MR(0x09E0) - 8) >> 2) & 0xFFF8);
        if ((uint16_t)(a - 0x0018) & 0x8000) return;
        if (!((uint16_t)(a - 0x0311) & 0x8000)) return;
        mp_00FCF4();
        MW(0x0C24, a);
        CPU_SET_A16(0x000F); call_jsl(0x01D368);
        call_jsr(0x00FD18);
        return;
    }
    uint16_t a = (uint16_t)(MR(0x04DC) + MR(0x09E0) - 0x40);
    if (a & 0x8000) return;
    uint16_t c = (uint16_t)(a >> 5);
    uint16_t x = (uint16_t)(((c << 1) + c) << 1);
    uint16_t t = (uint16_t)((uint16_t)(MR(0x0C24) - 0x10) >> 1);
    uint16_t last = (uint16_t)((t >> 1) + t);
    if (last == x) {                                        /* the end column */
        if (MR(0x04D0) != 0x000F) return;
        mp_00FCF4();
        CPU_SET_A16(0x000B); call_jsl(0x01D368);
        MW(0x0C24, 0x0310);
        call_jsr(0x00FD18);
        return;
    }
    if (last < x) return;
    uint16_t row = (uint16_t)((uint16_t)(MR(0x04DE) - 0x2C) >> 3);
    if (row >= 0x000D) return;
    uint16_t pitch = (uint16_t)(0x000D - row);
    if (MR(0x04D0) == 0x000F) {                             /* $F1B9: erase */
        uint16_t y = 3;
        for (; y; y--, x = (uint16_t)(x + 2))
            if ((MR(0x09E4 + x) & 0x00FF) == pitch) break;
        if (!y) return;
        mp_00FCF4();
        if (y == 1) { x = (uint16_t)(x - 2); y++; MW(0x09E6 + x, MR(0x09E4 + x)); }
        if (y == 2) { x = (uint16_t)(x - 2); MW(0x09E6 + x, MR(0x09E4 + x)); }
        MW(0x09E4 + x, 0xFFFF);
        CPU_SET_A16(0x000B); call_jsl(0x01D368);
        call_jsr(0x00FD18);
        return;
    }
    uint16_t note = (uint16_t)((MR(0x04D0) & 0xFF00) | pitch);
    CPU_SET_A16((uint16_t)(((uint16_t)((uint16_t)(note + 0x0100) >> 4) | note) & 0x00FF));
    call_jsl(0x01D328);
    x = (uint16_t)(x + 4);
    for (uint16_t y = 3; y; y--, x = (uint16_t)(x - 2)) {
        uint16_t v = (uint16_t)(MR(0x09E4 + x) & 0x00FF);
        if (v == pitch || v == 0x00FF) {
            mp_00FCF4();
            MW(0x09E4 + x, (uint16_t)((MR(0x04D0) & 0xFF00) | pitch));
            call_jsr(0x00FD18);
            return;
        }
    }
}
#undef MR
#undef MW

/* $00:FAB5: on button $20 of $04CA, a fresh song: sound off ($01:E2F3),
 * the play state cleared, the tempo from $0EB4, the jingle, the score
 * $09E4.. filled with $FFFF, tempo $50, the score view ($F921), the three
 * note-mark sprite rows ($FB47), the screen ($01:DE97), a frame, the tool
 * switch to page $0C32 + 1, and the view refresh ($FF5F, $FD18). */
void mp_00FAB5(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    if (!(R16(0x04CA) & 0x0020)) return;
    call_jsl(0x01E2F3);
    W16(0x0EB6, 0); W16(0x0E94, 0); W16(0x0E96, 0);
    W16(0x0E8C, R16(0x0EB4));
    CPU_SET_A16(0x0005); call_jsl(0x01D368);
    call_jsr(0x00FCF4);
    for (int x = 0x023E; x >= 0; x -= 2) W16(0x09E4 + x, 0xFFFF);
    W16(0x0C28, 0x0050);
    CPU_SET_A16(0x0050);                                    /* the tempo for $F921 */
    call_jsr(0x00F921);
    W16(0x09E0, 0); W16(0x0C26, 0);
    W16(0x0C24, 0x0310);
    static const uint16_t rows[3][2] = { {0x0C30,0x0544}, {0x0C13,0x054C}, {0x0C16,0x0554} };
    for (int i = 0; i < 3; i++) {
        CPU_SET_A16(rows[i][0]); g_cpu.X = rows[i][1]; g_cpu.Y = 0x0003;
        call_jsr(0x00FB47);
    }
    W16(0x1A7D, 0xFFFF);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    uint16_t w = (uint16_t)(R16(0x0C32) + 1);
    call_tool_switch(bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xAA)), 1, &w);
    call_jsr(0x00FF5F);
    call_jsr(0x00FD18);
#undef R16
#undef W16
}

void mp_register_music(void) {
    func_table_register(0x00FAB5, mp_00FAB5);
    func_table_register(0x00F7D4, mp_00F7D4);
    func_table_register(0x00F0A4, mp_00F0A4);
    func_table_register(0x00FEB1, mp_00FEB1);
    func_table_register(0x00FF5F, mp_00FF5F);
    func_table_register(0x00FE9E, mp_00FE9E);
    func_table_register(0x00FF36, mp_00FF36);
    func_table_register(0x00F3C9, mp_00F3C9);
    func_table_register(0x00F054, mp_00F054);
    func_table_register(0x00F3DE, mp_00F3DE);

    func_table_register(0x00FB47, mp_00FB47);
    func_table_register(0x00FCF4, mp_00FCF4);
    func_table_register(0x00FE21, mp_00FE21);
    func_table_register(0x00FD18, mp_00FD18);

    func_table_register(0x00F6AC, mp_00F6AC);
    func_table_register(0x00F623, mp_00F623);
    func_table_register(0x00ED76, mp_00ED76);
    func_table_register(0x00FC93, mp_00FC93);
    func_table_register(0x00FDFE, mp_00FDFE);
    func_table_register(0x00FD83, mp_00FD83);
    func_table_register(0x00F1FD, mp_00F1FD);
    func_table_register(0x00FE54, mp_00FE54);
    func_table_register(0x00FB5C, mp_00FB5C);
    func_table_register(0x01E998, mp_01E998);
}
