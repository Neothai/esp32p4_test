/**
 * @file    audio_eq.h
 * @brief   3-Band Parametric EQ — Runtime Computed, int32 Audio
 *
 * @note    คำนวณสัมประสิทธิ์ตอน runtime → รองรับ sample rate ใดๆ + ตั้ง bass freq ได้
 */

#ifndef AUDIO_EQ_H
#define AUDIO_EQ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

/* =========================================================================
 * Configuration
 * ========================================================================= */

#define EQ_Q_FORMAT         28
#define EQ_MIN_DB          -12
#define EQ_MAX_DB           12
#define EQ_DB_STEPS         25

/* Default frequencies */
#define EQ_DEFAULT_BASS_HZ     100
#define EQ_DEFAULT_MID_HZ     1200
#define EQ_DEFAULT_TREBLE_HZ  8000
#define EQ_DEFAULT_SR        44100

/* Configurable ranges */
#define EQ_BASS_FREQ_MIN       20
#define EQ_BASS_FREQ_MAX      500

/* =========================================================================
 * Data Types
 * ========================================================================= */

typedef struct {
    int32_t b0, b1, b2, a1, a2;   /* Q28 fixed-point */
} biquad_coeffs_t;

typedef struct {
    int32_t x1, x2;
    int32_t y1, y2;
} biquad_state_t;

typedef struct {
    /* Runtime-computed coefficients */
    biquad_coeffs_t bass_coeffs;
    biquad_coeffs_t mid_coeffs;
    biquad_coeffs_t treble_coeffs;

    /* Pointers (for processing loop convenience) */
    const biquad_coeffs_t *bass_coeff;
    const biquad_coeffs_t *mid_coeff;
    const biquad_coeffs_t *treble_coeff;

    /* State */
    biquad_state_t bass_l,   bass_r;
    biquad_state_t mid_l,    mid_r;
    biquad_state_t treble_l, treble_r;

    /* Gain settings */
    int8_t gain_bass_db;
    int8_t gain_mid_db;
    int8_t gain_treble_db;

    /* Frequency settings */
    uint16_t bass_freq_hz;
    uint16_t mid_freq_hz;
    uint16_t treble_freq_hz;
    uint32_t sample_rate;

    /* Flags */
    bool bass_active;
    bool mid_active;
    bool treble_active;
    bool enabled;

    /* Auto-Compressor */
    int32_t pre_scale;      /* Q28 */
    int32_t makeup_gain;    /* Q28 */
    int64_t limiter_env;
    bool    comp_active;
} audio_eq_3band_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialize EQ with defaults (100 Hz bass, 1.2 kHz mid, 8 kHz treble)
 */
void audio_eq_init(audio_eq_3band_t *eq);

/**
 * @brief  Set gain for each band (-12 to +12 dB)
 */
void audio_eq_set_params(audio_eq_3band_t *eq, int8_t bass_db, int8_t mid_db, int8_t treble_db);

/**
 * @brief  Set bass shelf frequency (20-500 Hz)
 *         Must call audio_eq_set_params() again to apply, or it auto-applies.
 */
void audio_eq_set_bass_freq(audio_eq_3band_t *eq, uint16_t freq_hz);

/**
 * @brief  Update sample rate (recompute all coefficients)
 *         Call when MP3 sample rate changes.
 */
void audio_eq_set_sample_rate(audio_eq_3band_t *eq, uint32_t sample_rate);

/**
 * @brief  Process stereo int32 audio (in-place)
 */
void audio_eq_process_stereo(audio_eq_3band_t *eq, int32_t *samples, size_t num_samples);

void audio_eq_reset_state(audio_eq_3band_t *eq);

#endif /* AUDIO_EQ_H */
