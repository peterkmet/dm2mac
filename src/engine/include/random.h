#ifndef RANDOM_H
#define RANDOM_H
#include <stdint.h>

/* Prefixes: rand_ */

/* The state of the random number generator; the save game stores it. */
extern uint32_t random_seed;

/* Advances the generator and returns 24 random bits. */
uint32_t rand_next(void);
/* A random number from 0 to n - 1; 0 without a draw when n is 0. */
uint16_t rand_below(uint16_t n);
/* A random 0 or 1. */
uint16_t rand_bit(void);
/* A random number from 0 to 3. */
uint16_t rand_2bits(void);

#endif
