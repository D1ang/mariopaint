/*
 * Mario Paint — Remaining boot chain helpers and utility routines.
 *
 * This file contains the miscellaneous routines needed to complete
 * the boot chain and game initialization:
 *   - Fade out, RNG read, sprite wrappers
 *   - Canvas palette/tool/stamp initialization
 *   - Title screen display setup
 *   - SRAM initialization
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <snesrecomp/func_table.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $01:E7C9 — Fade out (brightness ramp down)
 *
 * Ramps brightness from current level to 0, then sets force blank.
 * Complement of E794 (fade in).
 * ======================================================================== */
void mp_01E7C9(void) {
    uint8_t disp = bus_wram_read8(0x0104);
    uint8_t brt = disp & 0x0F;

    if (brt == 0) goto done;

    while (brt > 0 && !g_quit) {
        bus_wram_write8(0x04BD, 0x01);
        while (bus_wram_read8(0x04BD) != 0 && !g_quit) {
            mp_01E2CE();
            uint8_t t = bus_wram_read8(0x04BD);
            if (t > 0) bus_wram_write8(0x04BD, t - 1);
        }
        disp = bus_wram_read8(0x0104);
        disp--;
        bus_wram_write8(0x0104, disp);
        brt = disp & 0x0F;
    }

done:
    /* Set force blank */
    disp = bus_wram_read8(0x0104);
    disp |= 0x80;
    bus_wram_write8(0x0104, disp);
    if (!g_quit) mp_01E2CE();
}

/* ========================================================================
 * $01:E20C — Read random number from RNG table
 *
 * Advances the RNG index ($044A), performs shuffle at wraparound,
 * and returns the value from the RNG table at $044C.
 * ======================================================================== */
void mp_01E20C(void) {
    uint16_t idx = bus_wram_read16(0x044A);
    idx++;

    if (idx >= 0x0037) {
        /* Shuffle the table (CODE_01E298) */
        uint16_t modulus = bus_wram_read16(0x0448);
        for (uint16_t yi = 0; yi < 0x006E; yi += 2) {
            uint16_t other_y;
            if (yi < 0x0030) {
                other_y = yi + 0x003E;
            } else {
                other_y = yi - 0x0030;
            }
            uint16_t val = bus_wram_read16(0x044C + yi);
            uint16_t sub = bus_wram_read16(0x044C + other_y);
            if (val < sub) val += modulus;
            val -= sub;
            bus_wram_write16(0x044C + yi, val);
        }
        /* STZ $044A, but the lookup uses the A that $E298 returns: its
         * closing PLA/PLA leave the loop's last index, $6C, so this draw
         * reads $044C + $D8 = $0524, past the table, as the ROM does. */
        bus_wram_write16(0x044A, 0x0000);
        CPU_SET_A16(bus_wram_read16(0x044C + 0x006C * 2));
        return;
    }

    bus_wram_write16(0x044A, idx);
    CPU_SET_A16(bus_wram_read16(0x044C + idx * 2));
}

/* ========================================================================
 * $00:8A12 — Wrapper: calls 008A16 (BG2 tilemap fill)
 * ======================================================================== */
void mp_008A12(void) {
    mp_008A16();
}

/* ---- $00:A2CC (8-bit inside): A = tool. Waits out a pending DMA queue,
 * then queues the tool's palette records: the same tool again $A322 (10
 * bytes), else $A319 (19 bytes), for tool 1 first $A32C (55 bytes, one
 * frame of its own); one frame. */
