/*
 * Mario Paint — Recompiled title screen routine.
 *
 * $018000 is the biggest single routine in the game. It handles:
 *   1. SRAM checksum validation
 *   2. Title screen palette + sprite tile DMA from ROM
 *   3. Title screen tilemap setup and animation
 *   4. Fade-in effect
 *   5. Title screen input loop (wait for click or demo timeout)
 *   6. Transition: load main canvas palette + tiles + audio
 *   7. Fade-out and return to canvas mode
 *
 * Sub-routines that handle title animation/input ($018260, $018C43,
 * etc.) are dispatched via func_table_call and will work as they
 * get recompiled. The critical path here is the DMA chains.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>
#include <string.h>

extern bool g_quit;

#define REG_INIDISP  0x2100
#define REG_VMAIN    0x2115
#define REG_VMADDL   0x2116
#define REG_VMADDH   0x2117
#define REG_VMDATAL  0x2118
#define REG_CGRAM    0x2121
#define REG_APUIO0   0x2140
#define REG_BG12NBA  0x210B
#define REG_BG34NBA  0x210C
#define REG_TM       0x212C
#define REG_TMW      0x212E
#define REG_NMITIMEN 0x4200
#define REG_MDMAEN   0x420B
#define REG_HDMAEN   0x420C

#define DMA_PARAMS(n)   (0x4300 + (n) * 0x10 + 0)
#define DMA_DEST(n)     (0x4300 + (n) * 0x10 + 1)
#define DMA_SRCLO(n)    (0x4300 + (n) * 0x10 + 2)
#define DMA_SRCHI(n)    (0x4300 + (n) * 0x10 + 3)
#define DMA_SRCBANK(n)  (0x4300 + (n) * 0x10 + 4)
#define DMA_SIZELO(n)   (0x4300 + (n) * 0x10 + 5)
#define DMA_SIZEHI(n)   (0x4300 + (n) * 0x10 + 6)

static void cgram_dma(uint8_t bank, uint16_t addr, uint16_t size) {
    bus_write8(0x00, REG_CGRAM, 0x00);
    bus_write8(0x00, DMA_PARAMS(0), 0x00);
    bus_write8(0x00, DMA_DEST(0),   0x22);
    bus_write8(0x00, DMA_SRCLO(0),  (uint8_t)(addr & 0xFF));
    bus_write8(0x00, DMA_SRCHI(0),  (uint8_t)(addr >> 8));
    bus_write8(0x00, DMA_SRCBANK(0), bank);
    bus_write8(0x00, DMA_SIZELO(0), (uint8_t)(size & 0xFF));
    bus_write8(0x00, DMA_SIZEHI(0), (uint8_t)(size >> 8));
    bus_write8(0x00, REG_MDMAEN, 0x01);
}

static void vram_dma(uint16_t vram_addr, uint8_t bank, uint16_t addr, uint16_t size) {
    bus_write8(0x00, REG_VMADDL, (uint8_t)(vram_addr & 0xFF));
    bus_write8(0x00, REG_VMADDH, (uint8_t)(vram_addr >> 8));
    bus_write8(0x00, REG_VMAIN,  0x80);
    bus_write8(0x00, DMA_PARAMS(0), 0x01);
    bus_write8(0x00, DMA_DEST(0),   0x18);
    bus_write8(0x00, DMA_SRCLO(0),  (uint8_t)(addr & 0xFF));
    bus_write8(0x00, DMA_SRCHI(0),  (uint8_t)(addr >> 8));
    bus_write8(0x00, DMA_SRCBANK(0), bank);
    bus_write8(0x00, DMA_SIZELO(0), (uint8_t)(size & 0xFF));
    bus_write8(0x00, DMA_SIZEHI(0), (uint8_t)(size >> 8));
    bus_write8(0x00, REG_MDMAEN, 0x01);
}

/* ========================================================================
 * $00:D6D3 — SRAM checksum validation
 *
 * Computes a checksum of SRAM bank $70 and stores result at $1982.
 * $1982 = 1 if checksum matches (valid save), 0 if not.
 * ======================================================================== */
void mp_00D6D3(void) {
    /* The save check: a sum (with the carry running on from one add to the
     * next: one CLC before the loop, as in the ROM) and an XOR over SRAM
     * $70:0800..$77FF and the word at $70:07FE, from seeds $7003 / $2122,
     * against the stored pair at $70:07C2/$07C4. $1982 = 1 when they match. */
    bus_write16(0x00, 0x1982, 0x0000);
    uint32_t c = 0;
    uint16_t sum = 0x7003, xr = 0x2122;
    for (int x = 0x77FE; x >= 0; x -= 2) {
        uint16_t v = bus_read16(0x70, (uint16_t)(0x0800 + x));
        uint32_t t = (uint32_t)v + sum + c;
        sum = (uint16_t)t; c = t >> 16;
        xr ^= v;
    }
    uint16_t v = bus_read16(0x70, 0x07FE);
    sum = (uint16_t)((uint32_t)sum + v + c);
    xr ^= v;
    bus_write16(0x00, 0x197C, sum);
    bus_write16(0x00, 0x197E, xr);
    CPU_SET_A16(bus_read16(0x70, 0x07C2));
    if (bus_read16(0x70, 0x07C2) == sum && bus_read16(0x70, 0x07C4) == xr) {
        bus_write16(0x00, 0x1982, 0x0001);
        CPU_SET_A16(0x0001);
    }
}

/* $01:8000 (title screen and main init) lives in mp_scenes.c. */

/* ========================================================================
 * Register title screen functions.
 * ======================================================================== */
void mp_register_title(void) {
    func_table_register(0x00D6D3, mp_00D6D3);

}
