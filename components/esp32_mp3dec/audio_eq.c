/**
 * @file    audio_eq.c
 * @brief   3-Band Parametric EQ — Runtime Coefficient Computation
 *
 * Signal Flow:
 *   int32 In → Pre-att → Bass → Mid → Treble → Limiter → Makeup → SoftClip → int32 Out
 *
 * Coefficients are computed at runtime using Audio EQ Cookbook formulas.
 * This allows:
 *   - Any sample rate (22050, 32000, 44100, 48000 Hz, etc.)
 *   - Configurable bass frequency (20-500 Hz)
 */

#include "audio_eq.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* dB → Linear Gain Lookup (Q14) for compressor */
static const int32_t DB_TO_LIN_Q14[13] = {
    16384, 18382, 20621, 23149, 25971, 29137,
    32685, 36683, 41155, 46168, 51807, 58135, 65230
};


/* =========================================================================
 * Coefficient Computation (Audio EQ Cookbook — Robert Bristow-Johnson)
 * ========================================================================= */

/**
 * @brief  Compute Low-Shelf filter coefficients
 */
static void compute_low_shelf(uint16_t freq_hz, int8_t gain_db,
                              uint32_t sample_rate, biquad_coeffs_t *out)
{
    if (gain_db == 0) {
        out->b0 = (1 << EQ_Q_FORMAT);
        out->b1 = out->b2 = out->a1 = out->a2 = 0;
        return;
    }

    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * M_PI * (float)freq_hz / (float)sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * 0.7071f);  /* Q = 0.7071 (Butterworth) */
    float sqrtA = sqrtf(A);
    float two_sqrtA_alpha = 2.0f * sqrtA * alpha;

    float b0 = A * ((A + 1) - (A - 1) * cos_w0 + two_sqrtA_alpha);
    float b1 = 2.0f * A * ((A - 1) - (A + 1) * cos_w0);
    float b2 = A * ((A + 1) - (A - 1) * cos_w0 - two_sqrtA_alpha);
    float a0 = (A + 1) + (A - 1) * cos_w0 + two_sqrtA_alpha;
    float a1 = -2.0f * ((A - 1) + (A + 1) * cos_w0);
    float a2 = (A + 1) + (A - 1) * cos_w0 - two_sqrtA_alpha;

    /* Normalize and convert to Q28 */
    float scale = (float)(1 << EQ_Q_FORMAT) / a0;
    out->b0 = (int32_t)roundf(b0 * scale);
    out->b1 = (int32_t)roundf(b1 * scale);
    out->b2 = (int32_t)roundf(b2 * scale);
    out->a1 = (int32_t)roundf(a1 * scale);
    out->a2 = (int32_t)roundf(a2 * scale);
}

/**
 * @brief  Compute Peaking EQ filter coefficients
 */
static void compute_peaking(uint16_t freq_hz, int8_t gain_db,
                            uint32_t sample_rate, biquad_coeffs_t *out)
{
    if (gain_db == 0) {
        out->b0 = (1 << EQ_Q_FORMAT);
        out->b1 = out->b2 = out->a1 = out->a2 = 0;
        return;
    }

    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * M_PI * (float)freq_hz / (float)sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * 1.0f);  /* Q = 1.0 */

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cos_w0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha / A;

    float scale = (float)(1 << EQ_Q_FORMAT) / a0;
    out->b0 = (int32_t)roundf(b0 * scale);
    out->b1 = (int32_t)roundf(b1 * scale);
    out->b2 = (int32_t)roundf(b2 * scale);
    out->a1 = (int32_t)roundf(a1 * scale);
    out->a2 = (int32_t)roundf(a2 * scale);
}

/**
 * @brief  Compute High-Shelf filter coefficients
 */
