/*
 * Mario Paint — main states of $00:87A8 (JMP ($87B0,X) by $09D6).
 *
 * The animation path screens (states $03 and $04, with the recording of a
 * path), the pattern editor (state $15), saving and loading (states $08 and
 * $0A) and state $0C, translated from the ROM.
 */
#include <snesrecomp/cpu_ops.h>
#include <snesrecomp/func_table.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <stdbool.h>

extern bool g_quit;

#define R16(o)      bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v)   bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
#define R8(o)       bus_read8(g_cpu.DB, (uint16_t)(o))
#define W8(o, v)    bus_write8(g_cpu.DB, (uint16_t)(o), (uint8_t)(v))
#define L16(b, o)   bus_read16((b), (uint16_t)(o))
#define SL16(b, o, v) bus_write16((b), (uint16_t)(o), (uint16_t)(v))
#define DP(o)       ((uint16_t)(g_cpu.DP + (o)))
#define DPW(o, v)   bus_write16(0x00, DP(o), (uint16_t)(v))
#define DPR(o)      bus_read16(0x00, DP(o))

static void php(void) { bus_wram_write8(g_cpu.S, cpu_get_p()); g_cpu.S = (uint16_t)(g_cpu.S - 1); }
static void plp(void) { g_cpu.S = (uint16_t)(g_cpu.S + 1); cpu_set_p(bus_wram_read8(g_cpu.S)); }

static uint16_t peek16(void) { return bus_wram_read16((uint16_t)(g_cpu.S + 1)); }
static uint16_t pla16(void) {
    uint16_t v = peek16();
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    CPU_SET_A16(v);
    return v;
}

static void sound(uint16_t n) { CPU_SET_A16(n); call_jsl(0x01D368); }
static void sprite_script(uint16_t a) { CPU_SET_A16(a); call_jsl(0x01CDE1); }
static void jsr_a(uint32_t addr, uint16_t a) { CPU_SET_A16(a); call_jsr(addr); }
static void jsl_a(uint32_t addr, uint16_t a) { CPU_SET_A16(a); call_jsl(addr); }

/* LDA #n : STA $AA : PHA : LDA #0 : PHA : JSR $9FC4 */
static void tool(uint16_t t) { DPW(0xAA, t); call_tool_switch(t, 0, 0); }

/* LDX #x : LDY #y : JSL $01:F91E with the metasprite in A. */
static void metasprite(uint16_t a, uint16_t x, uint16_t y) {
    g_cpu.X = x; g_cpu.Y = y; CPU_SET_A16(a);
    call_jsl(0x01F91E);
}

/* PHA; JSL; PLA; DEC; CMP #last; BPL: the objects first down to last. */
static void objects_down(uint32_t routine, uint16_t first, uint16_t last) {
    uint16_t a = first;
    do {
        push_a16(a);
        jsl_a(routine, a);
        a = (uint16_t)(pla16() - 1);
        CPU_SET_A16(a);
    } while (!((uint16_t)(a - last) & 0x8000));
}

/* -: LDA $053A : BNE -  until the NMI has sent the sound command: in the
 * ROM's own loop (spin to sync) where it can run, else by frames. */
static void wait_sound_sent(uint32_t spin, uint32_t sync) {
    if (R16(0x053A) != 0 && !recomp_rom_wait(spin, sync))
        while (R16(0x053A) != 0 && !g_quit) call_jsl(0x01E2CE);
    CPU_SET_A16(0);
}

/* A long JSL that runs over many frames while the interrupts animate the
 * screen: the ROM runs it (from the JSL at jsl_pc), as a native body cannot
 * take an interrupt inside itself. */
static void rom_jsl(uint32_t routine, uint32_t jsl_pc) {
    if (!recomp_rom_wait(jsl_pc, jsl_pc + 4)) call_jsl(routine);
}

static void frames_while_214(void) {
    do call_jsl(0x01E2CE); while (R16(0x0214) != 0 && !g_quit);
}

/* ---- State $03: back to the path screen ($00:C460). ---- */
void mp_00C460(void) {
    call_jsr(0x00BA69);
    push_a16(0x2100); push_a16(0xA2C0); push_a16(0x0014);
    CPU_SET_A16(0x0014);
    call_jsr(0x00C490);
    objects_down(0x01F825, 0x0022, 0x001B);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    CPU_SET_A16(0x0004);
    W16(0x09D6, 0x0004);
}

/* The tile of the path screen's map ($7E:2000) under the cursor. */
static uint16_t tile_under_cursor(uint16_t y) {
    uint16_t row = (uint16_t)((y & 0xFFF8) << 3);
    push_a16(row);
    g_cpu.X = (uint16_t)(((R16(0x04DC) & 0xFFF8) >> 2) + peek16());
    pla16();
    uint16_t t = read_long16(0x7E2000u + g_cpu.X);
    CPU_SET_A16(t);
    return t;
}

/* ---- State $04: the path screen ($00:C4D6): a speed (x $40-$AF, y
 * $28-$87), the drawn paths below (y $98-$BF), the one on the right
 * (x $C0-$EF) or the exit (x $08-$16, y from $C8). ---- */
void mp_00C4D6(void) {
    objects_down(0x01F84F, 0x0022, 0x001B);
    if (!(R16(0x04CA) & 0x0020)) return;

    uint16_t x = R16(0x04DC), y = R16(0x04DE);
    if (x >= 0x0040 && x < 0x00B0 && y >= 0x0028 && y < 0x0088) {
        uint16_t n = y < 0x0048 ? 0 : y < 0x0068 ? 1 : 2;
        sound(n == 0 ? 0x0010 : (uint16_t)(n + 0x0042));
        g_cpu.Y = n;
        W16(0x19FA, 0x0001);
        DPW(0x0A, 0x4000);
        W16(0x19C2, n);
        SL16(0x7E, 0x3FFC, n);
        g_cpu.X = (uint16_t)((n + 0x001B) << 3);
        W16(0x1A15 + g_cpu.X, 0xFFFF);
        jsr_a(0x00C6C6, 0x0020);
        tail_jmp(0x00C626);
        return;
    }

    x = R16(0x04DC); y = R16(0x04DE);
    if (x >= 0x0008 && x < 0x00B0 && y >= 0x0098 && y < 0x00C0 && tile_under_cursor(y) != 0x01FF) {
        sound(0x0012);
        W16(0x1A1D, 0xFFFF);
        W16(0x1A25, 0xFFFF);
        jsr_a(0x00C6C6, 0x0030);
        tail_jmp(0x00BAAB);
        return;
    }

    x = R16(0x04DC); y = R16(0x04DE);
    if (x >= 0x00C0 && x < 0x00F0 && y >= 0x0028 && y < 0x00B8 && tile_under_cursor(y) != 0x01FF) {
        sound(0x0011);
        W16(0x1B05, 0xFFFF);
        W16(0x1B0D, 0xFFFF);
        W16(0x1A15, 0xFFFF);
        jsr_a(0x00C6C6, 0x00E0);
        tail_jmp(0x00BE43);
        return;
    }

    y = R16(0x04DE);
    CPU_SET_A16(y);
    if (y < 0x00C8) return;
    x = R16(0x04DC);
    CPU_SET_A16(x);
    if (x < 0x0008 || x >= 0x0017) return;
    sound(0x0041);
    W16(0x19FA, 0);
    DPW(0x0A, 0xA000);
    tail_jmp(0x00C626);
}

