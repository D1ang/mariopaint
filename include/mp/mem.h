/*
 * Memory access with the ROM's addressing rules.
 */
#ifndef MP_MEM_H
#define MP_MEM_H

#include <snesrecomp/bus.h>
#include <stdint.h>

/* LDA long,X: the 24-bit address carries into the next bank, so
 * $0A:8000 + $8000 is $0B:0000 (the next LoROM bank's $8000 half). */
static inline uint16_t read_long16(uint32_t addr) {
    addr &= 0xFFFFFF;
    uint8_t lo = bus_read8((uint8_t)(addr >> 16), (uint16_t)addr);
    addr = (addr + 1) & 0xFFFFFF;
    uint8_t hi = bus_read8((uint8_t)(addr >> 16), (uint16_t)addr);
    return (uint16_t)(lo | (hi << 8));
}

#endif