void mp_00A2CC(void) {
    uint8_t p = cpu_get_p();
    op_sep(0x30);
    uint8_t tool = CPU_A8();
    uint8_t db = g_cpu.DB;
    bus_write8(db, 0x0202, 0);
    if (bus_read8(db, 0x0204) != 0) call_jsl(0x01E2CE);
    uint16_t src, n;
    if (tool == bus_read8(db, 0x09A9)) { src = 0xA322; n = 0x0A; }
    else {
        if (tool == 0x01) {
            for (int x = 0x36; x >= 0; x--) bus_write8(db, (uint16_t)(0x0182 + x), bus_read8(db, (uint16_t)(0xA32C + x)));
            bus_write8(db, 0x0202, bus_read8(db, 0x0202) + 1);
            call_jsl(0x01E2CE);
        }
        src = 0xA319; n = 0x13;
    }
    for (int x = n - 1; x >= 0; x--) bus_write8(db, (uint16_t)(0x0182 + x), bus_read8(db, (uint16_t)(src + x)));
    bus_write8(db, 0x0202, bus_read8(db, 0x0202) + 1);
    call_jsl(0x01E2CE);
    CPU_SET_A8(tool);
    g_cpu.X = 0x00FF;
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* ========================================================================
 * $00:9FC4 — switch tool, with stack arguments. The caller pushes n words,
 * then the tool ($00AA) and n, and JSRs; with D = S after PHD these are
 * $05 = n, $07 = tool, $09.. = the words. On the way out the routine moves
 * its return address over that block (TSC + 2n + 8; PLD; PLY; TCS; PHY),
 * so the caller's stack is clean after the RTS.
 *
 * Changing between some tools first animates the bomb icon ($058D = -1
 * for $10 frames, then 1 or 0). Then the tool's toolbar and cursor
 * ($A25B, $A22D), each word through $A0B3 (after pushing the tool's word
 * from $A0EB), its palette ($A2CC), $016C = 0, $09A9 = tool.
 * ======================================================================== */
void mp_009FC4(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    uint16_t s0 = g_cpu.S, dp0 = g_cpu.DP;
    uint16_t d = (uint16_t)(s0 - 2);
    bus_wram_write16((uint16_t)(s0 - 1), dp0);              /* PHD */
    g_cpu.S = d; g_cpu.DP = d;                              /* TSC; TCD */
    W16(0x0589, 0x0001);
    uint16_t tool = R16(0x00AA), old = R16(0x058F);
    int bomb = 0;                                           /* 1: $A01B, 2: $A035 */
    if (tool != old) {
        if (tool == 0x0000) bomb = old == 0x0001 ? 2 : 0;
        else if (tool == 0x0007) bomb = old == 0x0000 ? 0 : 2;
        else if (tool == 0x0002) bomb = old == 0x0007 ? 3 : 1;
        else if (tool == 0x0003) bomb = old == 0x0001 ? 1 : 3;
        else if (tool == 0x0004) bomb = old == 0x0003 ? 3 : 1;
        else bomb = 3;                                      /* $A02D */
    }
    if (bomb == 1 || bomb == 2) {
        W16(0x058D, 0xFFFF);
        if (!recomp_frame_wait_possible()) {                /* the ROM waits the frames */
            CPU_SET_A16(0xFFFF);
            recomp_set_redirect(bomb == 1 ? 0x00A021 : 0x00A03B);
            return;
        }
        for (int n = 0x10; n; n--) call_jsl(0x01E2CE);
        W16(0x058D, bomb == 1 ? 0x0001 : 0x0000);
    } else if (bomb == 3) {
        W16(0x058D, 0x0001);
    }
    W16(0x058F, R16(0x00AA));                               /* $A04A */
    uint16_t n2 = (uint16_t)(bus_wram_read16((uint16_t)(d + 5)) << 1);
    bus_wram_write16((uint16_t)(d + 5), n2);
    tool = bus_wram_read16((uint16_t)(d + 7));
    CPU_SET_A16(tool); call_jsr(0x00A25B);
    CPU_SET_A16(tool); call_jsr(0x00A22D);
    if (n2 != 0) {
        uint16_t w = R16(0xA0EB + (uint16_t)(tool << 1));
        bus_wram_write16((uint16_t)(g_cpu.S - 1), w);       /* PHA */
        g_cpu.S = (uint16_t)(g_cpu.S - 2);
        for (uint16_t x = 0; x != n2; x = (uint16_t)(x + 2)) {
            g_cpu.X = x;
            CPU_SET_A16(bus_wram_read16((uint16_t)(d + 9 + x)));
            call_jsr(0x00A0B3);
        }
        g_cpu.S = (uint16_t)(g_cpu.S + 2);                  /* PLA */
    }
    CPU_SET_A16(bus_wram_read16((uint16_t)(d + 7)));
    call_jsr(0x00A2CC);
    W16(0x016C, 0);
    W16(0x09A9, bus_wram_read16((uint16_t)(d + 7)));
    /* TSC; ADC $05; ADC #8; PLD; PLY; TCS; PHY */
    uint16_t ret = bus_wram_read16((uint16_t)(s0 + 1));
    uint16_t ns = (uint16_t)(d + n2 + 8);
    g_cpu.DP = dp0;
    bus_wram_write16((uint16_t)(ns - 1), ret);
    g_cpu.S = (uint16_t)(ns - 2);
    g_cpu.Y = ret;
    CPU_SET_A16(ns);
    W16(0x0589, 0);
#undef R16
#undef W16
}

/* ========================================================================
 * $00:B66C — Load drawing tool pen graphics (foreground)
 *
 * Computes a ROM offset from the tool/palette index in A,
 * reads 128 bytes ($80) of tile data from ROM bank $0A at $8000,
 * and stores into the pen graphics buffer at $1BAA.
 * Then calls $01D56D to queue the pen tile DMA.
 * ======================================================================== */
void mp_00B66C(void) {
    uint16_t idx = CPU_A16();

    /* Compute ROM offset: complex bit manipulation */
    uint16_t row = idx & 0xFFF8;
    row <<= 1;  /* ASL */
    uint16_t col = idx & 0x0007;
    uint16_t offset = (row + col) << 6;  /* * 64 bytes per tile row */

    /* Clamp/adjust for special ranges */
    if (offset >= 0x7800) {
        /* Read from SRAM instead */
        offset -= 0x7800;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x700000u + offset + y);  /* SRAM */
            bus_wram_write16(0x1BAA + y, val);
        }
        offset += 0x0200;  /* Next row */
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x700000u + offset + y);
            bus_wram_write16(0x1BAA + 0x40 + y, val);
        }
    } else {
        if (offset < 0x3800) {
            /* Normal range */
        } else {
            offset += 0x0800;  /* Skip gap */
        }

        /* Read tile data from ROM bank $0A */
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x0A8000u + offset + y);
            bus_wram_write16(0x1BAA + y, val);
        }
        offset += 0x0200;  /* Next row = +$200 in ROM, +$40 in buffer */
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x0A8000u + offset + y);
            bus_wram_write16(0x1BAA + 0x40 + y, val);
        }
    }

    /* Queue pen tile DMA */
    call_jsl(0x01D56D);
}