/* ---- $00:C626: from the path screen back to the canvas, with the path
 * ($19FA) or without. ---- */
void mp_00C626(void) {
    DPW(0x20, 0);
    if (R16(0x19FA) == 0) { push_a16(0x0007); CPU_SET_A16(0x0007); call_jsr(0x00C414); }
    else                  { push_a16(0x0003); CPU_SET_A16(0x0003); call_jsr(0x00C3DE); }
    call_jsl(0x01DE97);
    W16(0x0446, 0x0004);
    call_jsl(0x01E09B);
    sprite_script(0xCE4B);
    W16(0x04D0, R16(0x1A04));
    op_sep(0x10);
    g_cpu.X = 0x02;
    W8(0x0105, 0x02);
    op_rep(0x10);
    W16(0x09D6, 0);
    call_jsr(0x00A451);
    call_jsr(0x00A525);
    DPW(0xAA, pla16());
    if (R16(0x19FA) != 0) {
        uint16_t t = (uint16_t)(R16(0x04D0) & 0x00FF);
        if (t == 0x0006) {
            call_tool_switch(DPR(0xAA), 0, 0);
        } else {
            if (t == 0x0007) t--;
            call_tool_switch(DPR(0xAA), 1, &t);
        }
    } else {
        W16(0x19C0, 0);
        call_tool_switch(DPR(0xAA), 0, 0);
    }
    W16(0x04CA, 0);
    W16(0x0446, 0x0004);
    CPU_SET_A16(R16(0x04D0));
    if (R16(0x04D0) == 0x0006) W16(0x09AB, R16(0x09AB) + 1);
    call_jsr(0x00A363);
    call_jsr(0x00C873);
    tail_jmp(0x008B03);
}

/* The start both path screens share: tool and map reset, then $B98C and
 * the frames until the fade $0214 has run out. */
static void path_open(uint16_t t) {
    tool(t);
    W16(0x19FC, R16(0x19FA));
    W16(0x19FA, 0);
    DPW(0x0A, 0x4000);
    call_jsr(0x00B98C);
    W16(0x0206, 0);
    W16(0x0216, 0);
    W16(0x0214, R16(0x0214) + 1);
    frames_while_214();
    W16(0x0206, R16(0x0206) + 1);
    g_cpu.X = (uint16_t)(R16(0x1A08) << 1);
    W16(0x1A02, R16(0xBE17 + g_cpu.X));
}

/* ---- $00:BAAB: a drawn path picked: state $05 plays it, with its sound
 * bank loaded. ---- */
void mp_00BAAB(void) {
    W16(0x1A06, 0);
    W16(0x09D1, R16(0x09D1) + 1);
    W16(0x04D0, 0x000C);
    call_jsl(0x01E06F);
    call_jsr(0x00C414);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    path_open(0x0006);
    W16(0x0EAC, 0);
    W16(0x19FE, 0);
    W16(0x09D6, 0x0005);
    W16(0x1A0A, 0);
    W16(0x19EA, 0xFFFE);
    W16(0x19EE, 0);
    W16(0x19F2, 0);
    for (int x = 0x7E; x >= 0; x -= 2)
        SL16(0x7E, 0x2040 + x, (L16(0x7E, 0x2040 + x) & 0xE3FF) | 0x1000);
    g_cpu.X = 0xFFFE;
    sprite_script(0xCF2B);
    op_sep(0x20);
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    bus_write8(0x00, DP(0xCC), 0x00);
    bus_write8(0x00, DP(0xCD), 0x80);
    bus_write8(0x00, DP(0xCE), 0x1C);
    CPU_SET_A8(0xFF);
    W8(0x2140, 0xFF);
    call_jsl(0x01DF25);
    /* -: LDA #1 : STA $2140 : LDA $2140 : CMP #1 : BNE -  until the sound
     * CPU answers: in the ROM's own loop where it can run. */
    CPU_SET_A8(0x01);
    if (!recomp_rom_wait(0x00BB5A, 0x00BB66))
        for (int n = 0; n < 24000; n++) {
            W8(0x2140, 0x01);
            if (R8(0x2140) == 0x01 || !recomp_timed_spin(64)) break;
        }
    op_rep(0x20);
    call_jsl(0x01E2F3);
}

/* ---- $00:BE43: record a new path ($BF5F), pack its last bits, keep its
 * length and go back to the path screen ($BB6D). ---- */
void mp_00BE43(void) {
    php();
    W16(0x09D1, R16(0x09D1) + 1);
    call_jsl(0x01E06F);
    W16(0x04D0, 0x000A);
    call_jsr(0x00C414);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    path_open(0x0005);
    W16(0x19FE, 0);
    W16(0x1A0A, 0);
    W16(0x19EA, 0);
    W16(0x19EC, 0);
    W16(0x19F4, 0);
    W16(0x19F8, 0);
    W16(0x19F0, 0);
    for (int x = 0x7E; x >= 0; x -= 2) {
        uint16_t col = (uint16_t)(x & 0x003F);
        uint16_t pal = (col >= 0x0030 && col < 0x003E) ? 0x1400 : 0x0C00;
        SL16(0x7E, 0x2040 + x, (L16(0x7E, 0x2040 + x) & 0xE3FF) | pal);
    }
    g_cpu.X = 0xFFFE;
    sprite_script(0xCF48);
    DPW(0x0A, 0xA000);
    call_jsr(0x008B03);
    W16(0x04D6, 0x03FF);
    call_jsr(0x00BF5F);
    W16(0x04D6, 0x0110);
    call_jsr(0x008B18);
    if (R16(0x19EC) != 0) {
        W16(0x19EC, R16(0x19EC) + 1);
        W16(0x19EC, R16(0x19EC) + 1);
        op_sep(0x20);
        for (;;) {                       /* the flag bits shifted down */
            uint8_t v = R8(0x19F0);
            W8(0x19F0, v >> 1);
            if (v & 1) break;
            W8(0x19F1, R8(0x19F1) >> 1);
            W8(0x19F9, R8(0x19F9) >> 1);
        }
        W8(0x19CE, R8(0x19F1));
        W8(0x19CF, R8(0x19F9));
        uint16_t y = 0, x = R16(0x19EA);
        do {
            uint8_t b = R8(0x19CE + y);
            CPU_SET_A8(b);
            bus_write8(0x7E, (uint16_t)(0x3800 + x), b);
            y++; x++;
        } while (y != R16(0x19EC));
        g_cpu.X = x; g_cpu.Y = y;
        W16(0x19EC, x);
        op_rep(0x20);
    } else {
        call_jsr(0x00BA78);
    }
    W16(0x04D0, 0x0009);
    CPU_SET_A16(R16(0x19EC));
    SL16(0x7E, 0x3FFA, R16(0x19EC));
    plp();
    sound(0x0041);
    tail_jmp(0x00BB6D);
}

