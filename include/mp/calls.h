/*
 * Calling other routines the way the ROM does.
 *
 * func_table_call() has JSL semantics and func_table_call_jsr() JSR
 * semantics. The difference matters when the target has no C version and
 * runs on the interpreter: it pushes a return frame of that size, and an
 * RTS routine entered with a 3-byte JSL frame never returns properly.
 *
 * A native target is called as a plain C function, so nothing is pushed
 * and S stays where the caller had it, 2 (JSR) or 3 (JSL) bytes higher
 * than in the ROM. Usually harmless, but some routines leave S in A
 * (TSC ... TCS epilogues) or read stack arguments relative to S, so the
 * helpers below move S as the real JSR/JSL would while a native body runs.
 *
 *   call_jsr(a)  JSR a         call_jsl(a)  JSL a
 *   tail_jmp(a)  JMP a, where a ends in RTS (no frame of its own: it
 *                returns to our caller, as the native caller will)
 */
#ifndef MP_CALLS_H
#define MP_CALLS_H

#include <snesrecomp/func_table.h>
#include <snesrecomp/cpu.h>
#include <stdint.h>

static inline void call_jsr(uint32_t addr) {
    if (func_table_native(addr)) {
        g_cpu.S = (uint16_t)(g_cpu.S - 2);
        func_table_call_jsr(addr);
        g_cpu.S = (uint16_t)(g_cpu.S + 2);
    } else {
        func_table_call_jsr(addr);
    }
}

static inline void call_jsl(uint32_t addr) {
    if (func_table_native(addr)) {
        g_cpu.S = (uint16_t)(g_cpu.S - 3);
        func_table_call(addr);
        g_cpu.S = (uint16_t)(g_cpu.S + 3);
    } else {
        func_table_call(addr);
    }
}

static inline void tail_jmp(uint32_t addr) { func_table_call_jsr(addr); }

/* PHA with a 16-bit A: the high byte first, at S. */
static inline void push_a16(uint16_t v) {
    bus_wram_write8(g_cpu.S, (uint8_t)(v >> 8));
    bus_wram_write8((uint16_t)(g_cpu.S - 1), (uint8_t)v);
    g_cpu.S = (uint16_t)(g_cpu.S - 2);
}

/* JSR $9FC4 with its stack arguments: the n words (first pushed first),
 * the tool, n. $9FC4 removes them itself. */
static inline void call_tool_switch(uint16_t tool, int n, const uint16_t *words) {
    for (int i = 0; i < n; i++) push_a16(words[i]);
    push_a16(tool);
    push_a16((uint16_t)n);
    CPU_SET_A16((uint16_t)n);
    call_jsr(0x009FC4);
}

#endif
