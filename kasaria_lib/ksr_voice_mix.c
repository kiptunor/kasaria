/*

TiMidity -- Experimental MIDI to WAVE converter
Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>
Copyright (C) 2026 Kiptunor

Suddenly, you realize that this program is free software; you get
an overwhelming urge to redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received another copy of the GNU General Public
License along with this program; if not, write to the Free
Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
I bet they'll be amazed.

mix.c */

#include <malloc.h>
#include <math.h>
#include <stdio.h>

#include "ksr_internal.h"

// Returns 1 if envelope runs out
int recompute_envelope(Kasaria *ksr, int v)
{
    int stage;

    stage = ksr->voice[v].envelope_stage;

    if(stage > 5)
    {
        // Envelope ran out.
        int tmp              = (ksr->voice[v].status == VOICE_DIE); /* Already displayed as dead */
        ksr->voice[v].status = VOICE_FREE;
        if(!tmp)
            return 1;
    }

    if(ksr->voice[v].sample->modes & MODES_ENVELOPE)
    {
        if(ksr->voice[v].status == VOICE_ON || ksr->voice[v].status == VOICE_SUSTAINED)
        {
            if(stage > 2)
            {
                // Freeze envelope until note turns off. Trumpets want this.
                ksr->voice[v].envelope_increment = 0;
                return 0;
            }
        }
    }
    ksr->voice[v].envelope_stage = stage + 1;
    if(ksr->voice[v].envelope_volume == ksr->voice[v].sample->envelope_offset[stage] || (stage > 2 && ksr->voice[v].envelope_volume < ksr->voice[v].sample->envelope_offset[stage]))
        return recompute_envelope(ksr, v);

    ksr->voice[v].envelope_target    = ksr->voice[v].sample->envelope_offset[stage];
    ksr->voice[v].envelope_increment = ksr->voice[v].sample->envelope_rate[stage];
    if(ksr->voice[v].envelope_target < ksr->voice[v].envelope_volume)
        ksr->voice[v].envelope_increment = -ksr->voice[v].envelope_increment;

    return 0;
}

void apply_envelope_to_amp(Kasaria *ksr, int v)
{
    f64  lamp = ksr->voice[v].left_amp, ramp;
    long la, ra;
    if(ksr->voice[v].panned == PANNED_MYSTERY)
    {
        ramp = ksr->voice[v].right_amp;
        if(ksr->voice[v].tremolo_phase_increment)
        {
            lamp *= ksr->voice[v].tremolo_volume;
            ramp *= ksr->voice[v].tremolo_volume;
        }
        if(ksr->voice[v].sample->modes & MODES_ENVELOPE)
        {
            lamp *= vol_table[ksr->voice[v].envelope_volume >> 23];
            ramp *= vol_table[ksr->voice[v].envelope_volume >> 23];
        }

        la = FSCALE(lamp, AMP_BITS);

        if(la > MAX_AMP_VALUE)
            la = MAX_AMP_VALUE;

        ra = FSCALE(ramp, AMP_BITS);
        if(ra > MAX_AMP_VALUE)
            ra = MAX_AMP_VALUE;


        ksr->voice[v].left_mix  = FINAL_VOLUME(la);
        ksr->voice[v].right_mix = FINAL_VOLUME(ra);
    }
    else
    {
        if(ksr->voice[v].tremolo_phase_increment)
            lamp *= ksr->voice[v].tremolo_volume;
        if(ksr->voice[v].sample->modes & MODES_ENVELOPE)
            lamp *= vol_table[ksr->voice[v].envelope_volume >> 23];

        la = FSCALE(lamp, AMP_BITS);

        if(la > MAX_AMP_VALUE)
            la = MAX_AMP_VALUE;

        ksr->voice[v].left_mix = FINAL_VOLUME(la);
    }
}

static int update_envelope(Kasaria *ksr, int v)
{
    ksr->voice[v].envelope_volume += ksr->voice[v].envelope_increment;
    // Why is there no ^^ operator??
    if(((ksr->voice[v].envelope_increment < 0) && (ksr->voice[v].envelope_volume <= ksr->voice[v].envelope_target)) || ((ksr->voice[v].envelope_increment > 0) && (ksr->voice[v].envelope_volume >= ksr->voice[v].envelope_target)))
    {
        ksr->voice[v].envelope_volume = ksr->voice[v].envelope_target;
        if(recompute_envelope(ksr, v))
            return 1;
    }
    return 0;
}

