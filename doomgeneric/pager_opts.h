// pager_opts.h - WiFi Pineapple Pager optimization macros
//
// Function attributes for hot code paths:
//   HOT_FUNC      - More aggressive optimization, place in hot section
//   FLATTEN_FUNC  - Inline all callees into this function
//   FORCE_INLINE  - Always inline this function
//   COLD_FUNC     - Rarely executed (error paths, init)
//   PURE_FUNC     - No side effects, result depends only on arguments
//   CONST_FUNC    - Like pure but doesn't read global memory
//
// These help the compiler make better decisions for MIPS 24KEc:
// - HOT functions get more aggressive loop unrolling and scheduling
// - FLATTEN reduces call overhead in tight loops
// - Proper cold/hot separation improves instruction cache usage

#ifndef PAGER_OPTS_H
#define PAGER_OPTS_H

#ifdef __GNUC__

// Hot code path - more aggressive optimization
#define HOT_FUNC __attribute__((hot))

// Inline all callees into this function
#define FLATTEN_FUNC __attribute__((flatten))

// Always inline even at -O0
#define FORCE_INLINE __attribute__((always_inline)) inline

// Cold code path - rarely executed (error handling, init)
#define COLD_FUNC __attribute__((cold))

// Pure function - no side effects, depends only on args and global memory
#define PURE_FUNC __attribute__((pure))

// Const function - no side effects, depends ONLY on arguments
#define CONST_FUNC __attribute__((const))

// Never inline (for debugging or code size)
#define NOINLINE_FUNC __attribute__((noinline))

// Likely/unlikely branch hints
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Prefetch hints
#define PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
#define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)

#else

// Non-GCC fallbacks
#define HOT_FUNC
#define FLATTEN_FUNC
#define FORCE_INLINE inline
#define COLD_FUNC
#define PURE_FUNC
#define CONST_FUNC
#define NOINLINE_FUNC
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#define PREFETCH_READ(addr)
#define PREFETCH_WRITE(addr)

#endif // __GNUC__

#endif // PAGER_OPTS_H