/* ========================================================================
 * $00:B6F4 — Load drawing tool pen graphics (background)
 *
 * Similar to B66C but for the background pen. Stores at $1B2A.
 * ======================================================================== */
void mp_00B6F4(void) {
    uint16_t idx = CPU_A16();

    uint16_t row = idx & 0xFFF8;
    row <<= 1;
    uint16_t col = idx & 0x0007;
    uint16_t offset = (row + col) << 6;

    if (offset >= 0x7800) {
        offset -= 0x7800;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x700000u + offset + y);
            bus_wram_write16(0x1B2A + y, val);
        }
        offset += 0x0200;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x700000u + offset + y);
            bus_wram_write16(0x1B2A + 0x40 + y, val);
        }
    } else {
        if (offset >= 0x0800) {
            /* offset already OK */
        } else {
            offset += 0x3000;  /* Wrap low addresses */
        }
        offset += 0x0800;

        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x0A8000u + offset + y);
            bus_wram_write16(0x1B2A + y, val);
        }
        offset += 0x0200;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = read_long16(0x0A8000u + offset + y);
            bus_wram_write16(0x1B2A + 0x40 + y, val);
        }
    }

    call_jsl(0x01D597);  /* Background pen transform: $1B2A → $1C2A */
}

/* ========================================================================
 * $00:BA78 — Initialize BG3 animation/scroll buffer
 *
 * Clears $7E:3800 (2KB), sets up initial values for the
 * BG3 scroll animation state.
 * ======================================================================== */