/* ---- $00:BB6D: after a recording: the path screen back (state $04). ---- */
void mp_00BB6D(void) {
    php();
    call_jsl(0x01E2CE);
    call_jsl(0x01E06F);
    call_jsr(0x00BA69);
    uint16_t path = R16(0x19FC);
    CPU_SET_A16(path);
    W16(0x19FA, path);
    if (path != 0) { call_jsr(0x00C3DE); DPW(0x0A, 0x4000); }
    else           { call_jsr(0x00C414); DPW(0x0A, 0xA000); }
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    W16(0x09D6, 0x0004);
    op_sep(0x20);
    CPU_SET_A8(0x02);
    W8(0x0105, 0x02);
    op_rep(0x20);
    call_jsr(0x0089C3);
    plp();
    call_jsl(0x01DE97);
    W16(0x0202, 0);
    W16(0x09AB, R16(0x09AB) + 1);
    call_jsr(0x00A363);
    W16(0x09D1, 0);
    W16(0x04D0, 0x0009);
    tool(0x0002);
    tail_jmp(0x00C460);
}

/* ---- $00:BF5F: the recording screen, a frame at a time, until the exit
 * (right button on x $08-$16, y from $C8). The button held draws ($C225)
 * while there is room; x $C8-$F3 above y $18 clears ($C386). ---- */
void mp_00BF5F(void) {
    while (!g_quit) {
        DPW(0x20, 0x0001);
        call_jsr(0x008B48);
        W16(0x04DC, R16(0x04DC) & 0x01FF);
        call_jsr(0x009378);
        W16(0x0446, 0x0004);
        op_sep(0x10);
        g_cpu.Y = (uint8_t)(R16(0x19C2) << 1);
        CPU_SET_A16(R16(0x19FE));
        g_cpu.X = R16(0x19FE) < R16(0xBE31 + g_cpu.Y) ? 0x02 : 0x0A;
        W8(0x0105, g_cpu.X);
        op_rep(0x10);
        call_jsr(0x00C04A);
        g_cpu.X = (uint16_t)(R16(0x19C2) << 1);
        uint16_t pic = (uint16_t)(R16(0xBE3D + g_cpu.X) + R16(0x19FE));
        g_cpu.X = R16(0x04DC);
        g_cpu.Y = R16(0x04DE);
        jsl_a(0x01F9BB, pic);

        uint16_t step = R16(0x1A02);
        bool next = step == 0;
        if (!next) {
            uint32_t sum = (uint32_t)step + R16(0x1A00);
            W16(0x1A00, sum);
            next = sum > 0xFFFF;
        }
        if (next) {
            g_cpu.X = (uint16_t)(R16(0x19C2) << 1);
            W16(0x19FE, R16(0x19FE) + 1);
            if (R16(0x19FE) == R16(0xBE37 + g_cpu.X)) W16(0x19FE, 0);
        }
        call_jsl(0x01E09B);
        call_jsl(0x01E2CE);
        W16(0x1996, 0);
        if (!(R16(0x04CA) & 0x0010)) {
            W16(0x1998, 0);
            W16(0x199A, 0);
            continue;
        }
        if (R16(0x04DE) >= 0x00C8 && (R16(0x04CA) & 0x0020)) {
            uint16_t x = R16(0x04DC);
            CPU_SET_A16(x);
            if (x >= 0x0008 && x < 0x0017) return;
        }
        if (R16(0x1970) != 0x0100) {
            W16(0x1996, R16(0x1996) + 1);
            call_jsr(0x00C225);
        } else if (!(R16(0x016C) & 0x0007)) {
            sound(0x0013);
        }
        if ((R16(0x04CA) & 0x0020) && R16(0x04DE) < 0x0018 &&
            R16(0x04DC) >= 0x00C8 && R16(0x04DC) < 0x00F4)
            call_jsr(0x00C386);
    }
}

/* ---- $00:C04A: the room left on the path, as a percentage (three digits
 * from the tables $C127/$C1A6) and a bar of sprites. ---- */
void mp_00C04A(void) {
    uint16_t x = (uint16_t)((uint16_t)(R16(0x19EA) + R16(0x19EC)) >> 4);
    uint16_t pct = 0x0100, y = 0x0064;
    if (x < 0x007F) {
        pct = (uint16_t)(R16(0xC127 + x) & 0x00FF);
        y = R16(0xC1A6 + x);
    }
    g_cpu.X = x; g_cpu.Y = y;
    W16(0x1970, pct);
    W16(0x1968, y & 0x00FF);
    W16(0x196A, 0x0008);
    W16(0x196C, 0x0048);
    W16(0x196E, 0x3E11);
    W16(0x1972, 0);

    uint16_t d = (uint16_t)((R16(0x1970) & 0x0F00) >> 8);
    CPU_SET_A16(d);
    if (d != 0) {
        metasprite((uint16_t)(d + 0x00B8), 0x0018, 0x0007);
        W16(0x1972, R16(0x1972) + 1);
    }
    d = (uint16_t)((R16(0x1970) & 0x00F0) >> 4);
    CPU_SET_A16(d);
    if (d != 0 || (g_cpu.X = R16(0x1972)) != 0)
        metasprite((uint16_t)(d + 0x00B8), 0x0020, 0x0007);
    metasprite((uint16_t)((R16(0x1970) & 0x000F) + 0x00B8), 0x0028, 0x0007);

    g_cpu.X = R16(0x0446);
    do {
        while (R16(0x1968) >= R16(0x196A)) {
            W16(0x1968, R16(0x1968) - R16(0x196A));
            W16(0x0226 + g_cpu.X, R16(0x196C) | 0x0F00);
            CPU_SET_A16(R16(0x196E));
            W16(0x0228 + g_cpu.X, R16(0x196E));
            call_jsr(0x00AF2C);
            g_cpu.X = (uint16_t)(g_cpu.X + 4);
            W16(0x196C, R16(0x196A) + R16(0x196C));
        }
        W16(0x196E, R16(0x196E) - 1);
        if ((R16(0x196E) & 0x000F) == 0x000F) W16(0x196E, 0x3E01);
        W16(0x196A, R16(0x196A) >> 1);
    } while (R16(0x196A) != 0 && !g_quit);
    W16(0x0446, g_cpu.X);
}

