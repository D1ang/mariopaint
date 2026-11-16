/*
 * Mario Paint — canvas click handling, translated from the ROM.
 *
 *   $00:8BEB  Canvas post-logic (state 0 of the $87A8 dispatch)
 *   $00:8D2C  Top-bar hit test
 *   $00:8E0B  Bottom-bar hit test
 *   $00:9564  Draw-tool dispatch
 *   $00:9598  Top-bar click dispatch
 *   $00:95E7  Bottom-bar click dispatch
 *
 * These replace earlier approximations (the hit tests used made-up zones
 * and $95E7 knew 5 of its table's entries). Every branch follows the ROM
 * instruction by instruction; comments give the ROM address of each step.
 * All run with 16-bit A and index registers (M=0, X=0), as in the ROM.
 *
 * Register results matter here: the hit tests return a category in X and
 * a slot in A, and the JMP (abs,x) dispatchers leave A = X = index*2 for
 * the handler, which may be genuine ROM code that reads them.
 */
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- small 65816 helpers (16-bit A and index) ---- */

static inline uint16_t lda(uint16_t wram_addr) {          /* LDA abs/dp */
    uint16_t v = bus_wram_read16(wram_addr);
    CPU_SET_A16(v);
    cpu_update_nz16(v);
    return v;
}
static inline void ldx(uint16_t v) { g_cpu.X = v; cpu_update_nz16(v); }
static inline void cmp(uint16_t reg, uint16_t m) {         /* CMP/CPX/CPY */
    g_cpu.flag_C = reg >= m;
    cpu_update_nz16((uint16_t)(reg - m));
}
static inline bool bit_imm(uint16_t m) {                   /* BIT #imm: Z only */
    bool nz = (CPU_A16() & m) != 0;
    g_cpu.flag_Z = !nz;
    return nz;
}

/* LDA idx / ASL / TAX / JMP (table,x): bank-0 jump table read from the ROM,
 * so the table's real size applies rather than a guess. */
static void jmp_table(uint16_t table, uint16_t idx) {
    uint16_t a = (uint16_t)(idx << 1);
    g_cpu.flag_C = (idx & 0x8000) != 0;
    CPU_SET_A16(a);
    ldx(a);
    func_table_call(0x000000 | bus_read16(0x00, (uint16_t)(table + a)));
}

/* ========================================================================
 * $00:8D2C — Top-bar hit test
 *
 * Looks the cursor X up in a table of 16 [min, max) ranges.
 * Hit:  A = slot (0-15); X = 1 for slots 0-14, 2 for slot 15.
 *       With the eraser menu up ($1992), a second table is used and
 *       slots 6+ return X = slot - 4 (A too).
 * Miss: X = 0, A = cursor X.
 * ======================================================================== */
void mp_008D2C(void) {
    bool erase = lda(0x1992) != 0;                          /* 8D2C */
    uint16_t table = erase ? 0x8DCB : 0x8D5C;               /* 8D9C / 8D31 */
    uint16_t cx = lda(0x04DC);
    uint16_t x = 0;
    do {
        uint16_t lo = bus_read16(g_cpu.DB, (uint16_t)(table + x));
        uint16_t hi = bus_read16(g_cpu.DB, (uint16_t)(table + x + 2));
        cmp(cx, lo);
        if (g_cpu.flag_C) {
            cmp(cx, hi);
            if (!g_cpu.flag_C) goto hit;
        }
        x += 4;
        cmp(x, 0x0040);
    } while (x != 0x0040);
    ldx(0x0000);                                            /* 8D4A miss */
    return;

hit: {
        uint16_t slot = x >> 2;                             /* TXA LSR LSR */
        CPU_SET_A16(slot);
        if (!erase) {
            ldx(0x0001);                                    /* 8D52 */
            cmp(slot, 0x000F);
            if (g_cpu.flag_C) ldx(0x0002);                  /* INX */
        } else {
            ldx(0x0001);                                    /* 8DBD */
            cmp(slot, 0x0006);
            if (g_cpu.flag_C) {                             /* SEC; SBC #4; TAX */
                uint16_t a = slot - 4;
                CPU_SET_A16(a);
                g_cpu.flag_C = true;
                ldx(a);
            }
        }
    }
}

