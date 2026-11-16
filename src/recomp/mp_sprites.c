/*
 * Mario Paint — Recompiled sprite engine.
 *
 * The sprite engine renders multi-sprite objects into OAM.
 * Sprite frame data is stored in ROM bank $0D at $B018 (pointer table)
 * with frame definitions containing per-tile X/Y offsets and tile IDs.
 *
 * Key routines:
 *   $01F91E — Simple sprite renderer (no flip/palette override)
 *   $01FA68 — Full sprite renderer (flip, palette, size override)
 *   $01962C — Sprite animation driver
 *
 * OAM buffer layout (per sprite, 4 bytes):
 *   byte 0: X position (low 8 bits)
 *   byte 1: Y position
 *   byte 2: Tile number
 *   byte 3: Attributes (palette, priority, flip)
 *
 * Upper OAM table: 2 bits per sprite (X bit 8, size flag)
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

/* OAM buffer addresses */
#define OAM_BUF       0x0226
#define OAM_HI_BUF    0x0426

/*
 * DATA_0DB000: Upper OAM bit patterns (4 entries, indexed by size/X-bit8)
 * DATA_0DB008: Upper OAM bit masks (8 entries, indexed by sprite-within-word)
 *
 * These are small constant tables in ROM bank $0D.
 * We read them via bus_read16 for correctness, but could also hardcode them.
 */

/* Helper: read 16-bit value from bank $0D */
static uint16_t rom0d_read16(uint16_t addr) {
    return bus_read16(0x0D, addr);
}

/*
 * Helper: update upper OAM bits for a sprite.
 *
 * The upper OAM table packs 4 sprites per byte (2 bits each).
 * This function updates the bits for the sprite at OAM index `sprite_idx`
 * with the given `bits` (0-3: bit0=X high, bit1=size).
 */
static void update_upper_oam(uint16_t sprite_idx, uint16_t bits) {
    /* sprite_idx is byte offset into OAM (0, 4, 8, ...) divided by 4 = sprite number
     * But we get the raw X index here. Compute sprite number. */
    uint16_t sprite_num = sprite_idx / 4;  /* 0-127 */
    uint16_t word_idx = (sprite_num / 8) * 2;  /* byte offset into upper OAM */
    uint16_t bit_pos = (sprite_num & 0x07);  /* which 2-bit slot within word */

    /* Read upper OAM bit pattern and mask from ROM tables */
    uint16_t pattern = rom0d_read16(0xB000 + (bits & 0x03) * 2);
    uint16_t mask = rom0d_read16(0xB008 + bit_pos * 2);

    uint16_t cur = bus_wram_read16(OAM_HI_BUF + word_idx);
    cur = (pattern ^ cur) & mask ^ cur;  /* Insert new bits */
    bus_wram_write16(OAM_HI_BUF + word_idx, cur);
}

/* ========================================================================
 * $01:F91E — Simple sprite renderer
 *
 * Renders a sprite object into OAM. Reads sprite frame data from ROM
 * and writes position + tile + attributes for each sub-sprite.
 *
 * Input:
 *   A = sprite ID (index into DATA_0DB018 pointer table)
 *   X = screen X position (9-bit)
 *   Y = screen Y position
 *
 * Sprite frame data format (at pointer from DATA_0DB018):
 *   word[0]: number of sub-sprites
 *   Then per sub-sprite:
 *     word[0]: X offset (signed, bit 9 = size flag)
 *     byte[0]: Y offset
 *     word[1]: tile + attributes
 * ======================================================================== */
