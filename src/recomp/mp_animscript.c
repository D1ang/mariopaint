/*
 * Mario Paint — sprite animation scripts, translated from the ROM.
 *
 *   $01:962C  run one frame of the animation script for sprite slot A
 *   $01:972D  run one script command (dispatched through $01:E393's table)
 *
 * Slot variables at $0792 + slot*8: X (10-bit, half pixels), Y (9-bit, half
 * pixels), delay counter, step index, sprite base. Script pointers per slot
 * at $0F:8000; each 4-byte step is [delay or command, dx, dy, sprite].
 * Commands (byte 0 with bit 7 set, operands b1-b3 = bytes 1-3):
 *   00 sprite base = word(b1,b2)       01 var[b1] = b2
 *   02 if --var[b1] != 0 goto b2       03 goto b1
 *   04-0B if var[b1] <cond> b2 goto b3 0C call b1
 *   0D-14 if var[b1] <cond> b2 call b3 15 return
 *   16-1D if var[b1] <cond> b2 return
 *   1E-2D if X/Y <cond> word(b1,b2) goto b3 (X and Y alternate)
 *   2E var[b1] = (random & b2) + b3
 * <cond> runs <, >=, ==, !=, minus, plus, V clear, V set: the ROM tests the
 * flags of CMP, and CMP leaves V alone, so the V tests see whatever V was
 * (from the caller, or command 2E's ADC). Variables are bytes at $0591+n,
 * the call stack is $0691+n with its pointer at $0791 (shared by all slots).
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t script;      /* $01: script base in bank $0F */
    uint16_t x;           /* slot offset (slot * 8) */
    uint16_t y;           /* step * 4 */
} Ctx;

/* [$01+4k],y: byte k of the current step (pointers wrap at 16 bits, the
 * +Y carries into the bank as for any long indirect access). */
static inline uint8_t op8(const Ctx *c, int k) {
    return bus_read8((uint8_t)((0x0F0000u + (uint16_t)(c->script + k) + c->y) >> 16),
                     (uint16_t)(0x0F0000u + (uint16_t)(c->script + k) + c->y));
}
static inline uint16_t op16(const Ctx *c, int k) {
    return read_long16(0x0F0000u + (uint16_t)(c->script + k) + c->y);
}

#define STEP(c)     (0x0797 + (c)->x)
#define VAR(n)      ((uint16_t)(0x0591 + (n)))

static inline void next_step(const Ctx *c) {
    bus_wram_write8(STEP(c), (uint8_t)(bus_wram_read8(STEP(c)) + 1));
}

/* CMP followed by the "skip" branch of the handler: true = condition met
 * (the handler acts), false = it moves on to the next step. */
static bool cond_met(int which, uint32_t a, uint32_t m, int bits) {
    uint32_t mask = bits == 8 ? 0xFF : 0xFFFF;
    a &= mask; m &= mask;
    bool c = a >= m, z = a == m, n = ((a - m) & mask) >> (bits - 1);
    bool v = g_cpu.flag_V;
    g_cpu.flag_C = c; g_cpu.flag_Z = z; g_cpu.flag_N = n;
    switch (which) {            /* skip branch: BCS BCC BNE BEQ BPL BMI BVS BVC */
    case 0: return !c;
    case 1: return c;
    case 2: return z;
    case 3: return !z;
    case 4: return n;
    case 5: return !n;
    case 6: return !v;
    default: return v;
    }
}

static void call_to(const Ctx *c, uint8_t target) {         /* $98DC body */
    uint8_t sp = bus_wram_read8(0x0791);
    bus_wram_write8(0x0791, (uint8_t)(sp + 1));
    bus_wram_write8((uint16_t)(0x0691 + sp), (uint8_t)(bus_wram_read8(STEP(c)) + 1));
    bus_wram_write8(STEP(c), target);
}

static void return_from(const Ctx *c) {                     /* $9AAA body */
    uint8_t sp = (uint8_t)(bus_wram_read8(0x0791) - 1);
    bus_wram_write8(0x0791, sp);
    bus_wram_write8(STEP(c), bus_wram_read8((uint16_t)(0x0691 + sp)));
}

