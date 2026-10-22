/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm.h"
#include "hmp.h"
#include "le.h"
#include <opl3/opl3.h>
#include <stdlib.h>
#include <string.h>

/* AdLib BNK file: header, 12-byte names, 30-byte instrument records. */
#define BNK_USED            0x08
#define BNK_NAMES           0x0c
#define BNK_RECORDS         0x10
#define BNK_INSTRUMENTS     128
#define BNK_NAME_SIZE       12
#define BNK_NAME_DRUM_PITCH 2 /* DRUM.BNK: the note that a drum sounds at */
#define BNK_RECORD_SIZE     30
#define BNK_UNPACKED        2 /* the driver packs used - 2 records; the last two stay raw */

/* Record: two operators of 13 fields from byte 2 (modulator) and byte 15 (carrier), then the wave selects. */
#define REC_MODULATOR 2
#define REC_CARRIER   15
#define REC_MOD_WAVE  28
#define REC_CAR_WAVE  29
enum { OP_KSL, OP_MULT, OP_FB, OP_AR, OP_SL, OP_EG, OP_DR, OP_RR, OP_TL, OP_AM, OP_VIB, OP_KSR, OP_CON };
/* After packing: OP_AM holds register 0x20, OP_KSL 0x40, OP_AR 0x60, OP_SL 0x80, modulator OP_CON 0xc0. */

/* OPL2 registers and bits. */
#define OPL_TEST               0x01
#define OPL_CHARACTER          0x20
#define OPL_LEVEL              0x40
#define OPL_ATTACK             0x60
#define OPL_SUSTAIN            0x80
#define OPL_FNUM_LOW           0xa0
#define OPL_KEY_BLOCK          0xb0
#define OPL_RHYTHM             0xbd
#define OPL_FEEDBACK           0xc0
#define OPL_WAVE               0xe0
#define OPL_WAVE_SELECT_ENABLE 0x20
#define OPL_DEEP_AM_VIBRATO    0xc0
#define OPL_KEY_ON             0x20
#define OPL_RELEASE_FAST       0x0f
#define OPL_TL_MASK            0x3f
#define OPL_KSL_MASK           0xc0
#define OPL_ADDITIVE           0x01
#define OPL_AM_SHIFT           7 /* the bit positions of the operator fields in their registers */
#define OPL_VIB_SHIFT          6
#define OPL_EG_SHIFT           5
#define OPL_KSR_SHIFT          4
#define OPL_KSL_SHIFT          6
#define OPL_HIGH_NIBBLE_SHIFT  4
#define OPL_FB_SHIFT           1

/* MIDI messages. */
#define MIDI_DATA_MASK       0x7f
#define MIDI_NOTE_OFF        0x80
#define MIDI_NOTE_ON         0x90
#define MIDI_CTRL_SUSTAIN    0x40
#define MIDI_CTRL_BEND_RANGE 0x66 /* HMI: pitch bend range in semitones */
#define MIDI_DRUM_CHANNEL    9
#define BEND_RANGE_DEFAULT   2

#define VOICES           9
#define OPERATORS        22
#define OP_SUSTAIN_SLOTS 16
#define SUSTAIN_QUEUE    32
#define RELEASE_WRITES   5
#define LEVEL_STEPS      64
#define LEVEL_SHIFT      7
#define NOTES_PER_OCTAVE 12
#define NOTE_FIRST       12  /* the frequency table of the driver starts at note 12 */
#define FREQ_STEP_MAX    719 /* a bend step above this crosses a block border */
#define FREQ_FNUM_MASK   0x3ff
#define FREQ_BLOCK_MASK  0x1c00
#define FREQ_BLOCK_ONE   0x400
#define FREQ_BEND_STEP   114 /* index of the bend-down steps by range */
#define FREQ_HALF_FNUM   115 /* index of the F-numbers one block up, from note B down to C */
#define BEND_SCALE       1000
#define BEND_SHIFT       6

