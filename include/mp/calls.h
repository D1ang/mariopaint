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

#endif
