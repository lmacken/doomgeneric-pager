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
#include <stdlib.h>
#include <limits.h>

//
// Fixed point, 32bit as 16.16.
//
#define FRACBITS		16
#define FRACUNIT		(1<<FRACBITS)

typedef int fixed_t;

//
// PAGER OPTIMIZATION: Inline fixed-point math
//
// When INLINE_FIXED_MATH is defined, FixedMul and FixedDiv are inlined.
// This eliminates function call overhead on 165+ call sites in hot paths
// (rendering, collision, AI).
//
// Source: FastDoom, GZDoom optimization techniques
// Expected gain: 5-10% in hot code paths
//
// Compile with: -DINLINE_FIXED_MATH
//
#ifdef INLINE_FIXED_MATH

static inline fixed_t FixedMul(fixed_t a, fixed_t b)
{
    return (fixed_t)(((int64_t)a * (int64_t)b) >> FRACBITS);
}

static inline fixed_t FixedDiv(fixed_t a, fixed_t b)
{
    if ((abs(a) >> 14) >= abs(b))
    {
        return (a ^ b) < 0 ? INT_MIN : INT_MAX;
    }
    return (fixed_t)(((int64_t)a << FRACBITS) / b);
}

#else

// Standard function declarations (implementations in m_fixed.c)
fixed_t FixedMul(fixed_t a, fixed_t b);
fixed_t FixedDiv(fixed_t a, fixed_t b);

#endif // INLINE_FIXED_MATH

#endif // __M_FIXED__
