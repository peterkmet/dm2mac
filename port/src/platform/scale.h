#ifndef SCALE_H
#define SCALE_H
#include <stdint.h>

/* AdvMAME Scale2x/Scale3x on palette indices; dst is (w*n) x (h*n), edges replicate. */
void scale2x(const uint8_t *src, int w, int h, uint8_t *dst);
void scale3x(const uint8_t *src, int w, int h, uint8_t *dst);
/* The upscale factor of a filter name: "none" 1, "scale2x" 2, "scale3x" 3; 0 for an unknown name. */
int scale_filter_factor(const char *name);

#endif
