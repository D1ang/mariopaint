/*
 * Mario Paint — Gnat Attack, translated from the ROM.
 *
 *   $01:A54F  move the swatter with the mouse (clamped to the field)
 *   $01:A5BB  draw the swatter in OAM slot 0
 *   $01:BD87  animate one gnat (8-bit registers; script via the caller)
 *   $01:BE40  swat test / falling gnat (respawn: $A819 any edge, $AB05 side)
 *   $01:A8C4  one frame of a gnat: course, bounce ($C51D), move ($BC7B)
 *   $01:CA98/$CAAE  sine / cosine
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>
#include <stdint.h>
#include <stdbool.h>

/* Sign-extend the mouse displacement byte. */
static inline uint16_t disp(uint16_t addr) {
    uint16_t v = bus_wram_read8(addr);
    return (v & 0x80) ? (uint16_t)(v | 0xFF00) : v;
}

/* The ROM clamps with CMP + BPL/BMI: on the sign of (pos - bound). */
static inline bool below(uint16_t a, uint16_t bound) { return ((uint16_t)(a - bound) & 0x8000) != 0; }

/* ---- $01:A54F ---- */
void mp_01A54F(void) {
    uint16_t x = (uint16_t)(disp(0x04C6) + bus_wram_read16(0x04DC));
    if (below(x, 0x00E8) && !below(x, 0x0018)) bus_wram_write16(0x04DC, x);
    uint16_t y = (uint16_t)(disp(0x04C8) + bus_wram_read16(0x04DE));
    if (below(y, 0x00D0) && !below(y, 0x0020)) bus_wram_write16(0x04DE, y);
    /* $1B26 bit 0 = moved this frame, bit 1 = just started moving,
     * bit 2 = still (the same flags the canvas uses). */
    uint16_t t = ((bus_wram_read16(0x04C6) | bus_wram_read16(0x04C8)) & 0x00FF) ? 0x0001 : 0x0004;
    uint16_t f = (uint16_t)((uint16_t)(((t ^ bus_wram_read16(0x1B28)) & t) << 1) | t);
    bus_wram_write16(0x1B26, f);
    bus_wram_write16(0x1B28, f);
    g_cpu.Y = t;
    CPU_SET_A16(t);
}

/* ---- $01:A5BB ---- */
void mp_01A5BB(void) {
    uint16_t saved = bus_wram_read16(0x0446);
    bus_wram_write16(0x0446, 0x0000);
    g_cpu.X = bus_wram_read16(0x04DC);
    g_cpu.Y = bus_wram_read16(0x04DE);
    uint16_t s = bus_read16(g_cpu.DB, 0x001C);              /* swatter sprite */
    if (!(s & 0x8000)) {
        CPU_SET_A16((uint16_t)(s + 0x010F));
        call_jsl(0x01F91E);
    }
    bus_wram_write16(0x0446, saved);
    CPU_SET_A16(saved);
}

/* ---- $01:BD87: X = gnat number. The animation script is the long
 * pointer the caller left on the stack just above the return address
 * ([$06] after PHX; PHD; TSC; TCD). Per gnat: frame timer $0088+X, step
 * $0047+X (2-byte steps [sprite, frames]; sprite $FF = jump to step
 * <frames>). Drawn when $0046 is set and the sprite is not 0, at
 * $014B+X / $01CD+X. */
void mp_01BD87(void) {
    uint8_t db = g_cpu.DB;
    uint8_t x = (uint8_t)g_cpu.X;
    uint16_t s = g_cpu.S;                                   /* return at S+1..S+2 */
    uint32_t script = bus_wram_read16((uint16_t)(s + 3)) |
                      ((uint32_t)bus_wram_read8((uint16_t)(s + 5)) << 16);
#define RD(o)    bus_read8(db, (uint16_t)(o))
#define WR(o, v) bus_write8(db, (uint16_t)(o), (uint8_t)(v))
#define SCR(y)   bus_read8((uint8_t)((script + (y)) >> 16), (uint16_t)(script + (y)))
    uint8_t timer = (uint8_t)(RD(0x0088 + x) - 1);
    WR(0x0088 + x, timer);
    if (timer & 0x80) {                                     /* next step */
        uint8_t step = (uint8_t)(RD(0x0047 + x) + 1);
        WR(0x0047 + x, step);
        uint8_t y = (uint8_t)(step << 1);
        if (SCR(y) == 0xFF) {
            y++;
            step = SCR(y);
            WR(0x0047 + x, step);
            y = (uint8_t)(step << 1);
        }
        y++;
        WR(0x0088 + x, SCR(y));
    }
    uint8_t y = (uint8_t)(RD(0x0047 + x) << 1);
    if (RD(0x0046) != 0) {
        uint16_t sprite = SCR(y);
        if (sprite != 0) {
            CPU_SET_A16((uint16_t)(sprite + 0x010F));
            g_cpu.flag_M = false; g_cpu.flag_X = false;     /* REP #$30 */
            g_cpu.Y = RD(0x01CD + x);
            g_cpu.X = RD(0x014B + x);
            call_jsl(0x01F91E);
            g_cpu.flag_M = true; g_cpu.flag_X = true;       /* SEP #$30 */
        }
    }
#undef RD
#undef WR
#undef SCR
    g_cpu.X = x;                                            /* PLX */
}

#define GRD(o)    bus_read8(g_cpu.DB, (uint16_t)(o))
#define GWR(o, v) bus_write8(g_cpu.DB, (uint16_t)(o), (uint8_t)(v))

static uint8_t rnd(void) { call_jsl(0x01E20C); return CPU_A8(); }   /* $01:E20C */

/* (r & 1) - 1, then +1 unless negative: -1 or +1. */
static uint8_t pm1(uint8_t r) { uint8_t v = (uint8_t)((r & 1) - 1); return (v & 0x80) ? v : (uint8_t)(v + 1); }

/* ---- $01:A819 / $01:AB05: put gnat X back in at an edge (respawn). */
static void respawn_common(uint8_t x) {                     /* $A88B */
    GWR(0x020E + x, 0x50);
    GWR(0x0290 + x, rnd() & 1);
    GWR(0x02D1 + x, pm1(rnd()));
    GWR(0x0312 + x, (rnd() & 7) + 0x20);
    GWR(0x0088 + x, rnd() & 7);
    GWR(0x0047 + x, 0); GWR(0x00C9 + x, 0);
}
static void respawn_edge(uint8_t x) {                       /* $A819: any edge */
    uint8_t e = rnd() & 3;
    if (e == 0)      { GWR(0x014B + x, rnd()); GWR(0x010A + x, 0); GWR(0x01CD + x, 0xF0); GWR(0x018C + x, 0); GWR(0x024F + x, 0x50); }
    else if (e == 1) { GWR(0x014B + x, rnd()); GWR(0x010A + x, 0); GWR(0x01CD + x, 0xF0); GWR(0x018C + x, 0); GWR(0x024F + x, 0xD0); }
    else if (e == 2) { GWR(0x01CD + x, rnd()); GWR(0x018C + x, 0); GWR(0x014B + x, 0x08); GWR(0x010A + x, 0); GWR(0x024F + x, 0x10); }
    else             { GWR(0x01CD + x, rnd()); GWR(0x018C + x, 0); GWR(0x014B + x, 0xF8); GWR(0x010A + x, 0); GWR(0x024F + x, 0x90); }
    respawn_common(x);
}
static void respawn_side(uint8_t x) {                       /* $AB05: left or right */
    if (rnd() & 1) { GWR(0x014B + x, 0xF8); GWR(0x010A + x, 0); GWR(0x024F + x, 0x80); }
    else           { GWR(0x014B + x, 0x08); GWR(0x010A + x, 0); GWR(0x024F + x, 0x00); }
    GWR(0x01CD + x, (rnd() & 0x7F) + 0x30);
    GWR(0x018C + x, 0);
    GWR(0x020E + x, 0x30);
    GWR(0x0290 + x, 0xFF);
    GWR(0x02D1 + x, 0);
    GWR(0x0312 + x, (rnd() & 3) + 0x10);
    GWR(0x0088 + x, rnd() & 7);
    GWR(0x0047 + x, 0); GWR(0x00C9 + x, 0);
}
void mp_01A819(void) { respawn_edge((uint8_t)g_cpu.X); }
void mp_01AB05(void) { respawn_side((uint8_t)g_cpu.X); }

/* ---- $01:CA98 / $01:CAAE: sine / cosine of angle A (256 steps) from the
 * quarter table at $01:CACF; the sign goes to $0000 (1 = negative). */
static uint8_t trig(uint8_t a, bool cosine) {
    uint8_t neg = 0;
    if (!cosine) {
        if (a >= 0x80) neg = 1;
        if (a & 0x40) a = (uint8_t)(-a);
    } else {
        if (a >= 0x40 && a < 0xC0) neg = 1;
        if (!(a & 0x40)) a = (uint8_t)(-a);
        a ^= 0x40;
    }
    GWR(0x0000, neg);
    return bus_read8(0x01, (uint16_t)(0xCACF + (a & 0x7F)));
}
void mp_01CA98(void) { CPU_SET_A8(trig(CPU_A8(), false)); }
void mp_01CAAE(void) { CPU_SET_A8(trig(CPU_A8(), true)); }

/* speed * trig / 16, signed, added to a 16-bit fixed-point coordinate
 * (fraction byte lo, whole byte hi). */
static void advance(uint8_t speed, uint8_t t, uint16_t frac, uint16_t whole, uint8_t x) {
    bus_write8(0x00, 0x4203, t);                            /* speed * t */
    uint16_t p = (uint16_t)(bus_read16(0x00, 0x4216) >> 4);
    if (GRD(0x0000) ^ GRD(0x0001)) p = (uint16_t)(-p);
    uint16_t lo = (uint16_t)((p & 0xFF) + GRD(frac + x));
    GWR(frac + x, lo);
    GWR(whole + x, (p >> 8) + GRD(whole + x) + (lo >> 8));
    (void)speed;
}

