/*
 * Mario Paint — Gnat Attack, translated from the ROM.
 *
 *   $01:A54F  move the swatter with the mouse (clamped to the field)
 *   $01:A5BB  draw the swatter in OAM slot 0
 *   $01:BD87  animate one gnat (8-bit registers; script via the caller)
 *   $01:BE40  swat test / falling gnat
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
                uint8_t i = (uint8_t)(x >> 4);
                tail_jmp(0x010000u | bus_read16(0x01, (uint16_t)(0xBF21 + (i << 1))));
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
}