void mp_00BA78(void) {
    uint8_t *wram = bus_get_wram();
    for (int x = 0x07F8; x >= 0; x -= 2) {
        wram[0x3800 + x]     = 0x00;
        wram[0x3800 + x + 1] = 0x00;
    }
    /* Set initial values */
    wram[0x3800] = 0x00; wram[0x3801] = 0x00;
    wram[0x3802] = 0x80; wram[0x3803] = 0x80;

    bus_wram_write16(0x19EA, 0xFFFE);
    bus_wram_write16(0x19EC, 0x0004);
    bus_write16(0x7E, 0x3FFA, 0x0004);
    bus_wram_write16(0x19EE, 0x0000);
    bus_wram_write16(0x19F2, 0x0000);
}

/* ========================================================================
 * $00:E25C — SRAM validation and clear
 *
 * Checks SRAM header at $70:07C0. If invalid, clears SRAM.
 * ======================================================================== */
void mp_00E25C(void) {
    /* Read and validate SRAM header */
    uint16_t header = bus_read16(0x70, 0x07C0);
    func_table_call(0x00E233);  /* Compute expected value */

    /* Compare with stored header */
    if (CPU_A16() == header) return;

    /* Invalid: clear SRAM $70:0000-$07BE */
    for (int x = 0x07BE; x >= 0; x -= 2) {
        bus_write16(0x70, x, 0x0000);
    }
}

/* ========================================================================
 * $00:E64F — build the window/colour-math tables at $0F44-$0FA3 from the
 * palette: entries X = 2..$0E and $42..$4C (two words per colour, Y
 * runs alongside, $60 from X = $40 on). Each colour pair comes from $0EC4
 * (with $0EB8), $1C2A (tool 3 or $09D6 = 2, with $A6 >= $70) or $1CAA,
 * into $1004/$1006 and $100A/$100C; each byte column (1004-1006-1008,
 * 1005-1007-1009, ...) is shifted right 4 bits as one 24-bit value. The
 * first and last entries are fixed ($E706). Entries 2, 8, $0E, $44, $4A
 * get other fill bits. Then the 10 DMA words at $00:E7C6 into the queue,
 * one frame. DB-relative throughout, as the ROM.
 * ======================================================================== */