/* ========================================================================
 * $00:8E0B — Bottom-bar hit test
 *
 * The bottom bar's layout depends on the current mode ($AA). For that
 * mode, $8E49 points at a list of [min, max) cursor-X ranges and $8E69
 * gives the list's byte length (kept in $1A0E).
 * Hit:  A = slot; X = slot + base[$AA] from $8FE1 (a global button id).
 * Miss: X = 0, A = the range-list pointer.
 * ======================================================================== */
void mp_008E0B(void) {
    uint16_t aa = lda(0x00AA);                              /* 8E0B */
    uint16_t x = (uint16_t)(aa << 1);                       /* ASL TAX */
    g_cpu.X = x;
    uint16_t ptr = bus_read16(g_cpu.DB, (uint16_t)(0x8E49 + x));
    CPU_SET_A16(ptr);
    op_pha16();                                             /* PHA (ptr)   */
    CPU_SET_A16((uint16_t)(ptr + 2));
    op_pha16();                                             /* PHA (ptr+2) */
    uint16_t len = bus_read16(g_cpu.DB, (uint16_t)(0x8E69 + x));
    bus_wram_write16(0x1A0E, len);                          /* STA $1A0E */
    uint16_t cx = lda(0x04DC);
    uint16_t y = 0;
    do {                                                    /* 8E22 */
        cmp(cx, bus_read16(g_cpu.DB, (uint16_t)(ptr + y)));
        if (g_cpu.flag_C) {
            cmp(cx, bus_read16(g_cpu.DB, (uint16_t)(ptr + 2 + y)));
            if (!g_cpu.flag_C) goto hit;
        }
        y += 4;
        cmp(y, len);
    } while (y != len);
    g_cpu.Y = y;
    op_pla16();                                             /* 8E33 */
    op_pla16();
    ldx(0x0000);
    return;

hit: {
        g_cpu.Y = y;
        op_pla16();                                         /* 8E3A */
        op_pla16();
        uint16_t slot = y >> 2;
        CPU_SET_A16(slot);
        op_pha16();
        uint16_t base = bus_read16(g_cpu.DB, (uint16_t)(0x8FE1 + x));
        uint32_t sum = (uint32_t)base + slot;               /* CLC; ADC $01,s */
        g_cpu.flag_C = sum > 0xFFFF;
        g_cpu.X = (uint16_t)sum;                            /* TAX */
        op_pla16();                                         /* A = slot */
    }
}

/* ========================================================================
 * $00:9564 — Draw-tool dispatch (by tool $04D0; $0567 forces entry 5)
 * ======================================================================== */
void mp_009564(void) {
    uint16_t a = lda(0x04D0) & 0x00FF;                      /* 9564 */
    a = (uint16_t)(a - 4);                                  /* SEC SBC #4 */
    if (a & 0x8000) a = 0;                                  /* BPL / LDA #0 */
    if (a == 0x000C) a = 2;
    g_cpu.Y = bus_wram_read16(0x0567);                      /* LDY $0567 */
    if (g_cpu.Y != 0) a = 5;
    jmp_table(0x9588, a);
}

/* ========================================================================
 * $00:9598 — Top-bar click dispatch on $A2 (the category from $8D2C).
 * Ignored with the stamp tool. Eraser menu ($1992) and $0EB8 modes have
 * their own tables.
 * ======================================================================== */
void mp_009598(void) {
    uint16_t tool = lda(0x04D0) & 0x00FF;                   /* 9598 */
    cmp(tool, 0x0006);
    if (tool == 0x0006) return;
    uint16_t a2 = bus_wram_read16(0x00A2);
    if (lda(0x1992) != 0) { jmp_table(0x95C2, a2); return; }   /* 95BB */
    if (lda(0x0EB8) != 0) { jmp_table(0x95E1, a2); return; }   /* 95DA */
    jmp_table(0x95B5, a2);                                       /* 95AE */
}

/* ========================================================================
 * $00:95E7 — Bottom-bar click dispatch on $A4 (the button id from $8E0B)
 * ======================================================================== */
void mp_0095E7(void) {
    jmp_table(0x95EE, bus_wram_read16(0x00A4));
}

/* ========================================================================
 * $00:8BEB — Canvas post-logic (state 0 of the $87A8 dispatch)
 *
 * Not drawing ($20 == 0): on a click, route it to the top bar ($8BA9),
 * the bottom bar ($8BC6), or the two-player / special-zone handlers.
 * Clicks inside the canvas do nothing here; the tools handle them.
 * Drawing: keep or end the stroke; the stamp tool has its own path.
 * ======================================================================== */