/* Operator offsets of the 9 voices (OPL2 register layout). */
static const uint8_t op_modulator[VOICES] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
static const uint8_t op_carrier[VOICES] = {3, 4, 5, 11, 12, 13, 19, 20, 21};

/* Velocity and volume level / 2 to OPL attenuation (driver table). */
static const uint8_t level_attenuation[LEVEL_STEPS] = {
    63, 58, 53, 48, 44, 41, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22,
    21, 20, 19, 18, 17, 16, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9,  9,  8,  8,  7,
    7,  6,  6,  6,  5,  5,  5,  4,  4,  4,  4,  3,  3,  3,  2,  2,  2,  1,  1,  0,
};

/* Block and F-number of each MIDI note as the driver reads them: its frequency table holds notes 12 to 114,
   notes 0 to 11 read the bytes of the attenuation table before it, and the bend tables follow it. */
static const uint32_t note_freq_table[] = {
    0x0d0e0e0f, 0x0b0c0c0d, 0x090a0a0b, 0x07080809, 0x06060607, 0x04050505, 0x03040404, 0x02020303, 0x00010102, 0x0,
    0x0,        0x0,        0x157,      0x16b,      0x181,      0x198,      0x1b0,      0x1ca,      0x1e5,      0x202,
    0x220,      0x241,      0x263,      0x287,      0x557,      0x56b,      0x581,      0x598,      0x5b0,      0x5ca,
    0x5e5,      0x602,      0x620,      0x641,      0x663,      0x687,      0x957,      0x96b,      0x981,      0x998,
    0x9b0,      0x9ca,      0x9e5,      0xa02,      0xa20,      0xa41,      0xa63,      0xa87,      0xd57,      0xd6b,
    0xd81,      0xd98,      0xdb0,      0xdca,      0xde5,      0xe02,      0xe20,      0xe41,      0xe63,      0xe87,
    0x1157,     0x116b,     0x1181,     0x1198,     0x11b0,     0x11ca,     0x11e5,     0x1202,     0x1220,     0x1241,
    0x1263,     0x1287,     0x1557,     0x156b,     0x1581,     0x1598,     0x15b0,     0x15ca,     0x15e5,     0x1602,
    0x1620,     0x1641,     0x1663,     0x1687,     0x1957,     0x196b,     0x1981,     0x1998,     0x19b0,     0x19ca,
    0x19e5,     0x1a02,     0x1a20,     0x1a41,     0x1a63,     0x1a87,     0x1d57,     0x1d6b,     0x1d81,     0x1d98,
    0x1db0,     0x1dca,     0x1de5,     0x1e02,     0x1e20,     0x1e41,     0x1e63,     0x1e87,     0x1eae,     0x1eb7,
    0x1f02,     0x1f30,     0x1f60,     0x1f94,     0x1fca,     0x144,      0x132,      0x121,      0x110,      0x101,
    0xf8,       0xe5,       0xd8,       0xcc,       0xc1,       0xb6,       0xac,       0xa1,       0x90,       0x99,
    0x86,       0x81,       0x7c,       0x73,       0x6c,       0x66,       0x60,       0x5b,       0x56,       0x2ae,
    0x2d6,      0x302,      0x330,      0x360,      0x394,      0x3de,      0x404,      0x440,      0x482,      0x4c6,
    0x50e,
};
#define FREQ_ENTRIES ((int)(sizeof note_freq_table / sizeof note_freq_table[0]))

static opl3_chip chip;
static void (*write_hook)(int reg, int value);
static uint8_t *banks[FM_BANK_COUNT];
static uint32_t bank_names[FM_BANK_COUNT], bank_records[FM_BANK_COUNT];

/* Driver state per voice, operator, and MIDI channel. */
static uint8_t voice_note[VOICES]; /* 0: the voice is free */
static int voice_channel[VOICES], voice_velocity[VOICES];
static uint8_t voice_key_block[VOICES];
static uint8_t op_level[OPERATORS]; /* instrument bytes of register 0x40 */
/* Instrument bytes of register 0x80. The driver keeps 16 of them before the low F-number bytes of the voices:
   operators 16 to 21 share the F-number bytes of voices 0 to 5. */