/* ---- $01:BC7B: move gnat X along its heading. */
void mp_01BC7B(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    GWR(0x0001, 0);
    uint8_t v = (uint8_t)(GRD(0x020E + x) + GRD(0x0290 + x));
    GWR(0x020E + x, v);
    if (v & 0x80) { v = (uint8_t)(-v); GWR(0x0001, 1); }
    bus_write8(0x00, 0x4202, v);
    uint8_t ang = (uint8_t)(GRD(0x024F + x) + GRD(0x02D1 + x));
    GWR(0x024F + x, ang);
    advance(v, trig(ang, true), 0x010A, 0x014B, x);
    advance(v, trig(ang, false), 0x018C, 0x01CD, x);
}

/* Mirror the heading off a vertical wall / reverse it off a horizontal one. */
static void mirror(uint8_t x) {
    GWR(0x024F + x, (uint8_t)((GRD(0x024F + x) ^ 0xFF) + 0x81 - GRD(0x02D1 + x)));
}
static void reverse(uint8_t x) {
    GWR(0x024F + x, (uint8_t)(((GRD(0x024F + x) ^ 0xFF) + 1) - GRD(0x02D1 + x)));
}

/* ---- $01:C51D / $C591: bounce gnat X off the field's edges. Which
 * headings bounce depends on the direction of travel ($020E sign). */
void mp_01C51D(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    if (GRD(0x00C9 + x) != 0) return;
    bool back = (GRD(0x020E + x) & 0x80) != 0;              /* $C591 */
    uint8_t px = GRD(0x014B + x), a = GRD(0x024F + x);
    bool right_half = a >= 0x40 && a < 0xC0;                /* heading left */
    if (px < 0x18) { if (back ? !right_half : right_half) mirror(x); }
    else if (px >= 0xE8) { if (back ? right_half : !right_half) mirror(x); }
    uint8_t py = GRD(0x01CD + x);
    a = GRD(0x024F + x);
    if (py < 0x18) { if (back ? !(a & 0x80) : (a & 0x80)) reverse(x); }
    else if (py >= 0xC8) { if (back ? (a & 0x80) : !(a & 0x80)) reverse(x); }
}

/* Push bytes like PHA (8-bit) so a callee can read them as $03,s.. */
static void push8(uint8_t v) { bus_wram_write8(g_cpu.S, v); g_cpu.S = (uint16_t)(g_cpu.S - 1); }

/* ---- $01:A8C4: one frame of gnat X. */
void mp_01A8C4(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    bool move = GRD(0x00C9 + x) != 0 || GRD(0x0042) != 0;
    if (move) {
        if (GRD(0x00C9 + x) == 0 && GRD(0x0312 + x) == 0) {   /* new course */
            GWR(0x0290 + x, pm1(rnd()));
            uint8_t t = (uint8_t)((rnd() & 0x1F) - 0x10);
            GWR(0x02D1 + x, (t & 0x80) ? t : (uint8_t)(t + 1));
            GWR(0x0312 + x, (rnd() & 0x1F) + 0x10);
        }
        GWR(0x0312 + x, GRD(0x0312 + x) - 1);              /* $A8FD */
        g_cpu.X = x; call_jsr(0x01C51D);
        g_cpu.X = x; call_jsr(0x01BC7B);
    }
    uint16_t s0 = g_cpu.S;                                  /* script $01:A933 */
    push8(0x01); push8(0xA9); push8(0x33);
    g_cpu.X = x; call_jsr(0x01BD87);
    g_cpu.S = s0;
    push8(0x03); push8(0x04); push8(0x04);                  /* fall step, w, h */
    uint8_t sound = (GRD(0x0014) == 2 && GRD(0x0016) == 0) ? 0x37 : 0x33;
    CPU_SET_A8(sound);
    g_cpu.X = x; call_jsr(0x01BE40);
    g_cpu.S = s0;
    g_cpu.X = x;
}

/* ---- $01:BE40: A = sound, X = gnat; the caller pushed the half width
 * ($04,s), half height ($03,s) and a fall animation step ($05,s).
 * A live gnat ($00C9+X = 0) inside the swatter box ($0030-$0032 by
 * $0034-$0036) while $002C is set gets swatted: sounds, fall state,
 * score $001A (milestones 10*, 25, 50, 75 into $05EB). A falling gnat
 * ($00C9+X > 0) steps its fall; once off the bottom it is removed, or
 * ($0044 vs $0045) handed to a respawn handler from the table at $BF21. */
void mp_01BE40(void) {
    uint8_t db = g_cpu.DB;
    uint8_t x = (uint8_t)g_cpu.X;
    uint16_t s = g_cpu.S;
#define RD(o)    bus_read8(db, (uint16_t)(o))
#define WR(o, v) bus_write8(db, (uint16_t)(o), (uint8_t)(v))
    uint8_t half_h = bus_wram_read8((uint16_t)(s + 3));
    uint8_t half_w = bus_wram_read8((uint16_t)(s + 4));
    uint8_t fall_step = bus_wram_read8((uint16_t)(s + 5));
    WR(0x0005, CPU_A8());
    uint8_t state = RD(0x00C9 + x);
    if (state != 0) {                                       /* $BED4 */
        if (state & 0x80) return;
        WR(0x0047 + x, fall_step);
        if (RD(0x01CD + x) >= 0xF0) {
            CPU_SET_A8((uint8_t)(RD(0x0005) - 0x2E));
            call_jsl(0x01D328);
            WR(0x0044, RD(0x0044) - 1);
            if (!((uint8_t)(RD(0x0044) - RD(0x0045)) & 0x80)) {   /* BPL $BF18 */
                /* JSL $01E393 table at $BF21 by X >> 4: $A819 for slots 0, 1
                 * and 3, $AB05 for slot 2; the handler returns for us. */
                uint16_t h = bus_read16(0x01, (uint16_t)(0xBF21 + ((x >> 4) << 1)));
                if (h == 0xA819) respawn_edge(x);
                else if (h == 0xAB05) respawn_side(x);
                else tail_jmp(0x010000u | h);
                g_cpu.X = x;
                return;
            }
            WR(0x020E + x, 0); WR(0x0290 + x, 0); WR(0x00C9 + x, 0xFF);
            return;
        }
        if ((uint8_t)(RD(0x020E + x) + RD(0x0290 + x)) >= 0x7F) {
            WR(0x020E + x, 0x7F);
            WR(0x0290 + x, 0);
        }
        return;
    }
    if (RD(0x002C) == 0) return;
    uint8_t gx = RD(0x014B + x), gy = RD(0x01CD + x);
    uint8_t d = (uint8_t)(gx - half_w);
    if (d < RD(0x0030)) { if ((uint8_t)(gx + half_w) < RD(0x0030)) return; }
    else if (d >= RD(0x0032)) return;
    d = (uint8_t)(gy - half_h);
    if (d < RD(0x0034)) { if ((uint8_t)(gy + half_h) < RD(0x0034)) return; }
    else if (d >= RD(0x0036)) return;
    CPU_SET_A8(0x0A); call_jsl(0x01D348);                   /* hit */
    CPU_SET_A8(RD(0x0005)); call_jsl(0x01D368);
    WR(0x020E + x, 0x00); WR(0x024F + x, 0x40); WR(0x0290 + x, 0x07); WR(0x02D1 + x, 0x00);
    WR(0x001A, RD(0x001A) + 1);
    WR(0x00C9 + x, RD(0x00C9 + x) + 1);
    WR(0x002E, 0x01);
    uint8_t score = RD(0x001A);
    if (score == 0x4B || score == 0x32 || score == 0x19 || (RD(0x0012) != 0 && score == 0x0A))
        WR(0x05EB, score);
#undef RD
#undef WR
    g_cpu.X = x;
}

/* ---- Gnat Attack game state ($01:A5E4). Absolute addresses go through
 * DB (the game's work bank), long ones ($00:04xx) to low WRAM. */
#define G16(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define S16(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))

/* $01:A7D0: the swatter's two hit boxes around the cursor: $30-$36 the
 * big one (x-8..x+8, y-16..y+8), $38-$3E the small one (x-6..x+6,
 * y-14..y+22). */
static void swatter_boxes(void) {
    uint16_t x = bus_wram_read16(0x04DC), y = bus_wram_read16(0x04DE);
    S16(0x0030, x - 0x0008); S16(0x0032, x + 0x0008);
    S16(0x0034, y - 0x0010); S16(0x0036, y + 0x0008);
    S16(0x0038, x - 0x0006); S16(0x003A, x + 0x0006);
    S16(0x003C, y - 0x000E);
    uint16_t a = (uint16_t)(y + 0x0016);
    S16(0x003E, a);
    CPU_SET_A16(a);
    g_cpu.flag_C = (uint32_t)(uint16_t)(y - 0x000E) + 0x0024 > 0xFFFF;   /* CLC; ADC #$24 */
    g_cpu.flag_V = ((~((uint16_t)(y - 0x000E) ^ 0x0024)) & ((uint16_t)(y - 0x000E) ^ a) & 0x8000) != 0;
    cpu_update_nz16(a);
}

/* Swing step: the byte pair at $01:table + 2 * step gives the sprite
 * ($1C, low byte) and how many frames it stays ($20, high byte). */
static void swing_step(uint16_t table, uint16_t step, bool ff_is_none) {
    g_cpu.X = (uint16_t)(step << 1);
    uint16_t w = bus_read16(0x01, (uint16_t)(table + g_cpu.X));
    uint16_t spr = w & 0x00FF;
    if (ff_is_none && spr == 0x00FF) spr = 0xFFFF;
    S16(0x001C, spr);
    S16(0x0020, w >> 8);
    swatter_boxes();
}

/* $A684: next frame of a swing; steps 3 and 6 end it ($22 = 0). */
static void swing_next(void) {
    uint16_t step = G16(0x001E);
    S16(0x001E, step + 1);
    if (step == 0x0003 || step == 0x0006) { S16(0x002C, 0); S16(0x0022, 0); }
    swing_step(0xA6B2, step, false);
}

/* $A6DA: next frame of being stung; at step 5 a life is lost ($18), and
 * when none are left the game is over ($22 = 3). */