static void update_tremolo(Kasaria *ksr, int v)
{
    long depth = ksr->voice[v].sample->tremolo_depth << 7;

    if(ksr->voice[v].tremolo_sweep)
    {
        // Update sweep position

        ksr->voice[v].tremolo_sweep_position += ksr->voice[v].tremolo_sweep;
        if(ksr->voice[v].tremolo_sweep_position >= (1 << SWEEP_SHIFT))
            ksr->voice[v].tremolo_sweep = 0; /* Swept to max amplitude */
        else
        {
            // Need to adjust depth
            depth  *= ksr->voice[v].tremolo_sweep_position;
            depth >>= SWEEP_SHIFT;
        }
    }

    ksr->voice[v].tremolo_phase  += ksr->voice[v].tremolo_phase_increment;

    // if(tm->voice[v].tremolo_phase >= (SINE_CYCLE_LENGTH<<RATE_SHIFT))
    //     tm->voice[v].tremolo_phase -= SINE_CYCLE_LENGTH<<RATE_SHIFT;

    ksr->voice[v].tremolo_volume  = 1.0 - FSCALENEG((sine(ksr->voice[v].tremolo_phase >> RATE_SHIFT) + 1.0) * depth * TREMOLO_AMPLITUDE_TUNING, 17);

    // I'm not sure about the +1.0 there -- it makes tremoloed voices' volumes on average the lower the higher the tremolo amplitude.
}

// Returns 1 if the note died
static int update_signal(Kasaria *ksr, int v)
{
    if(ksr->voice[v].envelope_increment && update_envelope(ksr, v))
        return 1;

    if(ksr->voice[v].tremolo_phase_increment)
        update_tremolo(ksr, v);

    apply_envelope_to_amp(ksr, v);
    return 0;
}

#ifdef LOOKUP_HACK
    #define MIXATION(a) *lp++ += ksr->mixup[(a << 8) | (uint8)s];
#else
    #define MIXATION(a) *lp++ += (a) * s;
#endif

static void mix_mystery_signal(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    Voice         *vp   = ksr->voice + v;
    final_volume_t left = vp->left_mix, right = vp->right_mix;
    int            cc;
    sample_t       s;

    if(!(cc = vp->control_counter))
    {
        cc = ksr->control_ratio;
        if(update_signal(ksr, v))
            return; // Envelope ran out

        left  = vp->left_mix;
        right = vp->right_mix;
    }

    while(count)
        if(cc < count)
        {
            count -= cc;
            while(cc--)
            {
                s = *sp++;
                MIXATION(left);
                MIXATION(right);
            }
            cc = ksr->control_ratio;
            if(update_signal(ksr, v))
                return; // Envelope ran out

            left  = vp->left_mix;
            right = vp->right_mix;
        }
        else
        {
            vp->control_counter = cc - count;
            while(count--)
            {
                s = *sp++;
                MIXATION(left);
                MIXATION(right);
            }
            return;
        }
}

static void mix_center_signal(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    Voice         *vp   = ksr->voice + v;
    final_volume_t left = vp->left_mix;
    int            cc;
    sample_t       s;

    if(!(cc = vp->control_counter))
    {
        cc = ksr->control_ratio;
        if(update_signal(ksr, v))
            return; // Envelope ran out

        left = vp->left_mix;
    }

    while(count)
        if(cc < count)
        {
            count -= cc;
            while(cc--)
            {
                s = *sp++;
                MIXATION(left);
                MIXATION(left);
            }
            cc = ksr->control_ratio;
            if(update_signal(ksr, v))
                return; // Envelope ran out

            left = vp->left_mix;
        }
        else
        {
            vp->control_counter = cc - count;
            while(count--)
            {
                s = *sp++;
                MIXATION(left);
                MIXATION(left);
            }
            return;
        }
}

static void mix_single_signal(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    Voice         *vp   = ksr->voice + v;
    final_volume_t left = vp->left_mix;
    int            cc;
    sample_t       s;

    if(!(cc = vp->control_counter))
    {
        cc = ksr->control_ratio;
        if(update_signal(ksr, v))
            return; // Envelope ran out

        left = vp->left_mix;
    }

    while(count)
        if(cc < count)
        {
            count -= cc;
            while(cc--)
            {
                s = *sp++;
                MIXATION(left);
                lp++;
            }
            cc = ksr->control_ratio;
            if(update_signal(ksr, v))
                return; // Envelope ran out

            left = vp->left_mix;
        }
        else
        {
            vp->control_counter = cc - count;
            while(count--)
            {
                s = *sp++;
                MIXATION(left);
                lp++;
            }
            return;
        }
}

static void mix_mono_signal(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    Voice         *vp   = ksr->voice + v;
    final_volume_t left = vp->left_mix;
    int            cc;
    sample_t       s;

    if(!(cc = vp->control_counter))
    {
        cc = ksr->control_ratio;
        if(update_signal(ksr, v))
            return; // Envelope ran out

        left = vp->left_mix;
    }

    while(count)
        if(cc < count)
        {
            count -= cc;
            while(cc--)
            {
                s = *sp++;
                MIXATION(left);
            }
            cc = ksr->control_ratio;
            if(update_signal(ksr, v))
                return; // Envelope ran out

            left = vp->left_mix;
        }
        else
        {
            vp->control_counter = cc - count;
            while(count--)
            {
                s = *sp++;
                MIXATION(left);
            }
            return;
        }
}

