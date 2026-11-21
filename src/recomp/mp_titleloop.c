/*
 * Mario Paint — Title screen animation loop and Bank 0F routines.
 *
 * The title screen has a 12-state animation machine that runs
 * the Nintendo/Mario Paint logo sequence, then waits for input
 * or times out into the demo playback.
 *
 * Bank $0F contains the demo playback system that feeds
 * recorded mouse/button data into the input state.
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
 * $01:82B1 — Title screen cursor position update
 *
 * Applies mouse displacement ($04C6/$04C8) to cursor position
 * ($04DC/$04DE), clamped to title screen bounds.
 * ======================================================================== */
void mp_0182B1(void) {
    /* The mouse deltas $04C6/$04C8 (low byte, sign-extended) added to the
     * title cursor $04DC/$04DE, kept only inside X $18..$E7 and Y $20..$BF.
     * The bounds tests are the ROM's: the N flag of the comparison. */
    static const uint16_t lim[2][2] = { { 0x00E8, 0x0018 }, { 0x00C0, 0x0020 } };
    for (int i = 0; i < 2; i++) {
        uint16_t d = bus_read16(0x00, (uint16_t)(0x04C6 + 2 * i)) & 0x00FF;
        if (d & 0x0080) d |= 0xFF00;
        uint16_t v = (uint16_t)(d + bus_read16(0x00, (uint16_t)(0x04DC + 2 * i)));
        CPU_SET_A16(v);
        if (!((uint16_t)(v - lim[i][0]) & 0x8000)) continue;      /* CMP #hi : BPL */
        if ((uint16_t)(v - lim[i][1]) & 0x8000) continue;         /* CMP #lo : BMI */
        bus_write16(0x00, (uint16_t)(0x04DC + 2 * i), v);
    }
}

/* ========================================================================
 * $01:82F6 — Title screen cursor sprite draw
 *
 * Draws the cursor sprite at the current position using
 * the sprite ID stored at $7F:0005.
 * ======================================================================== */
void mp_0182F6(void) {
    uint16_t x = bus_wram_read16(0x04DC);
    uint16_t y = bus_wram_read16(0x04DE);
    uint16_t sprite_id = bus_read16(0x7F, 0x0005);

    g_cpu.X = x;
    g_cpu.Y = y;
    CPU_SET_A16(sprite_id);
    mp_01F91E();
}

/* ========================================================================
 * $01:8308 — Title screen state machine
 *
 * 12-state animation machine that runs the title screen sequence.
 * State stored at $7F:0001. Each state handles part of the
 * logo animation, then advances to the next.
 *
 * Most states involve complex sprite animations. We dispatch
 * each to func_table_call so they work progressively.
 * ======================================================================== */
void mp_018308(void) {
    /* LDA $0001 (DB = $7F here); ASL; TAX; JMP ($8310,x) in bank $01 */
    uint16_t x = (uint16_t)(bus_read16(g_cpu.DB, 0x0001) << 1);
    g_cpu.X = x;
    uint16_t h = bus_read16(0x01, (uint16_t)(0x8310 + x));
    CPU_SET_A16(x);
    tail_jmp(0x010000u | h);
}

/* ========================================================================
 * $01:8C43 — Title screen sprite setup
 *
 * Renders the 24 title screen sprites (tool icons, text, etc.).
 * Skips the sprite at index A (used to hide a specific one).
 *
 * Sprite data tables at $01:8C6F (X), $01:8C87 (Y), $01:8C9F (tile).
 * ======================================================================== */
void mp_018C43(void) {
    uint16_t skip_idx = CPU_A16();

    for (int x = 0x17; x >= 0; x--) {
        if ((uint16_t)x == skip_idx) continue;

        /* Read sprite X, Y, tile from ROM tables */
        uint8_t spr_x = bus_read8(0x01, 0x8C6F + x);
        uint8_t spr_y = bus_read8(0x01, 0x8C87 + x);
        uint8_t spr_tile = bus_read8(0x01, 0x8C9F + x);

        g_cpu.X = spr_x;
        g_cpu.Y = spr_y;
        CPU_SET_A16(spr_tile & 0xFF);
        mp_01F91E();
    }
}

/* ========================================================================
 * $01:8CB7 — Title screen: animate sprite slot 0
 * ======================================================================== */
void mp_018CB7(void) {
    op_lda_imm16(0x0000);
    func_table_call(0x01962C);
}

/* Set by $018CBF when the title screen should hand over to the canvas.
 * The original does this with a stack trick (LDA $0003; TCS; RTS at $01:8D2B)
 * that unwinds straight out of the title loop; a flag is the C equivalent.
 * This used to borrow g_quit, which conflated "leave the title" with "close
 * the window". */
static bool s_title_exit = false;

/* ========================================================================
 * $01:8CBF — Title screen input check
 *
 * Checks for player input to skip the title screen:
 *   - P2 controller: L+R+A+B ($9080) → skip to canvas ($0565=0)
 *   - P2 controller: L+R+A+X ($A080) → skip to canvas ($0565=1)
 *   - P2 buttons = $FFFF → skip ($0565=1)
 *   - Mouse click on the logo → skip ($0565=0)
 *
 * If no input, calls E2CE (frame sync) and returns normally.
 * On skip, manipulates the stack to return directly from $018260.
 * ======================================================================== */
