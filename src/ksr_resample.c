/*

Kasaria -- A powerful and High efficiency MIDI Synth based on TiMidity
Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>
Copyright (C) 2026 Kiptunor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

resample.c
*/














#include <malloc.h>
#include <string.h>

#include "ksr_internal.h"
#include "ext_deps/log_c/log.h"













/* Vectorised resampling of the "play sample until it ends" / "play sample
 * until the loop wraps" inner loops.
 *
 * The AVX2 kernel replaces eight scalar RESAMPLATION steps with a single
 * gather + interpolation.  It is only used where it can produce bit-identical
 * results (see rs_block_ok()), and it always falls back to the scalar code for
 * the tail of a block, so the number of produced samples - and therefore the
 * *countptr bookkeeping of resample_voice() - stays exactly the same.
 *
 * Define KSR_USE_RESAMPLE_SIMD to 0 to build the plain scalar version. */
#ifndef KSR_USE_RESAMPLE_SIMD
#define KSR_USE_RESAMPLE_SIMD 1
#endif

#if defined(__AVX2__) && defined(LINEAR_INTERPOLATION) && !defined(LOOKUP_HACK) && KSR_USE_RESAMPLE_SIMD
#define KSR_RESAMPLE_SIMD 1
#include <immintrin.h>
#define RESAMPLE_BLOCK 8

/* The AVX2 kernel works on 32-bit lanes, so the fixed-point sample position is
 * split into an integer sample index plus a FRACTION_BITS wide fraction.  The
 * old formulation ("ofs + k*incr" truncated to int) silently wrapped for
 * samples longer than 2^31 fixed-point units, which is only ~12 s of audio at
 * 44.1 kHz - quite common in soundfonts. */
#define KSR_RS_MAX_INCR  (1L << 27)  /* 7*incr has to stay inside an int32 lane  */
#define KSR_RS_MAX_IDX   0x7F000000L /* sample index has to stay inside an int32   */
#define KSR_RS_MIN_IDX   (-0x7F000000L)

    /* Linear-interpolate 8 samples at fixed-point ofs stepping by incr.
       The gather reads src[idx], src[idx+1]; rs_block_ok() keeps it in-bounds.
       The result is bit-identical to the scalar RESAMPLATION macro. */
static inline void rs_block8(long ofs, long incr, const sample_t *__restrict src, sample_t *__restrict dest)
{
    long      base = ofs >> FRACTION_BITS;
    int32_t   lo   = (int32_t)(ofs & FRACTION_MASK);
    int32_t   inc  = (int32_t)incr;

    __m256i   kv   = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    /* t = lo + k*incr; q = t / 4096 (floor), frac = t % 4096 */
    __m256i   t    = _mm256_add_epi32(_mm256_set1_epi32(lo),
                                      _mm256_mullo_epi32(kv, _mm256_set1_epi32(inc)));
    __m256i   q    = _mm256_srai_epi32(t, FRACTION_BITS);
    __m256i   frac = _mm256_and_si256(t, _mm256_set1_epi32((int)FRACTION_MASK));
    __m256i   idx  = _mm256_add_epi32(_mm256_set1_epi32((int32_t)base), q);

    // 4-byte gather at src + idx*2 reads shorts [idx] and [idx+1]
    __m256i g  = _mm256_i32gather_epi32((const int *)src, idx, 2);
    __m256i v1 = _mm256_srai_epi32(_mm256_slli_epi32(g, 16), 16); // low short, sign-extended
    __m256i v2 = _mm256_srai_epi32(g, 16);                        // high short, sign-extended

    __m256i d = _mm256_mullo_epi32(_mm256_sub_epi32(v2, v1), frac);
    d = _mm256_add_epi32(_mm256_srai_epi32(d, FRACTION_BITS), v1);

    _mm_storeu_si128((__m128i *)dest,
                     _mm_packs_epi32(_mm256_castsi256_si128(d),
                                     _mm256_extracti128_si256(d, 1)));
}

/* Can the next RESAMPLE_BLOCK output samples be produced by rs_block8()?
 *
 *   lim      - fixed-point position where the sample/loop ends; the block must
 *              stay strictly before it, otherwise the voice would end (or the
 *              loop would wrap) in the middle of the block and the scalar
 *              tail below would no longer see a valid position.
 *   data_len - fixed-point length of the whole sample data; the 32-bit gather
 *              reads src[idx] *and* src[idx+1], so both shorts must be inside
 *              the sample.
 */
static inline int rs_block_ok(long ofs, long incr, long lim, long data_len)
{
    if(incr <= 0 || incr > KSR_RS_MAX_INCR)
        return 0;

    if(ofs < 0)
        return 0;

    /* the voice must not end (or the loop must not wrap) inside the block */
    if(ofs + (long)RESAMPLE_BLOCK * incr >= lim)
        return 0;

    /* 32-bit index arithmetic */
    long base = ofs >> FRACTION_BITS;

    if(base > KSR_RS_MAX_IDX || base < KSR_RS_MIN_IDX)
        return 0;

    /* keep the 32-bit word read by the gather inside the sample data */
    long hi = (ofs + (long)(RESAMPLE_BLOCK - 1) * incr) >> FRACTION_BITS;

    if(hi + 1 >= (data_len >> FRACTION_BITS))
        return 0;

    return 1;
}
#else
    #define KSR_RESAMPLE_SIMD 0
#endif