static bool s_redirected;                               /* handed over to the ROM */
static void stung_next(void) {
    uint16_t step = G16(0x001E);
    S16(0x001E, step + 1);
    if (step == 0x0005) {
        S16(0x0022, 0);
        uint16_t lives = (uint16_t)(G16(0x0018) - 1);
        S16(0x0018, lives);
        if (lives & 0x8000) {                               /* game over */
            S16(0x0022, 0x0003);
            op_sep(0x20);
            bus_write8(g_cpu.DB, 0x05D7, 0x09);
            bus_write8(g_cpu.DB, 0x0047, 0x36);
            bus_write8(g_cpu.DB, 0x020E, 0); bus_write8(g_cpu.DB, 0x0290, 0);
            bus_write8(g_cpu.DB, 0x024F, 0); bus_write8(g_cpu.DB, 0x02D1, 0);
            bus_write8(g_cpu.DB, 0x0049, 0); bus_write8(g_cpu.DB, 0x004A, 0);
            op_rep(0x20);
            if (!recomp_frame_wait_possible() || !recomp_timed_trusted(0x01C0D3)) {
                /* $C0D3 waits for a frame (or is not verified): the ROM goes
                 * on from its JSR, with the step this routine pushed at
                 * $A6E5 (PHA) on the stack. */
                push8(0x00); push8((uint8_t)step);
                recomp_set_redirect(0x01A714);
                s_redirected = true;
                return;
            }
            call_jsr(0x01C0D3);
            S16(0x0040, 0x0300);
        } else {
            S16(0x0040, 0x00C0);
            call_jsr(0x01A44D);
        }
    }
    if (s_redirected) return;
    swing_step(0xA749, step, true);
}
static void push8(uint8_t v);

/* Count $20 down; when it has run out, run the next step. */
static void countdown(void (*next)(void)) {
    uint16_t t = G16(0x0020);
    CPU_SET_A16(t);
    if (!(t & 0x8000)) { S16(0x0020, t - 1); cpu_update_nz16((uint16_t)(t - 1)); return; }
    next();
}

/* ---- $01:A5E4: the swatter, by state $22 (JSL $01E393 table at $A5EB):
 * 0 idle (button 0x20 starts a swing), 1 swinging, 2 stung (cursor
 * frozen), 3 game over (wait $40 frames, then start a new game). Paths
 * that wait for frames run here only where the body can wait for them
 * (recomp_frame_wait_possible); otherwise the ROM goes on from that point. */
void mp_01A5E4(void) {
    uint16_t state = G16(0x0022);
    uint16_t h = bus_read16(0x01, (uint16_t)(0xA5EB + ((state & 0x00FF) << 1)));
    switch (h) {
    case 0xA619:
        S16(0x0028, bus_wram_read16(0x04DC));
        S16(0x002A, bus_wram_read16(0x04DE));
        S16(0x002C, 0);
        S16(0x001C, 0);
        if (bus_wram_read16(0x04CA) & 0x0020) {
            S16(0x001E, 0);
            S16(0x0020, 0xFFFF);
            S16(0x0022, 0x0001);
        }
        swatter_boxes();
        return;
    case 0xA648:
        S16(0x0028, bus_wram_read16(0x04DC));
        S16(0x002A, bus_wram_read16(0x04DE));
        if (G16(0x002C) == 0) { CPU_SET_A16(0x0008); call_jsl(0x01D348); }
        S16(0x002C, 0x0001);
        if (G16(0x002E) != 0 && G16(0x001E) < 0x0004) S16(0x001E, 0x0004);
        countdown(swing_next);
        return;
    case 0xA6C0:
        S16(0x002C, 0);
        bus_wram_write16(0x04DC, G16(0x0028));
        bus_wram_write16(0x04DE, G16(0x002A));
        s_redirected = false;
        countdown(stung_next);
        return;
    case 0xA755:
        if (G16(0x0040) != 0) {
            S16(0x0040, G16(0x0040) - 1);
            swatter_boxes();
            return;
        }
        S16(0x0040, 0x0001);                                /* $A75F: a new game */
        if (!recomp_frame_wait_possible() || !recomp_timed_trusted(0x01B5DC)) {
            /* It waits for frames ($E7C9, $E2CE, $E794): the ROM goes on. */
            recomp_set_redirect(0x01A765);
            return;
        }
        call_jsl(0x01E7C9);
        op_sep(0x30);
        call_jsr(0x01B5DC);
        op_rep(0x30);
        S16(0x0018, 0x0003);
        S16(0x0012, 0); S16(0x0014, 0); S16(0x0016, 0); S16(0x001A, 0);
        S16(0x001C, 0); S16(0x0022, 0); S16(0x0024, 0); S16(0x05EA, 0);
        S16(0x0026, 0); S16(0x00C9, 0); S16(0x0040, 0);
        CPU_SET_A16(0x00AE);
        call_jsl(0x01E042);
        call_jsr(0x01A30D);
        call_jsr(0x01A2B7);
        call_jsr(0x01A373);
        call_jsr(0x01A44D);
        call_jsl(0x01DE97);
        call_jsl(0x01DECD);
        call_jsl(0x01E06F);
        bus_wram_write16(0x0446, 0x0040);
        call_jsl(0x01E2CE);
        call_jsl(0x01E2CE);
        call_jsl(0x01E2CE);
        call_jsl(0x01E794);
        swatter_boxes();
        return;
    default:                                                /* not in the ROM's table */
        CPU_SET_A16(h);
        tail_jmp(0x010000u | h);
        return;
    }
}

/* ---- $01:C5F8: once a frame in Gnat Attack. The pause button (bit 1 of
 * $04CA) plays sound $F0 and opens the pause menu ($C622); leaving it
 * with "quit" (carry set) abandons the game: S = the stack saved at $0006
 * when the game started, and the RTS returns from there. Otherwise sound
 * $F1, then wait for the next frame. */
void mp_01C5F8(void) {
    uint8_t p = cpu_get_p();                                /* PHP */
    op_rep(0x20);
    if (bus_wram_read16(0x04CA) & 0x0002) {
        CPU_SET_A16(0x00F0);
        call_jsl(0x01D308);
        call_jsr(0x01C622);
        if (g_cpu.flag_C) {                                 /* LDA $0006; TCS; RTS */
            uint16_t s = G16(0x0006);
            CPU_SET_A16(s);
            g_cpu.S = s;
            return;
        }
        CPU_SET_A16(0x00F1);
        call_jsl(0x01D308);
    }
    call_jsl(0x01E2CE);
    cpu_set_p(p);                                           /* PLP */
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}
#undef G16
#undef S16

/* ---- Gnat Attack pause menu ($01:C622 and helpers). Absolute addresses go
 * through DB (the game's work bank), long ones to bank 0 / $7E. */
#define G16(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define S16(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))

/* Under lockstep validation a frame driver normally suspends the body until
 * the genuine frame has run (main.c's k_frame_drivers). Should a frame be
 * presented some other way there, frames do not advance on the copy of the
 * machine and a loop that waits for input would never end: stop (the call
 * is skipped anyway). */
extern bool g_recomp_validating, g_recomp_frame_touched;
static inline bool frames_frozen(void) { return g_recomp_validating && g_recomp_frame_touched; }

static inline uint16_t xba16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

/* $01:C9CE: hide all 32 sprites ($F400 = Y 244), high table $5555. */
void mp_01C9CE(void) {
    for (int x = 0x3E; x >= 0; x -= 2)
        bus_wram_write16((uint16_t)(0x0226 + x), 0xF400);
    bus_wram_write16(0x0426, 0x5555);
    bus_wram_write16(0x0428, 0x5555);
    bus_wram_write16(0x0446, 0x0000);
    g_cpu.X = 0xFFFE;
    CPU_SET_A16(0x0000);
    g_cpu.flag_Z = true;
    g_cpu.flag_N = false;
}

/* $01:C9EF: build the HDMA table at $7E:4000 that dims the screen behind
 * the menu: $7F:000A - 1 lines plain (in runs of at most $7F), then the
 * entries at $01:CA78 with $7F:0008 added to each value pair (capped at
 * $FF). Entry byte h: < $80 one line pair, >= $80 repeat mode, h & $7F
 * pairs; 0 ends the table. 8-bit A, 16-bit X. */