void mp_018CBF(void) {
#define L16(o) bus_read16(0x00, (uint16_t)(o))
    int exit = -1;                                       /* $0565 value on the way out */
    if ((L16(0x013C) & 0x9080) && L16(0x0134) == 0x9080) exit = 0;          /* $8D1B */
    else if ((L16(0x013C) & 0xA080) && L16(0x0134) == 0xA080) exit = 1;     /* $8D24 */
    else if (L16(0x0134) == 0xFFFF) exit = 1;
    else if (L16(0x04CA) & 0x0020) {
        /* A click on the start sprite at $0792/$0794 (positions doubled):
         * unsigned window tests, as the ROM does them. */
        uint16_t dy = (uint16_t)((uint16_t)(L16(0x04DE) << 1) - L16(0x0794) + 0x0040);
        uint16_t dx = (uint16_t)((uint16_t)(L16(0x04DC) << 1) - L16(0x0792) + 0x0030);
        if (dy < 0x0048 && dx < 0x0060) exit = 0;
    }
    if (exit < 0) { call_jsl(0x01E2CE); return; }        /* $8D30 */
    bus_write16(0x00, 0x0565, (uint16_t)exit);
    /* $8D2B: LDA $0003; TCS; RTS - straight out of the title loop to the
     * stack point $01:8000 kept in $7F:0003 (DB is $7F here). */
    g_cpu.S = bus_read16(g_cpu.DB, 0x0003);
    CPU_SET_A16(g_cpu.S);
    s_title_exit = true;
#undef L16
}

/* ========================================================================
 * $01:8260 — Title screen loop
 *
 * Each frame: OAM cleared, the cursor moved and drawn, the title state
 * machine, the input check ($8CBF, which leaves the loop itself through
 * the stack point in $7F:0003). Any mouse activity clears the idle count
 * $7F:0411; $0800 idle frames start the attract demo, and the loop leaves
 * the same way, through the stack point.
 * ======================================================================== */
void mp_018260(void) {
#define R16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
    s_title_exit = false;
    for (;;) {
        if (g_quit) return;
        call_jsl(0x01E06F);
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        call_jsr(0x018308);
        if (s_title_exit) break;                         /* a state left through $8CBF */
        call_jsr(0x018CBF);
        if (s_title_exit) break;                         /* S already at the stack point */
        uint16_t act = (uint16_t)(bus_read16(0x00, 0x04C6) | bus_read16(0x00, 0x04C8)
                                  | bus_read16(0x00, 0x04CA));
        if (act != 0) { W16(0x0411, 0); continue; }
        W16(0x0411, R16(0x0411) + 1);
        if (R16(0x0411) == 0x0800) {
            bus_write16(0x00, 0x04DC, 0x0080);
            bus_write16(0x00, 0x04DE, 0x0080);
            call_jsl(0x0FC00E);
            bus_write16(0x00, 0x0448, 0x0100);
            CPU_SET_A16(bus_read16(0x00, 0x0002));
            call_jsl(0x01E238);
            break;
        }
    }
    s_title_exit = false;
    g_cpu.S = R16(0x0003);                               /* LDA $0003; TCS; RTS */
    CPU_SET_A16(g_cpu.S);
#undef R16
#undef W16
}


/* Title-state helpers. DB is $7F in the title loop; the ROM's long
 * $00:xxxx operands are bank 0. */
#define T16(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define TW16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define Z16(o)     bus_read16(0x00, (uint16_t)(o))
#define ZW16(o, v) bus_write16(0x00, (uint16_t)(o), (uint16_t)(v))
#define Z8(o)      (Z16(o) & 0x00FF)

/* $01:834B: title state 2: the sprites, the window ($91FE, X $D0, Y 4),
 * the logo; hovering the right letters plays a sound, the start letters
 * ($FB, or $8E on the second page $0593) move to state 1. */
void mp_01834B(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    g_cpu.X = 0x00D0; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
    call_jsr(0x018CB7);
    bool second = Z8(0x0593) != 0;
    uint16_t v = Z8(0x0797);
    uint16_t snd_at = second ? 0x0083 : 0x00F0, go_at = second ? 0x008E : 0x00FB;
    if (v == snd_at) {
        if (Z8(0x0796) == 0x0006) {
            CPU_SET_A16(second ? 0x004E : 0x004D);
            call_jsl(0x01D368);
        }
    } else if (v == go_at) {
        TW16(0x0001, 0x0001);
    }
}

/* $01:83B6: title state 3: a letter can be grabbed ($079F = $0A near the
 * cursor) and dragged; let go, it runs back along its track ($0219) and
 * snaps home. Loops by itself until $0595, through $8CBF's exit. */