/* ---- $00:C225: one point of the path: (x << 8 | y), repeats counted in
 * $19F4, a flag bit per entry in $19F0 (and x's ninth bit in $19F8); eight
 * entries go to $7E:3800 with their two flag bytes in front. ---- */
void mp_00C225(void) {
    php();
    if (!(R16(0x016C) & 0x000F)) sound(0x0026);
    push_a16((uint16_t)(R16(0x04DE) & 0x00FF));
    uint16_t point = (uint16_t)(((R16(0x04DC) & 0x00FF) << 8) | peek16());
    bus_wram_write16((uint16_t)(g_cpu.S + 1), point);
    CPU_SET_A16(point);
    g_cpu.X = R16(0x19EC);
    if (g_cpu.X != 0 && point == R16(0x19CE + g_cpu.X)) {
        W16(0x19F4, R16(0x19F4) + 1);
        CPU_SET_A16(R16(0x19F4));
        if (R16(0x19F4) != 0x00FF) goto out;
    }
    call_jsr(0x00C2FB);
    if (R16(0x19F4) != 0) {
        uint16_t old = R16(0x19CE + g_cpu.X);
        push_a16(old);
        W16(0x19CE + g_cpu.X, R16(0x19F4));
        g_cpu.X++;
        pla16();
        W16(0x19CE + g_cpu.X, old);
        W16(0x19F0, R16(0x19F0) | 0x8000);
        W16(0x19F4, 0);
    }
    g_cpu.X = (uint16_t)(g_cpu.X + 2);
    W16(0x19CE + g_cpu.X, peek16());
    W16(0x19EC, g_cpu.X);
    uint16_t f = R16(0x19F0);
    if (!(f & 1)) {                                   /* the group not full */
        W16(0x19F0, (f >> 1) | 0x0080);
        g_cpu.Y = R16(0x04DC);
        W16(0x19F8, (R16(0x19F8) >> 1) | (g_cpu.Y >= 0x0100 ? 0x8000 : 0));
        goto out;
    }
    op_sep(0x20);
    W8(0x19CE, R8(0x19F1));
    W8(0x19CF, R8(0x19F9));
    uint16_t y = 0, x = R16(0x19EA);
    do {
        bus_write8(0x7E, (uint16_t)(0x3800 + x), R8(0x19CE + y));
        y++; x++;
    } while (y != R16(0x19EC));
    g_cpu.X = x;
    op_rep(0x20);
    W16(0x19EA, x);
    W16(0x19D0, peek16());
    W16(0x19EC, 0x0002);
    W16(0x19F0, 0x0080);
    W16(0x19F4, 0);
    W16(0x19F8, 0);
    g_cpu.Y = R16(0x04DC);
    if (g_cpu.Y >= 0x0100) W16(0x19F8, 0x8000);
out:
    pla16();
    plp();
}

/* ---- $00:C2FB: the path's dot on the canvas at the cursor: two rows of
 * pixels in the tile $B305 finds, alternating between two colours. ---- */
void mp_00C2FB(void) {
    uint16_t saved_x = g_cpu.X;
    push_a16(saved_x);
    uint16_t x = R16(0x04DC);
    CPU_SET_A16(x);
    if (x >= 0x0004 && x < 0x00FC) {
        DPW(0x80, x);
        uint16_t y = R16(0x04DE);
        CPU_SET_A16(y);
        if (y >= 0x001C && y < 0x00C4) {
            DPW(0x82, y);
            call_jsr(0x00B305);
            g_cpu.Y = DPR(0x84);
            g_cpu.X = (uint16_t)((R16(0x04DC) & 0x0007) << 1);
            push_a16(R16(0xC36A + g_cpu.X));                 /* mask */
            g_cpu.X = 0x0008;
            if ((uint16_t)(R16(0x19EA) + R16(0x19EC)) != 0) {
                W16(0x1974, R16(0x1974) + 1);
                g_cpu.X = (uint16_t)((R16(0x1974) & 0x0001) << 2);
            }
            push_a16(R16(0xC37A + g_cpu.X));
            push_a16(R16(0xC37C + g_cpu.X));
            for (int row = 0; row < 2; row++) {
                uint16_t colour = bus_wram_read16((uint16_t)(g_cpu.S + (row ? 3 : 1)));
                uint16_t mask = bus_wram_read16((uint16_t)(g_cpu.S + 5));
                if (row) g_cpu.Y = (uint16_t)(g_cpu.Y + 0x0010);
                uint32_t at = dp_long_ptr(g_cpu.DP, 0x0A) + g_cpu.Y;
                uint16_t old = read_long16(at);
                uint16_t v = (uint16_t)(((colour ^ old) & mask) ^ old);
                write_long16(at, v);
                CPU_SET_A16(v);
            }
            pla16(); pla16(); pla16();
        }
    }
    g_cpu.X = pla16();
}

/* ---- $00:C386: the recording thrown away: sound $0F, the canvas back,
 * and the clear animation from $C3CE (a frame picture, five frames each). */
void mp_00C386(void) {
    sound(0x000F);
    call_jsr(0x008B18);
    W16(0x19EA, 0);
    W16(0x19EC, 0);
    W16(0x19F4, 0);
    W16(0x19F8, 0);
    W16(0x19F0, 0);
    for (int16_t i = 0x000E; i >= 0 && !g_quit; i -= 2) {
        push_a16((uint16_t)i);
        g_cpu.X = (uint16_t)i;
        W16(0x0446, 0x0004);
        uint16_t pic = R16(0xC3CE + i);
        CPU_SET_A16(pic);
        if (!(pic & 0x8000)) metasprite(pic, 0x00C0, 0x0007);
        call_jsl(0x01E09B);
        for (uint16_t n = 5; n; n--) {
            push_a16(n);
            g_cpu.X = n;
            call_jsl(0x01E2CE);
            g_cpu.S = (uint16_t)(g_cpu.S + 2);
        }
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
        g_cpu.X = (uint16_t)(i - 2);
    }
}

/* ---- $00:CE60: the save/load menu's screen (state $06). ---- */
void mp_00CE60(void) {
    call_jsr(0x00C414);
    sprite_script(0xCF16);
    sprite_script(0xCE74);
    push_a16(0x2100); push_a16(0xB700); push_a16(0x0014);
    CPU_SET_A16(0x0014);
    call_jsr(0x00C490);
    for (int x = 0x7E; x >= 0; x -= 2)
        SL16(0x7E, 0x2040 + x, R16(0xCEC0 + x));
    g_cpu.X = 0xFFFE;
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    tool(0x000C);
    objects_down(0x01F825, 0x000D, 0x000A);
    jsl_a(0x01F825, 0x0030);
    CPU_SET_A16(0x0006);
    W16(0x09D6, 0x0006);
}