static void compute_high_shelf(uint16_t freq_hz, int8_t gain_db,
                               uint32_t sample_rate, biquad_coeffs_t *out)
{
    if (gain_db == 0) {
        out->b0 = (1 << EQ_Q_FORMAT);
        out->b1 = out->b2 = out->a1 = out->a2 = 0;
        return;
    }

    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * M_PI * (float)freq_hz / (float)sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * 0.7071f);
    float sqrtA = sqrtf(A);
    float two_sqrtA_alpha = 2.0f * sqrtA * alpha;

    float b0 = A * ((A + 1) + (A - 1) * cos_w0 + two_sqrtA_alpha);
    float b1 = -2.0f * A * ((A - 1) + (A + 1) * cos_w0);
    float b2 = A * ((A + 1) + (A - 1) * cos_w0 - two_sqrtA_alpha);
    float a0 = (A + 1) - (A - 1) * cos_w0 + two_sqrtA_alpha;
    float a1 = 2.0f * ((A - 1) - (A + 1) * cos_w0);
    float a2 = (A + 1) - (A - 1) * cos_w0 - two_sqrtA_alpha;

    float scale = (float)(1 << EQ_Q_FORMAT) / a0;
    out->b0 = (int32_t)roundf(b0 * scale);
    out->b1 = (int32_t)roundf(b1 * scale);
    out->b2 = (int32_t)roundf(b2 * scale);
    out->a1 = (int32_t)roundf(a1 * scale);
    out->a2 = (int32_t)roundf(a2 * scale);
}


/* =========================================================================
 * Internal Helpers
 * ========================================================================= */

/**
 * @brief  Rational soft clip: int64 → int32 (no hard clip!)
 */
static inline int32_t soft_clip(int64_t x)
{
    const int64_t THRESH = (int64_t)INT32_MAX * 3 / 4;
    const int64_t R      = (int64_t)INT32_MAX - THRESH;

    int64_t sign = (x >= 0) ? 1 : -1;
    int64_t ax   = (x >= 0) ? x : -x;

    if (ax <= THRESH) {
        return (int32_t)(sign * ax);
    }

    int64_t t = ax - THRESH;
    int64_t y = THRESH + (R * t) / (t + R);

    if (y > INT32_MAX) y = INT32_MAX;
    return (int32_t)(sign * y);
}

/**
 * @brief  Biquad processing — int32 I/O, int64 accumulator
 */
static inline int32_t biquad_process(const biquad_coeffs_t *c,
                                     biquad_state_t *s,
                                     int32_t in)
{
    /* Bypass check for 0 dB (pass-through) */
    if (c->b1 == 0 && c->a1 == 0) return in;

    int64_t acc = (int64_t)c->b0 * in
                + (int64_t)c->b1 * s->x1
                + (int64_t)c->b2 * s->x2
                - (int64_t)c->a1 * s->y1
                - (int64_t)c->a2 * s->y2;

    int64_t shifted = acc >> EQ_Q_FORMAT;

    if (shifted > INT32_MAX) shifted = INT32_MAX;
    if (shifted < INT32_MIN) shifted = INT32_MIN;

    int32_t out = (int32_t)shifted;

    s->x2 = s->x1;
    s->x1 = in;
    s->y2 = s->y1;
    s->y1 = out;

    return out;
}


/* =========================================================================
 * Public API
 * ========================================================================= */

void audio_eq_init(audio_eq_3band_t *eq)
{
    if (!eq) return;
    memset(eq, 0, sizeof(audio_eq_3band_t));

    eq->bass_freq_hz   = EQ_DEFAULT_BASS_HZ;
    eq->mid_freq_hz    = EQ_DEFAULT_MID_HZ;
    eq->treble_freq_hz = EQ_DEFAULT_TREBLE_HZ;
    eq->sample_rate    = EQ_DEFAULT_SR;

    eq->bass_coeff   = &eq->bass_coeffs;
    eq->mid_coeff    = &eq->mid_coeffs;
    eq->treble_coeff = &eq->treble_coeffs;

    /* Compute default 0 dB (pass-through) coefficients */
    compute_low_shelf(eq->bass_freq_hz, 0, eq->sample_rate, &eq->bass_coeffs);
    compute_peaking(eq->mid_freq_hz, 0, eq->sample_rate, &eq->mid_coeffs);
    compute_high_shelf(eq->treble_freq_hz, 0, eq->sample_rate, &eq->treble_coeffs);

    eq->pre_scale   = (1 << EQ_Q_FORMAT);
    eq->makeup_gain = (1 << EQ_Q_FORMAT);
    eq->limiter_env = 0;
    eq->comp_active = false;
    eq->enabled     = false;
}