#ifdef LINEAR_INTERPOLATION
    #if defined(LOOKUP_HACK) && defined(LOOKUP_INTERPOLATION)
        #define RESAMPLATION                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
            v1      = src[ofs >> FRACTION_BITS];                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       \
            v2      = src[(ofs >> FRACTION_BITS) + 1];                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 \
            *dest++ = v1 + (ksr->iplookup[(((v2 - v1) << 5) & 0x03FE0) | ((ofs & FRACTION_MASK) >> (FRACTION_BITS - 5))]);
    #else
        #define RESAMPLATION                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
            v1      = src[ofs >> FRACTION_BITS];                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       \
            v2      = src[(ofs >> FRACTION_BITS) + 1];                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 \
            *dest++ = v1 + (((v2 - v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS);
    #endif
    #define INTERPVARS sample_t v1, v2
#else
    // Earplugs recommended for maximum listening enjoyment
    #define RESAMPLATION *dest++ = src[ofs >> FRACTION_BITS];
    #define INTERPVARS
#endif

#define FINALINTERP                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    \
    if(ofs == le)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      \
        *dest++ = src[(ofs >> FRACTION_BITS) - 1] / 2;
// So it isn't interpolation. At least it's final.

/*************** resampling with fixed increment *****************/

static sample_t *rs_plain(Kasaria *ksr, int v, long *countptr)
{

    // Play sample until end, then free the voice.

    INTERPVARS;
    Voice    *vp    = &ksr->voice[v];
    sample_t *dest  = ksr->resample_buffer;
    sample_t *src   = vp->sample->data;
    long      ofs   = vp->sample_offset;
    long      incr  = vp->sample_increment;
    long      le    = vp->sample->data_length;
    long      count = *countptr;

    if(ofs < 0 || ofs >= le)
    {
        vp->status = VOICE_FREE;
        *countptr = 0;
        return dest;
    }

#ifdef PRECALC_LOOPS
    long i;

    if(incr < 0)
        incr = -incr; // In case we're coming out of a bidir loop


    // Precalc how many times we should go through the loop.
    // NOTE: Assumes that incr > 0 and that ofs <= le

    if(incr == 0)
    {
        vp->status = VOICE_FREE;
        *countptr = 0;
        return ksr->resample_buffer;
    }

    i = (le - ofs) / incr + 1;

    if(i > count)
    {
        i     = count;
        count = 0;
    }
    else
        count -= i;

#if KSR_RESAMPLE_SIMD
    while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, le))
    {
        rs_block8(ofs, incr, src, dest);
        ofs  += (long)RESAMPLE_BLOCK * incr;
        dest += RESAMPLE_BLOCK;
        i    -= RESAMPLE_BLOCK;
    }
#endif

    while(i--)
    {
        RESAMPLATION;
        ofs += incr;
    }

    if(ofs >= le)
    {
        FINALINTERP;
        vp->status  = VOICE_FREE;
        *countptr  -= count + 1;
    }

#else  // PRECALC_LOOPS
#if KSR_RESAMPLE_SIMD
    /* Fast path: eight output samples per AVX2 iteration.  The block is only
     * used while it stays completely inside the sample data, so the scalar
     * tail below keeps the exact same *countptr semantics as before. */
    while(count >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, le))
    {
        rs_block8(ofs, incr, src, dest);
        dest  += RESAMPLE_BLOCK;
        ofs   += (long)RESAMPLE_BLOCK * incr;
        count -= RESAMPLE_BLOCK;
    }
#endif

    while(count--)
    {
        RESAMPLATION;
        ofs += incr;
        if(ofs >= le)
        {
            FINALINTERP;
            vp->status  = VOICE_FREE;
            *countptr  -= count + 1;
            break;
        }
    }
#endif // PRECALC_LOOPS

    vp->sample_offset = ofs; // Update offset
    return ksr->resample_buffer;
}

static sample_t *rs_loop(Kasaria *ksr, Voice *vp, long count)
{

    // Play sample until end-of-loop, skip back and continue.

    INTERPVARS;
    long      ofs  = vp->sample_offset;
    long      incr = vp->sample_increment;
    long      ls   = vp->sample->loop_start;
    long      le   = vp->sample->loop_end;
    long      ll   = le - ls;
    sample_t *dest = ksr->resample_buffer;
    sample_t *src  = vp->sample->data;

    if(incr == 0)
    {
        memset(dest, 0, count * sizeof(sample_t));
        return dest;
    }

    /* Fold the offset and increment modulo the loop length once, up front.
     * Wrapped positions are taken modulo ll, so using incr % ll is exact,
     * yet it lets the per-sample wrap below stay a single subtraction.
     * (An integer division per sample is far too costly for short loops /
     * heavily pitch-shifted voices, e.g. layered zones played far from their
     * root key, and the old ofs -= ll could walk out of the buffer there.) */
    if(ll > 0)
    {
        if(ofs >= le)
            ofs = ls + ((ofs - le) % ll);

        if(incr >= ll)
        {
            incr %= ll;
            if(incr == 0)
                incr = ll; /* exact multiple: position does not change */
        }
    }
    else if(ofs >= le)
    {
        ofs = ls; /* degenerate/malformed loop guard */
    }

#ifdef PRECALC_LOOPS
    long i;

    while(count)
    {
        /* ofs is < le at loop top; keep it that way after the inner step. */
        if(ofs >= le)
        {
            if(ll > 0)
                ofs -= ll;
            else
                ofs = ls;
        }

        // Precalc how many times we should go through the loop
        i = (le - ofs) / incr + 1;

        if(i > count)
        {
            i     = count;
            count = 0;
        }
        else
            count -= i;

#if KSR_RESAMPLE_SIMD
        while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, vp->sample->data_length))
        {
            rs_block8(ofs, incr, src, dest);
            ofs  += (long)RESAMPLE_BLOCK * incr;
            dest += RESAMPLE_BLOCK;
            i    -= RESAMPLE_BLOCK;
        }
