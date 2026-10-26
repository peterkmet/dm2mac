/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_RANDOM_H
#define DM2_ENGINE_RANDOM_H
/* Prefixes: rand_ */

/* The random number generator of the game rules. */

#include <stdint.h>

_Static_assert((-1 >> 1) == -1, "the port assumes an arithmetic right shift");

/* The state of the random number generator; the save game stores it. */
extern uint32_t g_random_seed;

/* Sets the state of the generator: a drawing that takes random numbers puts the state back. */
void rand_set_seed(uint32_t seed);
/* Advances the generator and returns 24 random bits. */
uint32_t rand_next(void);
/* A random number from 0 to n - 1; 0 without a draw when n is 0. */
uint16_t rand_below(uint16_t n);
/* A random 0 or 1. */
uint16_t rand_bit(void);
/* A random number from 0 to 3. */
uint16_t rand_2bits(void);

#endif /* DM2_ENGINE_RANDOM_H */
