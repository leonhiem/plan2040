/**
 * apps/babywarmer/warmup.h — the warm-up's numbers, shared by the control
 * (prog/warmer.cpp) and the guard (dev/guard.cpp)
 *
 * The guard keeps its own idea of the warm-up on purpose: it must not
 * trust what a job says (/dev/state), and warmer is a job. Both read
 * these numbers from here, so the two can't drift apart.
 */
#pragma once

#define WARMUP_POWER   80.0f   // boost: 40 % was too little, 100 % overshot (the field warmer)
#define WARMUP_EXIT_C  3.0f    // boost until the skin is within this of the setpoint