#endif

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
    }
#else
#if KSR_RESAMPLE_SIMD
    /* Fast path: eight output samples per AVX2 iteration.  The block is only
     * used while it stays completely inside the loop region, so the scalar
     * tail below keeps the exact same wrap-around behaviour as before. */
    while(count >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, vp->sample->data_length))
    {
        rs_block8(ofs, incr, src, dest);
        dest  += RESAMPLE_BLOCK;
        ofs   += (long)RESAMPLE_BLOCK * incr;
        count -= RESAMPLE_BLOCK;
    }
#endif

    while(count--)
    {
        RESAMPLATION;
        ofs += incr;
        if(ofs >= le)
        {
            /* incr < ll after the fold above, so one subtraction is enough. */
            if(ll > 0)
                ofs -= ll;
            else
                ofs = ls;
        }
    }
#endif

    vp->sample_offset = ofs; // Update offset
    return ksr->resample_buffer;
}

static sample_t *rs_bidir(Kasaria *ksr, Voice *vp, long count)
{
    INTERPVARS;
    long      ofs  = vp->sample_offset;
    long      incr = vp->sample_increment;
    long      le   = vp->sample->loop_end;
    long      ls   = vp->sample->loop_start;
    sample_t *dest = ksr->resample_buffer, *src = vp->sample->data;

#ifdef PRECALC_LOOPS
    long le2 = le << 1, ls2 = ls << 1, i;
    // Play normally until inside the loop region

    if(ofs <= ls)
    {
        /*
            NOTE: Assumes that incr > 0, which is NOT always the case
            when doing bidirectional looping.  I have yet to see a case
            where both ofs <= ls AND incr < 0, however.
        */
        i = (ls - ofs) / incr + 1;
        if(i > count)
        {
            i     = count;
            count = 0;
        }
        else
            count -= i;

#if KSR_RESAMPLE_SIMD
        if(incr > 0)
            while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, ls, vp->sample->data_length))
            {
                rs_block8(ofs, incr, src, dest);
                ofs  += (long)RESAMPLE_BLOCK * incr;
                dest += RESAMPLE_BLOCK;
                i    -= RESAMPLE_BLOCK;
            }
#endif
        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
    }

    // Then do the bidirectional looping

    while(count)
    {
        // Precalc how many times we should go through the loop
        i = ((incr > 0 ? le : ls) - ofs) / incr + 1;
        if(i > count)
        {
            i     = count;
            count = 0;
        }
        else
            count -= i;

#if KSR_RESAMPLE_SIMD
        /* Only the forward direction is vectorised; the ping-pong turnaround
         * changes the increment in the middle of the loop, so the backwards
         * half stays on the scalar path. */
        if(incr > 0)
        {
            while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, vp->sample->data_length))
            {
                rs_block8(ofs, incr, src, dest);
                ofs  += (long)RESAMPLE_BLOCK * incr;
                dest += RESAMPLE_BLOCK;
                i    -= RESAMPLE_BLOCK;
            }
        }
#endif

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(ofs >= le)
        {
            // fold the overshoot back in
            ofs   = le2 - ofs;
            incr *= -1;
        }
        else if(ofs <= ls)
        {
            ofs   = ls2 - ofs;
            incr *= -1;
        }
    }

#else  // PRECALC_LOOPS
    // Play normally until inside the loop region

    if(ofs < ls)
    {
        while(count--)
        {
            RESAMPLATION;
            ofs += incr;
            if(ofs >= ls)
                break;
        }
    }

    // Then do the bidirectional looping

    if(count > 0)
        while(count--)
        {
            RESAMPLATION;
            ofs += incr;
            if(ofs >= le)
            {
                // fold the overshoot back in
                ofs  = le - (ofs - le);
                incr = -incr;
            }
            else if(ofs <= ls)
            {
                ofs  = ls + (ls - ofs);
                incr = -incr;
            }
        }
#endif // PRECALC_LOOPS
    vp->sample_increment = incr;
    vp->sample_offset    = ofs; // Update offset
    return ksr->resample_buffer;
}

/*********************** vibrato versions ***************************/

// We only need to compute one half of the vibrato sine cycle
static int vib_phase_to_inc_ptr(int phase)
{
    if(phase < VIBRATO_SAMPLE_INCREMENTS / 2)
        return VIBRATO_SAMPLE_INCREMENTS / 2 - 1 - phase;
    else if(phase >= 3 * VIBRATO_SAMPLE_INCREMENTS / 2)
        return 5 * VIBRATO_SAMPLE_INCREMENTS / 2 - 1 - phase;
    else
        return phase - VIBRATO_SAMPLE_INCREMENTS / 2;
}

