/*
 * Mario Paint — Pixel drawing core and tool setup routines.
 *
 * The lowest-level drawing routines that actually manipulate pixels
 * in the canvas buffer, plus tool graphics setup and soft reset.
 *
 * The canvas is stored as 4BPP SNES tiles in WRAM. Each 8x8 tile
 * uses 32 bytes (4 bitplanes). The canvas grid maps screen coordinates
 * to tile positions via the tilemap at $7E:20C0.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <mp/calls.h>
#include <mp/mem.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $00:AAEF — Pixel plot entry point
 *
 * Copies cursor position ($22/$24) to draw position ($86/$88)
 * and calls the core pixel plotting routine at $00B051.
 * ======================================================================== */
void mp_00AAEF(void) {
    bus_wram_write16(0x0086, bus_wram_read16(0x0022));
    bus_wram_write16(0x0088, bus_wram_read16(0x0024));
    func_table_call(0x00B051);
}

/* ========================================================================
 * $00:B05A — the 16x16 pen dab at ($86, $88): $B051 without the masked
 * draw test; the title's drawing state enters here through $00:B046.
 * PHX/PHY/PHP around it, the tile offset $84 kept on the stack across the
 * two tile halves (Y = 0..$E, then $400..$40E, into $B0D3).
 * ======================================================================== */
void mp_00B05A(void) {
#define Z(o)     bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define ZW(o, v) bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
#define R(o)     bus_read16(g_cpu.DB, (uint16_t)(o))
    uint8_t p = cpu_get_p();
    uint16_t x0 = g_cpu.X, y0 = g_cpu.Y;
    if (Z(0xB8) != 0 && R(0x04D0) != 0x0007)
        call_jsr(0x00B7D6);                                 /* spray can */
    uint16_t x = (uint16_t)(Z(0x86) - 8);
    ZW(0x86, x);
    ZW(0x80, x);
    uint16_t y = Z(0x88);
    if (x & 0x8000) y = (uint16_t)(y - 8);                  /* PHP/PLP: N of x - 8 */
    ZW(0x82, y - 8);
    ZW(0x88, Z(0x88) - 8);
    call_jsr(0x00B305);
    uint16_t saved = Z(0x84);
    uint16_t row = 0;
    for (;;) {
        uint16_t cy = Z(0x88);
        if (!((uint16_t)(cy - R(0x19B2)) & 0x8000) && ((uint16_t)(cy - R(0x19B4)) & 0x8000)) {
            g_cpu.Y = row;
            call_jsr(0x00B0D3);
        }
        ZW(0x88, Z(0x88) + 1);
        row = (uint16_t)(row + 2);
        uint16_t a = (uint16_t)(row + Z(0x84));
        CPU_SET_A16((uint16_t)(a & 0x000F));
        if (!(a & 0x000F)) ZW(0x84, Z(0x84) + 0x03F0);
        if (row == 0x0410) break;
        if (row == 0x0010) {
            row = 0x0400;
            ZW(0x84, saved);
        }
    }
    CPU_SET_A16(saved);                                     /* PLA */
    g_cpu.X = x0; g_cpu.Y = y0;
    cpu_set_p(p);
#undef Z
#undef ZW
#undef R
}

/* ========================================================================
 * $00:B051 — Core pixel plotting routine
 *
 * Plots pixels into the canvas buffer based on the current pen
 * graphics and drawing position. This is the heart of the drawing
 * system — it reads pen tile data and writes individual pixels
 * into the 4BPP canvas tile buffer.
 *
 * The routine adjusts coordinates for canvas bounds, looks up
 * the correct tile in the canvas buffer, and applies the pen
 * pattern pixel-by-pixel across a 16x16 area (standard pen size)
 * or larger depending on the tool.
 * ======================================================================== */
void mp_00B051(void) {
    /* Check if this is an undo/alternate draw */
    uint16_t draw_state = bus_wram_read16(0x0020);
    uint16_t flag50 = bus_wram_read16(0x0050);
    if (draw_state & flag50) {
        func_table_call(0x00B32D);  /* Alternate draw mode */
        return;
    }

    mp_00B05A();                                            /* falls through */
}

/* ========================================================================
 * $00:9D7D — Draw stroke sound effect
 *
 * Plays the appropriate sound for the current drawing tool.
 * ======================================================================== */