void mp_0183B6(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    call_jsr(0x018CB7);
    call_jsr(0x018CBF);
    if (s_title_exit) return;
    TW16(0x0213, 0);
    for (;;) {
        ZW16(0x0446, 0);                                 /* $83C5 */
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        bool drawn = false;
        uint16_t t = T16(0x0213);
        if (t == 0) {
            if (Z8(0x079F) == 0x000A &&
                (uint16_t)(Z16(0x0792) - Z16(0x079A) + 0x0010) < 0x0020) {
                CPU_SET_A16(0x0006); call_jsl(0x01D348);
                TW16(0x0213, T16(0x0213) + 1);
                TW16(0x0215, Z16(0x079C));
                TW16(0x0219, Z16(0x0794));
                uint16_t x = (uint16_t)(Z8(0x0797) << 2);
                TW16(0x0217, (uint16_t)((bus_read16(0x0F, (uint16_t)(0x8063 + x)) & 0x007F)
                                        + Z16(0x0798)));
            }
        } else {
            if (t < 0x0002) {                            /* $8436: dragging */
                uint16_t v = Z16(0x079C);
                if (v != 0x01E2) {
                    ZW16(0x0794, (uint16_t)(v - T16(0x0215) + Z16(0x0794)));
                    TW16(0x0215, v);
                } else {
                    TW16(0x0213, T16(0x0213) + 1);
                    ZW16(0x0794, 0);
                }
            } else if (t != 0x00C0) {
                TW16(0x0213, (uint16_t)(t + 1));
            } else {                                     /* $846A: back home */
                uint16_t a = (uint16_t)((Z16(0x0794) + 2) & 0x01FE);
                if (a == T16(0x0219)) TW16(0x0213, 0);
                else {
                    a = (uint16_t)((a + 2) & 0x01FE);
                    if (a == T16(0x0219)) TW16(0x0213, 0);
                }
                ZW16(0x0794, a);
            }
            CPU_SET_A16(0x0001); call_jsl(0x01962C);     /* $8489 */
            CPU_SET_A16(0x0001); call_jsr(0x018C43);
            g_cpu.X = (uint16_t)(Z16(0x0792) >> 1);
            g_cpu.Y = (uint16_t)(Z16(0x0794) >> 1);
            CPU_SET_A16(T16(0x0217));
            call_jsl(0x01FA68);
            drawn = true;
        }
        if (!drawn) {                                    /* $84AB */
            CPU_SET_A16(0x0001); call_jsl(0x01962C);
            CPU_SET_A16(0x0001); call_jsr(0x018C43);
            call_jsr(0x018CB7);
        }
        if (Z16(0x079C) == 0) { CPU_SET_A16(0x0050); call_jsl(0x01D368); }
        if (Z8(0x0595)) { TW16(0x0001, 0x0001); return; }
        call_jsl(0x01E09B);
        call_jsr(0x018CBF);
        if (s_title_exit || g_quit) return;
    }
}


/* $01:84E2: title state 4: a click on the logo ($9244 with X = 2, carry
 * clear) plays $51; when $0596 is set, sound $52 and on to state 1. */
void mp_0184E2(void) {
    CPU_SET_A16(0x0002); call_jsl(0x01962C);
    CPU_SET_A16(0x0002); call_jsr(0x018C43);
    call_jsr(0x018CB7);
    if (Z16(0x04CA) & 0x0020) {
        g_cpu.X = 0x0002; call_jsr(0x019244);
        if (!g_cpu.flag_C) { CPU_SET_A16(0x0051); call_jsl(0x01D368); }
    }
    if (Z8(0x0596)) {
        CPU_SET_A16(0x0052); call_jsl(0x01D368);
        TW16(0x0001, 0x0001);
    }
}

/* $01:8521: title state 5: the backdrop colour fades with the count
 * $0207 (a grey level 0..$1E into $0121, its complement into $0007), sent
 * with the DMA record at $01:859B; past zero, sound $54 and state 1. */
void mp_018521(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    call_jsr(0x018CB7);
    uint16_t a = T16(0x0207);
    TW16(0x0207, (uint16_t)(a - 1));
    uint16_t v = a >= 0x007F ? (uint16_t)(0x009D - a) : (a >= 0x001F ? 0x001E : a);
    uint16_t c = (uint16_t)((uint16_t)(v * 33) * 32 + v);   /* the same level in R, G, B */
    TW16(0x0121, c);
    TW16(0x0007, (uint16_t)(0x7BDE - c));
    ZW16(0x0202, 0);
    for (int x = 7; x >= 0; x--)
        ZW16(0x0182 + x, bus_read16(0x01, (uint16_t)(0x859B + x)));
    ZW16(0x0204, 0x0007);
    ZW16(0x0202, 0x0001);
    if (T16(0x0207) & 0x8000) {
        CPU_SET_A16(0x0054); call_jsl(0x01D368);
        TW16(0x0001, 0x0001);
    }
}

/* $01:85A3: title state 6: once $0597 is set, sound $56 and the logo
 * sprites ($023A..) shake for $40 frames by the offsets at $01:8631,
 * with a pause at $3F; then state 1. */
void mp_0185A3(void) {
    CPU_SET_A16(0x0003); call_jsl(0x01962C);
    CPU_SET_A16(0x0004); call_jsr(0x018C43);
    TW16(0x0209, Z16(0x0446));
    call_jsr(0x018CB7);
    if (!Z8(0x0597)) return;
    call_jsr(0x018CBF);
    if (s_title_exit) return;
    CPU_SET_A16(0x0056); call_jsl(0x01D368);
    uint16_t n = 0x0040;
    do {
        push_a16(n);
        for (int x = 0x00C8; x >= 0; x -= 4) {
            uint16_t v = bus_read16(0x01, (uint16_t)(0x8631 + ((x & 0x1C) >> 1)));
            uint16_t o = (uint16_t)(0x023A + x);
            bus_write8(0x00, o, (uint8_t)(bus_read8(0x00, o) + (uint8_t)v));
            bus_write8(0x00, (uint16_t)(o + 1), (uint8_t)(bus_read8(0x00, (uint16_t)(o + 1)) + (uint8_t)(v >> 8)));
        }
        ZW16(0x0446, T16(0x0209));
        call_jsr(0x018CB7);
        call_jsl(0x01E09B);
        call_jsr(0x018CBF);
        if (s_title_exit || g_quit) return;
        if (n == 0x003F)
            for (int i = 0; i < 4; i++) { call_jsr(0x018CBF); if (s_title_exit) return; }
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    } while (--n != 0);
    TW16(0x0001, 0x0001);
}


#define IN_BOX(v, lo, w) ((uint16_t)((v) - (lo)) < (w))

