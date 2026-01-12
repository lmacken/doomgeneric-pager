// pager_optimizations.h
// WiFi Pineapple Pager DOOM Optimizations
//
// Compile-time flags to enable/disable individual optimizations.
// Use -D flags when building to enable specific optimizations.
//
// Based on research from:
// - FastDoom (DOS optimization techniques)
// - RP2040-doom (embedded rendering)
// - embeddedDOOM (memory reduction)
// - Our own benchmarking (see perf/PROMPT.md)

#ifndef PAGER_OPTIMIZATIONS_H
#define PAGER_OPTIMIZATIONS_H

// ============================================
// OPTIMIZATION FLAGS
// ============================================

// INLINE_FIXED_MATH: Inline FixedMul/FixedDiv
// Eliminates function call overhead on 165+ call sites
// Source: FastDoom, GZDoom
// Expected gain: 5-10% in hot paths
#ifdef INLINE_FIXED_MATH
#define USE_INLINE_FIXED 1
#else
#define USE_INLINE_FIXED 0
#endif

// AI_THROTTLE_ENABLED: Distance-based AI throttling
// Reduces think frequency for distant enemies
// Source: Custom optimization for embedded systems
// Expected gain: 10-15% game logic
#ifdef AI_THROTTLE_ENABLED
#define USE_AI_THROTTLE 1
#else
#define USE_AI_THROTTLE 0
#endif

// THINKER_PREFETCH_ENABLED: Prefetch in P_RunThinkers
// Prefetch next 2 thinkers to hide memory latency
// Source: Cache optimization analysis
// Expected gain: 5-10% game logic
#ifdef THINKER_PREFETCH_ENABLED
#define USE_THINKER_PREFETCH 1
#else
#define USE_THINKER_PREFETCH 0
#endif

// DOUBLE_BUFFER_ENABLED: Double buffering for display
// Render to back buffer while front transfers via SPI
// Source: RP2040-doom, nRF52840-doom
// Expected gain: Overlap CPU work with I/O
#ifdef DOUBLE_BUFFER_ENABLED
#define USE_DOUBLE_BUFFER 1
#else
#define USE_DOUBLE_BUFFER 0
#endif

// ADAPTIVE_VISPLANES: Dynamic visplane limits
// Start at 128 (vanilla), expand to 768 as needed
// Source: Custom for SIGIL compatibility
// Expected gain: Prevents crashes on complex maps
#ifdef ADAPTIVE_VISPLANES
#define USE_ADAPTIVE_VISPLANES 1
#else
#define USE_ADAPTIVE_VISPLANES 0
#endif

// RENDER_PREFETCH_ENABLED: Prefetch in render scaling loop
// Prefetch source pixels during framebuffer scaling
// Expected gain: Better cache utilization
#ifdef RENDER_PREFETCH_ENABLED
#define USE_RENDER_PREFETCH 1
#else
#define USE_RENDER_PREFETCH 0
#endif

// ============================================
// AI THROTTLING PARAMETERS
// ============================================

#if USE_AI_THROTTLE
// Distance thresholds in fixed-point units
// 1024 fracunits = 32 map units
#define AI_THROTTLE_DIST_NEAR   (1024 << FRACBITS)  // Always think
#define AI_THROTTLE_DIST_MID    (2048 << FRACBITS)  // Think every 2 tics
#define AI_THROTTLE_DIST_FAR    (4096 << FRACBITS)  // Think every 4 tics
#endif

// ============================================
// VISPLANE PARAMETERS
// ============================================

#if USE_ADAPTIVE_VISPLANES
#define MAXVISPLANES_POOL       768
#define MAXVISPLANES_DEFAULT    128
#define MAXVISPLANES_STEP1      256
#define MAXVISPLANES_STEP2      512
#define MAXVISPLANES_STEP3      768
#define VISPLANE_EXPAND_THRESHOLD 0.9
#define VISPLANE_MERGE_THRESHOLD  8
#else
#define MAXVISPLANES            512  // Increased from 128 for SIGIL
#endif

// ============================================
// RUNTIME DETECTION
// ============================================

// Print which optimizations are compiled in
static inline void PrintOptimizations(void) {
    printf("Pager optimizations:\n");
#if USE_INLINE_FIXED
    printf("  [x] Inline fixed-point math\n");
#else
    printf("  [ ] Inline fixed-point math\n");
#endif
#if USE_AI_THROTTLE
    printf("  [x] AI throttling\n");
#else
    printf("  [ ] AI throttling\n");
#endif
#if USE_THINKER_PREFETCH
    printf("  [x] Thinker prefetch\n");
#else
    printf("  [ ] Thinker prefetch\n");
#endif
#if USE_DOUBLE_BUFFER
    printf("  [x] Double buffering\n");
#else
    printf("  [ ] Double buffering\n");
#endif
#if USE_ADAPTIVE_VISPLANES
    printf("  [x] Adaptive visplanes\n");
#else
    printf("  [ ] Adaptive visplanes\n");
#endif
}

#endif // PAGER_OPTIMIZATIONS_H