void mp_008BEB(void) {
top:
    if (lda(0x0020) != 0) goto drawing;                     /* 8BEB */

    bus_wram_write16(0x053B, 0x0000);                       /* 8BEF */
    func_table_call(0x009001);
    lda(0x04CA);
    if (!bit_imm(0x0011)) return;                           /* 8BFB */
    if (lda(0x19AA) != 0) goto special;                     /* 8C00 */
    {
        uint16_t cy = lda(0x04DE);
        cmp(cy, 0x0018);
        if (!g_cpu.flag_C) goto top_bar;                    /* 8C08 */
        cmp(cy, 0x00C8);
        if (g_cpu.flag_C) goto bottom_bar;                  /* 8C0D */
    }
    return;                                                 /* 8C0F */

top_bar:                                                    /* 8BA9 */
    func_table_call(0x008D2C);
    bus_wram_write16(0x00A2, g_cpu.X);
    bus_wram_write16(0x00A8, CPU_A16());
    if (lda(0x1992) == 0 && g_cpu.X == 0x0002) goto top_bar_go;  /* 8BB0 */
    lda(0x04CA);
    if (bit_imm(0x0020)) goto top_bar_go;                   /* 8BBA */
    return;
top_bar_go:
    func_table_call(0x009598);                              /* 8BC3 */
    return;

bottom_bar: {                                               /* 8BC6 */
        func_table_call(0x008E0B);
        uint16_t slot = CPU_A16();                          /* PHA */
        uint16_t id = g_cpu.X;
        if (id != 0x000A && id != 0x0024 && id != 0x0044) {
            lda(0x04CA);
            if (!bit_imm(0x0020)) {                         /* 8BE9 */
                CPU_SET_A16(slot);                          /* PLA */
                cpu_update_nz16(slot);
                return;
            }
        }
        CPU_SET_A16(slot);                                  /* 8BE1 PLA */
        cpu_update_nz16(slot);
        bus_wram_write16(0x00A4, id);
        bus_wram_write16(0x00AC, slot);
        func_table_call(0x0095E7);
        return;
    }

special:                                                    /* 8C10 */
    if (lda(0x099F) != 0 && lda(0x00AA) != 0x0007) {
        lda(0x04CA);
        if (bit_imm(0x0002)) { func_table_call(0x009760); return; }
    }
    lda(0x04CA);                                            /* 8C27 */
    if (!bit_imm(0x0020)) return;
    func_table_call(0x008B03);
    func_table_call(0x009564);
    return;

drawing: {                                                  /* 8C35 */
        uint16_t tool = lda(0x04D0) & 0x00FF;
        if (tool == 0x0006) goto stamp;
        func_table_call(0x0091C7);
        if (!g_cpu.flag_C) { func_table_call(0x009564); return; }  /* 8C45 */
        lda(0x04CA);
        if (bit_imm(0x0010)) return;                        /* 8C4E: still held */
        bus_wram_write16(0x0020, 0x0000);                   /* 8C50 STZ $20 */
        tool = lda(0x04D0) & 0x00FF;
        cmp(tool, 0x0005);
        if (!g_cpu.flag_C) {                                /* 8C5D */
            op_lda_imm16(0x0018);
            func_table_call(0x01D368);
        }
        if (lda(0x00AE) == 0) return;                       /* 8C64 */
        func_table_call(0x008B18);
        return;
    }

stamp:                                                      /* 8C6B */
    g_cpu.Y = bus_wram_read16(0x099F);
    if (g_cpu.Y != 0 && lda(0x00AA) != 0x0007) {
        uint16_t cy = lda(0x04DE);
        cmp(cy, 0x00C8);
        if (!g_cpu.flag_C) {
            lda(0x04CA);
            if (bit_imm(0x0002)) { func_table_call(0x009760); return; }  /* 8CC4 */
        }
    }
    {
        uint16_t cy = lda(0x04DE);                          /* 8C87 */
        cmp(cy, 0x00C8);
        if (!g_cpu.flag_C) goto stamp_canvas;
    }
    {
        func_table_call(0x008E0B);                          /* 8C8F */
        uint16_t id = g_cpu.X;
        lda(0x04CA);
        if (id == 0x000A || id == 0x0024 || id == 0x0044) {
            if (!bit_imm(0x0022)) goto stamp_canvas;        /* 8CAB */
        } else {
            if (!bit_imm(0x0020)) goto stamp_canvas;        /* 8CA4 / 8CA9 */
        }
        if (id == 0x000B || id == 0x0025 || id == 0x002C || id == 0x0056) {
            func_table_call(0x009760);                      /* 8CC4 */
            return;
        }
        if (id == 0x0000) goto stamp_canvas;                /* 8CC7 */
        if (id == 0x0008) goto stamp_end;                   /* 8CCC */
        if (lda(0x19C0) == 0) {                             /* 8CD1 */
            if (id == 0x0025) goto stamp_end;
        } else {
            if (id == 0x0026) goto stamp_end;               /* 8CDD */
        }
        bus_wram_write16(0x0020, 0x0000);                   /* 8CE2 */
        goto top;                                           /* JMP $8BEB */
    }

stamp_canvas:                                               /* 8CE7 */
    if (lda(0x0020) == 0x0003) {
        func_table_call(0x009001);
        if (lda(0x19AA) != 0) { func_table_call(0x009564); return; }  /* 8D00 */
        lda(0x04CA);
        if (!bit_imm(0x0020)) return;                       /* 8D2B */
        goto stamp_end;
    }
    func_table_call(0x0091C7);                              /* 8D03 */
    if (!g_cpu.flag_C) { func_table_call(0x009564); return; }
    lda(0x04CA);
    if (bit_imm(0x0010)) return;                            /* 8D11 */

stamp_end:                                                  /* 8D13 */
    op_lda_imm16(0x0007);
    func_table_call(0x01D368);
    bus_wram_write16(0x0020, 0x0000);
    bus_wram_write16(0x1996, 0x0000);
    bus_wram_write16(0x1998, 0x0000);
    bus_wram_write16(0x199A, 0x0000);
    bus_wram_write16(0x199C, 0x0000);
    bus_wram_write16(0x199E, 0x0000);
}