/* $01:8641: title state 7: the music-note game. Notes appear one by one
 * ($020B, at most 9, while the random $01:E20C stays below 8); clicking a
 * note button plays its sound ($01:87EC), the ninth starts the tune
 * ($9270, sound $20); a note under the cursor chirps. A click on the
 * "go" spot ends it: sound $58, $7F frames of window ($8F1C), state 1. */
void mp_018641(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    g_cpu.X = 0x0088; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
    call_jsr(0x018CB7);
    call_jsr(0x018CBF);
    if (s_title_exit) return;
    for (;;) {
        ZW16(0x0446, 0);                                 /* $8656 */
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        if (T16(0x020B) == 0x0009) {
            ZW16(0x0446, 0x0100);
        } else {
            CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
            for (uint16_t x = T16(0x020B); x != 0; x--) {
                push_a16(x);
                uint16_t sx = bus_read16(0x01, (uint16_t)(0x87E0 + x)) & 0x00FF;
                g_cpu.Y = bus_read16(0x01, (uint16_t)(0x87E8 + x)) & 0x00FF;
                g_cpu.X = sx;
                CPU_SET_A16(0x30ED);
                call_jsl(0x01FA68);
                g_cpu.S = (uint16_t)(g_cpu.S + 2);
            }
            call_jsl(0x01E20C);
            if (CPU_A16() < 0x0008) TW16(0x020B, T16(0x020B) + 1);
        }
        for (uint16_t c = 0x000C; ; ) {                  /* $86AB */
            push_a16(c);
            if (c != 0x0004 || T16(0x020B) == 0x0009) {
                CPU_SET_A16(c); call_jsl(0x01962C);
                if (c == 0x000B) { CPU_SET_A16(0xFFFF); call_jsr(0x018CB7); }
            }
            g_cpu.S = (uint16_t)(g_cpu.S + 2);
            c--;
            if ((int16_t)(c - 0x0004) < 0) break;
        }
        g_cpu.X = 0x0088; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
        if (Z16(0x04CA) & 0x0020) {
            for (uint16_t x = 0x000C; ; ) {              /* $86EB */
                g_cpu.X = x;
                call_jsr(0x019244);
                x = g_cpu.X;
                if (!g_cpu.flag_C) {
                    if (x == 0x0004 && T16(0x020B) == 0x0009) {
                        TW16(0x021B, T16(0x021B) + 1);
                        call_jsr(0x019270);
                        CPU_SET_A16(0x0020); call_jsl(0x01D308);
                    } else {
                        CPU_SET_A16(bus_read16(0x01, (uint16_t)(0x87EC + x)));
                        call_jsl(0x01D368);
                    }
                }
                x--;
                if ((int16_t)(x - 0x0004) < 0) break;
            }
            uint16_t x = T16(0x020B);
            if (x != 0) {
                if (x == 0x0009) x--;
                do {
                    uint16_t ny = bus_read16(0x01, (uint16_t)(0x87E8 + x)) & 0x00FF;
                    uint16_t nx = bus_read16(0x01, (uint16_t)(0x87E0 + x)) & 0x00FF;
                    if ((uint16_t)(ny - Z16(0x04DE) + 8) < 0x0010 &&
                        (uint16_t)(nx - Z16(0x04DC) + 8) < 0x0010) {
                        CPU_SET_A16(0x0078); call_jsl(0x01D368);
                    }
                } while (--x != 0);
            }
            uint16_t my = Z16(0x04DE);
            if (my >= 0x0040 && my < 0x0050 && IN_BOX(Z16(0x04DC), 0x0080, 0x0010)) {
                call_jsr(0x018CBF);
                if (s_title_exit) return;
                CPU_SET_A16(0x0058); call_jsl(0x01D368);
                bus_write8(0x00, 0x0111, 0x03);
                for (uint16_t c = 0; c != 0x007F; ) {
                    push_a16(c);
                    CPU_SET_A16(c); call_jsr(0x018F1C);
                    ZW16(0x0220, 0x0001);
                    call_jsl(0x01E06F);
                    call_jsr(0x0182B1);
                    call_jsr(0x0182F6);
                    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
                    call_jsr(0x018CB7);
                    call_jsr(0x018CBF);
                    if (s_title_exit || g_quit) return;
                    c = (uint16_t)(bus_wram_read16((uint16_t)(g_cpu.S + 1)) + 1);
                    g_cpu.S = (uint16_t)(g_cpu.S + 2);
                }
                CPU_SET_A16(0x0045); call_jsl(0x01E024);
                call_jsl(0x01DE97);
                ZW16(0x0220, 0xFFFF);
                TW16(0x0001, 0x0001);
                return;
            }
        }
        if (T16(0x021B)) { call_jsr(0x0192D1); call_jsr(0x019329); }   /* $87CC */
        call_jsl(0x01E09B);
        call_jsr(0x018CBF);
        if (s_title_exit || g_quit) return;
    }
}

/* $01:87F9: title state 8: a click near the sprite at $07FA/$07FC plays
 * $7A; when $05A0 is set, sound $5A and state 1. */
void mp_0187F9(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    g_cpu.X = 0x0078; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
    CPU_SET_A16(0x000D); call_jsl(0x01962C);
    call_jsr(0x018CB7);
    if ((Z16(0x04CA) & 0x0020) &&
        (uint16_t)((uint16_t)(Z16(0x04DC) << 1) - Z16(0x07FA) + 0x0020) < 0x0040 &&
        (uint16_t)((uint16_t)(Z16(0x04DE) << 1) - Z16(0x07FC) + 0x0040) < 0x0040) {
        CPU_SET_A16(0x007A); call_jsl(0x01D368);
    }
    if (Z8(0x05A0)) {
        CPU_SET_A16(0x005A); call_jsl(0x01D368);
        TW16(0x0001, 0x0001);
    }
}

