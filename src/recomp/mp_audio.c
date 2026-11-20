/*
 * Mario Paint — Recompiled audio engine.
 *
 * Handles SPC700 audio data upload and per-frame command processing.
 * The SPC700 upload ($01:DF25/$01:DFC1) is the ROM's own port handshake
 * with the IPL ROM, byte by byte.
 *
 * Audio command queues at $04EC/$04FC/$050C/$051C (4 channels, 16 bytes each)
 * with write pointers at $0530-$0533 and read pointers at $052C-$052F.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <snesrecomp/snesrecomp.h>
#include <mp/mem.h>
#include <snesrecomp/func_table.h>

#include <stdbool.h>
#include <stdio.h>

extern bool g_quit;

#define REG_APUIO0  0x2140
#define REG_APUIO1  0x2141
#define REG_APUIO2  0x2142
#define REG_APUIO3  0x2143

/* Wait until APU port 0 (8-bit) or ports 0-1 (16-bit) read back v, as the
 * ROM's CMP $2140 / BNE loops do. The SPC700 only moves while the CPU
 * spends cycles, so each poll lets time pass: in the timed loop the whole
 * machine's clock, otherwise just the APU. */
static inline void apu_poll_delay(void) {
    if (!recomp_timed_spin(48)) bus_run_cycles(8);
}
static void apu_wait8(uint8_t v) {
    for (long n = 0; bus_read8(0x00, REG_APUIO0) != v; n++) {
        if (n > 4000000 || g_quit) { printf("mp: APU handshake timeout ($%02X)\n", v); return; }
        apu_poll_delay();
    }
}
static void apu_wait16(uint16_t v) {
    for (long n = 0;; n++) {
        uint16_t got = (uint16_t)(bus_read8(0x00, REG_APUIO0) | (bus_read8(0x00, REG_APUIO1) << 8));
        if (got == v) return;
        if (n > 4000000 || g_quit) { printf("mp: APU handshake timeout ($%04X)\n", v); return; }
        apu_poll_delay();
    }
}

/* ---- $01:DFC1: step Y through the upload data at [$CC],Y. When Y passes
 * $7FFF the pointer moves to the next bank's $8000 half: INC $CE twice,
 * a dummy LDA [$CC] / STA $0000 (it reads bank+2 and stores into DB:0000,
 * and replaces A), DEC $CE, Y = 0. INC/DEC/LDA/STA have the width M has,
 * so with a 16-bit A $CF is carried into as well. */
static void dfc1(uint16_t dp, bool m8) {
    g_cpu.Y = (uint16_t)(g_cpu.Y + 1);
    if (!(g_cpu.Y & 0x8000)) { cpu_update_nz16(g_cpu.Y); return; }
    uint16_t ce = (uint16_t)(dp + 0xCE);
    if (m8) {
        bus_wram_write8(ce, (uint8_t)(bus_wram_read8(ce) + 2));
        uint32_t ptr = dp_long_ptr(dp, 0xCC);
        uint8_t v = bus_read8((uint8_t)(ptr >> 16), (uint16_t)ptr);
        CPU_SET_A8(v);
        bus_write8(g_cpu.DB, 0x0000, v);
        bus_wram_write8(ce, (uint8_t)(bus_wram_read8(ce) - 1));
    } else {
        bus_wram_write16(ce, (uint16_t)(bus_wram_read16(ce) + 2));
        uint16_t v = read_long16(dp_long_ptr(dp, 0xCC));
        CPU_SET_A16(v);
        bus_write16(g_cpu.DB, 0x0000, v);
        bus_wram_write16(ce, (uint16_t)(bus_wram_read16(ce) - 1));
    }
    g_cpu.Y = 0x0000;
    g_cpu.flag_Z = true;
    g_cpu.flag_N = false;
}
void mp_01DFC1(void) { dfc1(g_cpu.DP, g_cpu.flag_M); }

/* LDA [$CC],Y with D = 0. */
static uint8_t upload_byte(void) {
    uint32_t a = (dp_long_ptr(0, 0xCC) + g_cpu.Y) & 0xFFFFFF;
    return bus_read8((uint8_t)(a >> 16), (uint16_t)a);
}
static uint16_t upload_word(void) { return read_long16(dp_long_ptr(0, 0xCC) + g_cpu.Y); }

/* ========================================================================
 * $01:DF25 — upload a program/data set to the SPC700 through the IPL ROM's
 * port protocol. [$CC] points at blocks of [u16 size][u16 dest][bytes],
 * ended by size 0 with dest = where the SPC700 starts running.
 *
 * The pointer is split as the ROM does it: $CC becomes $8000 and Y holds
 * the offset, so $01:DFC1 can carry into the next bank. Port 0 carries the
 * byte counter (echoed by the SPC700), port 1 the byte; each block starts
 * with the kick value (counter + 4, never 0) and port 1 = 1 (more to come)
 * or 0 (jump to the address in ports 2-3).
 * ======================================================================== */