void mp_00E64F(void) {
    uint8_t p = cpu_get_p();
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
#define R8(o)     bus_read8(db, (uint16_t)(o))
#define W8(o, v)  bus_write8(db, (uint16_t)(o), (uint8_t)(v))
    uint16_t x = 0, y = 0;
    for (;;) {
        if (x == 0x0000 || x == 0x004E) {                   /* $E706 */
            W16(0x1004, 0x000F); W16(0x1006, 0x00FF); W16(0x1008, 0x00F0);
            W16(0x100A, 0x0F0F); W16(0x100C, 0xFFFF); W16(0x100E, 0xF0F0);
        } else {
            uint16_t b1, b2, b3, b4;
            if (R16(0x0EB8) != 0) { b1 = 0x0EC4; b2 = 0x0EE4; b3 = 0x0ED4; b4 = 0x0EF4; }
            else {
                bool hi = false;
                if (R16(0x09D6) == 0x0002 || (R16(0x04D0) & 0x00FF) == 0x0003)
                    hi = bus_read16(0x00, (uint16_t)(g_cpu.DP + 0xA6)) >= 0x0070;
                if (hi) { b1 = 0x1C2A; b2 = 0x1C4A; b3 = 0x1C3A; b4 = 0x1C5A; }
                else    { b1 = 0x1CAA; b2 = 0x1CCA; b3 = 0x1CBA; b4 = 0x1CDA; }
            }
            W16(0x1004, R16(b1 + x)); W16(0x1006, R16(b2 + x));
            W16(0x100A, R16(b3 + x)); W16(0x100C, R16(b4 + x));
            W16(0x1008, 0); W16(0x100E, 0);
            static const uint16_t col[4] = { 0x1004, 0x1005, 0x100A, 0x100B };
            for (int c = 0; c < 4; c++) {                   /* LSR/ROR/ROR x4 */
                uint32_t v = ((uint32_t)R8(col[c]) << 16) | ((uint32_t)R8(col[c] + 2) << 8) | R8(col[c] + 4);
                v >>= 4;
                W8(col[c], v >> 16); W8(col[c] + 2, v >> 8); W8(col[c] + 4, v);
            }
        }
        bool edge = x == 0x0002 || x == 0x0008 || x == 0x000E || x == 0x0044 || x == 0x004A;
        W16(0x0F44 + y, R16(0x1004) | (edge ? 0x80D0 : 0x80F0));
        W16(0x0F64 + y, R16(0x1006));
        W16(0x0F84 + y, R16(0x1008) | (edge ? 0x010B : 0x010F));
        W16(0x0F54 + y, R16(0x100A) | 0xF0F0);
        W16(0x0F74 + y, R16(0x100C));
        W16(0x0F94 + y, R16(0x100E) | 0x0F0F);
        y = (uint16_t)(y + 2);
        x = (uint16_t)(x + 2);
        if (x == 0x0050) break;
        if (x == 0x0010) { x = 0x0040; y = 0x0060; }
    }
    W16(0x0202, 0);
    x = R16(0x0204);
    for (y = 0; y != 0x0014; y = (uint16_t)(y + 2), x = (uint16_t)(x + 2)) W16(0x0182 + x, R16(0xE7C6 + y));
    W16(0x0202, R16(0x0202) + 1);
    g_cpu.X = x; g_cpu.Y = y;
    call_jsl(0x01E2CE);
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
#undef R16
#undef W16
#undef R8
#undef W8
}

/* ========================================================================
 * $00:F39E — Initialize palette display state
 *
 * Fills the $09E4 palette display buffer with $FFFF (all empty),
 * sets up various palette display parameters.
 * ======================================================================== */
void mp_00F39E(void) {
    /* Fill palette display buffer with $FFFF */
    for (int x = 0x023E; x >= 0; x -= 2) {
        bus_wram_write16(0x09E4 + x, 0xFFFF);
    }

    op_lda_imm16(0x0050);                                   /* A is $F921's input */
    bus_wram_write16(0x0C28, 0x0050);
    call_jsr(0x00F921);

    bus_wram_write16(0x0C32, 0x0001);
    bus_wram_write16(0x09E0, 0x0000);
    bus_wram_write16(0x0C26, 0x0000);
    bus_wram_write16(0x0C24, 0x0310);
}

/* ========================================================================
 * $01:8F52 — The canvas opening: the iris in
 *
 * Tilemap $7E:2000 cleared, the title sprites, the screen on, then an
 * expanding circle window ($01:904A into the HDMA table pair, swapped
 * each frame) from radius 1 to $47; a click jumps ahead. Then idle until
 * a click or $05A9, and the circle shrinks back from $47 while the
 * brightness fades with it (radius/4, capped at $0F; $80 = blank at 0).
 * The save-wipe check ($934F) runs on the way.
 * ======================================================================== */