static long update_vibrato(Kasaria *ksr, Voice *vp, int sign)
{
    long depth;
    int  phase, pb;
    f64  a;

    if(vp->vibrato_phase++ >= 2 * VIBRATO_SAMPLE_INCREMENTS - 1)
        vp->vibrato_phase = 0;

    phase = vib_phase_to_inc_ptr(vp->vibrato_phase);

    if(vp->vibrato_sample_increment[phase])
    {
        if(sign)
            return -vp->vibrato_sample_increment[phase];
        else
            return vp->vibrato_sample_increment[phase];
    }

    // Need to compute this sample increment.

    depth = vp->sample->vibrato_depth << 7;

    if(vp->vibrato_sweep)
    {
        // Need to update sweep
        vp->vibrato_sweep_position += vp->vibrato_sweep;
        if(vp->vibrato_sweep_position >= (1 << SWEEP_SHIFT))
            vp->vibrato_sweep = 0;
        else
        {
            // Adjust depth
            depth  *= vp->vibrato_sweep_position;
            depth >>= SWEEP_SHIFT;
        }
    }

    a  = FSCALE(((f64)(vp->sample->sample_rate) * (f64)(vp->frequency)) / ((f64)(vp->sample->root_freq) * (f64)(ksr->play_mode.rate)), FRACTION_BITS);

    pb = (int)((sine(vp->vibrato_phase * (SINE_CYCLE_LENGTH / (2 * VIBRATO_SAMPLE_INCREMENTS))) * (f64)(depth)*VIBRATO_AMPLITUDE_TUNING));

    if(pb < 0)
    {
        pb  = -pb;
        a  /= bend_fine[(pb >> 5) & 0xFF] * bend_coarse[pb >> 13];
    }
    else
        a *= bend_fine[(pb >> 5) & 0xFF] * bend_coarse[pb >> 13];

    // If the sweep's over, we can store the newly computed sample_increment
    if(!vp->vibrato_sweep)
        vp->vibrato_sample_increment[phase] = (long)a;

    if(sign)
        a = -a; // need to preserve the loop direction

    return (long)a;
}

static sample_t *rs_vib_plain(Kasaria *ksr, int v, long *countptr)
{

    // Play sample until end, then free the voice.

    INTERPVARS;
    Voice    *vp    = &ksr->voice[v];
    sample_t *dest  = ksr->resample_buffer;
    sample_t *src   = vp->sample->data;
    long      le    = vp->sample->data_length;
    long      ofs   = vp->sample_offset;
    long      incr  = vp->sample_increment;
    long      count = *countptr;
    int       cc    = vp->vibrato_control_counter;

    // This has never been tested

    if(incr < 0)
        incr = -incr; // In case we're coming out of a bidir loop

    while(count--)
    {
        if(!cc--)
        {
            cc   = vp->vibrato_control_ratio;
            incr = update_vibrato(ksr, vp, 0);
        }
        RESAMPLATION;
        ofs += incr;
        if(ofs >= le)
        {
            FINALINTERP;
            vp->status  = VOICE_FREE;
            *countptr  -= count + 1;
            break;
        }
    }

    vp->vibrato_control_counter = cc;
    vp->sample_increment        = incr;
    vp->sample_offset           = ofs; // Update offset

    return ksr->resample_buffer;
}

static sample_t *rs_vib_loop(Kasaria *ksr, Voice *vp, long count)
{

    // Play sample until end-of-loop, skip back and continue.

    INTERPVARS;
    long      ofs  = vp->sample_offset;
    long      incr = vp->sample_increment;
    long      le   = vp->sample->loop_end;
    long      ll   = le - vp->sample->loop_start;
    sample_t *dest = ksr->resample_buffer;
    sample_t *src  = vp->sample->data;
    int       cc   = vp->vibrato_control_counter;

#ifdef PRECALC_LOOPS
    long i;
    int  vibflag = 0;

    while(count)
    {
        // Hopefully the loop is longer than an increment
        while(ofs >= le)
            ofs -= ll;

        // Precalc how many times to go through the loop, taking the vibrato control ratio into account this time.
        i = (le - ofs) / incr + 1;

        if(i > count)
            i = count;

        if(i > cc)
        {
            i       = cc;
            vibflag = 1;
        }
        else
            cc -= i;

        count -= i;

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(vibflag)
        {
            cc      = vp->vibrato_control_ratio;
            incr    = update_vibrato(ksr, vp, 0);
            vibflag = 0;
        }
    }

#else  // PRECALC_LOOPS
    while(count--)
    {
        if(!cc--)
        {
            cc   = vp->vibrato_control_ratio;
            incr = update_vibrato(ksr, vp, 0);
        }
        RESAMPLATION;
        ofs += incr;

        if(ofs >= le)
            ofs -= ll; // Hopefully the loop is longer than an increment.
    }
#endif // PRECALC_LOOPS

    vp->vibrato_control_counter = cc;
    vp->sample_increment        = incr;
    vp->sample_offset           = ofs; // Update offset
    return ksr->resample_buffer;
}