/* ---- State $0C ($00:D50D). ---- */
void mp_00D50D(void) {
    call_jsl(0x01EBB5);
    tail_jmp(0x00CE60);
}

/* ---- $00:D75B: the "cannot save" panel drawn into the map. ---- */
void mp_00D75B(void) {
    uint16_t x = 0x00A4, y = 0x006C;
    for (;;) {
        SL16(0x7E, 0x210C + x, R16(0xD796 + y));
        y = (uint16_t)(y - 2);
        x = (uint16_t)(x - 2);
        if (x & 0x003F) continue;
        x = (uint16_t)(x - 0x001C);
        if (x & 0x8000) break;
    }
    for (x = 0x0020; x != 0; x = (uint16_t)(x - 2))
        SL16(0x7E, 0x214E + x, R16(0xD802 + x));
    g_cpu.X = x; g_cpu.Y = y;
    call_jsl(0x01DE97);
    jsr_a(0x00D52C, 0xD56F);
    jsr_a(0x00D514, 0x0080);
}

/* One frame of the save/load screen's sprites. */
static void disk_sprites(void) {
    W16(0x0446, 0x0004);
    call_jsl(0x00D5F5);
    call_jsl(0x01E09B);
}

/* Frames of it until the object state byte at addr reaches value. */
static void disk_until(uint16_t addr, uint8_t value) {
    while ((R16(addr) & 0x00FF) != value && !g_quit) {
        disk_sprites();
        call_jsl(0x01E2CE);
    }
}

/* The common start of both screens, up to their wait for a click. */
static void disk_open(void) {
    call_jsr(0x00D842);
    W16(0x1980, 0x0090);
    W16(0x0446, 0x0004);
    call_jsr(0x00D590);
    call_jsl(0x01E09B);
    W16(0x0220, 0x0001);
    op_sep(0x20);
    CPU_SET_A8(0x22);
    W8(0x0111, 0x22);
    op_rep(0x20);
    call_jsr(0x00D73C);
}

/* One frame of the wait: true when the right button went down on the bar
 * (y from $C8, x from $08), with x in *x. */
static bool disk_click(uint16_t *x) {
    call_jsl(0x01E2CE);
    disk_sprites();
    call_jsr(0x008B48);
    if (R16(0x04DE) < 0x00C8) W16(0x04DE, 0x00C8);
    call_jsr(0x009378);
    if (!(R16(0x04CA) & 0x0020) || R16(0x04DE) < 0x00C8) return false;
    *x = R16(0x04DC);
    CPU_SET_A16(*x);
    return *x >= 0x0008;
}

static void disk_sound_change(uint32_t spin) {
    if (R16(0x1012) != R16(0x09AD)) {
        call_jsl(0x01D3AB);
        wait_sound_sent(spin, spin + 5);
    }
}

static void disk_close_start(void) {
    W16(0x0220, 0xFFFF);
    W16(0x0446, 0x0004);
    jsl_a(0x01F84F, 0x0018);
    call_jsl(0x01E09B);
    op_sep(0x20);
    CPU_SET_A8(R8(0x0105) & 0xDF);
    W8(0x0105, R8(0x0105) & 0xDF);
    op_rep(0x20);
}

/* ---- State $08: save ($00:D069). The picture, the song and the settings
 * are packed ($01:EDDB, $01:F03A) and written to the battery RAM with a
 * checksum; $055F is non-zero when it does not fit. ---- */
void mp_00D069(void) {
    W16(0x0206, 0);
    W16(0x0446, 0x0004);
    call_jsl(0x01E09B);
    call_jsr(0x00C414);
    call_jsl(0x01DE97);
    jsl_a(0x01F84F, 0x0018);
    call_jsl(0x01E2CE);
    W16(0x04DE, 0x00D0);
    op_sep(0x20);
    W8(0x0105, R8(0x0105) | 0x20);
    op_rep(0x20);
    W16(0x055F, 0);
    W16(0x0555, 0xF86A);
    W16(0x0557, 0);
    disk_open();

    while (!g_quit) {
        uint16_t x;
        if (!disk_click(&x)) continue;
        if (x < 0x0017) {                                      /* leave */
            if (R16(0x055F) == 0) sound(0x0041);
            break;
        }
        if (x < 0x0070 || x >= 0x0090 || bus_read16(0x00, 0x055F) != 0) continue;
        sound(0x0012);
        CPU_SET_A16(0x0007);
        W16(0x09AD, 0x0007);
        call_jsl(0x01D2BF);
        jsr_a(0x00D52C, 0xD559);
        W16(0x1980, 0x0091);
        W16(0x1AD5, 0x03FF);
        call_jsr(0x00C414);
        call_jsl(0x01DE97);
        call_jsl(0x01E2CE);
        for (int i = 0x07FE; i >= 0; i -= 2)
            SL16(0x7E, 0x9C00 + i, L16(0x7E, 0x3800 + i));
        for (int i = 0x024E; i >= 0; i -= 2)
            SL16(0x7E, 0xFC00 + i, bus_read16(0x00, (uint16_t)(0x09E4 + i)));
        g_cpu.X = 0xFFFE;
        W16(0x0549, 0xD5F4);
        W16(0x054B, 0);
        CPU_SET_A16(0x0002);
        W16(0x054D, 0x0002);
        rom_jsl(0x01EDDB, 0x00D194);
        W16(0x055F, CPU_A16());
        if (CPU_A16() == 0) {
            W16(0x0557, 0);
            uint16_t len = L16(0x7F, 0x0002);
            push_a16(len);
            W16(0x0557, (R16(0x0557) << 1) | (len >> 15));
            uint32_t sum = (uint32_t)(uint16_t)(len << 1) + 0x0200;
            W16(0x0555, sum);
            W16(0x0557, R16(0x0557) + (sum >> 16));
            jsr_a(0x00D842, R16(0x0557));
            for (int k = 0; k < 2; k++) {
                uint16_t lo = R16(0x0559);
                W16(0x0559, lo << 1);
                W16(0x055B, (R16(0x055B) << 1) | (lo >> 15));
            }
            rom_jsl(0x01F03A, 0x00D1CB);
            W16(0x055F, CPU_A16());
            len = pla16();
            SL16(0x7F, 0x0002, len);
            if (len >= 0x7800) W16(0x055F, 0xFFFF);
        }
        if (R16(0x055F) != 0) {
            W16(0x055D, 0);
            jsr_a(0x00D75B, 0);
        } else {
            W16(0x197C, 0x7003);
            W16(0x197E, 0x2122);
            unsigned carry = 0;
            for (int i = 0x77FE; i >= 0; i -= 2) {
                uint16_t v = L16(0x7F, 0x2000 + i);
                push_a16(v);
                uint32_t s = (uint32_t)v + R16(0x197C) + carry;
                carry = s >> 16;
                W16(0x197C, s);
                W16(0x197E, peek16() ^ R16(0x197E));
                pla16();
                SL16(0x70, 0x0800 + i, v);
            }
            g_cpu.X = 0xFFFE;
            uint16_t len = L16(0x7F, 0x0002);
            SL16(0x70, 0x07FE, len);
            push_a16(len);
            SL16(0x70, 0x07C2, R16(0x197C) + peek16() + carry);
            SL16(0x70, 0x07C4, R16(0x197E) ^ peek16());
            pla16();
            W16(0x1982, 0x0001);
            call_jsl(0x01E2CE);
            disk_until(0x1ACE, 0x2B);
        }
        if (R16(0x055F) == 0) jsr_a(0x00D52C, 0xD564);
        W16(0x1980, 0x0090);
        jsr_a(0x00D514, 0x0080);
        if (R16(0x055F) == 0) {
            W16(0x1A8D, 0x06FF);
            W16(0x1AD5, 0xFFFF);
        }
        disk_sound_change(0x00D297);
        if (R16(0x055F) == 0) break;                /* else wait again */
    }

    if (R16(0x055F) == 0) {
        sound(0x0041);
        W16(0x058D, 0xFFFF);
        jsr_a(0x00D514, 0x0040);
    }
    disk_close_start();
    W16(0x0206, R16(0x0206) + 1);
    W16(0x058D, 0x0001);
    CPU_SET_A16(0x0001);
    call_jsr(0x008B03);
    CPU_SET_A16(R16(0x055F));
    tail_jmp(R16(0x055F) == 0 ? 0x00CE60 : 0x00CF50);
}