static uint8_t op_sustain[OP_SUSTAIN_SLOTS + VOICES];
static int ch_program[MIDI_CHANNELS], ch_volume[MIDI_CHANNELS], ch_bend[MIDI_CHANNELS];
static int ch_bend_used[MIDI_CHANNELS], ch_bend_range[MIDI_CHANNELS], ch_sustain[MIDI_CHANNELS];
/* A note off that waits for the sustain pedal. */
enum { QUEUED_NOTE, QUEUED_VELOCITY, QUEUED_CHANNEL, QUEUED_SIZE };
static uint8_t sustain_queue[MIDI_CHANNELS][SUSTAIN_QUEUE][QUEUED_SIZE];
static int sustain_count[MIDI_CHANNELS];

static void opl_write(int reg, int value)
{
    if (write_hook)
        write_hook(reg, value & BYTE_MASK);
    OPL3_WriteRegBuffered(&chip, (uint16_t)reg, (uint8_t)value);
}

void fm_set_write_hook(void (*hook)(int reg, int value))
{
    write_hook = hook;
}

void fm_reset(uint32_t sample_rate)
{
    OPL3_Reset(&chip, sample_rate);
    opl_write(OPL_TEST, OPL_WAVE_SELECT_ENABLE);
    for (int v = 0; v < VOICES; v++)
        opl_write(OPL_KEY_BLOCK + v, 0);
    opl_write(OPL_RHYTHM, OPL_DEEP_AM_VIBRATO);
    memset(voice_note, 0, sizeof voice_note);
    memset(voice_channel, 0, sizeof voice_channel);
    memset(voice_key_block, 0, sizeof voice_key_block);
    memset(op_level, 0, sizeof op_level);
    memset(op_sustain, 0, sizeof op_sustain);
    memset(ch_program, 0, sizeof ch_program);
    memset(ch_bend_used, 0, sizeof ch_bend_used);
    memset(ch_sustain, 0, sizeof ch_sustain);
    memset(sustain_count, 0, sizeof sustain_count);
    for (int v = 0; v < VOICES; v++)
        voice_velocity[v] = MIDI_MAX;
    for (int ch = 0; ch < MIDI_CHANNELS; ch++) {
        ch_volume[ch] = MIDI_MAX;
        ch_bend[ch] = BEND_CENTER;
        ch_bend_range[ch] = BEND_RANGE_DEFAULT;
    }
}

/* Packs the 13 fields of an operator into its register bytes, in place like the driver. */
static void pack_operator(uint8_t *op)
{
    op[OP_AM] = (uint8_t)(op[OP_AM] << OPL_AM_SHIFT | op[OP_VIB] << OPL_VIB_SHIFT | op[OP_EG] << OPL_EG_SHIFT |
                          op[OP_KSR] << OPL_KSR_SHIFT | op[OP_MULT]);
    op[OP_KSL] = (uint8_t)(op[OP_KSL] << OPL_KSL_SHIFT | op[OP_TL]);
    op[OP_AR] = (uint8_t)(op[OP_AR] << OPL_HIGH_NIBBLE_SHIFT | op[OP_DR]);
    op[OP_SL] = (uint8_t)(op[OP_SL] << OPL_HIGH_NIBBLE_SHIFT | op[OP_RR]);
}

