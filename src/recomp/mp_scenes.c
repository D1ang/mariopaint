/*
 * Scene loaders: the routines that switch between the big screens
 * (forced blank, VRAM uploads by DMA, sound bank change, mode setup).
 * Translated by hand from the ROM; graphics come from the ROM at runtime.
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>
#include <snesrecomp/func_table.h>
#include <stdint.h>
#include <stdbool.h>

extern bool g_quit;

static void push8(uint8_t v) { bus_wram_write8(g_cpu.S, v); g_cpu.S = (uint16_t)(g_cpu.S - 1); }
static uint8_t pop8(void) { g_cpu.S = (uint16_t)(g_cpu.S + 1); return bus_wram_read8(g_cpu.S); }

/* One channel-0 DMA: mode, B-bus register, 24-bit source and size, with
 * the registers written in the ROM's order, then the start. */
static void dma0(uint8_t mode, uint8_t breg, uint32_t src, uint16_t size) {
    uint8_t db = g_cpu.DB;
    bus_write8(db, 0x4300, mode);
    bus_write8(db, 0x4301, breg);
    bus_write8(db, 0x4302, (uint8_t)src);
    bus_write8(db, 0x4303, (uint8_t)(src >> 8));
    bus_write8(db, 0x4304, (uint8_t)(src >> 16));
    bus_write8(db, 0x4305, (uint8_t)size);
    bus_write8(db, 0x4306, (uint8_t)(size >> 8));
    bus_write8(db, 0x420B, 0x01);
}
/* A DMA to VRAM at vaddr (increment mode $80, mode 1 to $2118). */
static void dma_vram(uint16_t vaddr, uint32_t src, uint16_t size) {
    uint8_t db = g_cpu.DB;
    bus_write8(db, 0x2116, (uint8_t)vaddr);
    bus_write8(db, 0x2117, (uint8_t)(vaddr >> 8));
    bus_write8(db, 0x2115, 0x80);
    dma0(0x01, 0x18, src, size);
}
/* The whole palette by DMA to CGRAM from address 0. */
static void dma_cgram(uint32_t src) {
    bus_write8(g_cpu.DB, 0x2121, 0x00);
    dma0(0x00, 0x22, src, 0x0200);
}
/* Forced blank and the screen off ($01:E30E, $E87B, $E88A), with A 8-bit,
 * then SEI and the ROM's short delay loop. */
static void blank_screen(void) {
    call_jsl(0x01E30E);
    call_jsl(0x01E87B);
    call_jsl(0x01E88A);
    g_cpu.flag_I = true;
    CPU_SET_A8(0x00);
}

/* Sound bank change: the APU source pointer in DP $CC..$CE, $FF to port 0
 * and the upload ($01:DF25). A is 8-bit here. */
static void sound_bank(uint8_t bank) {
    uint16_t dp = g_cpu.DP;
    bus_write8(0x00, (uint16_t)(dp + 0xCC), 0x00);
    bus_write8(0x00, (uint16_t)(dp + 0xCD), 0x80);
    bus_write8(0x00, (uint16_t)(dp + 0xCE), bank);
    bus_write8(g_cpu.DB, 0x2140, 0xFF);
    CPU_SET_A8(0xFF);
    call_jsl(0x01DF25);
}

/* BG1/BG2 character bases ($210B/$210C with shadows $010E/$010F) and the
 * main/window layer masks ($212C/$212E with shadows $011A/$011C). */
static void bg_setup(uint8_t b0b, uint8_t b0c, uint8_t layers, bool layers_first) {
    uint8_t db = g_cpu.DB;
    if (layers_first) {
        bus_write8(db, 0x011A, layers); bus_write8(db, 0x212C, layers);
        bus_write8(db, 0x011C, layers); bus_write8(db, 0x212E, layers);
    }
    bus_write8(db, 0x210B, b0b); bus_write8(db, 0x010E, b0b);
    bus_write8(db, 0x210C, b0c); bus_write8(db, 0x010F, b0c);
    if (!layers_first) {
        bus_write8(db, 0x011A, layers); bus_write8(db, 0x212C, layers);
        bus_write8(db, 0x011C, layers); bus_write8(db, 0x212E, layers);
    }
    CPU_SET_A8(layers_first ? b0c : layers);
}

