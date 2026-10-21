#ifndef FM_H
#define FM_H
#include <stddef.h>
#include <stdint.h>

/* Prefixes: fm_ */

/* The OPL2 FM music driver of HMIMDRV.386 (fmmidi.com, device 0xa002) on an emulated OPL3 chip. */

enum { FM_BANK_MELODIC, FM_BANK_DRUM, FM_BANK_COUNT };

enum {
    FM_OK = 0,
    FM_ERR_BANK = -1, /* not an AdLib BNK file with 128 instruments */
};

/* Resets the chip to sample_rate and the driver to its start state; keeps the banks. */
void fm_reset(uint32_t sample_rate);
/* Copies MELODIC.BNK or DRUM.BNK and packs its instruments into OPL register bytes. */
int fm_set_bank(int bank, const uint8_t *data, size_t size);
void fm_free_banks(void);
/* Plays a 3-byte MIDI channel message; the driver ignores aftertouch and channel pressure. */
void fm_send(const uint8_t *event);
/* Renders frames of stereo samples, left then right. */
void fm_render(int16_t *out, int frames);
/* Test hook: hook sees each OPL register write; NULL removes it. */
void fm_set_write_hook(void (*hook)(int reg, int value));

#endif