void mp_01F91E(void) {
    uint16_t sprite_id = CPU_A16();
    uint16_t base_x = g_cpu.X;
    uint16_t base_y = g_cpu.Y;

    /* Look up sprite data pointer from table in bank $0D */
    uint16_t ptr = rom0d_read16(0xB018 + sprite_id * 2);

    /* Read sub-sprite count */
    uint16_t count = bus_read16(0x0D, ptr);
    ptr += 2;

    uint16_t oam_idx = bus_wram_read16(0x0446);

    for (uint16_t i = 0; i < count && oam_idx < 0x0200; i++) {
        /* Read X offset (word) */
        uint16_t x_off = bus_read16(0x0D, ptr);
        ptr += 2;

        /* Compute screen X */
        uint16_t screen_x = (base_x + x_off) & 0x01FF;
        uint16_t x_bit9 = x_off & 0x0200;  /* Size/X-high flag from offset */

        /* Combine size flag with position */
        uint16_t oam_x_word = x_bit9 | screen_x;

        /* Write X position (low byte) to OAM */
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(screen_x & 0xFF));
        oam_idx++;

        /* Update upper OAM bits */
        {
            uint16_t sprite_num = (oam_idx - 1) / 4;
            uint16_t hi_byte = (oam_x_word >> 8) & 0x03;

            uint16_t word_ofs = (sprite_num / 8) * 2;
            uint16_t bit_slot = (sprite_num & 0x07);

            uint16_t pattern = rom0d_read16(0xB000 + (hi_byte & 0x03) * 2);
            uint16_t mask = rom0d_read16(0xB008 + bit_slot * 2);

            uint16_t cur = bus_wram_read16(OAM_HI_BUF + word_ofs);
            cur = ((pattern ^ cur) & mask) ^ cur;
            bus_wram_write16(OAM_HI_BUF + word_ofs, cur);
        }

        /* Read Y offset (byte) and add to base Y */
        uint8_t y_off_lo = bus_read8(0x0D, ptr);
        ptr++;
        uint16_t screen_y = (base_y + y_off_lo) & 0xFF;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)screen_y);
        oam_idx++;

        /* Read tile + attributes (word) */
        uint16_t tile_attr = bus_read16(0x0D, ptr);
        ptr += 2;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(tile_attr & 0xFF));
        oam_idx++;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(tile_attr >> 8));
        oam_idx++;
    }

    bus_wram_write16(0x0446, oam_idx);
}

/* ========================================================================
 * $01:FA68 — Draw a metasprite into the OAM buffer
 *
 * In: A = id (bit 15 flips vertically, bit 14 horizontally; bits 12-15
 * also go into each part's attribute byte), X/Y = screen position.
 * $0D:B018 + (id & $FFF)*2 points at the sprite: a part count, then
 * 5-byte parts: X offset (bit 9 = large), Y offset, tile, attribute.
 * Parts go to $0226 + $0446 (4 bytes each) until $0446 reaches $200;
 * a part whose X lands in $100-$1BF (off screen) is skipped. The 9th X
 * bit and the size bit go to the high table at $0426 through the masks
 * at $0D:B000 / $0D:B008. Flipped offsets subtract the part's width
 * ($012E small, $0130 large). Reads of the sprite data use DB = $0D;
 * everything else is in WRAM (mirrored there).
 * ======================================================================== */