/* ---- State $0A: load ($00:D321): the saved data unpacked from the
 * battery RAM ($01:F21D, $01:EF36). ---- */
void mp_00D321(void) {
    W16(0x0446, 0x0004);
    call_jsl(0x01E09B);
    call_jsr(0x00C414);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    W16(0x04DE, 0x00D0);
    op_sep(0x20);
    W8(0x0105, R8(0x0105) | 0x20);
    op_rep(0x20);
    W16(0x0555, (uint16_t)((uint16_t)(L16(0x70, 0x07FE) - 0x0800) >> 1) + 0xBA50);
    W16(0x0557, 0);
    disk_open();

    while (!g_quit) {
        uint16_t x;
        if (!disk_click(&x)) continue;
        if (x < 0x0017) { sound(0x0041); break; }              /* leave */
        if (x < 0x0070 || x >= 0x0090 || R16(0x1982) == 0) continue;
        sound(0x0012);
        CPU_SET_A16(0x0007);
        W16(0x09AD, 0x0007);
        call_jsl(0x01D2BF);
        jsr_a(0x00D52C, 0xD57A);
        W16(0x1980, 0x0091);
        W16(0x1A8D, 0x09FF);
        for (int i = 0x77FE; i >= 0; i -= 2)
            SL16(0x7F, 0x2000 + i, L16(0x70, 0x0800 + i));
        g_cpu.X = 0xFFFE;
        W16(0x0549, 0xD5F4);
        W16(0x054B, 0);
        W16(0x054D, 0x0002);
        uint16_t len = L16(0x70, 0x07FE);
        push_a16(len);
        jsl_a(0x01F21D, len);
        call_jsl(0x01EF36);
        pla16();
        for (int i = 0x07FE; i >= 0; i -= 2)
            SL16(0x7E, 0x3800 + i, L16(0x7E, 0x9C00 + i));
        for (int i = 0x024E; i >= 0; i -= 2)
            bus_write16(0x00, (uint16_t)(0x09E4 + i), L16(0x7E, 0xFC00 + i));
        g_cpu.X = 0xFFFE;
        W16(0x19EC, L16(0x7E, 0x3FFA));
        W16(0x19C2, L16(0x7E, 0x3FFC));
        W16(0x1A08, L16(0x7E, 0x3FFE));
        call_jsl(0x01E2CE);
        disk_until(0x1ABE, 0x0E);
        jsr_a(0x00D52C, 0xD585);
        W16(0x1980, 0x0090);
        disk_until(0x1AB6, 0x0B);
        disk_sound_change(0x00D4BE);
        break;
    }

    sound(0x0041);
    W16(0x058D, 0xFFFF);
    jsr_a(0x00D514, 0x0040);
    disk_close_start();
    W16(0x058D, 0x0001);
    CPU_SET_A16(0x0001);
    call_jsr(0x008B03);
    tail_jmp(0x00CE60);
}