/* $01:885F: title state 9: waits (screen blanked each frame by
 * $01:E30E/$E2F3 around the sprite update) for a click on the spot at
 * X $A0..$A7, Y $40..$4F: sound $5C, state 1. */
void mp_01885F(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    g_cpu.X = 0x0070; g_cpu.Y = 0x0002; call_jsr(0x0191FE);
    call_jsr(0x018CB7);
    call_jsr(0x018CBF);
    if (s_title_exit) return;
    for (;;) {
        call_jsl(0x01E30E);
        ZW16(0x0446, 0);
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        for (uint16_t c = 0x0025; ; ) {
            push_a16(c);
            CPU_SET_A16(c); call_jsl(0x01962C);
            g_cpu.S = (uint16_t)(g_cpu.S + 2);
            c--;
            if ((int16_t)(c - 0x000E) < 0) break;
        }
        call_jsr(0x018CB7);
        g_cpu.X = 0x0070; g_cpu.Y = 0x0002; call_jsr(0x0191FE);
        uint16_t my = Z16(0x04DE);
        if ((Z16(0x04CA) & 0x0020) && my >= 0x0040 && my < 0x0050 &&
            IN_BOX(Z16(0x04DC), 0x00A0, 0x0008)) {
            CPU_SET_A16(0x005C); call_jsl(0x01D368);
            TW16(0x0001, 0x0001);
            call_jsl(0x01E2F3);
            return;
        }
        call_jsl(0x01E09B);
        call_jsl(0x01E2F3);
        call_jsr(0x018CBF);
        if (s_title_exit || g_quit) return;
    }
}


/* The common end of the drawing states: the canvas palette and screen
 * back ($8A12), sound, state 1. */
static void title_draw_done(void) {
    ZW16(0x0172, 0); ZW16(0x0174, 0);
    bus_write8(0x00, 0x010E, 0x04);
    CPU_SET_A16(0x0045); call_jsl(0x01E024);
    call_jsl(0x01DE97);
    CPU_SET_A16(0x0000); call_jsl(0x01E033);
    call_jsl(0x008A12);
    ZW16(0x0798, Z16(0x0798) & 0xEFFF);
    CPU_SET_A16(0x005E); call_jsl(0x01D368);
    TW16(0x0001, 0x0001);
    call_jsl(0x01E2F3);
}

/* $01:88E3: title state 10: the picture builds up step by step from the
 * list at $01:8A09 ($8C19 per entry, a negative entry waits $FF frames
 * with the window $8B6E/$8BB5); at the end, or on a click at X $A8..$B7,
 * Y $40..$4F, the screen returns. $099F is set by L+click on step 9. */
void mp_0188E3(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    g_cpu.X = 0x0060; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
    call_jsr(0x018CB7);
    call_jsr(0x018CBF);
    if (s_title_exit) return;
    TW16(0x020D, 0);
    TW16(0x020F, 0x0001);
    for (;;) {
        call_jsl(0x01E30E);                              /* $8901 */
        ZW16(0x0446, 0);
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        ZW16(0x0446, 0x00E0);
        ZW16(0x0798, Z16(0x0798) | 0x3000);
        call_jsr(0x018CB7);
        TW16(0x020F, T16(0x020F) - 1);
        if (T16(0x020F) == 0) {
            for (;;) {
                uint16_t i = T16(0x020D);
                TW16(0x020D, (uint16_t)(i + 1));
                uint16_t v = bus_read16(0x01, (uint16_t)(0x8A09 + (i << 1)));
                if (v == 0) { title_draw_done(); return; }
                if (v & 0x8000) {
                    TW16(0x020F, 0x00FF);
                    call_jsl(0x01DE97);
                    break;
                }
                CPU_SET_A16(v); call_jsr(0x018C19);
            }
        }
        g_cpu.X = 0x0060; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
        ZW16(0x099F, 0);
        if (T16(0x020D) == 0x0009 && (Z16(0x04CA) & 0x0021) == 0x0021) ZW16(0x099F, 0x0001);
        uint16_t my = Z16(0x04DE);
        if ((Z16(0x04CA) & 0x0020) && my >= 0x0040 && my < 0x0050 &&
            IN_BOX(Z16(0x04DC), 0x00A8, 0x0010)) {
            title_draw_done();
            return;
        }
        if (T16(0x020F) != 0x00FF) { call_jsr(0x018B6E); call_jsl(0x01DE97); }
        CPU_SET_A16(T16(0x020F)); call_jsr(0x018BB5);
        call_jsl(0x01DEB2);
        call_jsl(0x01E09B);
        call_jsl(0x01E2F3);
        call_jsr(0x018CBF);
        if (s_title_exit || g_quit) return;
    }
}

/* $01:8AAF: title state 11: free drawing on the title ($00:B046 at the
 * cursor while the button is held, $0020 = right button); a click at
 * X $B8..$C7, Y $40..$4F ends it ($0005 = $EF, sound $60, $00:8A75). */