void audio_eq_set_params(audio_eq_3band_t *eq,
                         int8_t bass_db, int8_t mid_db, int8_t treble_db)
{
    if (!eq) return;

    if (bass_db   < EQ_MIN_DB) bass_db   = EQ_MIN_DB;
    if (bass_db   > EQ_MAX_DB) bass_db   = EQ_MAX_DB;
    if (mid_db    < EQ_MIN_DB) mid_db    = EQ_MIN_DB;
    if (mid_db    > EQ_MAX_DB) mid_db    = EQ_MAX_DB;
    if (treble_db < EQ_MIN_DB) treble_db = EQ_MIN_DB;
    if (treble_db > EQ_MAX_DB) treble_db = EQ_MAX_DB;

    eq->gain_bass_db   = bass_db;
    eq->gain_mid_db    = mid_db;
    eq->gain_treble_db = treble_db;

    /* Compute coefficients at runtime */
    compute_low_shelf(eq->bass_freq_hz, bass_db, eq->sample_rate, &eq->bass_coeffs);
    compute_peaking(eq->mid_freq_hz, mid_db, eq->sample_rate, &eq->mid_coeffs);
    compute_high_shelf(eq->treble_freq_hz, treble_db, eq->sample_rate, &eq->treble_coeffs);

    eq->bass_coeff   = &eq->bass_coeffs;
    eq->mid_coeff    = &eq->mid_coeffs;
    eq->treble_coeff = &eq->treble_coeffs;

    eq->bass_active   = (bass_db   != 0);
    eq->mid_active    = (mid_db    != 0);
    eq->treble_active = (treble_db != 0);
    eq->enabled       = eq->bass_active || eq->mid_active || eq->treble_active;

    /* Auto-Compressor */
    eq->comp_active = (bass_db > 0 || mid_db > 0 || treble_db > 0);
    eq->limiter_env = 0;

    if (eq->comp_active) {
        int32_t max_gain_q14 = 16384;
        int32_t g;
        
        if (bass_db > 0) {
            g = DB_TO_LIN_Q14[bass_db];
            if (g > max_gain_q14) max_gain_q14 = g;
        }
        if (mid_db > 0) {
            g = DB_TO_LIN_Q14[mid_db];
            if (g > max_gain_q14) max_gain_q14 = g;
        }
        if (treble_db > 0) {
            g = DB_TO_LIN_Q14[treble_db];
            if (g > max_gain_q14) max_gain_q14 = g;
        }

        /* pre_scale = 1/max_gain */
        int64_t pre = ((int64_t)1 << EQ_Q_FORMAT) * 16384 / max_gain_q14;
        eq->pre_scale = (int32_t)pre;

        /* makeup_gain = sqrt(max_gain) — ลดจาก 0.95 เป็น ~0.50 เพื่อลด distortion */
        /* sqrt(max_gain_q14 / 16384) * 16384 in Q14 */
        double max_gain_f = (double)max_gain_q14 / 16384.0;
        double sqrt_gain = sqrt(max_gain_f);
        int64_t mk = (int64_t)(sqrt_gain * (double)(1LL << EQ_Q_FORMAT));
        eq->makeup_gain = (int32_t)mk;
    } else {
        eq->pre_scale   = (1 << EQ_Q_FORMAT);
        eq->makeup_gain = (1 << EQ_Q_FORMAT);
    }

    /* Reset state */
    memset(&eq->bass_l,   0, sizeof(biquad_state_t));
    memset(&eq->bass_r,   0, sizeof(biquad_state_t));
    memset(&eq->mid_l,    0, sizeof(biquad_state_t));
    memset(&eq->mid_r,    0, sizeof(biquad_state_t));
    memset(&eq->treble_l, 0, sizeof(biquad_state_t));
    memset(&eq->treble_r, 0, sizeof(biquad_state_t));
}

void audio_eq_set_bass_freq(audio_eq_3band_t *eq, uint16_t freq_hz)
{
    if (!eq) return;
    if (freq_hz < EQ_BASS_FREQ_MIN) freq_hz = EQ_BASS_FREQ_MIN;
    if (freq_hz > EQ_BASS_FREQ_MAX) freq_hz = EQ_BASS_FREQ_MAX;

    eq->bass_freq_hz = freq_hz;

    /* Recompute bass coefficients with current gain */
    compute_low_shelf(freq_hz, eq->gain_bass_db, eq->sample_rate, &eq->bass_coeffs);

    /* Reset bass state */
    memset(&eq->bass_l, 0, sizeof(biquad_state_t));
    memset(&eq->bass_r, 0, sizeof(biquad_state_t));
}