void mp_018F52(void) {
#define R16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
    for (int x = 0x07FE; x >= 0; x -= 2) bus_write16(0x7E, (uint16_t)(0x2000 + x), 0x24E0);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    CPU_SET_A16(0x0028); call_jsl(0x019DFE);
    CPU_SET_A16(0x0028); call_jsl(0x01962C);
    bus_write8(0x00, 0x0104, 0x0F);
    call_jsl(0x01E2CE);
    W16(0x0211, 0x0001);
    bus_write8(0x00, 0x0111, 0x02);
    uint16_t x = 0x0047;
    bool skip = false;
    for (;;) {                                           /* $8F97: opening */
        push_a16(x);
        push_a16(0x0080); push_a16(0x0070);
        push_a16((uint16_t)(0x0048 - x));
        W16(0x0211, R16(0x0211) ^ 0x0003);
        call_jsr(0x01904A);
        bus_write16(0x00, 0x0220, R16(0x0211));
        call_jsl(0x01E2CE);
        x = bus_wram_read16((uint16_t)(g_cpu.S + 1)); g_cpu.S = (uint16_t)(g_cpu.S + 2);
        g_cpu.X = x;
        call_jsr(0x01934F);
        if (bus_read16(0x00, 0x04CA) & 0x0020) { x = (uint16_t)(0x0048 - x); skip = true; break; }
        if (--x == 0) break;
        if (g_quit) return;
    }
    if (!skip) {
        for (;;) {                                       /* $8FDB: wait */
            call_jsl(0x01E06F);
            CPU_SET_A16(0x0028); call_jsl(0x01962C);
            call_jsl(0x01E2CE);
            call_jsr(0x01934F);
            if (bus_read16(0x00, 0x04CA) & 0x0020) break;
            if (bus_read16(0x00, 0x05A9) & 0x00FF) break;
            if (g_quit) return;
        }
        x = 0x0048;
    }
    do {                                                 /* $9002: closing */
        push_a16(x);
        push_a16(0x0080); push_a16(0x0070);
        push_a16((uint16_t)(x - 1));
        W16(0x0211, R16(0x0211) ^ 0x0003);
        call_jsr(0x01904A);
        bus_write16(0x00, 0x0220, R16(0x0211));
        uint16_t b = (uint16_t)((bus_wram_read16((uint16_t)(g_cpu.S + 1)) - 1) >> 2);
        if (b == 0) b = 0x0080; else if (b >= 0x000F) b = 0x000F;
        bus_write8(0x00, 0x0104, (uint8_t)b);
        call_jsl(0x01E2CE);
        x = bus_wram_read16((uint16_t)(g_cpu.S + 1)); g_cpu.S = (uint16_t)(g_cpu.S + 2);
        x--;
        g_cpu.X = x;
        if (g_quit) return;
    } while (x != 0);
    call_jsl(0x01E06F);
#undef R16
#undef W16
}

/* ========================================================================
 * $01:F825 — Load one object's display data: A = object. Four words from
 * $0D:EB22 + 8A go to $1A10 + (8A & $FF) (the ROM masks only the
 * destination index: TAX; AND #$00FF; TAY). DB is 0 meanwhile.
 * ======================================================================== */
void mp_01F825(void) {
    uint16_t x = (uint16_t)(CPU_A16() << 3);
    uint16_t y = x & 0x00FF;
    for (int i = 0; i < 4; i++) {
        bus_wram_write16((uint16_t)(0x1A10 + y), read_long16(0x0DEB22 + x));
        x = (uint16_t)(x + 2);
        y = (uint16_t)(y + 2);
    }
    g_cpu.X = x;
    g_cpu.Y = y;
    CPU_SET_A16(0);
}

/* ========================================================================
 * Register all helper functions.
 * ======================================================================== */
/* ---- $00:A451: the visible palette $0EC4: the colours $1C2A, or with
 * $0EB8 set masked by $1CAA; queue the 10-word DMA records at $00:A49A,
 * one frame. */
void mp_00A451(void) {
    uint8_t db = g_cpu.DB;
#define R16(o)    bus_read16(db, (uint16_t)(o))
#define W16(o, v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    if (R16(0x0EB8) == 0) {
        W16(0x0202, 0);
        for (int x = 0x7E; x >= 0; x -= 2) W16(0x0EC4 + x, R16(0x1C2A + x));
    } else {
        for (int x = 0x7E; x >= 0; x -= 2) W16(0x0EC4 + x, R16(0x1CAA + x) & R16(0x1C2A + x));
    }
    uint16_t x = R16(0x0204);
    for (uint16_t y = 0; y != 0x0014; y += 2, x += 2) W16(0x0182 + x, R16(0xA49A + y));
    W16(0x0204, (uint16_t)(x - 2));
    W16(0x0202, R16(0x0202) + 1);
    g_cpu.X = (uint16_t)(x - 2); g_cpu.Y = 0x0014;
    call_jsl(0x01E2CE);