int fm_set_bank(int bank, const uint8_t *data, size_t size)
{
    if (bank < 0 || bank >= FM_BANK_COUNT || size < BNK_RECORDS + sizeof(uint32_t))
        return FM_ERR_BANK;
    uint32_t names = le32(data + BNK_NAMES), records = le32(data + BNK_RECORDS);
    int used = le16(data + BNK_USED);
    if (used > BNK_INSTRUMENTS || names > size || size - names < BNK_INSTRUMENTS * BNK_NAME_SIZE || records > size ||
        size - records < BNK_INSTRUMENTS * BNK_RECORD_SIZE)
        return FM_ERR_BANK;
    uint8_t *copy = malloc(size);
    if (!copy)
        return FM_ERR_BANK;
    memcpy(copy, data, size);
    for (int i = 0; i < used - BNK_UNPACKED; i++) {
        uint8_t *rec = copy + records + i * BNK_RECORD_SIZE;
        pack_operator(rec + REC_MODULATOR);
        rec[REC_MODULATOR + OP_CON] =
            (uint8_t)(rec[REC_MODULATOR + OP_FB] << OPL_FB_SHIFT | rec[REC_MODULATOR + OP_CON]);
        pack_operator(rec + REC_CARRIER);
    }
    free(banks[bank]);
    banks[bank] = copy;
    bank_names[bank] = names;
    bank_records[bank] = records;
    return FM_OK;
}

void fm_free_banks(void)
{
    for (int b = 0; b < FM_BANK_COUNT; b++) {
        free(banks[b]);
        banks[b] = NULL;
    }
}

static const uint8_t *instrument(int bank, int index)
{
    return banks[bank] + bank_records[bank] + index * BNK_RECORD_SIZE;
}

static int drum_pitch(int note)
{
    return banks[FM_BANK_DRUM][bank_names[FM_BANK_DRUM] + note * BNK_NAME_SIZE + BNK_NAME_DRUM_PITCH];
}

static uint32_t note_freq(int note)
{
    return note_freq_table[note < 0 ? 0 : note >= FREQ_ENTRIES ? FREQ_ENTRIES - 1 : note]; /* fix: clamped */
}

/* Picks a voice for a new note: a free voice, else a voice of the first channel without pitch bend. */
static int voice_alloc(int channel)
{
    for (int v = 0; v < VOICES; v++)
        if (!voice_note[v])
            return v;
    for (int ch = 0; ch < MIDI_CHANNELS; ch++) {
        if (ch_bend_used[ch])
            continue;
        for (int v = 0; v < VOICES; v++)
            if (voice_channel[v] == ch)
                return v;
    }
    return channel >= VOICES ? channel - VOICES : channel;
}

static void voice_off(int v)
{
    if (!voice_note[v])
        return;
    voice_key_block[v] &= (uint8_t)~OPL_KEY_ON;
    opl_write(OPL_KEY_BLOCK + v, voice_key_block[v]);
    voice_note[v] = 0;
}

/* Writes the instrument registers of a voice; keeps the level and sustain bytes for later changes. */
static void voice_load(int v, const uint8_t *rec)
{
    const uint8_t *mod = rec + REC_MODULATOR, *car = rec + REC_CARRIER;
    int m = op_modulator[v], c = op_carrier[v];
    op_sustain[c] = car[OP_SL];
    op_sustain[m] = mod[OP_SL];
    opl_write(OPL_CHARACTER + m, mod[OP_AM]);
    opl_write(OPL_LEVEL + m, mod[OP_KSL]);
    op_level[m] = mod[OP_KSL];
    opl_write(OPL_ATTACK + m, mod[OP_AR]);
    opl_write(OPL_SUSTAIN + m, mod[OP_SL]);
    opl_write(OPL_FEEDBACK + v, mod[OP_CON]);
    opl_write(OPL_WAVE + m, rec[REC_MOD_WAVE]);
    opl_write(OPL_CHARACTER + c, car[OP_AM]);
    op_level[c] = car[OP_KSL];
    opl_write(OPL_ATTACK + c, car[OP_AR]);
    opl_write(OPL_SUSTAIN + c, car[OP_SL]);
    opl_write(OPL_WAVE + c, rec[REC_CAR_WAVE]);
}