/* $01:9F6C (JSL): the coloring-book/stamp-picture scene: fade out, the
 * blank screen, its graphics into VRAM, the scene's sound bank $1E, BG
 * setup, the scene's state in bank $7F, its tilemaps ($A30D..$A44D), the
 * scene loop ($A505, which returns through the stack point kept in
 * $7F:0006), then back: sound bank $1A, the canvas, a wait until the
 * screen update is taken ($0208), the canvas BG setup and its tool bar. */
void mp_019F6C(void) {
#define R16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
    W16(0x0589, 0x0001);
    push8(cpu_get_p());
    call_jsl(0x01E2CE);
    bus_write16(0x00, 0x04E4, bus_read16(0x00, 0x04E2));
    bus_write16(0x00, 0x04E2, 0x0000);
    W16(0x09A9, 0xFFFF);
    W16(0x0206, 0x0000);
    call_jsl(0x01E06F);
    call_jsl(0x01E2CE);
    call_jsl(0x01E7C9);
    g_cpu.flag_M = true;
    blank_screen();
    dma_vram(0x4000, 0x0D8000, 0x2C00);
    dma_vram(0x5600, 0x09EC00, 0x0C00);
    dma_vram(0x5C00, 0x089000, 0x0600);
    dma_vram(0x5F00, 0x0DAC00, 0x0200);
    dma_vram(0x6000, 0x04C000, 0x2A00);
    dma_vram(0x7800, 0x04B000, 0x1000);
    CPU_SET_A8(0x01);
    g_cpu.flag_I = false;
    call_jsl(0x01E2F3);
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    sound_bank(0x1E);
    call_jsl(0x01E2F3);
    bg_setup(0x66, 0x66, 0x17, true);
    g_cpu.flag_M = false;
    W16(0x0178, 0x0000);
    W16(0x0176, 0x0000);
    push8(g_cpu.DB);
    g_cpu.DB = 0x7F;
    W16(0x0018, 0x0003);
    W16(0x05EA, 0); W16(0x05EB, 0);
    W16(0x0012, 0); W16(0x0014, 0); W16(0x0016, 0);
    W16(0x001A, 0); W16(0x001C, 0); W16(0x0022, 0); W16(0x0024, 0);
    W16(0x05EA, 0); W16(0x0026, 0); W16(0x0040, 0);
    if (bus_read16(0x00, 0x04DE) >= 0x00D0) bus_write16(0x00, 0x04DE, 0x00CF);
    CPU_SET_A16(0x0057); call_jsl(0x01E024);
    CPU_SET_A16(0x0057); call_jsl(0x01E033);
    CPU_SET_A16(0x00AE); call_jsl(0x01E042);
    call_jsr(0x01A30D);
    call_jsr(0x01A274);
    call_jsr(0x01A2B7);
    call_jsr(0x01A373);
    call_jsr(0x01A44D);
    call_jsl(0x01DE97);
    call_jsl(0x01DEB2);
    call_jsl(0x01DECD);
    call_jsl(0x01E2CE);
    call_jsl(0x01E794);
    call_jsl(0x01E2CE);
    bus_write16(0x00, 0x04E2, bus_read16(0x00, 0x04E4));
    push8(cpu_get_p());
    W16(0x0006, (uint16_t)(g_cpu.S - 2));        /* TSC; DEC; DEC: the loop's stack point */
    bus_write16(0x00, 0x04E6, 0x4A61);
    CPU_SET_A16(0x4A61);
    call_jsr(0x01A505);
    cpu_set_p(pop8());
    g_cpu.DB = pop8();
    call_jsl(0x01E7C9);
    call_jsl(0x01E06F);
    call_jsl(0x01E2CE);
    call_jsl(0x01E2CE);
    g_cpu.flag_M = true;
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    sound_bank(0x1A);
    g_cpu.flag_M = false;
    call_jsl(0x01E2F3);
    W16(0x0220, 0xFFFF);
    if (R16(0x04DE) >= 0x00D8) W16(0x04DE, 0x00D7);
    call_jsl(0x008994);
    W16(0x0208, 0x0000);
    CPU_SET_A16(0x0001);
    W16(0x0206, R16(0x0206) + 1);
    do call_jsl(0x01E2CE); while (R16(0x0208) != 0);
    g_cpu.flag_M = true;
    bg_setup(0x06, 0x66, 0x13, false);
    call_jsl(0x01E794);
    g_cpu.flag_M = false;
    call_jsl(0x00A4AD);
    call_jsl(0x01D3AB);
    cpu_set_p(pop8());
    if (g_cpu.flag_M) bus_write8(g_cpu.DB, 0x0589, 0);
    else W16(0x0589, 0x0000);
#undef R16
#undef W16
}