/* ========================================================================
 * Hover grid: $9001 / $9082 / $90C4 / $9107
 *
 * Each screen layout (picked by $19FA/$19C2) has a grid of buttons. The
 * table at $926C points at its grid: word 0 = column count, word 2 = row
 * count, then [min, max) X ranges for the columns and Y ranges for the rows.
 * $19A6 holds the hovered cell (row in the high byte, column in the low,
 * $FFFF for none), $19AA whether the cursor is on the grid, and $19A8/$19AC
 * their values from the previous frame.
 * ======================================================================== */

/* $19FA ? ($19C2 + 1) * 2 : 0 — the current layout's table index. */
static uint16_t layout_index(void) {
    uint16_t a = lda(0x19FA);
    if (a != 0) a = (uint16_t)(bus_wram_read16(0x19C2) + 1);
    return (uint16_t)(a << 1);
}

/* $00:9107 — set (flag != 0) or clear the highlight palette (bits 10-12 =
 * 5) on every tilemap entry of one grid cell in the BG buffer at $7E:2000. */
static void highlight_cell(uint16_t cell, uint16_t flag) {
    uint8_t db = g_cpu.DB;
    uint16_t x = layout_index();
    uint16_t p = bus_read16(db, (uint16_t)(0x9185 + x));
    uint16_t width = bus_read16(db, p);                     /* $05 */
    uint16_t end = bus_read16(db, (uint16_t)(p + 2));       /* $07 */
    uint16_t col = cell & 0x00FF, row = (cell >> 8) & 0x00FF;
    uint16_t y = (uint16_t)((col + 3 * (row + 1) - 1) << 1);   /* 9138-9141 */
    uint16_t base = bus_read16(db, (uint16_t)(p + y));      /* $03 */
    uint16_t pal = flag ? 0x1400 : 0x0000;                  /* $09 */
    y = 0;
    do {                                                    /* 9154 */
        uint16_t ix = (uint16_t)(y + base);
        uint32_t off = 0x2000u + ix;
        uint16_t t = bus_wram_read16(off);
        bus_wram_write16(off, (uint16_t)((t & 0xE3FF) | pal));
        g_cpu.X = ix;
        y += 2;
        if ((y & 0x003F) == width) y = (uint16_t)(y + 0x40 - width);
    } while (y != end);
    g_cpu.Y = y;
}

/* Entry from ROM code: JSR with the flag pushed first, then the cell. */
void mp_009107(void) {
    uint16_t s = g_cpu.S;
    uint16_t cell = bus_wram_read16((uint16_t)(s + 3));
    uint16_t flag = bus_wram_read16((uint16_t)(s + 5));
    highlight_cell(cell, flag);
    CPU_SET_A16((uint16_t)(s - 2));                         /* TSC at $917D */
    g_cpu.flag_C = false;
}