void mp_018AAF(void) {
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    g_cpu.X = 0x0040; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
    call_jsr(0x018CB7);
    call_jsr(0x018CBF);
    if (s_title_exit) return;
    ZW16(0x0020, 0);
    for (;;) {
        call_jsl(0x01E30E);                              /* $8ACB */
        ZW16(0x0446, 0);
        call_jsr(0x0182B1);
        call_jsr(0x0182F6);
        ZW16(0x0446, 0x00D0);
        call_jsr(0x018CB7);
        call_jsl(0x01E09B);
        call_jsl(0x01E2F3);
        if (Z16(0x0020) != 0 || (Z16(0x04CA) & 0x0020)) {
            ZW16(0x0086, Z16(0x04DC));
            ZW16(0x0088, Z16(0x04DE));
            call_jsl(0x00B046);
            uint16_t y = (Z16(0x04CA) & 0x0010) ? 1 : 0;
            g_cpu.Y = y;
            ZW16(0x0020, y);
            uint16_t my = Z16(0x04DE);
            if ((Z16(0x04CA) & 0x0020) && my >= 0x0040 && my < 0x0050 &&
                IN_BOX(Z16(0x04DC), 0x00B8, 0x0010)) {
                TW16(0x0005, 0x00EF);
                CPU_SET_A16(0x0060); call_jsl(0x01D368);
                TW16(0x0001, 0x0001);
                call_jsl(0x008A75);
                return;
            }
        }
        g_cpu.X = 0x0040; g_cpu.Y = 0x0004; call_jsr(0x0191FE);
        call_jsr(0x018CBF);
        if (s_title_exit || g_quit) return;
    }
}


/* $01:91FE: the colour cycle ($016C bits 3-5 as palette bits 10-12)
 * written into two (Y = 2) or four OAM entries from $0228 + X. A = the
 * palette bits. */
void mp_0191FE(void) {
    uint16_t pal = (uint16_t)((Z16(0x016C) & 0x0038) << 6);
    uint16_t x = g_cpu.X;
    int n = g_cpu.Y == 0x0002 ? 2 : 4;
    for (int i = 0; i < n; i++) {
        uint16_t o = (uint16_t)(0x0228 + 4 * i + x);
        ZW16(o, (uint16_t)((Z16(o) & 0xF1FF) | pal));
    }
    CPU_SET_A16(pal);
}

/* $01:9244: is the cursor on title sprite X (positions doubled at
 * $0792/$0794 + 8X)? Carry clear when it is. X is kept. */
void mp_019244(void) {
    uint16_t o = (uint16_t)(g_cpu.X << 3);
    uint16_t dy = (uint16_t)((uint16_t)(Z16(0x04DE) << 1) - Z16(0x0794 + o) + 0x0010);
    if (dy >= 0x0020) { g_cpu.flag_C = true; CPU_SET_A16(dy); return; }
    uint16_t dx = (uint16_t)((uint16_t)(Z16(0x04DC) << 1) - Z16(0x0792 + o) + 0x0010);
    g_cpu.flag_C = dx >= 0x0020;
    CPU_SET_A16(dx);
}

/* $01:9294: a new random note sprite in slot X: its X ($021D), start Y
 * $F000 ($0281), a tile ($02E5: $E5.., skipping $EE..) and a speed
 * ($0349: $20..$5F). */
void mp_019294(void) {
    uint16_t x = g_cpu.X;
    call_jsl(0x01E20C);
    TW16(0x021D + x, (uint16_t)((CPU_A16() & 0x00FF) << 7));
    TW16(0x0281 + x, 0xF000);
    call_jsl(0x01E20C);
    uint16_t r = CPU_A16() & 0x001F;
    if (r == 0) r = 0x0005;
    r = (uint16_t)(r + 0x00E5);
    if (r >= 0x00EE) r = (uint16_t)(r + 0x00D2);
    TW16(0x02E5 + x, r);
    call_jsl(0x01E20C);
    TW16(0x0349 + x, (uint16_t)((CPU_A16() & 0x003F) + 0x0020));
    g_cpu.X = x;
}

/* $01:9270: the tune: 5 frames, then 33 note sprites each with a random
 * start delay ($03AD), one more frame. X is kept. */
void mp_019270(void) {
    uint16_t x0 = g_cpu.X;
    push_a16(x0);
    for (uint16_t n = 5; n; n--) {
        push_a16(n);
        call_jsl(0x01E2CE);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    }
    for (int x = 0x0040; x >= 0; x -= 2) {
        g_cpu.X = (uint16_t)x;
        call_jsr(0x019294);
        call_jsl(0x01E20C);
        TW16(0x03AD + x, CPU_A16());
    }
    call_jsl(0x01E2CE);
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    g_cpu.X = x0;
}

/* $01:92D1: the note sprites fall: after their delay, each sways by its
 * sine ($01:CAAE, sign in $0000) and drops by its cosine ($01:CA98);
 * leaving the screen ($B000..$EFFF) restarts it. */
void mp_0192D1(void) {
    for (int x = 0x0040; x >= 0; x -= 2) {
        g_cpu.X = (uint16_t)x;
        if (T16(0x03AD + x) != 0) { TW16(0x03AD + x, T16(0x03AD + x) - 1); continue; }
        CPU_SET_A16(T16(0x0349 + x));
        g_cpu.flag_M = true; call_jsr(0x01CAAE); g_cpu.flag_M = false;
        uint16_t d = CPU_A16() & 0x00FF;
        if (T16(0x0000) & 0x00FF) d = (uint16_t)-d;
        TW16(0x021D + x, (uint16_t)(d + T16(0x021D + x)));
        CPU_SET_A16(T16(0x0349 + x));
        g_cpu.flag_M = true; call_jsr(0x01CA98); g_cpu.flag_M = false;
        uint16_t y = (uint16_t)(((CPU_A16() & 0x00FF) << 1) + T16(0x0281 + x));
        TW16(0x0281 + x, y);
        g_cpu.X = (uint16_t)x;
        if (y >= 0xB000 && y < 0xF000) call_jsr(0x019294);
    }
    g_cpu.X = 0xFFFE;
}