/* ---- $01:972D ---- */
static void run_command(const Ctx *c, uint8_t cmd) {
    uint8_t b1 = op8(c, 1), b2 = op8(c, 2), b3 = op8(c, 3);
    if (cmd == 0x00) {                                      /* $9792 */
        bus_wram_write16((uint16_t)(0x0798 + c->x), op16(c, 1));
        next_step(c);
    } else if (cmd == 0x01) {                               /* $979F */
        bus_wram_write8(VAR(b1), b2);
        next_step(c);
    } else if (cmd == 0x02) {                               /* $97B4 */
        uint8_t v = (uint8_t)(bus_wram_read8(VAR(b1)) - 1);
        bus_wram_write8(VAR(b1), v);
        if (v != 0) bus_wram_write8(STEP(c), b2); else next_step(c);
    } else if (cmd == 0x03) {                               /* $97D2 */
        bus_wram_write8(STEP(c), b1);
    } else if (cmd <= 0x0B) {                               /* $97DC-$98BC */
        if (cond_met(cmd - 0x04, bus_wram_read8(VAR(b1)), b2, 8)) bus_wram_write8(STEP(c), b3);
        else next_step(c);
    } else if (cmd == 0x0C) {                               /* $98DC */
        call_to(c, b1);
    } else if (cmd <= 0x14) {                               /* $98FA-$9A74 */
        if (cond_met(cmd - 0x0D, bus_wram_read8(VAR(b1)), b2, 8)) call_to(c, b3);
        else next_step(c);
    } else if (cmd == 0x15) {                               /* $9AAA */
        return_from(c);
    } else if (cmd <= 0x1D) {                               /* $9AC2-$9C12 */
        if (cond_met(cmd - 0x16, bus_wram_read8(VAR(b1)), b2, 8)) return_from(c);
        else next_step(c);
    } else if (cmd <= 0x2D) {                               /* $9C42-$9DC8 */
        int k = cmd - 0x1E;
        uint16_t pos = bus_wram_read16((uint16_t)((k & 1 ? 0x0794 : 0x0792) + c->x)) >> 1;
        if (cond_met(k >> 1, pos, op16(c, 1), 16)) bus_wram_write8(STEP(c), b3);
        else next_step(c);
    } else if (cmd == 0x2E) {                               /* $9DE2 */
        uint16_t saved_x = g_cpu.X;
        g_cpu.X = b1;
        call_jsl(0x01E20C);                                 /* random -> A */
        g_cpu.X = saved_x;
        uint8_t r = (uint8_t)(CPU_A8() & b2);
        uint16_t sum = (uint16_t)(r + b3);                  /* CLC; ADC (8-bit) */
        g_cpu.flag_V = (~(r ^ b3) & (r ^ sum) & 0x80) != 0;
        g_cpu.flag_C = sum > 0xFF;
        bus_wram_write8(VAR(b1), (uint8_t)sum);
        next_step(c);
    }
    /* Commands past $2E jump through bytes beyond the table in the ROM;
     * the scripts never use them. */
}

/* Add a signed offset to a slot coordinate and wrap it. */
static uint16_t move(uint16_t addr, uint16_t delta, uint16_t mask) {
    uint16_t v = (uint16_t)((bus_wram_read16(addr) + delta) & mask);
    bus_wram_write16(addr, v);
    return v;
}

/* ---- $01:962C ---- */
void mp_01962C(void) {
    uint8_t saved_p = cpu_get_p();
    uint16_t slot = CPU_A16();
    Ctx c;
    c.script = bus_read16(0x0F, (uint16_t)(0x8000 + (uint16_t)(slot << 1)));
    c.x = (uint16_t)(slot << 3);
    c.y = 0;

    uint16_t sprite;
    bool smooth;                                            /* step's sprite bit 7 */
    uint8_t d = (uint8_t)(bus_wram_read8((uint16_t)(0x0796 + c.x)) - 1);
    bus_wram_write8((uint16_t)(0x0796 + c.x), d);
    if (!(d & 0x80)) {                                      /* BPL $96B7: hold */
        c.y = (uint16_t)(bus_wram_read8(STEP(&c)) << 2);
        sprite = op16(&c, 3);
        smooth = (sprite & 0x0080) != 0;
        if (!smooth) goto draw;                             /* no movement */
        goto move_smooth;
    }
    next_step(&c);                                          /* INC $0797,x */
    for (;;) {                                              /* $966B */
        c.y = (uint16_t)(bus_wram_read8(STEP(&c)) << 2);
        uint8_t b0 = op8(&c, 0);
        if (!(b0 & 0x80)) break;
        run_command(&c, b0 & 0x7F);                         /* JSR $972D */
    }
    bus_wram_write8((uint16_t)(0x0796 + c.x), op8(&c, 0)); /* new delay */
    sprite = op16(&c, 3);
    smooth = (sprite & 0x0080) != 0;
    if (smooth) goto move_smooth;
    {                                                       /* whole pixels, on a new step */
        uint16_t dx = op8(&c, 1);
        if (dx & 0x80) dx |= 0x0100;
        move((uint16_t)(0x0792 + c.x), (uint16_t)(dx << 1), 0x03FF);
        uint16_t dy = op16(&c, 2);                          /* LDA [$09],y: dy + next byte */
        move((uint16_t)(0x0794 + c.x), (uint16_t)(dy << 1), 0x01FF);
    }
draw: {                                                     /* $96C9 */
        uint16_t id = (uint16_t)((sprite & 0x007F) + bus_wram_read16((uint16_t)(0x0798 + c.x)));
        CPU_SET_A16(id);
        g_cpu.X = bus_wram_read16((uint16_t)(0x0792 + c.x)) >> 1;
        g_cpu.Y = bus_wram_read16((uint16_t)(0x0794 + c.x)) >> 1;
        call_jsl(0x01FA68);
        goto out;
    }
move_smooth: {                                              /* $96E3: half pixels, every frame */
        uint16_t id = (uint16_t)((sprite & 0x007F) + bus_wram_read16((uint16_t)(0x0798 + c.x)));
        uint16_t dx = op8(&c, 1);
        if (dx & 0x80) dx |= 0x0300;
        uint16_t sx = move((uint16_t)(0x0792 + c.x), dx, 0x03FF) >> 1;
        uint16_t dy = op8(&c, 2);
        if (dy & 0x80) dy |= 0x0100;
        uint16_t sy = move((uint16_t)(0x0794 + c.x), dy, 0x01FF) >> 1;
        CPU_SET_A16(id);
        g_cpu.X = sx;
        g_cpu.Y = sy;
        call_jsl(0x01FA68);
    }
out:
    cpu_set_p(saved_p);
}

void mp_register_animscript(void) {
    func_table_register(0x01962C, mp_01962C);
}