/* ---- State $15: the pattern editor ($00:D8F1). ---- */
void mp_00D8F1(void) {
    W16(0x1996, 0);
    objects_down(0x01F84F, 0x003E, 0x003C);
    uint16_t b = R16(0x04CA);
    CPU_SET_A16(b);
    g_cpu.Y = R16(0x099F);
    if (g_cpu.Y != 0 && (b & 0x0002)) { tail_jmp(0x00E335); return; }
    if (!(b & 0x0010)) { W16(0x053B, 0); return; }

    if (b & 0x0020) {
        uint16_t y = R16(0x04DE);
        CPU_SET_A16(y);
        if (y >= 0x00C8) {                                     /* the bar */
            uint16_t x = R16(0x04DC);
            CPU_SET_A16(x);
            if (x >= 0x0008 && x < 0x0017) { tail_jmp(0x00DD97); return; }
            if (x >= 0x0020 && x < 0x00CF) {
                if (R16(0x099F) != 0) { tail_jmp(0x00E388); return; }
            } else if (x >= 0x00D8 && x < 0x00E7) {
                tail_jmp(0x00E27C);
                return;
            }
            W16(0x053B, 0);
            return;
        }
        if (y < 0x0018) {                                      /* colours */
            call_jsr(0x008D2C);
            if (g_cpu.X == 0) return;
            uint16_t c = CPU_A16();
            W16(0x1124, c);
            W16(0x04D0, (uint16_t)((c & 0x00FF) << 8) | 0x000B);
            call_jsr(0x00E2CC);
            call_jsr(0x00E64F);
            sound(0x0004);
            return;
        }
        uint16_t s0 = g_cpu.S;
        call_jsr_ret(0x00DA49, 0xD982);
        if (g_cpu.S != s0) {          /* a button: it left this routine */
            g_cpu.S = s0;
            return;
        }
    }

    uint16_t y = R16(0x04DE);
    CPU_SET_A16(y);
    if (y < 0x0030 || y >= 0x00B0) return;
    uint16_t x = R16(0x04DC);
    CPU_SET_A16(x);
    if (x < 0x0030 || x >= 0x00B0) return;

    push_a16(x);                                               /* the grid */
    if (R16(0x04CA) & 0x0020) call_jsr(0x00E2A3);
    x = pla16();
    W16(0x1996, R16(0x1996) + 1);
    push_a16((uint16_t)((x & 0xFFF8) >> 2));
    g_cpu.X = (uint16_t)(((R16(0x04DE) & 0xFFF8) << 3) + peek16());
    pla16();
    uint16_t tile = (uint16_t)(R16(0x1124) + 0x00D0);
    CPU_SET_A16(tile);
    if (tile != read_long16(0x7E2800u + g_cpu.X)) {
        push_a16(tile);
        sound(0x002B);
        pla16();
    }
    write_long16(0x7E2800u + g_cpu.X, tile);
    push_a16((uint16_t)(((uint16_t)(R16(0x04DC) - 0x0030) & 0xFF40) >> 1));
    uint16_t at = (uint16_t)(((uint16_t)(R16(0x04DE) - 0x0030) & 0xFF40) + peek16());
    bus_wram_write16((uint16_t)(g_cpu.S + 1), at);
    g_cpu.Y = (uint16_t)((((uint16_t)(R16(0x04DE) - 0x0030) & 0x0038) >> 2) + peek16());
    pla16();
    g_cpu.X = (uint16_t)(R16(0x1124) << 1);
    W16(0x1128, R16(0xDB09 + g_cpu.X));
    W16(0x112A, R16(0xDB29 + g_cpu.X));
    g_cpu.X = (uint16_t)((((uint16_t)(R16(0x04DC) - 0x0030) >> 3) & 0x0007) << 1);
    for (int plane = 0; plane < 2; plane++) {
        uint16_t dst = (uint16_t)((plane ? 0x102C : 0x101C) + g_cpu.Y);
        uint16_t old = R16(dst);
        uint16_t v = (uint16_t)(((R16(plane ? 0x112A : 0x1128) ^ old) & R16(0xDB49 + g_cpu.X)) ^ old);
        W16(dst, v);
        CPU_SET_A16(v);
    }
    tail_jmp(0x00DE67);
}

/* ---- $00:DD97: leave the pattern editor: the sound bank and the canvas
 * back, the tool from before, and the state from before ($09D8). ---- */
void mp_00DD97(void) {
    php();
    call_jsl(0x01D3AB);
    wait_sound_sent(0x00DD9C, 0x00DDA1);
    sound(0x0041);
    op_sep(0x20);
    CPU_SET_A8(0x06);
    W8(0x010E, 0x06);
    op_rep(0x20);
    jsl_a(0x01E033, 0x3FFF);
    call_jsr(0x008A16);
    call_jsl(0x01E2CE);
    call_jsl(0x01E06F);
    W16(0x04D0, R16(0x101A));
    if ((R16(0x04D0) & 0x00FF) == 0x0007) W16(0x1992, R16(0x1992) + 1);
    DPW(0xA6, R16(0x1126));
    DPW(0x20, 0);
    if (R16(0x19FA) == 0) call_jsr(0x00C414);
    else call_jsr(0x00C3DE);
    call_jsl(0x01DE97);
    call_jsl(0x01E2CE);
    call_jsr(0x00A451);
    call_jsr(0x00A525);
    tool(0x0007);
    W16(0x09D6, R16(0x09D8));
    call_jsl(0x01E895);
    call_jsl(0x01E8F6);
    if (R16(0x1992) != 0) {
        jsr_a(0x00B66C, 0x000F);
        jsr_a(0x00B6F4, (uint16_t)(R16(0x1994) + 0x0008));
    } else {
        CPU_SET_A16(R16(0x04D0) & 0xFF00);
        DPW(0xA8, CPU_A16());
        call_jsr(0x0096E1);
        CPU_SET_A16(R16(0x04D0));
        if (R16(0x04D0) == 0x0006) W16(0x09AB, R16(0x09AB) + 1);
    }
    call_jsr(0x00A363);
    plp();
}

/* ---- States $16-$1C: saving a special stamp to the battery RAM's slots
 * and loading one, from the pattern editor. ---- */

static void editor_objects(void) { objects_down(0x01F84F, 0x003E, 0x003C); }

/* State $16 ($00:DB59): the clear button: its animation ($1AF6) clears the
 * grid at step 1 and goes back to the editor at step 5. */
void mp_00DB59(void) {
    uint16_t step = (uint16_t)(R16(0x1AF6) & 0x00FF);
    CPU_SET_A16(step);
    if (step == 0x0001) call_jsr(0x00DE46);
    else if (step == 0x0005) W16(0x09D6, 0x0015);
    editor_objects();
}

/* Three map words counting up from tile. */
static void tiles3(uint16_t at, uint16_t tile) {
    for (int i = 0; i < 3; i++) SL16(0x7E, at + 2 * i, tile + i);
}

/* State $17 ($00:DB84): the save button: the slots shown (state $18). */
void mp_00DB84(void) {
    sound(0x000D);
    W16(0x04DE, 0x0010);
    editor_objects();
    static const uint16_t blank[] = { 0x2040, 0x2042, 0x2044, 0x2080, 0x2082, 0x2084, 0x207C, 0x20BC };
    for (int i = 0; i < 8; i++) SL16(0x7E, blank[i], 0x21EE);
    SL16(0x7E, 0x207A, 0x20B1);
    SL16(0x7E, 0x20BA, 0xA0B1);
    call_jsl(0x01DE97);
    DPW(0xA6, 0x00F0);
    call_jsr(0x00A363);
    CPU_SET_A16(0x0018);
    W16(0x09D6, 0x0018);
}

/* State $18 ($00:DBE4): a click on a slot (top bar) writes the pattern
 * there ($DEB3); the right side leaves (state $19). */
void mp_00DBE4(void) {
    editor_objects();
    uint16_t y = R16(0x04DE);
    CPU_SET_A16(y);
    if (y >= 0x0018) {
        CPU_SET_A16(0x0019);
        W16(0x09D6, 0x0019);
        return;
    }
    if (!(R16(0x04CA) & 0x0020)) return;
    call_jsr(0x008D2C);
    if (g_cpu.X != 0x0001) return;
    W16(0x112C, CPU_A16());
    call_jsr(0x00DEB3);
    call_jsr(0x00DF1C);
    call_jsr(0x00E233);
    call_jsr(0x00A363);
    sound(0x0004);
}