/* Total level of an operator: the instrument level attenuated by the note level. */
static void op_write_level(int op, int attenuation)
{
    int tl = (2 * LEVEL_STEPS * LEVEL_STEPS -
              (LEVEL_STEPS - attenuation) * 2 * (LEVEL_STEPS - (op_level[op] & OPL_TL_MASK))) >>
             LEVEL_SHIFT;
    opl_write(OPL_LEVEL + op, (tl & BYTE_MASK) | (op_level[op] & OPL_KSL_MASK));
}

/* Sets the loudness of a voice from its velocity and the channel volume. */
static void voice_set_level(int v)
{
    int ch = voice_channel[v];
    int level = (ch_volume[ch] << LEVEL_SHIFT) / MIDI_MAX * voice_velocity[v] >> LEVEL_SHIFT;
    int attenuation = level_attenuation[(level & BYTE_MASK) >> 1];
    /* The driver checks the melodic instrument of the channel, also for drums. */
    if (instrument(FM_BANK_MELODIC, ch_program[ch])[REC_MODULATOR + OP_CON] & OPL_ADDITIVE)
        op_write_level(op_modulator[v], attenuation);
    op_write_level(op_carrier[v], attenuation);
}

/* Sets the frequency of a voice with the key on; retrigger first sets the key off to start the note again. */
static void voice_set_freq(int v, uint32_t freq, int retrigger)
{
    op_sustain[OP_SUSTAIN_SLOTS + v] = (uint8_t)freq;
    voice_key_block[v] = (uint8_t)(freq >> BYTE_BIT_COUNT) | OPL_KEY_ON;
    opl_write(OPL_FNUM_LOW + v, freq & BYTE_MASK);
    if (retrigger)
        opl_write(OPL_KEY_BLOCK + v, voice_key_block[v] & ~OPL_KEY_ON);
    opl_write(OPL_KEY_BLOCK + v, voice_key_block[v]);
}

/* Block and F-number of a note bent by bend (0 to 127, center 64) over the bend range of the voice channel. */
static uint32_t bend_freq(int bend, int note, int v)
{
    int range = ch_bend_range[voice_channel[v]];
    uint32_t semitone = (uint32_t)(note - NOTE_FIRST) % NOTES_PER_OCTAVE;
    uint32_t freq = note_freq(note), step;
    if (bend < BEND_CENTER) {
        step = freq - note_freq(note - range);
        if (step > FREQ_STEP_MAX)
            step = ((freq & FREQ_FNUM_MASK) - note_freq(FREQ_BEND_STEP + range)) & FREQ_FNUM_MASK;
        return freq - step * ((BEND_CENTER - 1 - bend) * BEND_SCALE >> BEND_SHIFT) / BEND_SCALE;
    }
    step = note_freq(note + range) - freq;
    if (step > FREQ_STEP_MAX) {
        freq = ((freq & FREQ_BLOCK_MASK) + FREQ_BLOCK_ONE) |
               note_freq(FREQ_HALF_FNUM + NOTES_PER_OCTAVE - 1 - (int)semitone);
        step = note_freq(note + range) - freq;
    }
    return freq + step * ((bend - BEND_CENTER) * BEND_SCALE >> BEND_SHIFT) / BEND_SCALE;
}

static void note_on(int note, int velocity, int channel)
{
    int v = voice_alloc(channel);
    voice_off(v);
    voice_channel[v] = channel;
    /* Repeated writes give the old note time for a fast release before the new note starts. */
    for (int i = 0; i < RELEASE_WRITES; i++) {
        opl_write(OPL_SUSTAIN + op_carrier[v], op_sustain[op_carrier[v]] | OPL_RELEASE_FAST);
        opl_write(OPL_SUSTAIN + op_modulator[v], op_sustain[op_modulator[v]] | OPL_RELEASE_FAST);
    }
    int drum = channel == MIDI_DRUM_CHANNEL;
    voice_load(v, drum ? instrument(FM_BANK_DRUM, note) : instrument(FM_BANK_MELODIC, ch_program[channel]));
    voice_velocity[v] = velocity;
    voice_set_level(v);
    voice_set_freq(v, note_freq(drum ? drum_pitch(note) : note), 1);
    voice_note[v] = (uint8_t)note;
    if (!drum && ch_bend_used[channel])
        voice_set_freq(v, bend_freq(ch_bend[channel], note, v), 0);
}