void mp_01DF25(void) {
    uint8_t p = cpu_get_p();                                /* PHP PHB PHD */
    uint8_t db = g_cpu.DB;
    uint16_t dp = g_cpu.DP;
    g_cpu.DB = 0x01;                                        /* PHK PLB */
    g_cpu.DP = 0x0000;                                      /* PEA $0000 PLD */

    for (int i = 0; i < 4; i++)                             /* SEP #$30 */
        bus_wram_write8((uint16_t)(0x052C + i), bus_wram_read8((uint16_t)(0x0530 + i)));

    g_cpu.Y = (uint16_t)(bus_wram_read16(0x00CC) - 0x8000); /* REP #$30 */
    bus_wram_write16(0x00CC, 0x8000);
    CPU_SET_A16(0xBBAA);
    apu_wait16(0xBBAA);
    CPU_SET_A8(0xCC);                                       /* SEP #$20 */

    for (;;) {                                              /* $DF8E */
        uint8_t kick = CPU_A8();                            /* PHA */
        CPU_SET_A16(upload_word());                         /* REP #$20: size */
        dfc1(0, false);
        dfc1(0, false);
        g_cpu.X = CPU_A16();                                /* TAX */
        CPU_SET_A16(upload_word());                         /* destination */
        dfc1(0, false);
        dfc1(0, false);
        bus_write16(0x00, REG_APUIO2, CPU_A16());
        bool more = g_cpu.X >= 0x0001;                      /* SEP #$20; CPX #1 */
        CPU_SET_A8(more ? 1 : 0);                           /* LDA #0; ROL */
        bus_write8(0x00, REG_APUIO1, CPU_A8());
        g_cpu.flag_V = more;                                /* ADC #$7F */
        CPU_SET_A8(kick);                                   /* PLA */
        bus_write8(0x00, REG_APUIO0, kick);
        apu_wait8(kick);
        if (!more) break;                                   /* BVS $DF64 */

        CPU_SET_A8(upload_byte());                          /* $DF64 */
        dfc1(0, true);
        op_xba();
        CPU_SET_A8(0x00);
        for (;;) {                                          /* $DF7B */
            bus_write16(0x00, REG_APUIO0, CPU_A16());
            g_cpu.X = (uint16_t)(g_cpu.X - 1);
            if (g_cpu.X == 0) break;
            op_xba();                                       /* $DF6E */
            CPU_SET_A8(upload_byte());
            dfc1(0, true);
            op_xba();
            apu_wait8(CPU_A8());
            CPU_SET_A8(CPU_A8() + 1);
        }
        apu_wait8(CPU_A8());                                /* $DF85 */
        g_cpu.flag_C = true;
        do {                                                /* ADC #$03; BEQ */
            unsigned r = (unsigned)CPU_A8() + 3 + (g_cpu.flag_C ? 1 : 0);
            g_cpu.flag_C = r > 0xFF;
            CPU_SET_A8(r);
        } while (CPU_A8() == 0);
    }

    g_cpu.DP = dp;                                          /* PLD PLB PLP */
    g_cpu.DB = db;
    cpu_set_p(p);
    if (g_cpu.flag_X) { g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF; }
}

/* ========================================================================
 * $01:D308 / $01:D328 / $01:D348 / $01:D368 — APU command queue push
 *
 * Channel N keeps a 16-byte ring at $04EC + $10*N with write index $0530+N
 * and read index $052C+N. The ROM pushes A's low byte, advances the write
 * index unless that would catch up with the read index (full: the byte is
 * written but not committed), and returns with A's low byte = the advanced
 * index. PHX/PHP ... PLP/PLX: X and every flag, M and X included, come back
 * unchanged, which callers depend on (they continue in 16-bit mode).
 * ======================================================================== */
static void apu_queue_push(int ch) {
    uint8_t wp = bus_wram_read8(0x0530 + ch);
    bus_wram_write8(0x04EC + 0x10 * ch + wp, CPU_A8());
    wp = (wp + 1) & 0x0F;
    if (wp != bus_wram_read8(0x052C + ch))
        bus_wram_write8(0x0530 + ch, wp);
    CPU_SET_A8(wp);
}

void mp_01D308(void) { apu_queue_push(0); }
void mp_01D328(void) { apu_queue_push(1); }
void mp_01D348(void) { apu_queue_push(2); }
void mp_01D368(void) { apu_queue_push(3); }