static sample_t *rs_vib_bidir(Kasaria *ksr, Voice *vp, long count)
{
    INTERPVARS;
    long      ofs  = vp->sample_offset;
    long      incr = vp->sample_increment;
    long      le   = vp->sample->loop_end;
    long      ls   = vp->sample->loop_start;
    sample_t *dest = ksr->resample_buffer;
    sample_t *src  = vp->sample->data;
    int       cc   = vp->vibrato_control_counter;

#ifdef PRECALC_LOOPS
    long le2 = le << 1;
    long ls2 = ls << 1;
    long i;
    int  vibflag = 0;

    // Play normally until inside the loop region
    while(count && (ofs <= ls))
    {
        i = (ls - ofs) / incr + 1;

        if(i > count)
            i = count;

        if(i > cc)
        {
            i       = cc;
            vibflag = 1;
        }
        else
            cc -= i;

        count -= i;

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(vibflag)
        {
            cc      = vp->vibrato_control_ratio;
            incr    = update_vibrato(ksr, vp, 0);
            vibflag = 0;
        }
    }

    // Then do the bidirectional looping

    while(count)
    {
        // Precalc how many times we should go through the loop
        i = ((incr > 0 ? le : ls) - ofs) / incr + 1;
        if(i > count)
            i = count;

        if(i > cc)
        {
            i       = cc;
            vibflag = 1;
        }
        else
            cc -= i;

        count -= i;

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(vibflag)
        {
            cc      = vp->vibrato_control_ratio;
            incr    = update_vibrato(ksr, vp, (incr < 0));
            vibflag = 0;
        }
        if(ofs >= le)
        {
            // fold the overshoot back in
            ofs   = le2 - ofs;
            incr *= -1;
        }
        else if(ofs <= ls)
        {
            ofs   = ls2 - ofs;
            incr *= -1;
        }
    }

#else  // PRECALC_LOOPS
    // Play normally until inside the loop region

    if(ofs < ls)
    {
        while(count--)
        {
            if(!cc--)
            {
                cc   = vp->vibrato_control_ratio;
                incr = update_vibrato(ksr, vp, 0);
            }
            RESAMPLATION;
            ofs += incr;
            if(ofs >= ls)
                break;
        }
    }

    // Then do the bidirectional looping

    if(count > 0)
        while(count--)
        {
            if(!cc--)
            {
                cc   = vp->vibrato_control_ratio;
                incr = update_vibrato(ksr, vp, (incr < 0));
            }
            RESAMPLATION;
            ofs += incr;
            if(ofs >= le)
            {
                // fold the overshoot back in
                ofs  = le - (ofs - le);
                incr = -incr;
            }
            else if(ofs <= ls)
            {
                ofs  = ls + (ls - ofs);
                incr = -incr;
            }
        }
#endif // PRECALC_LOOPS

    vp->vibrato_control_counter = cc;
    vp->sample_increment        = incr;
    vp->sample_offset           = ofs; // Update offset

    return ksr->resample_buffer;
}

sample_t *resample_voice(Kasaria *ksr, int v, long *countptr)
{
    long   ofs;
    u_char modes;
    Voice *vp = &ksr->voice[v];

    if(!(vp->sample->sample_rate))
    {
        // Pre-resampled data -- just update the offset and check if we're out of data.
        ofs = vp->sample_offset >> FRACTION_BITS; // Kind of silly to use FRACTION_BITS here...

        if(*countptr >= (vp->sample->data_length >> FRACTION_BITS) - ofs)
        {
            // Note finished. Free the voice.
            vp->status = VOICE_FREE;

            // Let the caller know how much data we had left
            *countptr  = (vp->sample->data_length >> FRACTION_BITS) - ofs;
        }
        else
            vp->sample_offset += *countptr << FRACTION_BITS;

        return vp->sample->data + ofs;
    }

    // Need to resample. Use the proper function.
    modes = vp->sample->modes;

    if(vp->vibrato_control_ratio)
    {
        if((modes & MODES_LOOPING) && ((modes & MODES_ENVELOPE) || (vp->status == VOICE_ON || vp->status == VOICE_SUSTAINED)))
        {
            if(modes & MODES_PINGPONG)
                return rs_vib_bidir(ksr, vp, *countptr);
            else
                return rs_vib_loop(ksr, vp, *countptr);
        }
        else
            return rs_vib_plain(ksr, v, countptr);
    }
    else
    {
        if((modes & MODES_LOOPING) && ((modes & MODES_ENVELOPE) || (vp->status == VOICE_ON || vp->status == VOICE_SUSTAINED)))
        {
            if(modes & MODES_PINGPONG)
                return rs_bidir(ksr, vp, *countptr);
            else
                return rs_loop(ksr, vp, *countptr);
        }
        else
            return rs_plain(ksr, v, countptr);
    }
}

static sample_t *rs_plain_right(Kasaria *ksr, int v, long *countptr)
{
    INTERPVARS;
    Voice    *vp    = &ksr->voice[v];
    sample_t *dest  = ksr->resample_buffer_right;
    sample_t *src   = vp->sample_right->data;
    long      ofs   = vp->sample_offset_right;
    long      incr  = vp->sample_increment_right;
    long      le    = vp->sample_right->data_length;
    long      count = *countptr;

    if(!vp->sample_right || !src)
    {
        memset(dest, 0, count * sizeof(sample_t));
        *countptr = count;
        return dest;
    }

    if(ofs < 0 || ofs >= le)
    {
        memset(dest, 0, count * sizeof(sample_t));
        *countptr = count;
        return dest;
    }

#ifdef PRECALC_LOOPS
    long i;

    if(incr < 0)
        incr = -incr;

    if(incr == 0)
    {
        memset(dest, 0, count * sizeof(sample_t));
        *countptr = count;
        return dest;
    }

    i = (le - ofs) / incr + 1;

    if(i > count)
    {
        i     = count;
        count = 0;
    }
    else
        count -= i;

#if KSR_RESAMPLE_SIMD
    while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, le))
    {
        rs_block8(ofs, incr, src, dest);
        ofs  += (long)RESAMPLE_BLOCK * incr;
        dest += RESAMPLE_BLOCK;
        i    -= RESAMPLE_BLOCK;
    }