static void note_off(int note, int velocity, int channel)
{
    if (ch_sustain[channel] && sustain_count[channel] < SUSTAIN_QUEUE) {
        uint8_t *q = sustain_queue[channel][sustain_count[channel]++];
        q[QUEUED_NOTE] = (uint8_t)note;
        q[QUEUED_VELOCITY] = (uint8_t)velocity;
        q[QUEUED_CHANNEL] = (uint8_t)channel;
        return;
    }
    for (int v = 0; v < VOICES; v++)
        if (voice_note[v] == note && voice_channel[v] == channel)
            voice_off(v);
}

static void notes_off(int channel)
{
    for (int v = 0; v < VOICES; v++)
        if (voice_channel[v] == channel)
            voice_off(v);
}

static void reset_controllers(int channel)
{
    ch_volume[channel] = MIDI_MAX;
    /* The driver indexes the voice velocities with the channel here. */
    if (channel < VOICES)
        voice_velocity[channel] = MIDI_MAX;
    ch_sustain[channel] = 0;
    ch_bend[channel] = BEND_CENTER;
    ch_bend_range[channel] = BEND_RANGE_DEFAULT;
}

static void control(int channel, int number, int value)
{
    switch (number) {
    case MIDI_CTRL_VOLUME:
        ch_volume[channel] = value;
        for (int v = 0; v < VOICES; v++)
            if (voice_note[v] && voice_channel[v] == channel)
                voice_set_level(v);
        break;
    case MIDI_CTRL_SUSTAIN:
        ch_sustain[channel] = value;
        while (!value && sustain_count[channel]) {
            const uint8_t *q = sustain_queue[channel][--sustain_count[channel]];
            note_off(q[QUEUED_NOTE], q[QUEUED_VELOCITY], q[QUEUED_CHANNEL]);
        }
        break;
    case MIDI_CTRL_BEND_RANGE:
        ch_bend_range[channel] = value;
        break;
    case MIDI_CTRL_RESET:
        reset_controllers(channel);
        break;
    case MIDI_CTRL_NOTES_OFF:
        notes_off(channel);
        break;
    }
}

/* Pitch bend from the high data byte only; the drum channel has no pitch bend. */
static void pitch_bend(int channel, int bend)
{
    if (channel == MIDI_DRUM_CHANNEL)
        return;
    ch_bend[channel] = bend;
    ch_bend_used[channel] = 1;
    for (int v = 0; v < VOICES; v++)
        if (voice_note[v] && voice_channel[v] == channel)
            voice_set_freq(v, bend_freq(bend, voice_note[v], v), 0);
}

void fm_send(const uint8_t *event)
{
    if (!banks[FM_BANK_MELODIC] || !banks[FM_BANK_DRUM])
        return;
    int channel = event[MIDI_STATUS] & MIDI_CHANNEL_MASK;
    int data1 = event[MIDI_DATA1] & MIDI_DATA_MASK,
        data2 = event[MIDI_DATA2] & MIDI_DATA_MASK; /* fix: the driver does not mask */
    switch (event[MIDI_STATUS] & MIDI_STATUS_MASK) {
    case MIDI_NOTE_OFF:
        note_off(data1, data2, channel);
        break;
    case MIDI_NOTE_ON:
        if (data2)
            note_on(data1, data2, channel);
        else
            note_off(data1, data2, channel);
        break;
    case MIDI_CONTROL:
        control(channel, data1, data2);
        break;
    case MIDI_PROGRAM:
        ch_program[channel] = data1;
        break;
    case MIDI_PITCH_BEND:
        pitch_bend(channel, data2);
        break;
    }
}

void fm_render(int16_t *out, int frames)
{
    OPL3_GenerateStream(&chip, out, (uint32_t)frames);
}