/* ========================================================================
 * $01:D2BF — Channel-0 command that first makes sure the APU is listening
 *
 * If no command is outstanding ($053A == 0) and channel 0's ring is empty,
 * queue $02 on channel 2, arm the $20-frame $053A timeout, then push A on
 * channel 0 as $01:D308 does. Otherwise the byte is written at the read index
 * (overwriting the next unsent command) and A comes back unchanged.
 * Registers and flags preserved as in $01:D308.
 * ======================================================================== */
void mp_01D2BF(void) {
    uint8_t val = CPU_A8();
    if (bus_wram_read8(0x053A) != 0 || bus_wram_read8(0x0530) != bus_wram_read8(0x052C)) {
        bus_wram_write8(0x04EC + bus_wram_read8(0x052C), val);
        return;
    }
    CPU_SET_A8(0x02);
    apu_queue_push(2);
    bus_wram_write8(0x053A, 0x20);
    CPU_SET_A8(val);
    apu_queue_push(0);
}

/* ========================================================================
 * $01:D388 — Sound reset (A 8-bit)
 *
 * Command $00 through $01:D2BF, a spin until the NMI's sound handler has
 * cleared the busy count $053A, then $12 frames.
 * ======================================================================== */
void mp_01D388(void) {
    CPU_SET_A8(0x00);
    call_jsl(0x01D2BF);
    /* -: LDA $053A : BNE -  The NMI's sound handler counts it down. A
     * native body cannot take the NMI in the middle of its own loop, so it
     * waits in the ROM's loop ($D38E, on to $D393); only where it cannot
     * (outside the timed loop) does it wait by frames instead. It passes the
     * check, but in the timed loop the bank upload after it ($DF25) then
     * waits for the sound CPU forever, so it is not run natively there
     * (conformance.py NOT_TIMED). */
    if (bus_read8(g_cpu.DB, 0x053A) != 0 && !recomp_rom_wait(0x01D38E, 0x01D393))
        while (bus_read8(g_cpu.DB, 0x053A) != 0 && !g_quit)
            call_jsl(0x01E2CE);
    for (uint8_t n = 0x12; n; n--) {
        CPU_SET_A8(n);
        call_jsl(0x01E2CE);
    }
    CPU_SET_A8(0x00);
}

/* ========================================================================
 * $01:DDB8 — NMI audio: check queue drain timer
 *
 * Manages the $053D countdown timer that ensures audio commands
 * have time to process before new ones are sent.
 * ======================================================================== */
void mp_01DDB8(void) {
    if (bus_wram_read8(0x053B) == 0) return;

    uint8_t x = bus_wram_read8(0x053F);
    uint8_t rp = bus_wram_read8(0x052C + x);
    uint8_t wp = bus_wram_read8(0x0530 + x);

    if (rp != wp) return;

    if (bus_wram_read8(0x053D) == 0) {
        bus_wram_write8(0x053D, bus_wram_read8(0x053B));
        rp = (rp - 1) & 0x0F;
        bus_wram_write8(0x052C + x, rp);
    }

    uint8_t timer = bus_wram_read8(0x053D);
    if (timer > 0) bus_wram_write8(0x053D, timer - 1);
}

/* ========================================================================
 * $01:DDE1 — NMI audio: process channel 0 commands
 *
 * Reads commands from channel 0 queue ($04EC) and sends them
 * to APU port 0. Handles the handshake protocol where the SPC700
 * acknowledges by echoing the sent byte.
 * ======================================================================== */
void mp_01DDE1(void) {
    if (bus_wram_read8(0x0538) != 0) return;

    /* Check $053A countdown */
    if (bus_wram_read8(0x053A) != 0) {
        uint8_t t = bus_wram_read8(0x053A) - 1;
        bus_wram_write8(0x053A, t);
        return;
    }

    /* Process channel 0 queue */
    uint8_t rp = bus_wram_read8(0x052C);
    uint8_t wp = bus_wram_read8(0x0530);
    if (rp == wp) return;

    uint8_t sending = bus_wram_read8(0x0534);
    uint8_t data = bus_wram_read8(0x04EC + rp);

    if (sending == 0) {
        /* Send new byte */
        bus_write8(0x00, REG_APUIO0, data);
        bus_wram_write8(0x0534, 0x01);
    } else {
        /* Check if SPC700 acknowledged */
        uint8_t ack = bus_read8(0x00, REG_APUIO0);
        if (ack == data) {
            /* Acknowledged — advance read pointer */
            rp = (rp + 1) & 0x0F;
            bus_wram_write8(0x052C, rp);
            bus_wram_write8(0x0534, 0x00);
        } else {
            /* Not yet — resend */
            bus_write8(0x00, REG_APUIO0, data);
        }
    }
}