/* $01:8000 (JSL): the title/main screen: the start state ($00:D6D3), the
 * blank screen with its palette and graphics, BG setup, the title loop
 * (stack point in $7F:0003, $8260, $00:8A75). Back from it: done when a
 * save was loaded ($04E2); to the demo ($9372) when $0565 is set; else the
 * canvas graphics, the sound bank (bank $16 or $17 by the ROM-checksum
 * test $01:E20C into $0547), the BG bases and the start of the canvas. */
void mp_018000(void) {
#define R16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
    push8(cpu_get_p());
    push8(g_cpu.DB);
    call_jsl(0x00D6D3);
    g_cpu.flag_M = true;
    blank_screen();
    dma_cgram(0x02FC00);
    dma_vram(0x4000, 0x09C000, 0x4000);
    CPU_SET_A8(0x01);
    g_cpu.flag_I = false;
    call_jsl(0x01E2F3);
    bg_setup(0x04, 0x44, 0x13, true);
    g_cpu.flag_M = false;
    CPU_SET_A16(0x0045); call_jsl(0x01E024);
    call_jsl(0x01DE97);
    CPU_SET_A16(0x0000); call_jsl(0x01E033);
    call_jsl(0x008A12);
    W16(0x0170, 0); W16(0x016E, 0); W16(0x0174, 0); W16(0x0172, 0);
    call_jsl(0x01DFD3);
    g_cpu.DB = 0x7F;                             /* PEA $7F7F; PLB; PLB */
    W16(0x021B, 0);
    call_jsl(0x01E06F);
    CPU_SET_A16(0xFFFF); call_jsr(0x018C43);
    call_jsl(0x01E794);
    W16(0x0001, 0);
    W16(0x0005, 0x00EF);
    push8(cpu_get_p());
    W16(0x0003, (uint16_t)(g_cpu.S - 2));        /* TSC; DEC; DEC: the loop's stack point */
    W16(0x0411, 0);
    call_jsr(0x018260);
    call_jsl(0x008A75);
    cpu_set_p(pop8());
    CPU_SET_A16(0x0000); call_jsl(0x01D2BF);
    call_jsl(0x01E7C9);
    bus_write16(0x00, 0x0172, 0); bus_write16(0x00, 0x0174, 0);
    CPU_SET_A16(0x0000);
    call_jsl(0x01E06F);
    bus_write16(0x00, 0x0220, 0xFFFF);
    g_cpu.DB = pop8();
    if (R16(0x04E2) != 0) { cpu_set_p(pop8()); return; }
    if (R16(0x0565) != 0) {                      /* JMP $9372: it never returns */
        cpu_set_p(pop8());
        func_table_call(0x019372);
        return;
    }
    g_cpu.flag_M = true;
    blank_screen();
    dma_cgram(0x02FC00);
    dma_vram(0x4700, 0x04EA00, 0x0E00);
    CPU_SET_A8(0x01);
    g_cpu.flag_M = false;
    W16(0x0547, 0);
    call_jsl(0x01E20C);
    bool hi = CPU_A16() >= 0x00C0;
    W16(0x0547, hi ? 1 : 0);                     /* ROL $0547 */
    g_cpu.flag_I = false;
    call_jsl(0x01E2F3);
    g_cpu.flag_M = true;
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    if (hi) { sound_bank(0x16); }
    else {
        uint16_t dp = g_cpu.DP;                  /* bank $17 from $9000 */
        bus_write8(0x00, (uint16_t)(dp + 0xCC), 0x00);
        bus_write8(0x00, (uint16_t)(dp + 0xCD), 0x90);
        bus_write8(0x00, (uint16_t)(dp + 0xCE), 0x17);
        bus_write8(g_cpu.DB, 0x2140, 0xFF);
        CPU_SET_A8(0xFF);
        call_jsl(0x01DF25);
    }
    g_cpu.flag_M = false;
    call_jsl(0x01E2F3);
    g_cpu.flag_M = true;
    bus_write8(g_cpu.DB, 0x210B, 0x04); bus_write8(g_cpu.DB, 0x010E, 0x04);
    bus_write8(g_cpu.DB, 0x210C, 0x44); bus_write8(g_cpu.DB, 0x010F, 0x44);
    g_cpu.flag_M = false;
    CPU_SET_A16(0x0045); call_jsl(0x01E024);
    call_jsl(0x01DE97);
    CPU_SET_A16(0x0000); call_jsl(0x01E033);
    call_jsl(0x008A12);
    push8(g_cpu.DB);
    g_cpu.DB = 0x7F;
    CPU_SET_A16(bus_read16(0x00, 0x0547) ? 0x0022 : 0x0021);
    call_jsl(0x01D308);
    call_jsr(0x018F52);
    bus_write16(0x00, 0x0220, 0xFFFF);
    g_cpu.DB = pop8();
    cpu_set_p(pop8());
#undef R16
#undef W16
}


