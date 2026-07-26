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

filter.c: written by Vincent Pagel ( pagel@loria.fr )

implements fir antialiasing filter : should help when setting sample
rates as low as 8Khz.

April 95
- first draft

22/5/95
- modify "filter" so that it simulate leading and trailing 0 in the buffer
*/














#include "ksr_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// bessel  function
static f64 ino(f32 x)
{
    f64 y, de, e, sde;
    int i;

    y  = x / 2;
    e  = 1.0;
    de = 1.0;
    i  = 1;
    do
    {
        de   = de * y / (f64)i;
        sde  = de * de;
        e   += sde;
    } while(!((e * 1.0e-08 - sde > 0) || (i++ > 25)));

    return (e);
}

// Kaiser Window (symetric)
static void kaiser(f64 *w, int n, f64 beta)
{
    f64 xind;
    f64 xi;
    int i;

    xind = (2 * n - 1) * (2 * n - 1);
    for(i = 0; i < n; i++)
    {
        xi   = i + 0.5;
        w[i] = ino((f64)(beta * sqrt((f64)(1. - 4 * xi * xi / xind)))) / ino((f64)beta);
    }
}

/*
 * fir coef in g, cuttoff frequency in fc
 */
static void designfir(f64 *g, f64 fc)
{
    int i;
    f64 xi;
    f64 omega;
    f64 att;
    f64 beta;
    f64 w[ORDER2];

    for(i = 0; i < ORDER2; i++)
    {
        xi    = (f64)i + 0.5;
        omega = PI * xi;
        g[i]  = sin((f64)omega * fc) / omega;
    }

    att  = 40.; /* attenuation  in  db */
    beta = (f64)exp(log((f64)0.58417 * (att - 20.96)) * 0.4) + 0.07886 * (att - 20.96);
    kaiser(w, ORDER2, beta);

    // Matrix product
    for(i = 0; i < ORDER2; i++)
        g[i] = g[i] * w[i];
}

/*
 * FIR filtering -> apply the filter given by coef[] to the data buffer
 * Note that we simulate leading and trailing 0 at the border of the
 * data buffer
 */

// This is quick hack for antialiasing filter's bug fix.
#define sample_t short

static void filter(sample_t *result, sample_t *data, long length, f64 coef[])
{
    long  sample, i, sample_window;
    short peak = 0;
    f64   sum;

    // Simulate leading 0 at the begining of the buffer
    for(sample = 0; sample < ORDER2; sample++)
    {
        sum           = 0.0;
        sample_window = sample - ORDER2;

        for(i = 0; i < ORDER; i++)
            sum += coef[i] * ((sample_window < 0) ? 0.0 : data[sample_window++]);

        // Saturation ???
        if(sum > 32767.)
        {
            sum = 32767.;
            peak++;
        }

        if(sum < -32768.)
        {
            sum = -32768;
            peak++;
        }
        result[sample] = (sample_t)sum;
    }

    // The core of the buffer
    for(sample = ORDER2; sample < length - ORDER + ORDER2; sample++)
    {
        sum           = 0.0;
        sample_window = sample - ORDER2;

        for(i = 0; i < ORDER; i++)
            sum += data[sample_window++] * coef[i];

        // Saturation ???
        if(sum > 32767.)
        {
            sum = 32767.;
            peak++;
        }
        if(sum < -32768.)
        {
            sum = -32768;
            peak++;
        }
        result[sample] = (sample_t)sum;
    }

    // Simulate 0 at the end of the buffer
    for(sample = length - ORDER + ORDER2; sample < length; sample++)
    {
        sum           = 0.0;
        sample_window = sample - ORDER2;

        for(i = 0; i < ORDER; i++)
            sum += coef[i] * ((sample_window >= length) ? 0.0 : data[sample_window++]);

        // Saturation ???
        if(sum > 32767.)
        {
            sum = 32767.;
            peak++;
        }
        if(sum < -32768.)
        {
            sum = -32768;
            peak++;
        }
        result[sample] = (sample_t)sum;
    }
}

/***********************************************************************/
/* Prevent aliasing by filtering any freq above the output_rate        */
/*                                                                     */
/* I don't worry about looping point -> they will remain soft if they  */
/* were already                                                        */
/***********************************************************************/
void antialiasing(Sample *sp, long output_rate)
{
    sample_t *temp;
    int       i;
    f64       fir_symetric[ORDER];
    f64       fir_coef[ORDER2];
    f64       freq_cut; // cutoff frequency [0..1.0] FREQ_CUT/SAMP_FREQ

    // No oversampling
    if(output_rate >= sp->sample_rate)
        return;

    freq_cut = (f64)output_rate / (f64)sp->sample_rate;

    designfir(fir_coef, freq_cut);

    // Make the filter symetric
    for(i = 0; i < ORDER2; i++)
        fir_symetric[ORDER - 1 - i] = fir_symetric[i] = fir_coef[ORDER2 - 1 - i];

    // We apply the filter we have designed on a copy of the patch
    temp = (sample_t *)safe_malloc(sp->data_length);
    memcpy(temp, sp->data, sp->data_length);

    filter((short *)sp->data, temp, sp->data_length / sizeof(sample_t), fir_symetric);

    free(temp);
}

void audio_compressor(CompressorSettings *compr_settings, void *buffer, u32 length)
{
    f32 *samples = (f32 *)buffer;
    u32  count   = length / sizeof(f32);

    for(u32 i = 0; i < count; ++i)
    {
        f32 input                = samples[i];
        f32 abs_input            = fabsf(input);

        // Envelope detection
        f32 coeff                = (abs_input > compr_settings->envelope) ? compr_settings->attack_coeff : compr_settings->release_coeff;
        compr_settings->envelope = coeff * compr_settings->envelope + (1.0f - coeff) * abs_input;

        // Gain computation
        f32 target_gain          = 1.0f;
        if(compr_settings->envelope > compr_settings->threshold)
            target_gain = compr_settings->threshold / compr_settings->envelope;

        // Always smooth gain
        compr_settings->gain = 0.001f * compr_settings->gain + 0.999f * target_gain;

        samples[i]           = input * compr_settings->gain;
    }
}