/* ========================================================================
 * $01:DE2D — NMI: send one queued command per APU channel 3..1
 *
 * Skipped while $0538 is set. For each channel X (ring at $04EC+$10*X,
 * indices $052C+X read / $0530+X write, state $0534+X, retries $0541+X):
 *   ring empty          -> clear APU port X
 *   idle, port busy     -> wait
 *   idle, port 0        -> write the command, 4 retries, state = sending
 *   sending, echoed     -> advance read index, back to idle, clear the port
 *   sending, no echo    -> rewrite it until retries run out, then as echoed
 * PHP/PLP around the loop; A returns as the last ring base pulled ($04EC),
 * X as 0 and Y as channel 1's read index (+1 if it advanced).
 * ======================================================================== */
void mp_01DE2D(void) {
    uint8_t busy = bus_wram_read8(0x0538);
    if (busy != 0) {
        CPU_SET_A8(busy);
        g_cpu.flag_Z = false;
        g_cpu.flag_N = (busy & 0x80) != 0;
        return;
    }
    uint8_t y = (uint8_t)g_cpu.Y;
    for (int x = 3; x >= 1; x--) {
        uint16_t base = (uint16_t)(0x04EC + 0x10 * x);
        uint16_t port = (uint16_t)(REG_APUIO0 + x);
        y = bus_wram_read8(0x052C + x);
        bool done = false;
        if (y == bus_wram_read8(0x0530 + x)) {
            bus_write8(0x00, port, 0x00);
            continue;
        }
        if (bus_wram_read8(0x0534 + x) == 0) {
            if (bus_read8(0x00, port) != 0) continue;
            bus_wram_write8(0x0541 + x, 0x04);
            bus_write8(0x00, port, bus_wram_read8(base + y));
            bus_wram_write8(0x0534 + x, 0x01);
            continue;
        }
        uint8_t data = bus_wram_read8(base + y);
        if (data == bus_read8(0x00, port)) {
            done = true;
        } else {
            uint8_t retry = (uint8_t)(bus_wram_read8(0x0541 + x) - 1);
            bus_wram_write8(0x0541 + x, retry);
            if (retry == 0) done = true;
            else {
                bus_write8(0x00, port, data);
                bus_wram_write8(0x0534 + x, 0x01);
            }
        }
        if (done) {
            y++;
            bus_wram_write8(0x052C + x, y & 0x0F);
            bus_wram_write8(0x0534 + x, 0x00);
            bus_wram_write8(0x0541 + x, 0x00);
            bus_write8(0x00, port, 0x00);
        }
    }
    g_cpu.C = 0x04EC;
    g_cpu.X = 0;
    g_cpu.Y = g_cpu.flag_X ? y : (uint16_t)((g_cpu.Y & 0xFF00) | y);
    g_cpu.flag_Z = true;     /* PLP: the flags of LDA $0538 (zero) */
    g_cpu.flag_N = false;
}

/* ========================================================================
 * $01:DFD3 — Compute sprite size mirror values
 *
 * Reads OBSEL ($0105) to determine the sprite sizes configured,
 * then stores the pixel dimensions into $012E (small) and $0130 (large).
 * ======================================================================== */
void mp_01DFD3(void) {
    /* Table: small/large sprite pixel widths indexed by OBSEL size bits */
    static const uint8_t small_sizes[6] = { 0x08, 0x08, 0x08, 0x10, 0x10, 0x20 };
    static const uint8_t large_sizes[6] = { 0x10, 0x20, 0x40, 0x20, 0x40, 0x40 };

    uint8_t obsel = bus_wram_read8(0x0105);
    uint8_t idx = (obsel & 0xE0) >> 5;
    if (idx >= 6) idx = 5;

    bus_wram_write16(0x012E, small_sizes[idx]);
    bus_wram_write16(0x0130, large_sizes[idx]);
}

/* ========================================================================
 * Register all audio engine functions.
 * ======================================================================== */
void mp_register_audio(void) {
    func_table_register(0x01DF25, mp_01DF25);
    func_table_register(0x01DFC1, mp_01DFC1);
    func_table_register(0x01D308, mp_01D308);
    func_table_register(0x01D328, mp_01D328);
    func_table_register(0x01D348, mp_01D348);
    func_table_register(0x01D368, mp_01D368);
    func_table_register(0x01D2BF, mp_01D2BF);
    func_table_register(0x01D388, mp_01D388);
    func_table_register(0x01DDB8, mp_01DDB8);
    func_table_register(0x01DDE1, mp_01DDE1);
    func_table_register(0x01DE2D, mp_01DE2D);
    func_table_register(0x01DFD3, mp_01DFD3);
}
