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

void mp_register_gnat(void) {
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