#endif

    while(i--)
    {
        RESAMPLATION;
        ofs += incr;
    }

    if(ofs >= le)
    {
        FINALINTERP;
        *countptr  -= count + 1;
    }

#else
#if KSR_RESAMPLE_SIMD
    while(count >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, le))
    {
        rs_block8(ofs, incr, src, dest);
        dest  += RESAMPLE_BLOCK;
        ofs   += (long)RESAMPLE_BLOCK * incr;
        count -= RESAMPLE_BLOCK;
    }
#endif

    while(count--)
    {
        RESAMPLATION;
        ofs += incr;
        if(ofs >= le)
        {
            FINALINTERP;
            *countptr  -= count + 1;
            break;
        }
    }
#endif

    vp->sample_offset_right = ofs;
    return ksr->resample_buffer_right;
}

static sample_t *rs_loop_right(Kasaria *ksr, Voice *vp, long count)
{
    INTERPVARS;
    long      ofs  = vp->sample_offset_right;
    long      incr = vp->sample_increment_right;
    long      ls   = vp->sample_right->loop_start;
    long      le   = vp->sample_right->loop_end;
    long      ll   = le - ls;
    sample_t *dest = ksr->resample_buffer_right;
    sample_t *src  = vp->sample_right->data;

    if(!vp->sample_right || !src)
    {
        memset(dest, 0, count * sizeof(sample_t));
        return dest;
    }

    if(incr == 0)
    {
        memset(dest, 0, count * sizeof(sample_t));
        return dest;
    }

    if(ll > 0)
    {
        if(ofs >= le)
            ofs = ls + ((ofs - le) % ll);

        if(incr >= ll)
        {
            incr %= ll;
            if(incr == 0)
                incr = ll;
        }
    }
    else if(ofs >= le)
    {
        ofs = ls;
    }

#ifdef PRECALC_LOOPS
    long i;

    while(count)
    {
        if(ofs >= le)
        {
            if(ll > 0)
                ofs -= ll;
            else
                ofs = ls;
        }

        i = (le - ofs) / incr + 1;

        if(i > count)
        {
            i     = count;
            count = 0;
        }
        else
            count -= i;

#if KSR_RESAMPLE_SIMD
        while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, vp->sample_right->data_length))
        {
            rs_block8(ofs, incr, src, dest);
            ofs  += (long)RESAMPLE_BLOCK * incr;
            dest += RESAMPLE_BLOCK;
            i    -= RESAMPLE_BLOCK;
        }
#endif

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
    }
#else
#if KSR_RESAMPLE_SIMD
    while(count >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, vp->sample_right->data_length))
    {
        rs_block8(ofs, incr, src, dest);
        dest  += RESAMPLE_BLOCK;
        ofs   += (long)RESAMPLE_BLOCK * incr;
        count -= RESAMPLE_BLOCK;
    }
#endif

    while(count--)
    {
        RESAMPLATION;
        ofs += incr;
        if(ofs >= le)
        {
            if(ll > 0)
                ofs -= ll;
            else
                ofs = ls;
        }
    }
#endif

    vp->sample_offset_right = ofs;
    vp->sample_increment_right = incr;
    return ksr->resample_buffer_right;
}

static sample_t *rs_bidir_right(Kasaria *ksr, Voice *vp, long count)
{
    INTERPVARS;
    long      ofs  = vp->sample_offset_right;
    long      incr = vp->sample_increment_right;
    long      le   = vp->sample_right->loop_end;
    long      ls   = vp->sample_right->loop_start;
    sample_t *dest = ksr->resample_buffer_right, *src = vp->sample_right->data;

    if(!vp->sample_right || !src)
    {
        memset(dest, 0, count * sizeof(sample_t));
        return dest;
    }

#ifdef PRECALC_LOOPS
    long le2 = le << 1, ls2 = ls << 1, i;

    if(ofs <= ls)
    {
        i = (ls - ofs) / incr + 1;
        if(i > count)
        {
            i     = count;
            count = 0;
        }
        else
            count -= i;

#if KSR_RESAMPLE_SIMD
        if(incr > 0)
            while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, ls, vp->sample_right->data_length))
            {
                rs_block8(ofs, incr, src, dest);
                ofs  += (long)RESAMPLE_BLOCK * incr;
                dest += RESAMPLE_BLOCK;
                i    -= RESAMPLE_BLOCK;
            }
#endif
        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
    }

    while(count)
    {
        i = ((incr > 0 ? le : ls) - ofs) / incr + 1;
        if(i > count)
        {
            i     = count;
            count = 0;
        }
        else
            count -= i;

#if KSR_RESAMPLE_SIMD
        if(incr > 0)
        {
            while(i >= RESAMPLE_BLOCK && rs_block_ok(ofs, incr, le, vp->sample_right->data_length))
            {
                rs_block8(ofs, incr, src, dest);
                ofs  += (long)RESAMPLE_BLOCK * incr;
                dest += RESAMPLE_BLOCK;
                i    -= RESAMPLE_BLOCK;
            }
        }
#endif

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(ofs >= le)
        {
            ofs   = le2 - ofs;
            incr *= -1;
        }
        else if(ofs <= ls)
        {
            ofs   = ls2 - ofs;
            incr *= -1;
        }
    }
#else
    if(ofs < ls)
    {
        while(count--)
        {
            RESAMPLATION;
            ofs += incr;
            if(ofs >= ls)
                break;
        }
    }

    if(count > 0)
        while(count--)
        {
            RESAMPLATION;
            ofs += incr;
            if(ofs >= le)
            {
                ofs  = le - (ofs - le);
                incr = -incr;
            }
            else if(ofs <= ls)
            {
                ofs  = ls + (ls - ofs);
                incr = -incr;
            }
        }
