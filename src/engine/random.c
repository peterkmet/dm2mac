/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "random.h"

/* Linear congruential generator; reDM2 src/rng_next.c finds the same constants. */
#define RANDOM_MULTIPLIER 0xbb40e62du
#define RANDOM_INCREMENT  0x0000000bu

/* The bits of the generator that a draw drops, and the masks of the small draws. */
#define RANDOM_DISCARDED_LOW_BITS 0x08
#define ONE_BIT_MASK              0x01u
#define TWO_BITS_MASK             0x03u

uint32_t g_random_seed;

uint32_t rand_next(void)
{
    g_random_seed = g_random_seed * RANDOM_MULTIPLIER + RANDOM_INCREMENT;
    return g_random_seed >> RANDOM_DISCARDED_LOW_BITS;
}

uint16_t rand_below(uint16_t n)
{
    if (n == 0)
        return 0;
    return (uint16_t)rand_next() % n;
}

uint16_t rand_bit(void)
{
    return rand_next() & ONE_BIT_MASK;
}

uint16_t rand_2bits(void)
{
    return rand_next() & TWO_BITS_MASK;
}
