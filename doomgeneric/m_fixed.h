//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Fixed point arithemtics, implementation.
//


#ifndef __M_FIXED__
#define __M_FIXED__

#include <stdint.h>

//
// Fixed point, 32bit as 16.16.
//
#define FRACBITS		16
#define FRACUNIT		(1<<FRACBITS)

typedef int fixed_t;

//
// OPTIMIZED: Inline FixedMul for maximum performance
// This eliminates function call overhead for ~250 call sites
// On MIPS 24KEc, the mult instruction is used directly
//
#if defined(__GNUC__) && (defined(__mips__) || defined(__mips))
// MIPS-specific inline assembly version
// Uses the hardware 32x32->64 multiplier and extracts bits 47:16
static inline fixed_t FixedMul(fixed_t a, fixed_t b)
{
    int32_t lo;
    int32_t hi;
    __asm__ __volatile__ (
        "mult %2, %3\n\t"
        "mflo %0\n\t"
        "mfhi %1"
        : "=r" (lo), "=r" (hi)
        : "r" (a), "r" (b)
    );
    // Result is (hi << 16) | (lo >> 16) = bits 47:16 of the 64-bit product
    return (hi << 16) | ((uint32_t)lo >> 16);
}
#else
// Generic C version for other platforms
static inline fixed_t FixedMul(fixed_t a, fixed_t b)
{
    return (fixed_t)(((int64_t)a * (int64_t)b) >> FRACBITS);
}
#endif

// FixedDiv is less frequently called, keep as external function
fixed_t FixedDiv(fixed_t a, fixed_t b);


#endif