/* $01:9329: the note sprites into OAM ($01:F91E: X screen x 0..$1FF,
 * Y line, A tile). */
void mp_019329(void) {
    for (int x = 0x0040; x >= 0; x -= 2) {
        push_a16((uint16_t)x);
        uint16_t a = T16(0x021D + x);
        uint16_t r = (uint16_t)((a << 1) | (a >> 15));
        uint16_t sx = (uint16_t)(((r << 8) | (r >> 8)) & 0x01FF);
        g_cpu.Y = (uint16_t)(T16(0x0281 + x) >> 8);
        CPU_SET_A16(T16(0x02E5 + x));
        g_cpu.X = sx;
        call_jsl(0x01F91E);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    }
    g_cpu.X = 0xFFFE;
}

/* $01:8C19: one row of $20 title tiles from $02:C700 + (A low) x $40 to
 * $7E:2000 + (A high) x $40. */
void mp_018C19(void) {
    uint16_t a = CPU_A16();
    uint16_t x = (uint16_t)((a & 0x00FF) << 6), y = (uint16_t)((a & 0xFF00) >> 2);
    for (int n = 0; n < 0x20; n++, x += 2, y += 2)
        bus_write16(0x7E, (uint16_t)(0x2000 + y), bus_read16(0x02, (uint16_t)(0xC700 + x)));
    g_cpu.X = x; g_cpu.Y = y;
    CPU_SET_A16(a);
}

/* $01:8B6E: the title picture's palette pulse: a level from the count A
 * into tile palette bits of $7E:22C0.. ($180 bytes). */
void mp_018B6E(void) {
    uint16_t a = CPU_A16(), r;
    if (a < 0x0010) r = 0x003F;
    else if (a < 0x0050) r = (uint16_t)(0x004F - a);
    else if (a < 0x0090) r = 0x0000;
    else if (a < 0x00D0) r = (uint16_t)(a - 0x0090);
    else r = 0x003F;
    uint16_t v = (uint16_t)((r & 0x0038) << 7);
    for (int x = 0x017E; x >= 0; x -= 2) {
        uint16_t o = (uint16_t)(0x22C0 + x);
        bus_write16(0x7E, o, (uint16_t)((bus_read16(0x7E, o) & 0xE3FF) | v));
    }
    g_cpu.X = 0xFFFE;
    CPU_SET_A16(v);
}

/* $01:8BB5: the second layer's map ($7E:2AC0..): a plain fill that
 * slides with the count, or between $40 and $9F a copy of the picture
 * one palette over with the scroll at -2. */
void mp_018BB5(void) {
    uint16_t a = CPU_A16(), v;
    if (a >= 0x0040 && a < 0x00A0) {
        for (int x = 0x017E; x >= 0; x -= 2)
            bus_write16(0x7E, (uint16_t)(0x2AC0 + x),
                        (uint16_t)(bus_read16(0x7E, (uint16_t)(0x22C0 + x)) + 0x1800));
        ZW16(0x0172, 0xFFFE); ZW16(0x0174, 0xFFFE);
        CPU_SET_A16(0xFFFE);
    } else {
        if (a < 0x0040) v = a;
        else if (a < 0x00E0) v = (uint16_t)(0x00DF - a);
        else v = 0;
        v = (uint16_t)(v + 0x3CE0);
        for (int x = 0x017E; x >= 0; x -= 2) bus_write16(0x7E, (uint16_t)(0x2AC0 + x), v);
        ZW16(0x0172, 0); ZW16(0x0174, 0);
        CPU_SET_A16(0);
    }
    g_cpu.X = 0xFFFE;
}

/* $01:8F1C: the window HDMA table at $7E:4000: two $70-line blocks with
 * left edge A and right edge ~A, then the end byte. */
void mp_018F1C(void) {
    uint16_t a = CPU_A16();
    uint8_t l = (uint8_t)a, r = (uint8_t)~a;
    const uint8_t t[7] = { 0x70, l, r, 0x70, l, r, 0x00 };
    for (int i = 0; i < 7; i++) bus_write8(0x7E, (uint16_t)(0x4000 + i), t[i]);
    CPU_SET_A16(a);
}


/* $00:B046 (JSL): the plot routine $B05A with DB = 0. */
void mp_00B046(void) {
    uint8_t db = g_cpu.DB;
    bus_wram_write8(g_cpu.S, db);                        /* PHB */
    g_cpu.S = (uint16_t)(g_cpu.S - 1);
    g_cpu.DB = 0x00;
    call_jsr(0x00B05A);
    g_cpu.S = (uint16_t)(g_cpu.S + 1);
    g_cpu.DB = db;
}

/* ========================================================================
 * $0F:C000 — Bank 0F: per-frame demo dispatch
 *
 * Called from the NMI handler every frame. If demo is not active
 * ($04E2 = 0), just returns. If active, plays back recorded input.
 * ======================================================================== */