void mp_009D7D(void) {
    uint16_t tool = bus_wram_read16(0x04D0) & 0xFF;
    uint16_t sound;

    if (tool < 3) {
        sound = tool + 0x0019;
    } else if (tool == 3) {
        sound = 0x0029;
    } else if (tool == 4) {
        sound = 0x001C;
    } else {
        sound = 0x0047 + bus_wram_read16(0x1994);  /* Erase size */
    }

    CPU_SET_A16(sound);
    func_table_call(0x01D368);
}

/* ========================================================================
 * $00:B305 — Compute canvas tile buffer offset
 *
 * Converts screen coordinates ($80/$82) to a tile buffer offset
 * in the canvas. Stores result in $84.
 *
 * The canvas buffer is organized as SNES 4BPP tiles:
 *   Each tile = 32 bytes (8 rows x 4 bitplanes)
 *   Tile layout follows the BG tilemap addressing
 * ======================================================================== */
void mp_00B305(void) {
    uint16_t x = bus_wram_read16(0x0080);
    uint16_t y = bus_wram_read16(0x0082);

    /* From ASM: (x & $F8) * 4 + (y & $F8) * 128 + (y & 7) * 2 - $800 */
    uint16_t offset = ((x & 0x00F8) << 2)
                    + ((y & 0x00F8) << 7)
                    + ((y & 0x0007) << 1)
                    - 0x0800;

    bus_wram_write16(0x0084, offset);
}

/* ========================================================================
 * $00:B3FF — the paint can: flood fill from the cursor ($86, $88)
 *
 * The canvas inside $19B6..$19BC; $7F:0000.. marks filled pixels (cleared
 * by $8B29); the colour under the cursor ($B5CB) is the one to replace
 * ($6C). A queue of spans at $7F:6000 ($7C head, $7E tail, wrapping at
 * $2000) is worked through: each span runs left while the colour matches,
 * then right, plotting ($B393) and queueing the rows above and below where
 * they start matching. A mouse button pressed meanwhile ($04CA into
 * $1A0C, bit $20) stops it at once. $1B1C is $FF while it runs.
 * ======================================================================== */
#define FD(o)      bus_read16(0x00, (uint16_t)(g_cpu.DP + (o)))
#define FDW(o, v)  bus_write16(0x00, (uint16_t)(g_cpu.DP + (o)), (uint16_t)(v))
#define FD8(o)     bus_read8(0x00, (uint16_t)(g_cpu.DP + (o)))
#define FDW8(o, v) bus_write8(0x00, (uint16_t)(g_cpu.DP + (o)), (uint8_t)(v))

static uint32_t fill_canvas(void) {                     /* the long pointer DP $0A */
    uint16_t a = (uint16_t)(g_cpu.DP + 0x0A);
    return bus_read16(0x00, a) | ((uint32_t)bus_read8(0x00, (uint16_t)(a + 2)) << 16);
}
static uint16_t fill_rd(uint32_t a) { return bus_read16((uint8_t)(a >> 16), (uint16_t)a); }
static void fill_wr(uint32_t a, uint16_t v) { bus_write16((uint8_t)(a >> 16), (uint16_t)a, v); }
static uint16_t fill_bit(uint16_t x2) { return bus_read16(g_cpu.DB, (uint16_t)(0xB600 + x2)); }

/* $00:B5CB: the colour (0-15) of pixel bit X/2 at tile offset Y. */
static uint16_t fill_colour(uint16_t x2, uint16_t y) {
    uint32_t c = fill_canvas();
    uint16_t m = fill_bit(x2);
    FDW(0x76, fill_rd(c + y) & m);
    FDW(0x78, fill_rd(c + (uint16_t)(y + 0x10)) & m);
    uint16_t v = 0;
    v = (uint16_t)((v + (FD8(0x79) ? 1 : 0)) << 1);
    v = (uint16_t)((v + (FD8(0x78) ? 1 : 0)) << 1);
    v = (uint16_t)((v + (FD8(0x77) ? 1 : 0)) << 1);
    v = (uint16_t)(v + (FD8(0x76) ? 1 : 0));
    return v;
}
void mp_00B5CB(void) {
    uint16_t y = g_cpu.Y;
    uint16_t v = fill_colour((uint16_t)(g_cpu.X & 0x00FF), y);
    CPU_SET_A16(v);
    g_cpu.X &= 0x00FF;
    g_cpu.Y = y;
}