void mp_01FA68(void) {
    uint8_t saved_p = cpu_get_p();
    uint16_t id = CPU_A16(), sx = g_cpu.X, sy = g_cpu.Y;
    uint16_t pal_bits = id & 0xC000;                        /* $13 */
    uint16_t pri_bits = id & 0x3000;                        /* $15 */
    bool vflip = (id & 0x8000) != 0;                        /* $0D sign */
    bool hflip = (id & 0x4000) != 0;                        /* $0F sign */
    uint16_t ix = (uint16_t)(((uint16_t)(id << 2) & 0x3FFF) >> 1);
#define D16(a) bus_read16(0x0D, (uint16_t)(a))
    uint16_t ptr = D16(0xB018 + ix);                        /* $05 */
    uint16_t y = 0;
    uint16_t count = D16(ptr + y); y += 2;                  /* $09 */
    uint16_t x = bus_wram_read16(0x0446);
    while (x < 0x0200) {                                    /* FAAD */
        uint16_t e = D16(ptr + y); y += 2;
        uint16_t width = bus_wram_read16((e & 0x0200) ? 0x0130 : 0x012E);   /* $11 */
        uint16_t a = hflip ? (uint16_t)(((e ^ 0x01FF) + 1) - width) : e;
        a = (uint16_t)((a + sx) & 0x01FF);                  /* $07 */
        if (a >= 0x0100 && a < 0x01C0) {                    /* off screen */
            y += 3;
        } else {
            uint16_t xw = (uint16_t)((e & 0x0200) | a);
            bus_wram_write16((uint16_t)(0x0226 + x), xw);   /* X (+junk) */
            x++;
            /* High table: 2 bits per sprite, 8 sprites per word. */
            uint16_t n = x >> 2;                            /* $0B */
            uint16_t hi = (uint16_t)((x >> 4) & 0xFFFE);
            uint16_t bits = D16(0xB000 + (((xw >> 8) & 3) << 1));
            uint16_t mask = D16(0xB008 + ((n & 7) << 1));
            uint16_t h = bus_wram_read16((uint16_t)(0x0426 + hi));
            bus_wram_write16((uint16_t)(0x0426 + hi), (uint16_t)(((bits ^ h) & mask) ^ h));
            uint16_t ey = D16(ptr + y); y += 1;
            uint16_t b = vflip ? (uint16_t)(((ey ^ 0x00FF) + 1) - width) : ey;
            bus_wram_write16((uint16_t)(0x0226 + x), (uint16_t)(b + sy));   /* Y (+junk) */
            x++;
            uint16_t t = D16(ptr + y); y += 2;
            bus_wram_write16((uint16_t)(0x0226 + x), (uint16_t)((t ^ pal_bits) | pri_bits));
            x += 2;
        }
        if (--count == 0) break;                            /* DEC $09 */
    }
#undef D16
    bus_wram_write16(0x0446, x);                            /* FB5B */
    g_cpu.X = x;
    g_cpu.Y = y;
    CPU_SET_A16((uint16_t)(g_cpu.S - 3));                   /* TSC; ADC #$22 */
    cpu_set_p(saved_p);
}

/* ========================================================================
 * $01:962C — Sprite animation driver
 *
 * Advances animation state for a sprite slot and renders the current
 * frame. Animation data is in Bank $0F at DATA_0F8000.
 *
 * Input: A = sprite slot index (e.g. $0026=color bar, $0027=toolbar)
 *
 * Per-slot state at $0792+slot*8:
 *   +0: X position (9-bit)
 *   +2: Y position
 *   +4: frame delay counter
 *   +5: current frame index
 *   +6: palette/attribute base
 *
 * Animation data format (4 bytes per frame, pointed to by $0F:8000 table):
 *   byte 0: delay count (or $80+ = command)
 *   byte 1: X delta
 *   byte 2: Y delta
 *   byte 3: sprite ID + flags → passed to $01FA68
 * ======================================================================== */