void audio_eq_set_sample_rate(audio_eq_3band_t *eq, uint32_t sample_rate)
{
    if (!eq || sample_rate == 0) return;
    if (sample_rate == eq->sample_rate) return;

    eq->sample_rate = sample_rate;

    /* Recompute all coefficients for new sample rate */
    compute_low_shelf(eq->bass_freq_hz, eq->gain_bass_db, sample_rate, &eq->bass_coeffs);
    compute_peaking(eq->mid_freq_hz, eq->gain_mid_db, sample_rate, &eq->mid_coeffs);
    compute_high_shelf(eq->treble_freq_hz, eq->gain_treble_db, sample_rate, &eq->treble_coeffs);

    /* Reset all state */
    memset(&eq->bass_l,   0, sizeof(biquad_state_t));
    memset(&eq->bass_r,   0, sizeof(biquad_state_t));
    memset(&eq->mid_l,    0, sizeof(biquad_state_t));
    memset(&eq->mid_r,    0, sizeof(biquad_state_t));
    memset(&eq->treble_l, 0, sizeof(biquad_state_t));
    memset(&eq->treble_r, 0, sizeof(biquad_state_t));

    eq->limiter_env = 0;
}

void audio_eq_process_stereo(audio_eq_3band_t *eq,
                             int32_t *samples, size_t num_samples)
{
    if (!eq || !eq->enabled || !samples || num_samples < 2) return;

    const bool do_bass   = eq->bass_active;
    const bool do_mid    = eq->mid_active;
    const bool do_treble = eq->treble_active;
    const bool do_comp   = eq->comp_active;

    const biquad_coeffs_t *cb = eq->bass_coeff;
    const biquad_coeffs_t *cm = eq->mid_coeff;
    const biquad_coeffs_t *ct = eq->treble_coeff;

    num_samples &= ~(size_t)1;

    const int64_t LIM_THRESH = (int64_t)1800000000;
    const int32_t pre_scale   = eq->pre_scale;
    const int32_t makeup_gain = eq->makeup_gain;
    int64_t env = eq->limiter_env;

    for (size_t i = 0; i < num_samples; i += 2)
    {
        /* ---- Left ---- */
        int32_t l = samples[i];

        if (do_comp) l = (int32_t)(((int64_t)l * pre_scale) >> EQ_Q_FORMAT);
        if (do_bass)   l = biquad_process(cb, &eq->bass_l, l);
        if (do_mid)    l = biquad_process(cm, &eq->mid_l,  l);
        if (do_treble) l = biquad_process(ct, &eq->treble_l, l);

        /* ---- Right ---- */
        int32_t r = samples[i + 1];

        if (do_comp) r = (int32_t)(((int64_t)r * pre_scale) >> EQ_Q_FORMAT);
        if (do_bass)   r = biquad_process(cb, &eq->bass_r, r);
        if (do_mid)    r = biquad_process(cm, &eq->mid_r,  r);
        if (do_treble) r = biquad_process(ct, &eq->treble_r, r);

        /* ---- Limiter + Makeup + Soft Clip ---- */
        if (do_comp) {
            int64_t abs_l = (l >= 0) ? (int64_t)l : -(int64_t)l;
            int64_t abs_r = (r >= 0) ? (int64_t)r : -(int64_t)r;
            int64_t peak  = (abs_l > abs_r) ? abs_l : abs_r;

            if (peak > env) {
                env += (peak - env) >> 7;
            } else {
                env -= env >> 13;
            }

            if (env > LIM_THRESH) {
                int32_t gain = (int32_t)((LIM_THRESH << EQ_Q_FORMAT) / env);
                l = (int32_t)(((int64_t)l * gain) >> EQ_Q_FORMAT);
                r = (int32_t)(((int64_t)r * gain) >> EQ_Q_FORMAT);
            }

            int64_t l_mk = ((int64_t)l * makeup_gain) >> EQ_Q_FORMAT;
            int64_t r_mk = ((int64_t)r * makeup_gain) >> EQ_Q_FORMAT;

            samples[i]     = soft_clip(l_mk);
            samples[i + 1] = soft_clip(r_mk);
        } else {
            samples[i]     = soft_clip((int64_t)l);
            samples[i + 1] = soft_clip((int64_t)r);
        }
    }

    eq->limiter_env = env;
}

void audio_eq_reset_state(audio_eq_3band_t *eq)
{
    // Reset biquad states (clear history)
    memset(&eq->bass_l, 0, sizeof(biquad_state_t));
    memset(&eq->bass_r, 0, sizeof(biquad_state_t));
    memset(&eq->mid_l, 0, sizeof(biquad_state_t));
    memset(&eq->mid_r, 0, sizeof(biquad_state_t));
    memset(&eq->treble_l, 0, sizeof(biquad_state_t));
    memset(&eq->treble_r, 0, sizeof(biquad_state_t));
}