/* $01:9372: the special start (P2 code, $0565 set by the title).
 * With a bad save ($1982 set) a fixed screen and a hang ($9378).
 * Otherwise the saved animation is loaded from SRAM and played forever
 * as an attract loop: the canvas, SRAM $70:0800.. into $7F:2000, the
 * animation frames and score from the save, sound banks $1A and $1C. */
void mp_019372(void) {
#define R16(o)    bus_read16(g_cpu.DB, (uint16_t)(o))
#define W16(o, v) bus_write16(g_cpu.DB, (uint16_t)(o), (uint16_t)(v))
    uint8_t db = g_cpu.DB;
    if (bus_read16(0x00, 0x1982) == 0) {                 /* $9378 */
        CPU_SET_A16(0xCFBF); call_jsl(0x01CDE1);
        for (uint16_t x = 0; x != 0x0028; x += 2)
            W16(0x0226 + x, bus_read16(0x01, (uint16_t)(0x95CC + x)));
        W16(0x0426, 0xAAAA);
        W16(0x0428, 0x555A);
        static const uint16_t regs[8] = { 0x011A, 0x011B, 0x011C, 0x011D, 0x212C, 0x212D, 0x212E, 0x212F };
        for (int i = 0; i < 8; i++) bus_write8(db, regs[i], 0x10);
        call_jsl(0x01E794);
        while (!g_quit) call_jsl(0x01E2CE);
        return;
    }
    call_jsl(0x008994);
    call_jsl(0x01E30E);
    W16(0x2116, 0x7FE0);
    bus_write8(db, 0x2115, 0x80);
    for (int n = 0x10; n; n--) W16(0x2118, 0xFF00);
    for (int x = 0x07FE; x >= 0; x -= 2) {
        uint16_t v = bus_read16(0x7E, (uint16_t)(0x3000 + x));
        if (v == 0x2FFC) bus_write16(0x7E, (uint16_t)(0x3000 + x), (uint16_t)(v ^ 0x1400));
    }
    call_jsl(0x01DECD);
    bus_write8(db, 0x2121, 0x00);
    bus_write8(db, 0x2122, 0xFF);
    bus_write8(db, 0x2122, 0x7F);
    call_jsl(0x01E2F3);
    W16(0x0172, 0); W16(0x0174, 0);
    call_jsl(0x01E06F);
    W16(0x0220, 0xFFFF);
    W16(0x0208, 0);
    W16(0x0206, 0x0001);
    do call_jsl(0x01E2CE); while (R16(0x0208) != 0 && !g_quit);
    g_cpu.flag_M = true;
    bg_setup(0x06, 0x66, 0x04, false);
    g_cpu.flag_M = false;
    call_jsl(0x01E794);
    CPU_SET_A16(0xCFCA); call_jsl(0x01CDE1);
    for (int x = 0x001A; x >= 0; x -= 2) {
        bus_write16(0x7E, (uint16_t)(0x3396 + x), bus_read16(0x01, (uint16_t)(0x95F4 + x)));
        bus_write16(0x7E, (uint16_t)(0x33D6 + x), bus_read16(0x01, (uint16_t)(0x9610 + x)));
    }
    call_jsl(0x01DECD);
    call_jsl(0x01E2CE);
    for (int x = 0x77FE; x >= 0; x -= 2)
        bus_write16(0x7F, (uint16_t)(0x2000 + x), bus_read16(0x70, (uint16_t)(0x0800 + x)));
    bus_write16(0x00, 0x0549, 0xD657);
    bus_write16(0x00, 0x054B, 0x0000);
    bus_write16(0x00, 0x054D, 0xFFFF);
    push_a16(bus_read16(0x70, 0x07FE));
    CPU_SET_A16(bus_read16(0x70, 0x07FE));
    call_jsl(0x01F21D);
    call_jsl(0x01EF36);
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    for (int x = 0x07FE; x >= 0; x -= 2)
        bus_write16(0x7E, (uint16_t)(0x3800 + x), bus_read16(0x7E, (uint16_t)(0x9C00 + x)));
    for (int x = 0x024E; x >= 0; x -= 2)
        bus_write16(0x00, (uint16_t)(0x09E4 + x), bus_read16(0x7E, (uint16_t)(0xFC00 + x)));
    W16(0x19EC, bus_read16(0x7E, 0x3FFA));
    W16(0x19C2, bus_read16(0x7E, 0x3FFC));
    W16(0x1A08, bus_read16(0x7E, 0x3FFE));
    W16(0x19FA, 0);
    bus_write16(0x00, (uint16_t)(g_cpu.DP + 0x0A), 0x4000);
    CPU_SET_A16(0x4000);
    call_jsl(0x00B988);
    W16(0x0206, 0);
    W16(0x0216, 0);
    W16(0x0214, R16(0x0214) + 1);
    do call_jsl(0x01E2CE); while (R16(0x0214) != 0 && !g_quit);
    W16(0x0206, R16(0x0206) + 1);
    W16(0x1A02, R16(0xBE17 + (uint16_t)(R16(0x1A08) << 1)));
    W16(0x0EAC, 0x0001);
    W16(0x19FE, 0); W16(0x1A0A, 0);
    W16(0x19EA, 0xFFFE);
    W16(0x19EE, 0); W16(0x19F2, 0);
    g_cpu.flag_M = true;
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    sound_bank(0x1A);
    call_jsl(0x01E2F3);
    call_jsl(0x01D388);
    call_jsl(0x01E30E);
    sound_bank(0x1C);
    /* Until the sound CPU answers: in the ROM's own loop ($9573 to $957F),
     * as a native spin on the port left the sound CPU unanswered later. */
    if (!recomp_rom_wait(0x019573, 0x01957F))
        for (int n = 0; n < 24000; n++) {
            bus_write8(db, 0x2140, 0x01);
            if (bus_read8(db, 0x2140) == 0x01 || !recomp_timed_spin(64)) break;
        }
    g_cpu.flag_M = false;
    call_jsl(0x01E2F3);
    W16(0x0446, 0);
    call_jsl(0x00C7AF);
    bus_write8(db, 0x011A, 0x16);
    bus_write8(db, 0x011C, 0x16);
    for (int x = 0x001A; x >= 0; x -= 2) {
        bus_write16(0x7E, (uint16_t)(0x3396 + x), 0x03FE);
        bus_write16(0x7E, (uint16_t)(0x33D6 + x), 0x03FE);
    }
    call_jsl(0x01DECD);
    call_jsl(0x01E2CE);
    while (!g_quit) {                                    /* $95B2: play forever */
        W16(0x04DC, 0); W16(0x04DE, 0); W16(0x04CA, 0);
        call_jsl(0x00BBD4);
        call_jsl(0x01E09B);
        call_jsl(0x01E2CE);
    }
#undef R16
#undef W16
}

void mp_register_scenes(void) {
    func_table_register(0x019372, mp_019372);
    func_table_register(0x018000, mp_018000);
    func_table_register(0x019F6C, mp_019F6C);
}