/* $00:B393: plot the fill colour (the brush colour words $9A/$9C of
 * $B23C) at pixel $86 of tile offset $84, and mark it in $7F:0000. */
void mp_00B393(void) {
    uint16_t bit = fill_bit((uint16_t)((FD(0x86) & 7) << 1));
    FDW(0x92, bit);
    FDW(0x94, bit);
    g_cpu.Y = 0;
    push_a16(bit);
    call_jsr(0x00B23C);
    g_cpu.S = (uint16_t)(g_cpu.S + 2);
    bus_write16(g_cpu.DB, 0x19BE, 0xFFFF);
    uint16_t v = (uint16_t)(FD(0x9A) | FD(0x9C));
    uint16_t both = (uint16_t)(((uint16_t)((v << 8) | (v >> 8))) | v);
    FDW(0x92, (uint16_t)(both & FD(0x92)));
    FDW(0x94, (uint16_t)(both & FD(0x94)));
    uint32_t c = fill_canvas();
    uint16_t y = FD(0x84);
    uint16_t m = bus_read16(g_cpu.DB, 0x19BE);
    uint16_t d = fill_rd(c + y);
    fill_wr(c + y, (uint16_t)(((FD(0x9A) ^ d) & FD(0x92) & m) ^ d));
    y = (uint16_t)(y + 0x10);
    d = fill_rd(c + y);
    fill_wr(c + y, (uint16_t)(((FD(0x9C) ^ d) & FD(0x94) & m) ^ d));
    g_cpu.Y = y;
    uint16_t x = FD(0x84);
    bus_write8(0x7F, x, (uint8_t)(bus_read8(0x7F, x) | (uint8_t)bit));
    g_cpu.X = x;
}

/* $00:B610: queue a span start (pixel x, row y, tile offset), unless the
 * queue is full. */
static void fill_push(uint8_t px, uint8_t py, uint16_t off) {
    uint16_t t = FD(0x7E);
    bus_write16(0x7F, (uint16_t)(0x6000 + t), (uint16_t)((px << 8) | py));
    bus_write16(0x7F, (uint16_t)(0x6002 + t), off);
    if ((uint16_t)((FD(0x7C) - 4) & 0x1FFF) == t) return;
    FDW(0x7E, (uint16_t)((t + 4) & 0x1FFF));
}

/* Is pixel bit X2 of tile offset X already filled? */
static bool fill_done(uint16_t x2, uint16_t off) {
    return ((uint8_t)fill_bit(x2) & bus_read8(0x7F, off)) != 0;
}