void mp_0FC000(void) {
    /* JMP ($C009,X) on $04E2: state 0 returns, state 1 plays. */
    if (bus_wram_read16(0x04E2) == 0) return;

    /* Any real mouse input ends the demo. */
    if ((bus_wram_read16(0x04C6) | bus_wram_read16(0x04C8) | bus_wram_read16(0x04CA)) != 0)
        goto end_demo;

    /* The three streams (buttons, Y, X) at $12/$11/$10 + $04E8, $8000 + $04E6. */
    {
        uint16_t bo = bus_wram_read16(0x04E8);
        uint16_t delay = bus_wram_read16(0x04EA);
        if (delay == 0) {
            uint16_t y = bus_wram_read16(0x04E6);
            /* The pointers are pushed $10, $11, $12 and read back from the
             * stack as [$01], [$05], [$09]: the last pushed ($12) first. */
            uint8_t cmd = bus_read8((uint8_t)(0x12 + bo), (uint16_t)(0x8000 + y));
            uint8_t s2 = bus_read8((uint8_t)(0x11 + bo), (uint16_t)(0x8000 + y));
            uint8_t s3 = bus_read8((uint8_t)(0x10 + bo), (uint16_t)(0x8000 + y));
            if (cmd & 0x80) {
                /* A pause: its length into $04EB:$04EA (8-bit stores), no input. */
                bus_wram_write8(0x04EB, s2);
                bus_wram_write8(0x04EA, s3);
                bus_wram_write8(0x04CA, 0x00);
                bus_wram_write8(0x04C8, 0x00);
                bus_wram_write8(0x04C6, 0x00);
            } else {
                bus_wram_write8(0x04CA, cmd);
                bus_wram_write8(0x04C8, s2);
                bus_wram_write8(0x04C6, s3);
            }
            /* $C118: the next entry; past $7FFF the other bank pair. */
            uint16_t idx = (uint16_t)((bus_wram_read16(0x04E6) + 1) & 0x7FFF);
            if (idx == 0) bus_wram_write16(0x04E8, (uint16_t)(bus_wram_read16(0x04E8) ^ 0x0003));
            bus_wram_write16(0x04E6, idx);
        } else {
            /* In a pause: no input, one frame less (the index stays). */
            bus_wram_write8(0x04CA, 0x00);
            bus_wram_write8(0x04C8, 0x00);
            bus_wram_write8(0x04C6, 0x00);
            bus_wram_write16(0x04EA, (uint16_t)(delay - 1));
        }
        /* The end of this demo: its last index in the ROM's table at
         * $0F:C10E, by demo number $0008. */
        uint16_t n = bus_wram_read16(0x0008);
        if (bus_wram_read16(0x04E6) != bus_read16(0x0F, (uint16_t)(0xC10E + (uint16_t)(n << 1))))
            return;
    }

end_demo:
    /* The next demo (0..4), demo off, fade out, then the soft reset
     * (JML $0082B8). */
    {
        uint16_t n = (uint16_t)(bus_wram_read16(0x0008) + 1);
        if (n == 0x0005) n = 0;
        bus_wram_write16(0x0008, n);
        bus_wram_write16(0x04E2, 0x0000);
        mp_01E7C9();
        func_table_call(0x0082B8);
    }
}

/* ========================================================================
 * $0F:C00E — Start demo playback
 *
 * Initializes demo state and sets the demo active flag.
 * ======================================================================== */
void mp_0FC00E(void) {
    bus_wram_write16(0x0002, 0x0000);
    bus_wram_write16(0x04E8, 0x0000);
    bus_wram_write16(0x04EA, 0x0000);

    /* This demo's first index: the ROM's table at $0F:C033, by demo
     * number $0008 (16-bit). */
    uint16_t n = bus_wram_read16(0x0008);
    bus_wram_write16(0x04E6, bus_read16(0x0F, (uint16_t)(0xC033 + (uint16_t)(n << 1))));

    bus_wram_write16(0x04E2, 0x0001);  /* Demo active */
}


/* ========================================================================
 * $00:8B44 — Wrapper: calls 009378 (cursor rendering)
 * ======================================================================== */
void mp_008B44(void) {
    mp_009378();
}

/* ========================================================================
 * Register all title loop and Bank 0F functions.
 * ======================================================================== */
void mp_register_titleloop(void) {
    func_table_register(0x00B046, mp_00B046);
    func_table_register(0x0191FE, mp_0191FE);
    func_table_register(0x019244, mp_019244);
    func_table_register(0x019294, mp_019294);
    func_table_register(0x019270, mp_019270);
    func_table_register(0x0192D1, mp_0192D1);
    func_table_register(0x019329, mp_019329);
    func_table_register(0x018C19, mp_018C19);
    func_table_register(0x018B6E, mp_018B6E);
    func_table_register(0x018BB5, mp_018BB5);
    func_table_register(0x018F1C, mp_018F1C);
    func_table_register(0x0188E3, mp_0188E3);
    func_table_register(0x018AAF, mp_018AAF);
    func_table_register(0x018641, mp_018641);
    func_table_register(0x0187F9, mp_0187F9);
    func_table_register(0x01885F, mp_01885F);
    func_table_register(0x0184E2, mp_0184E2);
    func_table_register(0x018521, mp_018521);
    func_table_register(0x0185A3, mp_0185A3);
    func_table_register(0x01834B, mp_01834B);
    func_table_register(0x0183B6, mp_0183B6);
    func_table_register(0x018260, mp_018260);
    func_table_register(0x0182B1, mp_0182B1);
    func_table_register(0x0182F6, mp_0182F6);
    func_table_register(0x018308, mp_018308);
    func_table_register(0x018C43, mp_018C43);
    func_table_register(0x018CB7, mp_018CB7);
    func_table_register(0x018CBF, mp_018CBF);
    func_table_register(0x0FC000, mp_0FC000);
    func_table_register(0x0FC00E, mp_0FC00E);
    func_table_register(0x008BEB, mp_008BEB);
    func_table_register(0x008B44, mp_008B44);
}