void mp_01C9EF(void) {
    uint8_t p = cpu_get_p();                                /* PHP PHB */
    uint8_t db = g_cpu.DB;
    op_sep(0x20);
    g_cpu.DB = 0x01;
    uint16_t x = 0;
#define T(v) bus_write8(0x7E, (uint16_t)(0x4000 + x), (uint8_t)(v)), x++
    uint8_t a = (uint8_t)(bus_read8(0x7F, 0x000A) - 1);
    while (a >= 0x7F) {                                     /* $C9FF */
        a = (uint8_t)(a - 0x7F);
        T(0x7F); T(0x01); T(0x00);
    }
    if (a != 0) { T(a); T(0x01); T(0x00); }                 /* $CA1E */
    uint8_t add = bus_read8(0x7F, 0x0008);
    uint16_t y = 0;
    for (;;) {                                              /* $CA38 */
        a = bus_read8(0x01, (uint16_t)(0xCA78 + y));
        if (a == 0) break;
        bus_write8(0x7E, (uint16_t)(0x4000 + x), a);
        if (a & 0x80) a = (uint8_t)(a - 1);
        y++; x++;
        do {                                                /* $CA46 */
            for (int k = 0; k < 2; k++) {
                unsigned v = (unsigned)bus_read8(0x01, (uint16_t)(0xCA78 + y)) + add;
                T(v > 0xFF ? 0xFF : v);
                y++;
            }
            a = (uint8_t)(a - 1);
        } while (a & 0x80);
    }
    bus_write8(0x7E, (uint16_t)(0x4000 + x), 0x00);         /* $CA71 */
#undef T
    g_cpu.X = x;
    g_cpu.Y = y;
    CPU_SET_A8(0x00);
    g_cpu.DB = db;                                          /* PLB PLP */
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* $01:A274: frame the play field in the BG map at $7E:2800 with tile
 * $2DC7: the two leftmost and rightmost columns, then the top two and
 * bottom three rows. */
void mp_01A274(void) {
    for (uint16_t x = 0; x != 0x0800; x = (uint16_t)(x + 0x40)) {
        write_long16(0x7E2800 + x, 0x2DC7);
        write_long16(0x7E2802 + x, 0x2DC7);
        write_long16(0x7E283C + x, 0x2DC7);
        write_long16(0x7E283E + x, 0x2DC7);
    }
    for (uint16_t x = 0; x != 0x0040; x = (uint16_t)(x + 2)) {
        write_long16(0x7E2800 + x, 0x2DC7);
        write_long16(0x7E2840 + x, 0x2DC7);
        write_long16(0x7E2E80 + x, 0x2DC7);
        write_long16(0x7E2EC0 + x, 0x2DC7);
        write_long16(0x7E2F00 + x, 0x2DC7);
    }
    g_cpu.X = 0x0040;
    CPU_SET_A16(0x2DC7);
    g_cpu.flag_C = true;
    g_cpu.flag_Z = true;
    g_cpu.flag_N = false;
}

/* $01:C95E: one frame of the menu: move its cursor ($0C/$0E) by the mouse,
 * kept inside the box ($08/$0A + $67 x $6F), draw it and the three lever
 * objects ($23-$25), then wait for the frame. */
void mp_01C95E(void) {
    call_jsr(0x01C9CE);
    uint16_t nx = (uint16_t)(disp(0x04C6) + G16(0x000C));
    uint16_t d = (uint16_t)(nx - G16(0x0008));
    if (!(d & 0x8000) && ((uint16_t)(d - 0x0067) & 0x8000)) S16(0x000C, nx);
    uint16_t ny = (uint16_t)(disp(0x04C8) + G16(0x000E));
    d = (uint16_t)(ny - G16(0x000A));
    if (!(d & 0x8000) && ((uint16_t)(d - 0x006F) & 0x8000)) S16(0x000E, ny);
    g_cpu.X = G16(0x000C);
    g_cpu.Y = G16(0x000E);
    CPU_SET_A16(0x0136);
    call_jsl(0x01F91E);
    for (uint16_t o = 0x23; o <= 0x25; o++) { CPU_SET_A16(o); call_jsl(0x01F84F); }
    call_jsl(0x01E2CE);
}

/* Restore what $C622 saved, PLP, and answer in carry: set = quit. */
static void pause_exit(uint16_t saved446, uint8_t saved104, uint8_t p, bool quit) {
    CPU_SET_A16(saved446);
    bus_wram_write16(0x0446, saved446);
    op_sep(0x20);
    CPU_SET_A8(saved104);
    bus_wram_write8(0x0104, saved104);
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
    g_cpu.flag_C = quit;
}

/* Lever animation: play sound $3F, flag the lever object, run the menu
 * $40 more frames. */
static void pull_lever(uint16_t flag_addr) {
    CPU_SET_A16(0x003F);
    call_jsl(0x01D368);
    bus_wram_write16(flag_addr, 0xFFFF);
    for (int n = 0x40; n > 0; n--) call_jsr(0x01C95E);
}

/* ---- $01:C622: the pause menu. Opens a 13x14-tile box (from $01:C7F2)
 * near the swatter, dims the rest of the screen (HDMA table, $C9EF) and
 * places the lever objects $23-$25 relative to it; then runs frames until
 * a click on CONTINUE (carry clear: the field is redrawn) or EXIT (carry
 * set). The top lever does nothing but animate. */
void mp_01C622(void) {
    uint8_t p = cpu_get_p();                                /* PHP */
    uint8_t saved104 = bus_wram_read8(0x0104);              /* SEP #$20 */
    bus_wram_write8(0x0104, 0x0F);                          /* main screen: BG1-4 only */
    uint16_t saved446 = bus_wram_read16(0x0446);            /* REP #$20 */
    op_rep(0x20);

    uint16_t x = bus_wram_read16(0x04DC);
    if (x < 0x0050) x = 0x0050;
    if (x >= 0x00A8) x = 0x00A8;
    x &= 0xFFF8;
    S16(0x000C, x);
    S16(0x0008, x - 0x0030);
    uint16_t col = (uint16_t)((uint16_t)(x - 0x0030) >> 2);
    uint16_t y = bus_wram_read16(0x04DE);
    if (y < 0x0060) y = 0x0060;
    if (y >= 0x0090) y = 0x0090;
    y &= 0xFFF8;
    S16(0x000E, y);
    S16(0x000A, y - 0x0040);
    uint16_t at = (uint16_t)((uint16_t)((uint16_t)(y - 0x0040) << 3) + col);

    uint16_t src = 0;                                       /* PHB; DB = 1 */
    for (int row = 0; row < 14; row++, at = (uint16_t)(at + 0x40))
        for (uint16_t k = 0; k < 13; k++, src = (uint16_t)(src + 2))
            write_long16(0x7E2800 + (uint16_t)(at + 2 * k), bus_read16(0x01, (uint16_t)(0xC7F2 + src)));
    g_cpu.X = at;
    g_cpu.Y = src;

    call_jsl(0x01E2CE);
    call_jsl(0x01DEB2);
    bus_wram_write16(0x0220, 0x0001);
    bus_wram_write8(0x0112, 0x02);                          /* HDMA channel 1 on */
    call_jsr(0x01C9EF);
    static const uint16_t lever_pos[3] = { 0x1A28, 0x1A30, 0x1A38 };
    for (int i = 0; i < 3; i++) {
        CPU_SET_A16(0x0023 + i);
        call_jsl(0x01F825);
        uint16_t ax = xba16((uint16_t)(G16(0x0008) >> 1));
        bus_wram_write16(lever_pos[i], (uint16_t)(ax + bus_wram_read16(lever_pos[i])));
        uint16_t ay = xba16(G16(0x000A));
        bus_wram_write16((uint16_t)(lever_pos[i] + 2), (uint16_t)(ay + bus_wram_read16((uint16_t)(lever_pos[i] + 2))));
    }
    bus_wram_write16(0x04CA, 0x0000);

    for (;;) {                                              /* $C73D */
        if (bus_wram_read16(0x04CA) & 0x0020) {
            uint16_t c = G16(0x000C), l = G16(0x0008);
            uint16_t dx = (uint16_t)(c - l);
            uint16_t dy = (uint16_t)(G16(0x000E) - G16(0x000A));
            if (c >= l && dx < 0x0067 && dy >= 0x0008) {
                if (dy < 0x001F) {                          /* top lever */
                    bus_wram_write16(0x1A3D, 0xFFFF);
                } else if (dy >= 0x0030 && dy < 0x0047) {   /* CONTINUE */
                    pull_lever(0x1A2D);
                    call_jsr(0x01C9CE);
                    CPU_SET_A16(0x0057);
                    call_jsl(0x01E033);
                    call_jsr(0x01A274);
                    call_jsl(0x01DEB2);
                    bus_wram_write16(0x0220, 0xFFFF);
                    pause_exit(saved446, saved104, p, false);
                    return;
                } else if (dy >= 0x0050 && dy < 0x0067) {   /* EXIT */
                    pull_lever(0x1A35);
                    pause_exit(saved446, saved104, p, true);
                    return;
                }
            }
        }
        call_jsr(0x01C95E);                                 /* $C779 */
        if (frames_frozen()) return;                        /* skipped anyway */
    }
}
#undef G16
#undef S16

/* ======================================================================
 * Gnat Attack field, score and projectiles (16-bit unless noted).
 * ==================================================================== */
#define GW16(o)     bus_read16(g_cpu.DB, (uint16_t)(o))

/* $01:A2E7: A = tile; fill every second word of the BG map at $7E:2000
 * from X on, 16 per row, skipping a row after each: a checkerboard. A and X
 * come back unchanged. */
static void fill_checker(uint16_t a, uint16_t x) {
    do {
        write_long16(0x7E2000 + x, a);
        x = (uint16_t)(x + 4);
        if ((x & 0x003C) == 0) x = (uint16_t)(x + 0x0040);
    } while (x < 0x0800);
}
void mp_01A2E7(void) {
    fill_checker(CPU_A16(), g_cpu.X);
    g_cpu.flag_C = true;                                    /* CPX #$0800 */
    cpu_update_nz16(CPU_A16());                             /* PLX; PLA */
}

/* $01:A2B7: the field background for wave $0014 (mod 3): the tile pair
 * from $01:A307, laid out as a 2x2 checkerboard. */
void mp_01A2B7(void) {
    uint16_t w = GW16(0x0014);
    while (w >= 0x0003) w = (uint16_t)(w - 3);
    uint16_t t = bus_read16(0x01, (uint16_t)(0xA307 + (w << 1)));
    fill_checker(t, 0x0000);
    fill_checker((uint16_t)(t + 1), 0x0002);
    fill_checker((uint16_t)(t + 0x10), 0x0040);
    fill_checker((uint16_t)(t + 0x11), 0x0042);
    g_cpu.X = 0x0042;
    CPU_SET_A16((uint16_t)(t + 0x11));
    g_cpu.flag_C = true;
    cpu_update_nz16(CPU_A16());
}

/* Digit d (0-9) as a 2x2 tile group in the BG map at $7E:3000 + o. */
static uint16_t digit_tile(uint16_t d) { return (uint16_t)((uint16_t)(((d & 0xFFF8) + d) << 2) + 0x2F00); }
static uint16_t put_digit(uint16_t o, uint16_t d) {
    uint16_t t = digit_tile(d);
    write_long16(0x7E3000 + o, t);
    write_long16(0x7E3002 + o, (uint16_t)(t + 2));
    write_long16(0x7E3040 + o, (uint16_t)(t + 0x20));
    write_long16(0x7E3042 + o, (uint16_t)(t + 0x22));
    return (uint16_t)(t + 0x22);
}

/* $01:A373: how many gnats are left to swat (100 - $001A), up to three
 * digits; blank tiles ($00AE) where there is no digit. Nothing when none
 * are left. */
void mp_01A373(void) {
    static const uint16_t blank[] = { 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2, 0xD8, 0xDA, 0xDC, 0xDE, 0xE0, 0xE2 };
    for (int i = 0; i < 12; i++) write_long16(0x7E3000 + blank[i], 0x00AE);
    uint16_t a = (uint16_t)(0x0064 - GW16(0x001A));          /* EOR #$FFFF; ADC #$65 */
    g_cpu.Y = 0;
    if (a == 0 || (a & 0x8000)) {
        CPU_SET_A16(a);
        g_cpu.flag_C = GW16(0x001A) <= 0x0064;              /* carry out of EOR/ADC #$65 */
        cpu_update_nz16(a);
        return;
    }
    uint16_t tens = 0;
    while (a >= 0x000A) { a = (uint16_t)(a - 10); tens++; }
    uint16_t units = a;
    if (tens >= 10) {                                       /* 100 */
        put_digit(0x98, 1);
        units = 0;
        tens = 0;
        put_digit(0x9C, 0);
    } else if (tens != 0) {
        put_digit(0x9C, tens);
    }
    g_cpu.Y = tens;
    CPU_SET_A16(put_digit(0xA0, units));
    g_cpu.X = units;
    g_cpu.flag_C = false;                                   /* ADC #$1E */
    cpu_update_nz16(CPU_A16());
}

/* $01:A44D: the lives display: clear its area, then one swatter icon per
 * life ($0018), or from 7 lives on one icon, "x" and the number. */
void mp_01A44D(void) {
    uint16_t x = 0x00CC;
    for (;;) {
        write_long16(0x7E30AA + x, 0x00AE);
        x = (uint16_t)(x - 2);
        if (x & 0x003F) continue;
        x = (uint16_t)(x - 0x0034);
        if (x & 0x8000) break;
    }
    uint16_t y = GW16(0x0018);
    g_cpu.X = x;
    g_cpu.Y = y;
    if (y == 0) return;
    if (y < 0x0007) {
        x = 0;
        uint16_t a = 0;
        do {
            write_long16(0x7E30AC + x, 0x2F48);
            write_long16(0x7E30AE + x, 0x2F4A);
            write_long16(0x7E30EC + x, 0x2F68);
            write_long16(0x7E30EE + x, 0x2F6A);
            x = (uint16_t)(x + 4);
            a = x & 0x003F;
            if (a == 0x000C) { x = (uint16_t)(x + 0x0074); a = x; }
            y--;
        } while (y != 0);
        g_cpu.X = x;
        g_cpu.Y = 0;
        CPU_SET_A16(a);
        g_cpu.flag_Z = true;                                /* DEY */
        g_cpu.flag_N = false;
        return;
    }
    write_long16(0x7E30AC, 0x2F48); write_long16(0x7E30AE, 0x2F4A); write_long16(0x7E30B0, 0x2FD4);
    write_long16(0x7E30EC, 0x2F68); write_long16(0x7E30EE, 0x2F6A); write_long16(0x7E30F0, 0x2FF4);
    uint16_t a = y, tens = 0;
    while (a >= 0x000A) { a = (uint16_t)(a - 10); tens++; }
    write_long16(0x7E30B2, (uint16_t)((tens << 1) + 0x2FC0));
    write_long16(0x7E30F2, (uint16_t)((tens << 1) + 0x2FE0));
    write_long16(0x7E30B4, (uint16_t)((a << 1) + 0x2FC0));
    write_long16(0x7E30F4, (uint16_t)((a << 1) + 0x2FE0));
    g_cpu.Y = tens;
    CPU_SET_A16((uint16_t)((a << 1) + 0x2FE0));
}

/* $01:B255: queue the 8-byte DMA record at $01:B275 (8-bit). */
void mp_01B255(void) {
    bus_wram_write8(0x0202, 0x00);
    for (int x = 7; x >= 0; x--) bus_wram_write8((uint16_t)(0x0182 + x), bus_read8(0x01, (uint16_t)(0xB275 + x)));
    bus_wram_write8(0x0204, 0x07);
    bus_wram_write8(0x0202, 0x01);
    g_cpu.X = (uint16_t)((g_cpu.X & 0xFF00) | 0x00FF);
    CPU_SET_A8(0x01);
}

/* $01:B5DC: copy the 16 colours at $02:FF60 to $05B5 and queue them. */
void mp_01B5DC(void) {
    uint8_t p = cpu_get_p();
    for (int x = 0x1E; x >= 0; x -= 2)
        bus_write16(g_cpu.DB, (uint16_t)(0x05B5 + x), bus_read16(0x02, (uint16_t)(0xFF60 + x)));
    op_rep(0x30);
    g_cpu.X = 0xFFFE;                                       /* DEX past 0 */
    op_sep(0x30);
    mp_01B255();
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* $01:BF99: no projectiles ($0353-$0382 cleared). */
void mp_01BF99(void) {
    for (int x = 0x2E; x >= 0; x -= 2) bus_write16(g_cpu.DB, (uint16_t)(0x0353 + x), 0);
    g_cpu.X = g_cpu.flag_X ? 0x00FE : 0xFFFE;               /* REP: DEX past 0; PLP */
}

/* ---- Projectiles (8-bit; X = slot 0-$1F): flag $0353, frame step $0383,
 * frame timer $03B3, position $03E3/$0413 (x, fraction/whole) and
 * $0443/$0473 (y), speed $04A3 (+ $0503 per frame), heading $04D3
 * (+ $0533), sting delay $0563. */

/* $01:BF29: does projectile X (half size w x h) touch the swatter's small
 * box? Then it stings: sound 9 (not after game over), the projectile is
 * gone and the swatter goes to state 2. Not while blinking ($0040) or
 * already stung. */
static void sting_test(uint8_t x, uint8_t w, uint8_t h) {
    if (GRD(0x0040) != 0 || GRD(0x0022) == 0x02) return;
    uint8_t px = GRD(0x0413 + x), d = (uint8_t)(px - w);
    if (d < GRD(0x0038)) { if ((uint8_t)(px + w) < GRD(0x0038)) return; }
    else if (d >= GRD(0x003A)) return;
    uint8_t py = GRD(0x0473 + x);
    d = (uint8_t)(py - h);
    if (d < GRD(0x003C)) { if ((uint8_t)(py + h) < GRD(0x003C)) return; }
    else if (d >= GRD(0x003E)) return;
    if (GRD(0x0022) != 0x03) { CPU_SET_A8(0x09); call_jsl(0x01D348); }
    GWR(0x0353 + x, 0);
    bus_write16(g_cpu.DB, 0x001E, 0x0000);
    bus_write16(g_cpu.DB, 0x0020, 0xFFFF);
    if (GRD(0x0022) != 0x03) GWR(0x0022, 0x02);
}
void mp_01BF29(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    sting_test(x, bus_wram_read8((uint16_t)(g_cpu.S + 4)), bus_wram_read8((uint16_t)(g_cpu.S + 3)));
    g_cpu.X = x;
}

/* $01:BD01: move projectile X: speed += accel, heading += turn, then
 * speed * cos/sin into x/y, as $BC7B does for gnats. */
static void move_shot(uint8_t x) {
    GWR(0x0001, 0);
    uint8_t v = (uint8_t)(GRD(0x04A3 + x) + GRD(0x0503 + x));
    GWR(0x04A3 + x, v);
    if (v & 0x80) { v = (uint8_t)(-v); GWR(0x0001, 1); }
    bus_write8(0x00, 0x4202, v);
    uint8_t ang = (uint8_t)(GRD(0x04D3 + x) + GRD(0x0533 + x));
    GWR(0x04D3 + x, ang);
    advance(v, trig(ang, true), 0x03E3, 0x0413, x);
    advance(v, trig(ang, false), 0x0443, 0x0473, x);
}
void mp_01BD01(void) { move_shot((uint8_t)g_cpu.X); }

/* $01:BDE6: animate and draw projectile X from the script at ptr (the long
 * pointer the caller pushed): 2-byte steps [sprite, frames], sprite $FF =
 * jump to step <frames>; sprite 0 = not drawn. */
static void anim_shot(uint8_t x, uint32_t script) {
#define SCR(y) bus_read8((uint8_t)((script + (y)) >> 16), (uint16_t)(script + (y)))
    uint8_t t = (uint8_t)(GRD(0x03B3 + x) - 1);
    GWR(0x03B3 + x, t);
    if (t & 0x80) {
        uint8_t step = (uint8_t)(GRD(0x0383 + x) + 1);
        GWR(0x0383 + x, step);
        uint8_t y = (uint8_t)(step << 1);
        if (SCR(y) == 0xFF) {
            y++;
            step = SCR(y);
            GWR(0x0383 + x, step);
            y = (uint8_t)(step << 1);
        }
        y++;
        GWR(0x03B3 + x, SCR(y));
    }
    uint16_t sprite = SCR((uint8_t)(GRD(0x0383 + x) << 1));
#undef SCR
    if (sprite != 0) {
        op_rep(0x30);
        CPU_SET_A16((uint16_t)(sprite + 0x010F));
        g_cpu.Y = GRD(0x0473 + x);
        g_cpu.X = GRD(0x0413 + x);
        call_jsl(0x01F91E);
        op_sep(0x30);
    }
    g_cpu.X = x;
}
void mp_01BDE6(void) {
    uint16_t s = g_cpu.S;
    uint32_t script = bus_wram_read16((uint16_t)(s + 3)) | ((uint32_t)bus_wram_read8((uint16_t)(s + 5)) << 16);
    anim_shot((uint8_t)g_cpu.X, script);
}

/* $01:CB10: the heading from ($0002, $0003) = |dx|, |dy| and the signs in
 * $0000 (bit 0 x negative, bit 1 y negative), via the hardware divider and
 * the arctangent table at $01:CB96; result in $0004 (256 steps). X is kept. */
static uint8_t heading(void) {
    uint8_t dx = GRD(0x0002), dy = GRD(0x0003), a;
    if (dx == 0) a = 0x40;
    else if (dy == 0) a = 0x00;
    else if (dy == dx) a = 0x20;
    else if (dy < dx) {
        bus_write8(0x00, 0x4205, dy); bus_write8(0x00, 0x4204, 0); bus_write8(0x00, 0x4206, dx);
        a = bus_read8(0x01, (uint16_t)(0xCB96 + bus_read8(0x00, 0x4214)));
    } else {
        bus_write8(0x00, 0x4205, dx); bus_write8(0x00, 0x4204, 0); bus_write8(0x00, 0x4206, dy);
        a = (uint8_t)(0x40 - bus_read8(0x01, (uint16_t)(0xCB96 + bus_read8(0x00, 0x4214))));
    }
    uint8_t q = GRD(0x0000);
    if (q >= 1 && q < 3) a = (uint8_t)(0x80 - a);           /* EOR #$FF; ADC #$81 */
    if (q >= 2) a = (uint8_t)(a + 0x80);
    GWR(0x0004, a);
    return a;
}
void mp_01CB10(void) { uint8_t x = (uint8_t)g_cpu.X; CPU_SET_A8(heading()); g_cpu.X = x; }

/* Off the field (x outside $10-$EF or y >= $E0): gone. */
static void shot_bounds(uint8_t x) {
    uint8_t px = GRD(0x0413 + x);
    if (px < 0x10 || px >= 0xF0 || GRD(0x0473 + x) >= 0xE0) GWR(0x0353 + x, 0);
}

/* $01:AAAD: move the straight projectiles (slots $0F-$00). */
void mp_01AAAD(void) {
    uint8_t a = 0;
    for (int x = 0x0F; x >= 0; x--) {
        if ((a = GRD(0x0353 + x)) == 0) continue;
        move_shot((uint8_t)x);
        shot_bounds((uint8_t)x);
        if ((a = GRD(0x0563 + x)) != 0) GWR(0x0563 + x, a - 1);
        else { sting_test((uint8_t)x, 4, 4); a = 4; }       /* PLA */
    }
    CPU_SET_A8(a);
    g_cpu.X = 0x00FF;
    g_cpu.flag_N = true; g_cpu.flag_Z = false;              /* DEX past 0 */
}

/* $01:AAE3 / $01:AD78: animate the projectiles of slots $0F-$00 (script
 * $01:AAFD) / $1F-$10 (script $01:AD94). */
void mp_01AAE3(void) {
    for (int x = 0x0F; x >= 0; x--) if (GRD(0x0353 + x)) anim_shot((uint8_t)x, 0x01AAFD);
    g_cpu.X = 0x00FF;
    CPU_SET_A8(0x01);                                       /* the last PLA */
    g_cpu.flag_N = false; g_cpu.flag_Z = false;
}
void mp_01AD78(void) {
    for (int x = 0x1F; x >= 0x10; x--) if (GRD(0x0353 + x)) anim_shot((uint8_t)x, 0x01AD94);
    g_cpu.X = 0x000F;
    CPU_SET_A8(0x01);
    g_cpu.flag_N = false; g_cpu.flag_Z = false;
}

/* $01:ACD5: move the homing projectiles (slots $1F-$10). While the delay
 * $0563 runs below $50 they fly at speed $05AD toward the swatter: below
 * $30 straight at it, before that turning by 1/16 of the difference. */
void mp_01ACD5(void) {
    uint8_t alast = 0;
    for (int x = 0x1F; x >= 0x10; x--) {
        if ((alast = GRD(0x0353 + x)) == 0) continue;
        move_shot((uint8_t)x);
        shot_bounds((uint8_t)x);
        uint8_t delay = GRD(0x0563 + x);
        if (delay != 0) {
            GWR(0x0563 + x, delay - 1);
            alast = delay;
            if (delay >= 0x50) continue;
            GWR(0x04A3 + x, GRD(0x05AD));
            if ((alast = GRD(0x0040)) != 0) continue;
            GWR(0x0000, 0);
            uint8_t a = (uint8_t)bus_wram_read8(0x04DE), b = GRD(0x0473 + x);
            if (a < b) { a = (uint8_t)(b - a); GWR(0x0000, 1); } else a = (uint8_t)(a - b);
            GWR(0x0003, a);
            GWR(0x0000, (uint8_t)(GRD(0x0000) << 1));
            a = (uint8_t)bus_wram_read8(0x04DC); b = GRD(0x0413 + x);
            if (a < b) { a = (uint8_t)(b - a); GWR(0x0000, GRD(0x0000) + 1); } else a = (uint8_t)(a - b);
            GWR(0x0002, a);
            uint8_t h = heading();
            if (GRD(0x0563 + x) < 0x30) {
                GWR(0x04D3 + x, h);
            } else {
                uint8_t d = (uint8_t)(h - GRD(0x04D3 + x));
                for (int k = 0; k < 4; k++) d = (uint8_t)((d >> 1) | (d & 0x80));   /* CMP #$80; ROR */
                GWR(0x04D3 + x, d + GRD(0x04D3 + x));
            }
        }
        sting_test((uint8_t)x, 4, 4);
        alast = 4;
    }
    CPU_SET_A8(alast);
    g_cpu.X = 0x000F;
    g_cpu.flag_N = true; g_cpu.flag_Z = false; g_cpu.flag_C = false;   /* CPX #$10 */
}
#undef GW16

/* ---- Gnat kinds of the later waves (8-bit, X = gnat slot $00/$10/...). */

/* Call $BD87 / $BE40 with their stack arguments, as the ROM pushes them. */
static void gnat_anim(uint8_t x, uint32_t script) {
    uint16_t s0 = g_cpu.S;
    push8((uint8_t)(script >> 16)); push8((uint8_t)(script >> 8)); push8((uint8_t)script);
    g_cpu.X = x; call_jsr(0x01BD87);
    g_cpu.S = s0;
    g_cpu.X = x;
}
static void gnat_swat(uint8_t x, uint8_t fall, uint8_t w, uint8_t h, uint8_t sound) {
    uint16_t s0 = g_cpu.S;
    push8(fall); push8(w); push8(h);
    CPU_SET_A8(sound);
    g_cpu.X = x; call_jsr(0x01BE40);
    g_cpu.S = s0;
    g_cpu.X = x;
}

/* (r & mask) - sub, then +1 unless negative: a signed step that is never 0. */
static uint8_t nz_step(uint8_t r, uint8_t mask, uint8_t sub) {
    uint8_t v = (uint8_t)((r & mask) - sub);
    return (v & 0x80) ? v : (uint8_t)(v + 1);
}

/* Shared by $A93D and $AB61: wander; inside the middle of the field and
 * at speed (|$020E| >= $30) a gnat halves its speed and starts its "aim"
 * animation (step + 3), or at standstill step 5. */
static void gnat_wander(uint8_t x, bool shooter2) {
    if (GRD(0x00C9 + x) == 0) {
        if (GRD(0x0042) == 0) return;                       /* frozen: no move */
        if (GRD(0x0047 + x) < 0x06) {
            if (GRD(0x0312 + x) == 0) {                      /* new course */
                if (!shooter2) {
                    GWR(0x0290 + x, nz_step(rnd(), 0x03, 0x03));
                    GWR(0x02D1 + x, nz_step(rnd(), 0x1F, 0x10));
                    GWR(0x0312 + x, (rnd() & 0x1F) + 0x10);
                } else {
                    GWR(0x0290 + x, nz_step(rnd(), 0x03, 0x02));
                    GWR(0x02D1 + x, 0);
                    GWR(0x0312 + x, (rnd() & 0x03) + 0x10);
                }
            }
            GWR(0x0312 + x, GRD(0x0312 + x) - 1);
            uint8_t gx = GRD(0x014B + x), gy = GRD(0x01CD + x), v = GRD(0x020E + x);
            if (gx >= 0x30 && gx < 0xD0 && gy >= 0x20 && gy < 0xC0 && !(v >= 0x30 && v < 0xD0)) {
                if (v == 0xFF) v = 0;
                v = (uint8_t)((v >> 1) | (v & 0x80));        /* CMP #$80; ROR */
                GWR(0x020E + x, v);
                GWR(0x0290 + x, 0);
                GWR(0x0312 + x, 0xFF);
                if (GRD(0x0047 + x) < 0x03) GWR(0x0047 + x, GRD(0x0047 + x) + 3);
                if (GRD(0x020E + x) == 0) { GWR(0x0047 + x, 0x05); GWR(0x0088 + x, 0xFF); }
            }
        }
    }
    g_cpu.X = x; call_jsr(0x01C51D);                        /* bounce */
    g_cpu.X = x; call_jsr(0x01BC7B);                        /* move */
}

/* Fire up to four shots from gnat X: the free slots of 16 (straight,
 * $0F-$00, $0593 list) or 32 (homing, $1F-$10, $0597 list); none when fewer
 * than four are free. The pushed values are the stack frame the ROM uses
 * as locals: x, y, then the angle (straight, +$40 each) or the delay
 * (homing, +4 each). A comes back as the gnat's x. */
static void gnat_fire(uint8_t x, bool homing) {
    uint8_t gx = GRD(0x014B + x), gy = (uint8_t)(GRD(0x01CD + x) + (homing ? 8 : 0));
    uint8_t var = homing ? 0x50 : 0x20;
    uint16_t list = homing ? 0x0597 : 0x0593;
    uint8_t y = 0, lo = homing ? 0x10 : 0x00;
    int k = homing ? 0x1F : 0x0F;
    for (;; k--) {
        if (GRD(0x0353 + k) == 0) { GWR(list + y, k); y++; }
        if (y == 4) break;
        if (k - 1 < lo) { CPU_SET_A8(gx); g_cpu.X = x; return; }
    }
    for (int i = 3; i >= 0; i--) {
        uint8_t j = GRD(list + i);
        GWR(0x0413 + j, gx); GWR(0x03E3 + j, 0);
        GWR(0x0473 + j, gy); GWR(0x0443 + j, 0);
        if (homing) {
            GWR(0x04A3 + j, 0);
            GWR(0x04D3 + j, 0x40);
            GWR(0x0503 + j, 0); GWR(0x0533 + j, 0);
            GWR(0x0563 + j, var);
            var = (uint8_t)(var + 4);
        } else {
            GWR(0x04A3 + j, GRD(0x05AC));
            GWR(0x04D3 + j, var);
            var = (uint8_t)(var + 0x40);
            GWR(0x0503 + j, 0); GWR(0x0533 + j, 0);
            GWR(0x0563 + j, 0x08);
        }
        GWR(0x0383 + j, 0); GWR(0x03B3 + j, 0);
        GWR(0x0353 + j, GRD(0x0353 + j) + 1);
    }
    CPU_SET_A8(gx);
    g_cpu.X = x;
}
void mp_01AA48(void) { uint16_t y = g_cpu.Y; gnat_fire((uint8_t)g_cpu.X, false); g_cpu.Y = y; }
void mp_01AC70(void) { gnat_fire((uint8_t)g_cpu.X, true); }

/* ---- $01:A93D: a gnat that fires four straight shots (animation step
 * $10 of script $01:AA20). */
void mp_01A93D(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    gnat_wander(x, false);
    gnat_anim(x, 0x01AA20);
    if (GRD(0x0047 + x) == 0x10) {
        CPU_SET_A8(0x0B); call_jsl(0x01D328);
        uint16_t y = g_cpu.Y; gnat_fire(x, false); g_cpu.Y = y;
        GWR(0x020E + x, (rnd() & 0x7F) + 0x40);
        GWR(0x0312 + x, 0);
    }
    gnat_swat(x, 0x12, 0x04, 0x08, 0x34);
    CPU_SET_A8(0x12);
}

/* ---- $01:AB61: a gnat that drops four homing shots (step $11 of script
 * $01:AC3C) and speeds off at step $16. */
void mp_01AB61(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    gnat_wander(x, true);
    gnat_anim(x, 0x01AC3C);
    if (GRD(0x0047 + x) == 0x11) {
        CPU_SET_A8(0x0C); call_jsl(0x01D328);
        gnat_fire(x, true);
    }
    if (GRD(0x0047 + x) == 0x16) {
        GWR(0x020E + x, (rnd() & 0x7F) + 0x40);
        GWR(0x0312 + x, 0);
    }
    gnat_swat(x, 0x18, 0x04, 0x08, 0x35);
    CPU_SET_A8(0x18);
}

/* ---- $01:AD9C: a gnat that dives at the swatter: within $30 of it, it
 * locks on (step + 3, $0312 = $18) and then flies at it at speed $05AE;
 * from step 8 on it stings like a shot ($BF29 through the shot arrays,
 * slot X - $10, after copying its position to $0403/$0463 + X). */
void mp_01AD9C(void) {
    uint8_t x = (uint8_t)g_cpu.X;
    bool move = true;
    if (GRD(0x00C9 + x) == 0) {
        if (GRD(0x0042) == 0) move = false;
        else if (GRD(0x0047 + x) >= 0x03) {                 /* $ADDE: aim at the swatter */
            GWR(0x020E + x, GRD(0x05AE));
            GWR(0x0000, 0);
            uint8_t a = (uint8_t)bus_wram_read8(0x04DE), b = GRD(0x01CD + x);
            if (a < b) { a = (uint8_t)(b - a); GWR(0x0000, 1); } else a = (uint8_t)(a - b);
            GWR(0x0003, a);
            GWR(0x0000, (uint8_t)(GRD(0x0000) << 1));
            a = (uint8_t)bus_wram_read8(0x04DC); b = GRD(0x014B + x);
            if (a < b) { a = (uint8_t)(b - a); GWR(0x0000, GRD(0x0000) + 1); } else a = (uint8_t)(a - b);
            GWR(0x0002, a);
            g_cpu.X = x;
            GWR(0x024F + x, heading());
        } else if (GRD(0x0312 + x) == 0) {                  /* new course */
            GWR(0x0290 + x, pm1(rnd()));
            GWR(0x02D1 + x, nz_step(rnd(), 0x1F, 0x10));
            GWR(0x0312 + x, (rnd() & 0x1F) + 0x10);
        }
    }
    if (move) {                                             /* $AE19 */
        GWR(0x0312 + x, GRD(0x0312 + x) - 1);
        g_cpu.X = x; call_jsr(0x01C51D);
        g_cpu.X = x; call_jsr(0x01BC7B);
    }
    uint8_t gx = GRD(0x014B + x), gy = GRD(0x01CD + x);      /* $AE22 */
    uint8_t cx = (uint8_t)bus_wram_read8(0x04DC), cy = (uint8_t)bus_wram_read8(0x04DE);
    if (gx >= 0x30 && gx < 0xD0 && gy >= 0x20 && gy < 0xC0 &&
        !(gx >= 0x30 && (uint8_t)(gx - 0x30) >= cx) &&      /* SBC #$30: compared without borrow only */
        !(gx + 0x30 <= 0xFF && (uint8_t)(gx + 0x30) < cx) &&  /* ADC #$30: compared without carry only */
        gy < 0xF8 &&
        !(gy >= 0x30 && (uint8_t)(gy - 0x30) >= cy) &&
        !(gy + 0x30 <= 0xFF && (uint8_t)(gy + 0x30) < cy) &&
        GRD(0x0047 + x) < 0x03) {
        GWR(0x0047 + x, GRD(0x0047 + x) + 3);
        GWR(0x0312 + x, 0x18);
    }
    uint8_t st = GRD(0x0047 + x);
    if (st >= 0x03 && st < 0x06 && GRD(0x00C9 + x) == 0 && GRD(0x0312 + x) == 0) {
        CPU_SET_A8(0x0A); call_jsl(0x01D328);
        GWR(0x0047 + x, 0x08);
    }
    if (GRD(0x0047 + x) == 0x0F) respawn_edge(x);
    uint16_t s0 = g_cpu.S;
    push8(0x01); push8(0xAE); push8(0xF4);
    g_cpu.X = x; call_jsr(0x01BD87);
    g_cpu.S = s0;
    if (GRD(0x0047 + x) < 0x08) {
        gnat_swat(x, 0x06, 0x08, 0x08, 0x36);
        CPU_SET_A8(0x06);
    } else {
        GWR(0x0403 + x, GRD(0x014B + x));
        GWR(0x0463 + x, GRD(0x01CD + x));
        sting_test((uint8_t)(x - 0x10), 0x10, 0x10);
        CPU_SET_A8(0x10);
    }
    g_cpu.X = x;
}

/* ======================================================================
 * Gnat Attack's game loop and its phases.
 * ==================================================================== */
#define G16(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
#define S16(o, v)  bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))

/* JSR $C5F8 (pause check, one frame). True if the player quit: $C5F8 then
 * left on the stack saved at $0006, so every native level above returns at
 * once, each giving back the 2 bytes its caller's call_jsr will add, and
 * the outermost ends with S where the ROM's RTS pops from. */
static bool pause_frame(void) {
    uint16_t s0 = g_cpu.S;
    call_jsr(0x01C5F8);
    if (g_cpu.S == s0) return false;
    g_cpu.S = (uint16_t)(g_cpu.S - 2);
    return true;
}
/* call_jsr to a routine that may end in pause_frame's exit. */
static bool call_may_quit(uint32_t addr) {
    uint16_t s0 = g_cpu.S;
    call_jsr(addr);
    if (g_cpu.S == s0) return false;
    g_cpu.S = (uint16_t)(g_cpu.S - 2);
    return true;
}

/* One frame of the swatter alone ($C057/$C095): input, state, draw, pause. */
static bool swatter_frame(void) {
    call_jsl(0x01E06F);
    bus_wram_write16(0x0446, 0x0040);
    call_jsr(0x01A54F);
    call_jsr(0x01A5E4);
    call_jsr(0x01A5BB);
    return pause_frame();
}

/* ---- $01:C012: phase 0, a wave's title: "WAVE n" for $40 frames, then
 * the field with the count and lives, the wave's tune, $B4 more frames. */
void mp_01C012(void) {
    call_jsr(0x01A44D);
    call_jsr(0x01A373);
    for (int x = 8; x >= 0; x -= 2) {
        write_long16(0x7E32D6 + x, bus_read16(0x01, (uint16_t)(0xC0BF + x)));
        write_long16(0x7E3316 + x, bus_read16(0x01, (uint16_t)(0xC0C9 + x)));
    }
    uint16_t t = (uint16_t)((G16(0x0014) << 2) + 0x2F04);
    write_long16(0x7E32E4, t);
    write_long16(0x7E32E6, (uint16_t)(t + 2));
    write_long16(0x7E3324, (uint16_t)(t + 0x20));
    write_long16(0x7E3326, (uint16_t)(t + 0x22));
    call_jsl(0x01DECD);
    for (int n = 0x40; n > 0; n--) if (swatter_frame()) return;
    CPU_SET_A16(0x00AE);
    call_jsl(0x01E042);
    call_jsr(0x01A30D);
    call_jsr(0x01A44D);
    call_jsr(0x01A373);
    CPU_SET_A16((uint16_t)(G16(0x0014) + 8));
    call_jsl(0x01D308);
    call_jsl(0x01DECD);
    for (int n = 0xB4; n > 0; n--) if (swatter_frame()) return;
    bus_wram_write16(0x0446, 0x0000);
    call_jsr(0x01A5BB);
    S16(0x0024, G16(0x0024) + 1);
}

/* ---- $01:C0D3: game over: sound $0E, clear the field, the GAME OVER
 * tiles from $01:C10F/$01:C121, one frame. */
void mp_01C0D3(void) {
    CPU_SET_A16(0x000E);
    call_jsl(0x01D308);
    CPU_SET_A16(0x00AE);
    call_jsl(0x01E042);
    call_jsr(0x01A30D);
    if (G16(0x0024) < 0x0003) call_jsr(0x01A373);
    for (int x = 0x10; x >= 0; x -= 2) {
        write_long16(0x7E32D6 + x, bus_read16(0x01, (uint16_t)(0xC10F + x)));
        write_long16(0x7E3316 + x, bus_read16(0x01, (uint16_t)(0xC121 + x)));
    }
    g_cpu.X = 0xFFFE;
    call_jsl(0x01DECD);
    call_jsl(0x01E2CE);
}

/* ---- $01:AF14: phase 1, set up wave $0014 step $0016: per gnat kind the
 * count ($05A0, wave total; $05A4 on screen), speed fraction step ($05A8),
 * shot speeds ($05AC-$05AE) from the tables at $01:CC96-$01:CD68 (index
 * wave * 5 + step), and every gnat on screen respawned. 8-bit. */
void mp_01AF14(void) {
    uint8_t p = cpu_get_p();
    op_sep(0x30);
    uint8_t x = (uint8_t)((uint8_t)(GRD(0x0014) << 2) + GRD(0x0014) + GRD(0x0016));
    static const uint16_t src[15] = { 0xCD3B, 0xCD4A, 0xCD59, 0xCD68, 0xCCFF, 0xCD0E, 0xCD1D, 0xCD2C,
                                      0xCC96, 0xCCA5, 0xCCB4, 0xCCC3, 0xCCD2, 0xCCE1, 0xCCF0 };
    for (int i = 0; i < 15; i++) GWR(0x05A0 + i, bus_read8(0x01, (uint16_t)(src[i] + x)));
    for (int k = 0; k < 4; k++) {
        uint8_t y = GRD(0x05A4 + k);
        for (uint8_t gx = (uint8_t)(0x0F + 0x10 * k); y; y--, gx--) {
            if (k == 2) respawn_side(gx); else respawn_edge(gx);
        }
    }
    mp_01BF99();
    GWR(0x0046, 0x01);
    GWR(0x0024, GRD(0x0024) + 1);
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* ---- $01:AFDB: phase 2, one frame of play: each gnat kind ($A8C4,
 * $A93D, $AB61, $AD9C; slots $0F/$1F/$2F/$3F down) moves when its speed
 * fraction $059C+k overflows ($0042); $0044/$0045 carry the kind's counts
 * through the gnat routines. Then the shots. When no gnats are left the
 * step ends ($0024 = 1, $0016 + 1; after 5 steps $0024 = 3). 8-bit. */
void mp_01AFDB(void) {
    uint8_t p = cpu_get_p();
    op_sep(0x30);
    static const uint32_t kind[4] = { 0x01A8C4, 0x01A93D, 0x01AB61, 0x01AD9C };
    for (int k = 0; k < 4; k++) {
        uint8_t y = GRD(0x05A4 + k);
        if (k != 0 || y != 0) GWR(0x0045, y);
        if (y == 0) continue;
        GWR(0x0044, GRD(0x05A0 + k));
        unsigned sum = (unsigned)GRD(0x059C + k) + GRD(0x05A8 + k);
        GWR(0x059C + k, sum);
        GWR(0x0042, sum > 0xFF ? 1 : 0);
        for (uint8_t gx = (uint8_t)(0x0F + 0x10 * k); y; y--, gx--) {
            g_cpu.X = gx;
            call_jsr(kind[k]);
        }
        GWR(0x05A0 + k, GRD(0x0044));
    }
    mp_01AAAD();
    mp_01AAE3();
    mp_01ACD5();
    mp_01AD78();
    if ((GRD(0x05A0) | GRD(0x05A1) | GRD(0x05A2) | GRD(0x05A3)) == 0) {
        GWR(0x0024, 0x01);
        GWR(0x0016, GRD(0x0016) + 1);
        if (GRD(0x0016) == 0x05) GWR(0x0024, 0x03);
    }
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* ---- $01:C3C4: bonus state 0: every 10 swats (or, after the waves, when
 * gnat 0 falls) a bonus fly ($00C8 etc., slot -1) enters from the right,
 * unless the lives are full (6); then state 1. 8-bit. */
void mp_01C3C4(void) {
    uint8_t p = cpu_get_p();
    op_sep(0x30);
    uint8_t a;
    bool go;
    if (GRD(0x0012) != 0) { a = GRD(0x05EB); go = a == 0x0A; }
    else if (GRD(0x0024) >= 0x03) { a = GRD(0x00C9); go = a == 0x0F; }
    else { a = GRD(0x05EB); go = a != 0; }
    if (go && a != GRD(0x05EA)) {
        GWR(0x05EA, a);
        GWR(0x05EB, 0);
        if (GRD(0x0018) != 0x06) {
            GWR(0x018B, (rnd() & 0x3F) + 0x60);
            GWR(0x014A, 0);
            GWR(0x020D, 0xF8);
            GWR(0x01CC, 0);
            GWR(0x024E, 0x20);
            GWR(0x028F, 0x60);
            GWR(0x02D0, 0);
            GWR(0x0311, (uint8_t)((rnd() & 0x03) - 4));
            GWR(0x00C8, 0); GWR(0x0087, 0); GWR(0x0109, 0);
            GWR(0x0026, GRD(0x0026) + 1);
        }
    }
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* Handlers of the two state machines that have a C version here, and may
 * run it: the play frame only while every gnat kind on screen has a
 * verified routine (the later kinds are not reached by the tour yet). */
static bool phase_native(uint16_t h) {
    if (h == 0xAFDB) {
        static const uint32_t kind[3] = { 0x01A93D, 0x01AB61, 0x01AD9C };
        for (int k = 0; k < 3; k++)
            if (bus_read8(g_cpu.DB, (uint16_t)(0x05A5 + k)) && !recomp_timed_trusted(kind[k])) return false;
        return true;
    }
    return h == 0xC012 || h == 0xAF14 || h == 0xC3C4;
}
/* $A60E ($0026, table $01:A615) and $A5F3 ($0024, table $01:A5FA) dispatch
 * through $01:E393; the handler runs where the JSR left S. */
static uint16_t phase_handler(uint16_t table, uint16_t var) {
    return bus_read16(0x01, (uint16_t)(table + ((G16(var) & 0x00FF) << 1)));
}
static void phase_dispatch(uint16_t table, uint16_t var) {
    uint16_t h = phase_handler(table, var);
    CPU_SET_A16(h);
    /* No C version, or one that waits for frames where the body cannot:
     * the ROM handler runs, as $E393's RTL would enter it. */
    if (!phase_native(h) || (h == 0xC012 && !recomp_frame_wait_possible())) {
        recomp_set_redirect(0x010000u | h);
        return;
    }
    tail_jmp(0x010000u | h);
}
void mp_01A60E(void) { phase_dispatch(0xA615, 0x0026); }
void mp_01A5F3(void) { phase_dispatch(0xA5FA, 0x0024); }

/* ---- $01:A505: Gnat Attack's game loop, one frame per pass: input,
 * swatter, bonus fly ($A60E), phase ($A5F3), count, sprites, pause. It
 * never returns; quitting from the pause menu leaves through the stack
 * saved at $0006. A phase without a C version here hands the loop to the
 * ROM at its JSR (the loop then stays on the ROM); so does a body that
 * cannot wait for frames, after the first call. */
void mp_01A505(void) {
    call_jsl(0x01E06F);
    if (!recomp_frame_wait_possible()) { recomp_set_redirect(0x01A509); return; }
    for (;;) {
        bus_wram_write16(0x0446, 0x0040);
        call_jsr(0x01A54F);
        call_jsr(0x01A5E4);
        if (G16(0x0022) != 0x0003) {                        /* $A516 */
            uint16_t t = G16(0x0040);
            if (t == 0) call_jsr(0x01A5BB);
            else {
                S16(0x0040, t - 1);
                if (t & 1) call_jsr(0x01A5BB);              /* blinking */
            }
        }
        S16(0x002E, 0);
        if (!phase_native(phase_handler(0xA615, 0x0026))) { recomp_set_redirect(0x01A531); return; }
        if (call_may_quit(0x01A60E)) return;
        if (!phase_native(phase_handler(0xA5FA, 0x0024))) { recomp_set_redirect(0x01A534); return; }
        if (call_may_quit(0x01A5F3)) return;
        if (G16(0x0024) < 0x0003) call_jsr(0x01A373);
        call_jsl(0x01DE97);
        call_jsl(0x01DECD);
        if (pause_frame()) return;
        call_jsl(0x01E06F);
    }
}
#undef G16
#undef S16

void mp_register_gnat(void) {
    func_table_register(0x01C012, mp_01C012);
    func_table_register(0x01C0D3, mp_01C0D3);
    func_table_register(0x01AF14, mp_01AF14);
    func_table_register(0x01AFDB, mp_01AFDB);
    func_table_register(0x01C3C4, mp_01C3C4);
    func_table_register(0x01A60E, mp_01A60E);
    func_table_register(0x01A5F3, mp_01A5F3);
    func_table_register(0x01A505, mp_01A505);

    func_table_register(0x01AA48, mp_01AA48);
    func_table_register(0x01AC70, mp_01AC70);
    func_table_register(0x01A93D, mp_01A93D);
    func_table_register(0x01AB61, mp_01AB61);
    func_table_register(0x01AD9C, mp_01AD9C);

    func_table_register(0x01A2E7, mp_01A2E7);
    func_table_register(0x01A2B7, mp_01A2B7);
    func_table_register(0x01A373, mp_01A373);
    func_table_register(0x01A44D, mp_01A44D);
    func_table_register(0x01B255, mp_01B255);
    func_table_register(0x01B5DC, mp_01B5DC);
    func_table_register(0x01BF99, mp_01BF99);
    func_table_register(0x01BF29, mp_01BF29);
    func_table_register(0x01BD01, mp_01BD01);
    func_table_register(0x01BDE6, mp_01BDE6);
    func_table_register(0x01CB10, mp_01CB10);
    func_table_register(0x01AAAD, mp_01AAAD);
    func_table_register(0x01AAE3, mp_01AAE3);
    func_table_register(0x01AD78, mp_01AD78);
    func_table_register(0x01ACD5, mp_01ACD5);

    func_table_register(0x01C622, mp_01C622);
    func_table_register(0x01C95E, mp_01C95E);
    func_table_register(0x01C9CE, mp_01C9CE);
    func_table_register(0x01C9EF, mp_01C9EF);
    func_table_register(0x01A274, mp_01A274);
    func_table_register(0x01A5E4, mp_01A5E4);
    func_table_register(0x01C5F8, mp_01C5F8);
    func_table_register(0x01A54F, mp_01A54F);
    func_table_register(0x01A5BB, mp_01A5BB);
    func_table_register(0x01BD87, mp_01BD87);
    func_table_register(0x01BE40, mp_01BE40);
    func_table_register(0x01A819, mp_01A819);
    func_table_register(0x01AB05, mp_01AB05);
    func_table_register(0x01CA98, mp_01CA98);
    func_table_register(0x01CAAE, mp_01CAAE);
    func_table_register(0x01BC7B, mp_01BC7B);
    func_table_register(0x01C51D, mp_01C51D);
    func_table_register(0x01A8C4, mp_01A8C4);
}