void mp_00B3FF(void) {
    uint8_t db = g_cpu.DB;
    uint8_t p0 = cpu_get_p();
#define R16(o)   bus_read16(db, (uint16_t)(o))
#define W16(o,v) bus_write16(db, (uint16_t)(o), (uint16_t)(v))
    FDW(0xB0, (uint16_t)(g_cpu.S - 1));                     /* PHP; TSX; STX $B0 */
    W16(0x19B6, R16(0x19AE) + 5);
    W16(0x19B8, R16(0x19B0) + 4);
    W16(0x19BA, R16(0x19B2) + 1);
    W16(0x19BC, R16(0x19B4) - 1);
    W16(0x04CA, 0);
    W16(0x1A0C, 0);
    bus_write8(db, 0x1B1E, 0x00);
    bus_write8(db, 0x1B1C, 0xFF);
    bus_write8(db, 0x1B1D, 0xFF);
    call_jsr(0x008B29);
    FDW8(0x80, FD8(0x86));
    FDW8(0x82, FD8(0x88));
    call_jsr(0x00B305);
    FDW(0x6C, fill_colour((uint16_t)((FD(0x86) & 7) << 1), FD(0x84)));
    FDW(0x7C, 0);
    FDW(0x7E, 0);
    fill_push(FD8(0x86), FD8(0x88), FD(0x84));
    for (;;) {                                               /* $B46F */
        if (R16(0x1A0C) & 0x0020) break;                     /* $B637: stopped */
        if (FD(0x7C) == FD(0x7E)) break;                     /* queue empty */
        uint16_t h = FD(0x7C);
        uint16_t e = bus_read16(0x7F, (uint16_t)(0x6000 + h));
        FDW(0x84, bus_read16(0x7F, (uint16_t)(0x6002 + h)));
        FDW(0x7C, (uint16_t)((h + 4) & 0x1FFF));
        FDW8(0x88, (uint8_t)e);
        FDW8(0x86, (uint8_t)(e >> 8));
        if (fill_done((uint16_t)((FD(0x86) & 7) << 1), FD(0x84))) continue;
        uint16_t y = FD(0x84);
        for (;;) {                                           /* $B4A4: run left */
            if (FD(0x86) < R16(0x19B6)) break;
            uint16_t x2 = (uint16_t)(((uint16_t)(FD(0x86) - 1) & 7) << 1);
            if (x2 == 0x000E) y = (uint16_t)(y - 0x20);
            if (fill_colour(x2, y) != FD(0x6C)) break;
            FDW(0x84, y);
            FDW(0x86, FD(0x86) - 1);
        }
        FDW8(0x70, 0); FDW8(0x72, 0);
        bus_write8(db, 0x1A0C, 0x00);
        for (;;) {                                           /* $B4D4: run right */
            if (FD8(0x86) >= (uint8_t)R16(0x19B8)) break;
            bus_write8(db, 0x1A0C, (uint8_t)(bus_read8(db, 0x04CA) | bus_read8(db, 0x1A0C)));
            call_jsr(0x00B393);
            for (int dir = -1; dir <= 1; dir += 2) {         /* the row above, then below */
                uint8_t row = FD8(0x88);
                if (dir < 0 ? row < (uint8_t)R16(0x19BA) : row >= (uint8_t)R16(0x19BC)) continue;
                FDW8(0x74, 0);
                uint16_t o = FD(0x84);
                if (dir < 0) {
                    if (((uint16_t)(row - 1) & 7) == 7) o = (uint16_t)(o - 0x03F0);
                    o = (uint16_t)(o - 2);
                } else {
                    if (((uint16_t)(row + 1) & 7) == 0) o = (uint16_t)(o + 0x03F0);
                    o = (uint16_t)(o + 2);
                }
                uint16_t x2 = (uint16_t)((FD(0x86) & 7) << 1);
                if (fill_colour(x2, o) == FD(0x6C)) FDW(0x74, FD(0x74) + 1);
                uint16_t flag = dir < 0 ? 0x70 : 0x72;
                if (!fill_done(x2, o) && FD8(flag) == 0 && FD8(0x74) != 0)
                    fill_push(FD8(0x86), (uint8_t)(row + dir), o);
                FDW8(flag, FD8(0x74));
            }
            uint16_t o = FD(0x84);                           /* $B5A6: next pixel right */
            uint16_t x2 = (uint16_t)(((uint16_t)(FD(0x86) + 1) & 7) << 1);
            if (x2 == 0) o = (uint16_t)(o + 0x20);
            if (fill_colour(x2, o) != FD(0x6C)) break;
            FDW(0x84, o);
            FDW(0x86, FD(0x86) + 1);
        }
    }
    bus_write8(db, 0x1B1C, 0x00);
    cpu_set_p(p0);
#undef R16
#undef W16
}
#undef FD
#undef FDW
#undef FD8
#undef FDW8

/* ========================================================================
 * $00:A22D — Load toolbar display tilemap
 *
 * Copies toolbar tilemap data from ROM ($02:9600) to the
 * BG1 tilemap buffer at $7E:2640.
 * ======================================================================== */
void mp_00A22D(void) {
    uint16_t tool = CPU_A16();
    uint16_t src_ofs = (uint16_t)(tool * 3);  /* ASL; ADC $01,s: 3 * 64 bytes = $C0 */
    src_ofs = (uint16_t)(src_ofs << 6);
    src_ofs = (uint16_t)(src_ofs + 0x00BE);

    /* Copy $C0 bytes of tilemap data */
    for (int y = 0x00BE; y >= 0; y -= 2) {
        uint16_t val = bus_read16(0x02, 0x9600 + src_ofs);
        bus_write16(0x7E, 0x2640 + y, val);
        src_ofs -= 2;
    }

    /* No upload here: the ROM leaves queueing the BG1 map ($01:DE97) to its
     * callers. An extra call once papered over a partly-native boot, but it
     * adds a DMA record the ROM never makes. */
}