void mp_01962C(void) {
    uint16_t slot = CPU_A16();

    /* Compute slot data offset: slot * 8 */
    uint16_t slot_ofs = slot * 8;

    /* Animation pointer from Bank $0F table at $8000 */
    uint16_t anim_ptr = bus_read16(0x0F, 0x8000 + slot * 2);

    /* Decrement frame delay counter */
    op_sep(0x20);
    uint8_t delay = bus_wram_read8(0x0796 + slot_ofs);
    delay--;
    bus_wram_write8(0x0796 + slot_ofs, delay);

    if ((int8_t)delay < 0) {
        /* Delay expired — advance to next frame */
        uint8_t frame_idx = bus_wram_read8(0x0797 + slot_ofs);
        frame_idx++;
        bus_wram_write8(0x0797 + slot_ofs, frame_idx);

        op_rep(0x20);

        /* Read animation frame data */
        uint16_t frame_ofs = (uint16_t)bus_wram_read8(0x0797 + slot_ofs) * 4;

        /* Read frame entry: byte 0 = delay/command */
        uint8_t cmd = bus_read8(0x0F, anim_ptr + frame_ofs);

        if (cmd & 0x80) {
            /* Command byte — animation control.
             * The original uses $01E393 (indexed JSL dispatch) which
             * can't work in recompiled C. Handle commands directly:
             *   $80 = loop (reset frame to 0)
             *   $81 = end (hold last frame)
             *   $82+ = other (treat as end) */
            uint8_t cmd_type = cmd & 0x7F;
            if (cmd_type == 0) {
                /* Loop: reset frame index to 0 */
                bus_wram_write8(0x0797 + slot_ofs, 0x00);
                frame_ofs = 0;
                cmd = bus_read8(0x0F, anim_ptr);
                bus_wram_write8(0x0796 + slot_ofs, cmd);
            } else {
                /* End / other: rewind one frame and hold */
                uint8_t fi = bus_wram_read8(0x0797 + slot_ofs);
                if (fi > 0) fi--;
                bus_wram_write8(0x0797 + slot_ofs, fi);
                bus_wram_write8(0x0796 + slot_ofs, 1);
            }
            delay = bus_wram_read8(0x0796 + slot_ofs);
        } else {
            /* Normal frame: set delay */
            bus_wram_write8(0x0796 + slot_ofs, cmd);
        }

        /* Read frame data: X delta, Y delta, sprite data */
        frame_ofs = (uint16_t)bus_wram_read8(0x0797 + slot_ofs) * 4;

        op_rep(0x20);

        /* Read sprite render data from frame */
        uint8_t sprite_data_byte = bus_read8(0x0F, anim_ptr + frame_ofs + 3);
        /*
         * Both deltas are doubled before being added ($9697 ASL / $96A4 ASL):
         * slot positions are stored in half-pixel units, so a delta byte of -1
         * means one screen pixel. Without the shift the sprite crawls at half
         * speed, which is what left the title sprite drifting out of step with
         * where its click target actually was.
         */
        if (sprite_data_byte & 0x80) {
            int8_t x_delta = (int8_t)bus_read8(0x0F, anim_ptr + frame_ofs + 1);
            int16_t x_pos = (int16_t)bus_wram_read16(0x0792 + slot_ofs);
            x_pos += (int16_t)x_delta * 2;
            x_pos &= 0x03FF;
            bus_wram_write16(0x0792 + slot_ofs, (uint16_t)x_pos);

            int8_t y_delta = (int8_t)bus_read8(0x0F, anim_ptr + frame_ofs + 2);
            int16_t y_pos = (int16_t)bus_wram_read16(0x0794 + slot_ofs);
            y_pos += (int16_t)y_delta * 2;
            y_pos &= 0x01FF;
            bus_wram_write16(0x0794 + slot_ofs, (uint16_t)y_pos);
        }
    } else {
        op_rep(0x20);
    }

    /* Render current frame sprite */
    {
        uint16_t frame_ofs = (uint16_t)bus_wram_read8(0x0797 + slot_ofs) * 4;

        /* Read render sprite ID + flags from frame data byte 3 */
        uint8_t render_byte = bus_read8(0x0F, anim_ptr + frame_ofs + 3);
        uint16_t render_id = (uint16_t)render_byte;

        /* Add palette/attribute base from slot */
        render_id += bus_wram_read16(0x0798 + slot_ofs);

        /* Get position */
        uint16_t x_pos = bus_wram_read16(0x0792 + slot_ofs) >> 1;
        uint16_t y_pos = bus_wram_read16(0x0794 + slot_ofs) >> 1;

        /* Call FA68 to render */
        CPU_SET_A16(render_id);
        g_cpu.X = x_pos;
        g_cpu.Y = y_pos;
        mp_01FA68();
    }
}

/* ========================================================================
 * Register sprite engine functions.
 * ======================================================================== */
void mp_register_sprites(void) {
    func_table_register(0x01F91E, mp_01F91E);
    func_table_register(0x01FA68, mp_01FA68);
    /* $01962C is deliberately NOT registered. Its animation-command decoding is
     * a stub: the original dispatches each command byte through an indexed JSL
     * table at $01E393, which this translation replaced with a guess ("$80 =
     * loop, $81 = end, $82+ = treat as end"). Most frames in bank $0F are
     * command bytes, so the wrong frames played and animated sprites drifted —
     * the title screen's Mario wandered diagonally off-screen instead of
     * walking left at a fixed height, which also moved the click target that
     * starts the game away from where the sprite appeared.
     *
     * Leaving it unregistered sends callers to the interpreter, which runs the
     * genuine driver. Verified against a real-frame capture: slot 0 now tracks
     * x decreasing steadily with y pinned at 338, exactly like the real ROM.
     * Register it again once $01E393's dispatch is translated. */
}