/* $00:DF09: eight bytes of both bit planes to the slot at $70:0000 + X. */
void mp_00DF09(void) {
    SL16(0x70, g_cpu.X, R16(0x101C + g_cpu.Y));
    CPU_SET_A16(R16(0x102C + g_cpu.Y));
    SL16(0x70, 0x0010 + g_cpu.X, CPU_A16());
    g_cpu.X = (uint16_t)(g_cpu.X + 2);
    g_cpu.Y = (uint16_t)(g_cpu.Y + 2);
}

/* $00:DEB3: the pattern's four tiles to the battery RAM slot $112C. */
void mp_00DEB3(void) {
    push_a16((uint16_t)((R16(0x112C) & 0x0008) << 7));
    g_cpu.X = (uint16_t)(((R16(0x112C) & 0x0007) << 6) + peek16());
    pla16();
    static const struct { uint16_t step, from, to; } part[4] = {
        { 0x0000, 0x00, 0x10 }, { 0x0010, 0x20, 0x30 }, { 0x01D0, 0x40, 0x50 }, { 0x0010, 0x60, 0x70 } };
    for (int i = 0; i < 4; i++) {
        if (i) g_cpu.X = (uint16_t)(g_cpu.X + part[i].step);
        g_cpu.Y = part[i].from;
        do call_jsr(0x00DF09); while (g_cpu.Y != part[i].to && !g_quit);
    }
}

/* State $19 ($00:DC29): the slots closed, back to the editor. */
void mp_00DC29(void) {
    W16(0x1AFD, 0x06FF);
    editor_objects();
    DPW(0xA6, 0x0100);
    call_jsr(0x00A363);
    tiles3(0x2040, 0x2100);
    tiles3(0x2080, 0x2110);
    SL16(0x7E, 0x207A, 0x212D);
    SL16(0x7E, 0x207C, 0x212E);
    SL16(0x7E, 0x20BA, 0x213D);
    SL16(0x7E, 0x20BC, 0x213E);
    call_jsl(0x01DE97);
    CPU_SET_A16(0x0015);
    W16(0x09D6, 0x0015);
}

/* State $1A ($00:DC8B): the load button: the slots shown (state $1B). */
void mp_00DC8B(void) {
    sound(0x000E);
    W16(0x04DE, 0x0010);
    editor_objects();
    DPW(0xA6, 0x00F0);
    call_jsr(0x00A363);
    jsr_a(0x00B66C, 0x00F0);
    jsr_a(0x00B6F4, 0x000D);
    call_jsr(0x00E64F);
    CPU_SET_A16(0x001B);
    W16(0x09D6, 0x001B);
}

/* State $1B ($00:DCC5): a slot under the held button shown, a click loads
 * it; the page arrows (either button) step the page in DP $A6; below the
 * top bar the screen closes (state $1C). */
void mp_00DCC5(void) {
    editor_objects();
    uint16_t y = R16(0x04DE);
    CPU_SET_A16(y);
    if (y >= 0x0018) {
        CPU_SET_A16(0x001C);
        W16(0x09D6, 0x001C);
        return;
    }
    uint16_t b = R16(0x04CA);
    CPU_SET_A16(b);
    if (!(b & 0x0011)) return;
    call_jsr(0x008D2C);
    if (g_cpu.X == 0x0001) { tail_jmp(0x00DD31); return; }
    if (g_cpu.X != 0x0002) return;
    b = (uint16_t)(R16(0x04CA) & 0x0044);
    CPU_SET_A16(b);
    if (!b) return;
    g_cpu.Y = (b & 0x0040) ? 0x0010 : 0xFFF0;
    push_a16(g_cpu.Y);
    uint16_t page = (uint16_t)(DPR(0xA6) + peek16());
    if (page == 0x0060) page = (uint16_t)(page + peek16());
    page &= 0x00F0;
    DPW(0xA6, page);
    uint16_t n = (uint16_t)(page >> 4);
    if (n >= 0x0006) n--;
    sound((uint16_t)(n + 0x0062));
    g_cpu.Y = pla16();
    call_jsr(0x00A363);
}

/* $00:DD31: slot A of the page: its cursor, and on a click its pattern
 * into the grid. */
void mp_00DD31(void) {
    W16(0x112C, CPU_A16());
    if (!(R16(0x04CA) & 0x0020)) return;
    jsr_a(0x00B66C, (uint16_t)(R16(0x112C) | DPR(0xA6)));
    jsr_a(0x00B6F4, 0x000D);
    call_jsr(0x00E64F);
    call_jsr(0x00E0D4);
    call_jsr(0x00E176);
    call_jsl(0x01DEB2);
    call_jsl(0x01E2CE);
    call_jsr(0x00DE67);
    sound(0x0004);
}

/* State $1C ($00:DD6D): the slots closed, the tool's colours back. */
void mp_00DD6D(void) {
    W16(0x1B05, 0x06FF);
    editor_objects();
    DPW(0xA6, 0x0100);
    call_jsr(0x00A363);
    call_jsr(0x00E2CC);
    call_jsr(0x00E64F);
    CPU_SET_A16(0x0015);
    W16(0x09D6, 0x0015);
}

void mp_register_states(void) {
    func_table_register(0x00C460, mp_00C460);
    func_table_register(0x00C4D6, mp_00C4D6);
    func_table_register(0x00C626, mp_00C626);
    func_table_register(0x00BAAB, mp_00BAAB);
    func_table_register(0x00BE43, mp_00BE43);
    func_table_register(0x00BB6D, mp_00BB6D);
    func_table_register(0x00BF5F, mp_00BF5F);
    func_table_register(0x00C04A, mp_00C04A);
    func_table_register(0x00C225, mp_00C225);
    func_table_register(0x00C2FB, mp_00C2FB);
    func_table_register(0x00C386, mp_00C386);
    func_table_register(0x00CE60, mp_00CE60);
    func_table_register(0x00D50D, mp_00D50D);
    func_table_register(0x00D75B, mp_00D75B);
    func_table_register(0x00D069, mp_00D069);
    func_table_register(0x00D321, mp_00D321);
    func_table_register(0x00D8F1, mp_00D8F1);
    func_table_register(0x00DD97, mp_00DD97);
    func_table_register(0x00DB59, mp_00DB59);
    func_table_register(0x00DB84, mp_00DB84);
    func_table_register(0x00DBE4, mp_00DBE4);
    func_table_register(0x00DEB3, mp_00DEB3);
    func_table_register(0x00DF09, mp_00DF09);
    func_table_register(0x00DC29, mp_00DC29);
    func_table_register(0x00DC8B, mp_00DC8B);
    func_table_register(0x00DCC5, mp_00DCC5);
    func_table_register(0x00DD31, mp_00DD31);
    func_table_register(0x00DD6D, mp_00DD6D);
}