/* ========================================================================
 * $00:A25B — Load tool graphics into the $7F:8000 buffer
 *
 * $01:DB31 + tool*2 gives the start of a $FFFF-terminated list at $01:DB53
 * of tile offsets into bank $03. Each tile copies three $40-byte rows,
 * from $03:8000/$8200/$8400 + offset to $7F:8000/$8200/$8400 + Y; Y moves
 * on by $40 per tile and by another $400 after every 8 tiles. Offsets
 * $5400 and $5A00 get $0999 added unless $AA is $0F.
 * ======================================================================== */
void mp_00A25B(void) {
    uint16_t tool = CPU_A16();
    uint16_t list = bus_read16(0x01, (uint16_t)(0xDB31 + (uint16_t)(tool << 1)));
    uint16_t y = 0, count = 0;
    for (;;) {
        uint16_t t = read_long16(0x01DB53u + list);          /* A274 */
        if (t & 0x8000) break;                              /* BMI: end */
        if ((t == 0x5400 || t == 0x5A00) && bus_wram_read16(0x00AA) != 0x000F)
            t = (uint16_t)(t + bus_wram_read16(0x0999));
        uint16_t x = t;
        do {                                                /* A296 */
            bus_write16(0x7F, (uint16_t)(0x8000 + y), read_long16(0x038000u + x));
            bus_write16(0x7F, (uint16_t)(0x8200 + y), read_long16(0x038200u + x));
            bus_write16(0x7F, (uint16_t)(0x8400 + y), read_long16(0x038400u + x));
            x += 2;
            y += 2;
        } while (y & 0x003F);
        list += 2;                                          /* PLX; INX x2 */
        count++;
        if ((count & 0x0007) == 0) y += 0x0400;             /* A2BF */
    }
    g_cpu.X = list;
    g_cpu.Y = y;
}

/* ========================================================================
 * $00:82B8 — Soft reset (from demo end)
 *
 * Resets the game to the boot sequence. Disables NMI, sends
 * reset commands to the SPC700, clears the demo flag, and
 * jumps back to $008013 (hardware init).
 * ======================================================================== */
void mp_0082B8(void) {
    op_sep(0x30);
    g_cpu.DP = 0x0000;
    g_cpu.DB = 0x00;

    /* Disable NMI */
    mp_01E30E();

    /* Delay loop (SPC700 reset timing) */
    /* Original code reads $7FFFFF in a tight loop for delay */
    bus_run_cycles(65536);

    /* Send reset command to SPC700 */
    bus_write8(0x00, 0x2142, 0x02);

    /* Another delay */
    bus_run_cycles(65536);

    /* Clear APU ports */
    bus_write8(0x00, 0x2140, 0xFF);
    bus_write8(0x00, 0x2141, 0x00);
    bus_write8(0x00, 0x2142, 0x00);
    bus_write8(0x00, 0x2143, 0x00);

    /* Switch to native mode, clear demo flag */
    OP_SEI();
    OP_CLC();
    op_xce();
    op_rep(0x20);
    bus_wram_write16(0x04E2, 0x0000);

    /* Restart hardware init (not full reset — preserves SRAM) */
    mp_008013();
}

/* ========================================================================
 * $00:8B3C — Wrapper: calls $008B48 (cursor movement)
 * ======================================================================== */
void mp_008B3C(void) {
    mp_008B48();
}

/* ========================================================================
 * Register all drawing core functions.
 * ======================================================================== */
void mp_register_draw(void) {
    func_table_register(0x00AAEF, mp_00AAEF);
    func_table_register(0x00B051, mp_00B051);
    func_table_register(0x00B05A, mp_00B05A);
    func_table_register(0x009D7D, mp_009D7D);
    func_table_register(0x00B305, mp_00B305);
    func_table_register(0x00B3FF, mp_00B3FF);
    func_table_register(0x00B5CB, mp_00B5CB);
    func_table_register(0x00B393, mp_00B393);
    func_table_register(0x00A22D, mp_00A22D);
    func_table_register(0x00A25B, mp_00A25B);
    func_table_register(0x0082B8, mp_0082B8);
    func_table_register(0x008B3C, mp_008B3C);
}
