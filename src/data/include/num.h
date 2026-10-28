/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_NUM_H
#define DM2_DATA_NUM_H
/* Prefixes: abs_ clamp_ max_ min_ */

/* Integer helpers: the smaller, the bigger, the absolute and the limited value. */

/* The smaller of a and b. */
static inline int min_int(int a, int b)
{
    return a < b ? a : b;
}
/* The bigger of a and b. */
static inline int max_int(int a, int b)
{
    return a > b ? a : b;
}
/* The absolute value of v. */
static inline int abs_int(int v)
{
    return v < 0 ? -v : v;
}
/* The value v limited to lo and hi; lo wins when lo is bigger than hi. */
static inline int clamp_int(int lo, int v, int hi)
{
    return max_int(lo, min_int(v, hi));
}

#endif /* DM2_DATA_NUM_H */