#endif
    vp->sample_increment_right = incr;
    vp->sample_offset_right    = ofs;
    return ksr->resample_buffer_right;
}

static sample_t *rs_vib_plain_right(Kasaria *ksr, int v, long *countptr)
{
    INTERPVARS;
    Voice    *vp    = &ksr->voice[v];
    sample_t *dest  = ksr->resample_buffer_right;
    sample_t *src   = vp->sample_right->data;
    long      le    = vp->sample_right->data_length;
    long      ofs   = vp->sample_offset_right;
    long      incr  = vp->sample_increment_right;
    long      count = *countptr;
    int       cc    = vp->vibrato_control_counter;

    if(!vp->sample_right || !src)
    {
        memset(dest, 0, count * sizeof(sample_t));
        *countptr = count;
        return dest;
    }

    if(incr < 0)
        incr = -incr;

    while(count--)
    {
        if(!cc--)
        {
            cc   = vp->vibrato_control_ratio;
            incr = update_vibrato(ksr, vp, 0);
        }
        RESAMPLATION;
        ofs += incr;
        if(ofs >= le)
        {
            FINALINTERP;
            *countptr  -= count + 1;
            break;
        }
    }

    vp->vibrato_control_counter = cc;
    vp->sample_increment_right  = incr;
    vp->sample_offset_right     = ofs;
    return ksr->resample_buffer_right;
}

static sample_t *rs_vib_loop_right(Kasaria *ksr, Voice *vp, long count)
{
    INTERPVARS;
    long      ofs  = vp->sample_offset_right;
    long      incr = vp->sample_increment_right;
    long      le   = vp->sample_right->loop_end;
    long      ll   = le - vp->sample_right->loop_start;
    sample_t *dest = ksr->resample_buffer_right;
    sample_t *src  = vp->sample_right->data;
    int       cc   = vp->vibrato_control_counter;

    if(!vp->sample_right || !src)
    {
        memset(dest, 0, count * sizeof(sample_t));
        return dest;
    }

#ifdef PRECALC_LOOPS
    long i;
    int  vibflag = 0;

    while(count)
    {
        while(ofs >= le)
            ofs -= ll;

        i = (le - ofs) / incr + 1;

        if(i > count)
            i = count;

        if(i > cc)
        {
            i       = cc;
            vibflag = 1;
        }
        else
            cc -= i;

        count -= i;

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(vibflag)
        {
            cc      = vp->vibrato_control_ratio;
            incr    = update_vibrato(ksr, vp, 0);
            vibflag = 0;
        }
    }
#else
    while(count--)
    {
        if(!cc--)
        {
            cc   = vp->vibrato_control_ratio;
            incr = update_vibrato(ksr, vp, 0);
        }
        RESAMPLATION;
        ofs += incr;

        if(ofs >= le)
            ofs -= ll;
    }
#endif

    vp->vibrato_control_counter = cc;
    vp->sample_increment_right  = incr;
    vp->sample_offset_right     = ofs;
    return ksr->resample_buffer_right;
}

static sample_t *rs_vib_bidir_right(Kasaria *ksr, Voice *vp, long count)
{
    INTERPVARS;
    long      ofs  = vp->sample_offset_right;
    long      incr = vp->sample_increment_right;
    long      le   = vp->sample_right->loop_end;
    long      ls   = vp->sample_right->loop_start;
    sample_t *dest = ksr->resample_buffer_right;
    sample_t *src  = vp->sample_right->data;
    int       cc   = vp->vibrato_control_counter;

    if(!vp->sample_right || !src)
    {
        memset(dest, 0, count * sizeof(sample_t));
        return dest;
    }

#ifdef PRECALC_LOOPS
    long le2 = le << 1;
    long ls2 = ls << 1;
    long i;
    int  vibflag = 0;

    while(count && (ofs <= ls))
    {
        i = (ls - ofs) / incr + 1;

        if(i > count)
            i = count;

        if(i > cc)
        {
            i       = cc;
            vibflag = 1;
        }
        else
            cc -= i;

        count -= i;

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(vibflag)
        {
            cc      = vp->vibrato_control_ratio;
            incr    = update_vibrato(ksr, vp, 0);
            vibflag = 0;
        }
    }

    while(count)
    {
        i = ((incr > 0 ? le : ls) - ofs) / incr + 1;
        if(i > count)
            i = count;

        if(i > cc)
        {
            i       = cc;
            vibflag = 1;
        }
        else
            cc -= i;

        count -= i;

        while(i--)
        {
            RESAMPLATION;
            ofs += incr;
        }
        if(vibflag)
        {
            cc      = vp->vibrato_control_ratio;
            incr    = update_vibrato(ksr, vp, (incr < 0));
            vibflag = 0;
        }
        if(ofs >= le)
        {
            ofs   = le2 - ofs;
            incr *= -1;
        }
        else if(ofs <= ls)
        {
            ofs   = ls2 - ofs;
            incr *= -1;
        }
    }
#else
    if(ofs < ls)
    {
        while(count--)
        {
            if(!cc--)
            {
                cc   = vp->vibrato_control_ratio;
                incr = update_vibrato(ksr, vp, 0);
            }
            RESAMPLATION;
            ofs += incr;
            if(ofs >= ls)
                break;
        }
    }

    if(count > 0)
        while(count--)
        {
            if(!cc--)
            {
                cc   = vp->vibrato_control_ratio;
                incr = update_vibrato(ksr, vp, (incr < 0));
            }
            RESAMPLATION;
            ofs += incr;
            if(ofs >= le)
            {
                ofs  = le - (ofs - le);
                incr = -incr;
            }
            else if(ofs <= ls)
            {
                ofs  = ls + (ls - ofs);
                incr = -incr;
            }
        }
#endif

    vp->vibrato_control_counter = cc;
    vp->sample_increment_right  = incr;
    vp->sample_offset_right     = ofs;
    return ksr->resample_buffer_right;
}