#undef R16
#undef W16
}
void mp_00A44D(void) { call_jsr(0x00A451); }

/* ---- $00:A525: queue the stamp picture $1994 (4 rows of 2 tiles from
 * $0A:B000 + ((s & 7) + (s & $FFF8) * 2) * 64) to VRAM $46E0, one frame. */
void mp_00A525(void) {
    uint16_t t = bus_read16(g_cpu.DB, 0x1994);
    uint16_t a = (uint16_t)((t & 0xFFF8) << 1);
    uint16_t b = (uint16_t)((t & 0x0007) + a);
    uint16_t src = (uint16_t)(((uint16_t)((b << 8) | (b >> 8)) >> 2) + 0xB000);
    uint16_t sv[5] = { src, 0x000A, 0x46E0, 0x0002, 0x0002 };
    for (int i = 0; i < 5; i++) {
        bus_wram_write8(g_cpu.S, (uint8_t)(sv[i] >> 8));
        bus_wram_write8((uint16_t)(g_cpu.S - 1), (uint8_t)sv[i]);
        g_cpu.S = (uint16_t)(g_cpu.S - 2);
    }
    call_jsl(0x01E3AC);
    bus_write16(g_cpu.DB, 0x0202, bus_read16(g_cpu.DB, 0x0202) + 1);
    call_jsl(0x01E2CE);
}
void mp_00A4AD(void) { call_jsr(0x00A451); call_jsr(0x00A525); }

/* ---- $01:E298: stir the random table: each of the 55 words $044C + i
 * minus the word 31 places on (i + $3E, wrapping as i - $30), modulo
 * $0448. A comes back as the last index ($6C, the closing PLA/PLA). */
void mp_01E298(void) {
    uint8_t db = g_cpu.DB;
    uint16_t b = 0;
    for (uint16_t y = 0; y != 0x006E; y += 2) {
        uint16_t j = y < 0x0030 ? (uint16_t)(y + 0x003E) : (uint16_t)(y - 0x0030);
        b = bus_read16(db, (uint16_t)(0x044C + j));
        uint16_t v = bus_read16(db, (uint16_t)(0x044C + y));
        uint16_t r = (uint16_t)(v - b);
        if (v < b) r = (uint16_t)(r + bus_read16(db, 0x0448));
        bus_write16(db, (uint16_t)(0x044C + y), r);
    }
    g_cpu.Y = 0x006E;
    CPU_SET_A16(0x006C);
}

/* $00:E64B (JSL): the window tables ($E64F) for long callers. */
void mp_00E64B(void) { call_jsr(0x00E64F); }

void mp_register_helpers(void) {
    func_table_register(0x00E64B, mp_00E64B);
    func_table_register(0x01E298, mp_01E298);
    func_table_register(0x00A451, mp_00A451);
    func_table_register(0x00A44D, mp_00A44D);
    func_table_register(0x00A525, mp_00A525);
    func_table_register(0x00A4AD, mp_00A4AD);

    func_table_register(0x01E7C9, mp_01E7C9);
    func_table_register(0x01E20C, mp_01E20C);
    func_table_register(0x008A12, mp_008A12);
    func_table_register(0x009FC4, mp_009FC4);
    func_table_register(0x00A2CC, mp_00A2CC);
    func_table_register(0x00B66C, mp_00B66C);
    func_table_register(0x00B6F4, mp_00B6F4);
    func_table_register(0x00BA78, mp_00BA78);
    func_table_register(0x00E25C, mp_00E25C);
    func_table_register(0x00E64F, mp_00E64F);
    func_table_register(0x00F39E, mp_00F39E);
    func_table_register(0x018F52, mp_018F52);
    func_table_register(0x01F825, mp_01F825);
}