/* $00:9082 — copy the hovered cell's 8-byte record to $19AE. */
static void load_cell_record(void) {
    uint8_t db = g_cpu.DB;
    uint16_t x = layout_index();
    uint16_t recs = bus_read16(db, (uint16_t)(0x92C8 + x));
    uint16_t col = bus_wram_read16(0x19A6) & 0x00FF;
    uint16_t row = bus_wram_read16(0x19A7) & 0x00FF;
    uint16_t stride = bus_read16(db, (uint16_t)(0x92D0 + x));
    uint16_t idx = col;
    for (uint16_t i = 0; i < row; i++) idx = (uint16_t)(idx + stride);
    uint16_t y = (uint16_t)(idx << 3);
    for (uint16_t i = 0; i < 8; i += 2, y += 2)
        bus_wram_write16(0x19AE + i, bus_read16(db, (uint16_t)(recs + y)));
    g_cpu.X = 0x0008;
    g_cpu.Y = y;
    CPU_SET_A16(recs);
}

/* $00:90C4 — move the highlight when the hovered cell changes. */
static void update_highlight(void) {
    bool redraw = false;
    if (lda(0x19AA) == 0) {
        if (lda(0x19AC) != 0) redraw = true;                /* left the grid */
    } else if (lda(0x19A6) != bus_wram_read16(0x19A8)) {    /* 90D0 */
        uint16_t now = bus_wram_read16(0x19A6);
        if (!(now & 0x8000)) highlight_cell(now, 1);        /* 90D8 */
        redraw = true;
    }
    if (redraw) {                                           /* 90E7 */
        uint16_t old = bus_wram_read16(0x19A8);
        if (!(old & 0x8000)) highlight_cell(old, 0);
        func_table_call(0x01DE97);                          /* upload BG1 map */
    }
    bus_wram_write16(0x19AC, lda(0x19AA));                  /* 90FA */
    bus_wram_write16(0x19A8, lda(0x19A6));
}

/* $00:9001 — find the hovered grid cell. */
void mp_009001(void) {
    func_table_call(0x0091C7);
    if (!g_cpu.flag_C) return;                              /* 9004 */
    uint8_t db = g_cpu.DB;
    bus_wram_write16(0x19A6, 0x0000);
    bus_wram_write16(0x19AA, 0x0000);
    uint16_t grid = bus_read16(db, (uint16_t)(0x926C + layout_index()));
    uint16_t cols = bus_read16(db, grid);
    uint16_t rows = bus_read16(db, (uint16_t)(grid + 2));

    /* Rows: Y ranges follow the column ranges. */
    uint16_t y = (uint16_t)((cols << 2) + 4), n = rows;
    uint16_t cy = bus_wram_read16(0x04DE);
    for (;;) {                                              /* 9031 */
        bool ge = cy >= bus_read16(db, (uint16_t)(grid + y));
        y += 2;
        if (ge && cy < bus_read16(db, (uint16_t)(grid + y))) break;
        y += 2;
        bus_wram_write16(0x19A6, (uint16_t)(bus_wram_read16(0x19A6) + 1));
        if (--n == 0) {                                     /* 9043 none */
            bus_wram_write16(0x19A6, 0xFFFF);
            goto done;
        }
    }
    {                                                       /* 904B XBA */
        uint16_t v = bus_wram_read16(0x19A6);
        bus_wram_write16(0x19A6, (uint16_t)((v << 8) | (v >> 8)));
    }

    /* Columns. */
    y = 4; n = cols;
    uint16_t cx = bus_wram_read16(0x04DC);
    for (;;) {                                              /* 905E */
        bool ge = cx >= bus_read16(db, (uint16_t)(grid + y));
        y += 2;
        if (ge && cx < bus_read16(db, (uint16_t)(grid + y))) break;
        y += 2;
        bus_wram_write16(0x19A6, (uint16_t)(bus_wram_read16(0x19A6) + 1));
        if (--n == 0) {                                     /* 9070 none */
            bus_wram_write16(0x19A6, 0xFFFF);
            goto done;
        }
    }
    load_cell_record();                                     /* 9078 */
    bus_wram_write16(0x19AA, (uint16_t)(bus_wram_read16(0x19AA) + 1));

done:
    update_highlight();                                     /* 907E JMP $90C4 */
}