static void mix_mystery(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    final_volume_t left  = ksr->voice[v].left_mix;
    final_volume_t right = ksr->voice[v].right_mix;
    sample_t       s;

    while(count--)
    {
        s = *sp++;
        MIXATION(left);
        MIXATION(right);
    }
}

static void mix_center(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    final_volume_t left = ksr->voice[v].left_mix;
    sample_t       s;

    while(count--)
    {
        s = *sp++;
        MIXATION(left);
        MIXATION(left);
    }
}

static void mix_single(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    final_volume_t left = ksr->voice[v].left_mix;
    sample_t       s;

    while(count--)
    {
        s = *sp++;
        MIXATION(left);
        lp++;
    }
}

static void mix_mono(Kasaria *ksr, sample_t *sp, f32 *lp, int v, int count)
{
    final_volume_t left = ksr->voice[v].left_mix;
    sample_t       s;

    while(count--)
    {
        s = *sp++;
        MIXATION(left);
    }
}

// Ramp a note out in c samples
static void ramp_out(Kasaria *ksr, sample_t *sp, f32 *lp, int v, long c)
{

    // should be final_volume_t, but uint8 gives trouble.
    long     left;
    long     right;
    long     li;
    long     ri;

    sample_t s = 0; // silly warning about uninitialized s

    left       = ksr->voice[v].left_mix;
    li         = -(left / c);
    if(!li)
        li = -1;

    if(!(ksr->play_mode.encoding & PE_MONO))
    {
        if(ksr->voice[v].panned == PANNED_MYSTERY)
        {
            right = ksr->voice[v].right_mix;
            ri    = -(right / c);
            while(c--)
            {
                left += li;
                if(left < 0)
                    left = 0;

                right += ri;
                if(right < 0)
                    right = 0;

                s = *sp++;

                MIXATION(left);
                MIXATION(right);
            }
        }
        else if(ksr->voice[v].panned == PANNED_CENTER)
        {
            while(c--)
            {
                left += li;
                if(left < 0)
                    return;

                s = *sp++;

                MIXATION(left);
                MIXATION(left);
            }
        }
        else if(ksr->voice[v].panned == PANNED_LEFT)
        {
            while(c--)
            {
                left += li;
                if(left < 0)
                    return;
                s = *sp++;

                MIXATION(left);
                lp++;
            }
        }
        else if(ksr->voice[v].panned == PANNED_RIGHT)
        {
            while(c--)
            {
                left += li;
                if(left < 0)
                    return;

                s = *sp++;
                lp++;

                MIXATION(left);
            }
        }
    }
    else
    {
        // Mono output.
        while(c--)
        {
            left += li;
            if(left < 0)
                return;

            s = *sp++;

            MIXATION(left);
        }
    }
}


/**************** interface function ******************/

void mix_voice(Kasaria *ksr, f32 *buf, int v, long c)
{
    // Prob this is where I have to handle SF2 effects
    Voice    *vp = ksr->voice + v;
    sample_t *sp;
    if(vp->status == VOICE_DIE)
    {
        if(c >= MAX_DIE_TIME)
            c = MAX_DIE_TIME;

        sp = resample_voice(ksr, v, &c);

        if(c > 0)
            ramp_out(ksr, sp, buf, v, c);
        vp->status = VOICE_FREE;
    }
    else
    {
        sp = resample_voice(ksr, v, &c);

        if(ksr->play_mode.encoding & PE_MONO)
        {
            /* Mono output. */
            if(vp->envelope_increment || vp->tremolo_phase_increment)
                mix_mono_signal(ksr, sp, buf, v, c);
            else
                mix_mono(ksr, sp, buf, v, c);
        }
        else
        {
            if(vp->panned == PANNED_MYSTERY)
            {
                if(vp->envelope_increment || vp->tremolo_phase_increment)
                    mix_mystery_signal(ksr, sp, buf, v, c);
                else
                    mix_mystery(ksr, sp, buf, v, c);
            }
            else if(vp->panned == PANNED_CENTER)
            {
                if(vp->envelope_increment || vp->tremolo_phase_increment)
                    mix_center_signal(ksr, sp, buf, v, c);
                else
                    mix_center(ksr, sp, buf, v, c);
            }
            else
            {
                /* It's either full left or full right. In either case,
                every other sample is 0. Just get the offset right: */
                if(vp->panned == PANNED_RIGHT)
                    buf++;

                if(vp->envelope_increment || vp->tremolo_phase_increment)
                    mix_single_signal(ksr, sp, buf, v, c);
                else
                    mix_single(ksr, sp, buf, v, c);
            }
        }
    }
}