sample_t *resample_voice_right(Kasaria *ksr, int v, long *countptr)
{
    long   ofs;
    u_char modes;
    Voice *vp = &ksr->voice[v];

    if(!vp->sample_right)
    {
        memset(ksr->resample_buffer_right, 0, *countptr * sizeof(sample_t));
        return ksr->resample_buffer_right;
    }

    if(!(vp->sample_right->sample_rate))
    {
        ofs = vp->sample_offset_right >> FRACTION_BITS;

        if(*countptr >= (vp->sample_right->data_length >> FRACTION_BITS) - ofs)
        {
            *countptr  = (vp->sample_right->data_length >> FRACTION_BITS) - ofs;
            if(*countptr < 0)
                *countptr = 0;
        }
        else
            vp->sample_offset_right += *countptr << FRACTION_BITS;

        return vp->sample_right->data + ofs;
    }

    modes = vp->sample_right->modes;

    if(vp->vibrato_control_ratio)
    {
        if((modes & MODES_LOOPING) && ((modes & MODES_ENVELOPE) || (vp->status == VOICE_ON || vp->status == VOICE_SUSTAINED)))
        {
            if(modes & MODES_PINGPONG)
                return rs_vib_bidir_right(ksr, vp, *countptr);
            else
                return rs_vib_loop_right(ksr, vp, *countptr);
        }
        else
            return rs_vib_plain_right(ksr, v, countptr);
    }
    else
    {
        if((modes & MODES_LOOPING) && ((modes & MODES_ENVELOPE) || (vp->status == VOICE_ON || vp->status == VOICE_SUSTAINED)))
        {
            if(modes & MODES_PINGPONG)
                return rs_bidir_right(ksr, vp, *countptr);
            else
                return rs_loop_right(ksr, vp, *countptr);
        }
        else
            return rs_plain_right(ksr, v, countptr);
    }
}

void pre_resample(Kasaria *ksr, Sample *sp)
{
    f64               a;
    f64               xdiff;
    long              incr;
    long              ofs;
    long              newlen;
    long              count;
    long              v;
    long              v1;
    long              v2;
    long              v3;
    long              v4;
    long              i;
    short            *newdata;
    short            *dest;
    short            *src = (short *)sp->data;
    short            *vptr;

    static const char note_name[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    a                                  = ((f64)(sp->sample_rate) * freq_table[(int)(sp->note_to_use)]) / ((f64)(sp->root_freq) * ksr->play_mode.rate);

    if(sp->data_length / a >= 0x7fffffffL)
    {
        // Too large to compute
        return;
    }
    newlen = (long)(sp->data_length / a);
    dest = newdata = (short *)safe_malloc((newlen >> (FRACTION_BITS - 1)) + 2);

    count          = (newlen >> FRACTION_BITS) - 1;
    ofs = incr = (sp->data_length - (1 << FRACTION_BITS)) / count;

    if(--count)
        *dest++ = src[0];

    /*
        Since we're pre-processing and this doesn't have to be done in
        real-time, we go ahead and do the full sliding cubic interpolation.
    */
    count--;
    for(i = 0; i < count; i++)
    {
        vptr  = src + (ofs >> FRACTION_BITS);
        v1    = *(vptr - 1);
        v2    = *vptr;
        v3    = *(vptr + 1);
        v4    = *(vptr + 2);
        xdiff = FSCALENEG(ofs & FRACTION_MASK, FRACTION_BITS);
        v     = (long)(v2 + (xdiff / 6.0) * (-2 * v1 - 3 * v2 + 6 * v3 - v4 + xdiff * (3 * (v1 - 2 * v2 + v3) + xdiff * (-v1 + 3 * (v2 - v3) + v4))));

        if(v < -32768)
            *dest++ = -32768;
        else if(v > 32767)
            *dest++ = 32767;
        else
            *dest++ = (short)v;
        ofs += incr;
    }

    if(ofs & FRACTION_MASK)
    {
        v1      = src[ofs >> FRACTION_BITS];
        v2      = src[(ofs >> FRACTION_BITS) + 1];
        *dest++ = (short)(v1 + (((v2 - v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS));
    }
    else
        *dest++ = src[ofs >> FRACTION_BITS];

    *dest           = *(dest - 1) / 2;
    *dest           = *(dest - 1) / 2;

    sp->data_length = newlen;
    sp->loop_start  = (long)(sp->loop_start / a);
    sp->loop_end    = (long)(sp->loop_end / a);

    // Clamp the lop start and loop end of each sample
    if(sp->loop_end > newlen)
        sp->loop_end = newlen;
    if(sp->loop_start >= sp->loop_end)
        sp->loop_start = sp->loop_end - 1;

    free(sp->data);

    sp->data        = (sample_t *)newdata;
    sp->sample_rate = 0;
}