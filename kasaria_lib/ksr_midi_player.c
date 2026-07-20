/*

TiMidity -- Experimental MIDI to WAVE converter
Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>

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

playmidi.c -- random stuff in need of rearrangement

*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>


#ifndef _WIN32_WCE
    #include <string.h>
#endif

#include "ksr_internal.h"
#include "ksr_sf2.h"

static void adjust_amplification(Kasaria *ksr, int amplification)
{
    ksr->master_volume = (f64)(amplification) / 100.0L;
}

static void reset_voices(Kasaria *ksr)
{
    int i;
    for(i = 0; i < MAX_VOICES; i++)
        ksr->voice[i].status = VOICE_FREE;
}

/* Process the Reset All Controllers event */
static void reset_controllers(Kasaria *ksr, int c)
{
    ksr->channel[c].volume      = 90;  /* Some standard says, although the SCC docs say 0. */
    ksr->channel[c].expression  = 127; /* SCC-1 does this. */
    ksr->channel[c].sustain     = 0;
    ksr->channel[c].mono        = 0;
    ksr->channel[c].pitchbend   = 0x2000;
    ksr->channel[c].pitchfactor = 0; /* to be computed */
}

static void reset_midi(Kasaria *ksr)
{
    int i;
    for(i = 0; i < 16; i++)
    {
        reset_controllers(ksr, i);
        /* The rest of these are unaffected by the Reset All Controllers event */
        ksr->channel[i].program   = ksr->default_program;
        ksr->channel[i].panning   = NO_PANNING;
        ksr->channel[i].pitchsens = 2;
        ksr->channel[i].bank      = 0; /* tone bank or drum set */
        ksr->rpn_msb[i]           = 0xff;
        ksr->rpn_lsb[i]           = 0xff;
    }
    reset_voices(ksr);
    ksr->lost_notes = 0;
    ksr->cut_notes  = 0;
}

static void select_sample(Kasaria *ksr, int v, Instrument *ip)
{
    long  f, cdiff, diff;
    int     s, i;
    Sample *sp, *closest;

    s  = ip->samples;
    sp = ip->sample;

    if(s == 1)
    {
        ksr->voice[v].sample = sp;
        return;
    }

    f = ksr->voice[v].orig_frequency;
    for(i = 0; i < s; i++)
    {
        if(sp->low_freq <= f && sp->high_freq >= f)
        {
            ksr->voice[v].sample = sp;
            return;
        }
        sp++;
    }

    /*
    No suitable sample found! We'll select the sample whose root
    frequency is closest to the one we want. (Actually we should
    probably convert the low, high, and root frequencies to MIDI note
    values and compare those.) */

    cdiff   = 0x7FFFFFFF;
    closest = sp = ip->sample;
    for(i = 0; i < s; i++)
    {
        diff = sp->root_freq - f;
        if(diff < 0)
            diff = -diff;
        if(diff < cdiff)
        {
            cdiff   = diff;
            closest = sp;
        }

        sp++;
    }
    ksr->voice[v].sample = closest;
    return;
}

static void recompute_freq(Kasaria *ksr, int v)
{
    int sign = (ksr->voice[v].sample_increment < 0), /* for bidirectional loops */
        pb   = ksr->channel[ksr->voice[v].channel].pitchbend;
    f64 a;

    if(!ksr->voice[v].sample->sample_rate)
        return;

    if(ksr->voice[v].vibrato_control_ratio)
    {
        /* This instrument has vibrato. Invalidate any precomputed
        sample_increments. */

        int i = VIBRATO_SAMPLE_INCREMENTS;
        while(i--)
            ksr->voice[v].vibrato_sample_increment[i] = 0;
    }

    if(pb == 0x2000 || pb < 0 || pb > 0x3FFF)
        ksr->voice[v].frequency = ksr->voice[v].orig_frequency;
    else
    {
        pb -= 0x2000;
        if(!(ksr->channel[ksr->voice[v].channel].pitchfactor))
        {
            /* Damn. Somebody bent the pitch. */
            long i = pb * ksr->channel[ksr->voice[v].channel].pitchsens;
            if(pb < 0)
                i = -i;

            ksr->channel[ksr->voice[v].channel].pitchfactor = bend_fine[(i >> 5) & 0xFF] * bend_coarse[i >> 13];
        }

        if(pb > 0)
            ksr->voice[v].frequency = (long)(ksr->channel[ksr->voice[v].channel].pitchfactor * (f64)(ksr->voice[v].orig_frequency));
        else
            ksr->voice[v].frequency = (long)((f64)(ksr->voice[v].orig_frequency) / ksr->channel[ksr->voice[v].channel].pitchfactor);
    }

    a = FSCALE(((f64)(ksr->voice[v].sample->sample_rate) * (f64)(ksr->voice[v].frequency)) / ((f64)(ksr->voice[v].sample->root_freq) * (f64)(ksr->play_mode.rate)), FRACTION_BITS);

    if(sign)
        a = -a; /* need to preserve the loop direction */

    ksr->voice[v].sample_increment = (long)(a);
}

static void recompute_amp(Kasaria *ksr, int v)
{
    long tempamp;

    /* TODO: use fscale */

    tempamp = (ksr->voice[v].velocity * ksr->channel[ksr->voice[v].channel].volume * ksr->channel[ksr->voice[v].channel].expression); /* 21 bits */

    if(!(ksr->play_mode.encoding & PE_MONO))
    {
        if(ksr->voice[v].panning > 60 && ksr->voice[v].panning < 68)
        {
            ksr->voice[v].panned   = PANNED_CENTER;

            ksr->voice[v].left_amp = FSCALENEG((f64)(tempamp)*ksr->voice[v].sample->volume * ksr->master_volume, 21);
        }
        else if(ksr->voice[v].panning < 5)
        {
            ksr->voice[v].panned   = PANNED_LEFT;

            ksr->voice[v].left_amp = FSCALENEG((f64)(tempamp)*ksr->voice[v].sample->volume * ksr->master_volume, 20);
        }
        else if(ksr->voice[v].panning > 123)
        {
            ksr->voice[v].panned   = PANNED_RIGHT;

            /* left_amp will be used */
            ksr->voice[v].left_amp = FSCALENEG((f64)(tempamp)*ksr->voice[v].sample->volume * ksr->master_volume, 20);
        }
        else
        {
            ksr->voice[v].panned     = PANNED_MYSTERY;

            ksr->voice[v].left_amp   = FSCALENEG((f64)(tempamp)*ksr->voice[v].sample->volume * ksr->master_volume, 27);
            ksr->voice[v].right_amp  = ksr->voice[v].left_amp * (ksr->voice[v].panning);
            ksr->voice[v].left_amp  *= (f64)(127 - ksr->voice[v].panning);
        }
    }
    else
    {
        ksr->voice[v].panned   = PANNED_CENTER;

        ksr->voice[v].left_amp = FSCALENEG((f64)(tempamp)*ksr->voice[v].sample->volume * ksr->master_volume, 21);
    }
}

static void start_note(Kasaria *ksr, MidiEvent *e, int i)
{
    Instrument *ip;
    int         j;

    if(ISDRUMCHANNEL(ksr, e->channel))
    {
        if(!ksr->drumset[ksr->channel[e->channel].bank] && !ksr->drumset[0])
            return; /* No drumset? Then we can't play. */

        if(ksr->drumset[ksr->channel[e->channel].bank]->tone[e->a].name && !ksr->drumset[ksr->channel[e->channel].bank]->tone[e->a].instrument)
        {
            ksr->drumset[ksr->channel[e->channel].bank]->tone[e->a].instrument = MAGIC_LOAD_INSTRUMENT;
            load_missing_instruments(ksr);
        }

        if(ksr->drumset[0]->tone[e->a].name && !ksr->drumset[0]->tone[e->a].instrument)
        {
            ksr->drumset[0]->tone[e->a].instrument = MAGIC_LOAD_INSTRUMENT;
            load_missing_instruments(ksr);
        }

        if(!(ip = ksr->drumset[ksr->channel[e->channel].bank]->tone[e->a].instrument))
        {
            if(!(ip = ksr->drumset[0]->tone[e->a].instrument))
                return; /* No instrument? Then we can't play. */
        }

        if(ip->sample->note_to_use) /* Do we have a fixed pitch? */
            ksr->voice[i].orig_frequency = freq_table[(int)(ip->sample->note_to_use)];
        else
            ksr->voice[i].orig_frequency = freq_table[e->a & 0x7F];

        /* drums are supposed to have only one sample */
        ksr->voice[i].sample = ip->sample;
    }
    else
    {
        if(!ksr->tonebank[ksr->channel[e->channel].bank] && !ksr->tonebank[0] && ksr->channel[e->channel].program != SPECIAL_PROGRAM)
            return; /* No tonebank? Then we can't play. */

        if(ksr->channel[e->channel].program != SPECIAL_PROGRAM)
        {
            if(ksr->tonebank[ksr->channel[e->channel].bank]->tone[ksr->channel[e->channel].program].name && !ksr->tonebank[ksr->channel[e->channel].bank]->tone[ksr->channel[e->channel].program].instrument)
            {
                ksr->tonebank[ksr->channel[e->channel].bank]->tone[ksr->channel[e->channel].program].instrument = MAGIC_LOAD_INSTRUMENT;
                load_missing_instruments(ksr);
            }
            if(ksr->tonebank[0]->tone[ksr->channel[e->channel].program].name && !ksr->tonebank[0]->tone[ksr->channel[e->channel].program].instrument)
            {
                ksr->tonebank[0]->tone[ksr->channel[e->channel].program].instrument = MAGIC_LOAD_INSTRUMENT;
                load_missing_instruments(ksr);
            }
        }
        if(ksr->channel[e->channel].program == SPECIAL_PROGRAM)
            ip = ksr->default_instrument;
        else if(!(ip = ksr->tonebank[ksr->channel[e->channel].bank]->tone[ksr->channel[e->channel].program].instrument))
        {
            if(!(ip = ksr->tonebank[0]->tone[ksr->channel[e->channel].program].instrument))
                return; /* No instrument? Then we can't play. */
        }

        if(!ip)
            return;

        if(ip->sample->note_to_use) /* Fixed-pitch instrument? */
            ksr->voice[i].orig_frequency = freq_table[(int)(ip->sample->note_to_use)];
        else
            ksr->voice[i].orig_frequency = freq_table[e->a & 0x7F];
        select_sample(ksr, i, ip);
    }

    ksr->voice[i].status                  = VOICE_ON;
    ksr->voice[i].channel                 = e->channel;
    ksr->voice[i].note                    = e->a;
    ksr->voice[i].velocity                = e->b;
    ksr->voice[i].sample_offset           = 0;
    ksr->voice[i].sample_increment        = 0; /* make sure it isn't negative */

    ksr->voice[i].tremolo_phase           = 0;
    ksr->voice[i].tremolo_phase_increment = ksr->voice[i].sample->tremolo_phase_increment;
    ksr->voice[i].tremolo_sweep           = ksr->voice[i].sample->tremolo_sweep_increment;
    ksr->voice[i].tremolo_sweep_position  = 0;

    ksr->voice[i].vibrato_sweep           = ksr->voice[i].sample->vibrato_sweep_increment;
    ksr->voice[i].vibrato_sweep_position  = 0;
    ksr->voice[i].vibrato_control_ratio   = ksr->voice[i].sample->vibrato_control_ratio;
    ksr->voice[i].vibrato_control_counter = ksr->voice[i].vibrato_phase = 0;
    for(j = 0; j < VIBRATO_SAMPLE_INCREMENTS; j++)
        ksr->voice[i].vibrato_sample_increment[j] = 0;

    if(ksr->channel[e->channel].panning != NO_PANNING)
        ksr->voice[i].panning = ksr->channel[e->channel].panning;
    else
        ksr->voice[i].panning = ksr->voice[i].sample->panning;

    recompute_freq(ksr, i);
    recompute_amp(ksr, i);
    if(ksr->voice[i].sample->modes & MODES_ENVELOPE)
    {
        /* Ramp up from 0 */
        ksr->voice[i].envelope_stage  = 0;
        ksr->voice[i].envelope_volume = 0;
        ksr->voice[i].control_counter = 0;
        recompute_envelope(ksr, i);
        apply_envelope_to_amp(ksr, i);
    }
    else
    {
        ksr->voice[i].envelope_increment = 0;
        apply_envelope_to_amp(ksr, i);
    }
    /* SF2 stereo support: start partner voice for stereo samples */
    if(!ISDRUMCHANNEL(ksr, e->channel) && ip && ip->samples > 1)
    {
        Sample *primary = ksr->voice[i].sample;
        int     si;
        for(si = 0; si < ip->samples; si++)
        {
            Sample *candidate = &ip->sample[si];
            if(candidate == primary)
                continue;
            if(candidate->low_freq == primary->low_freq && candidate->high_freq == primary->high_freq && candidate->panning != primary->panning)
            {
                int stereo_v;
                for(stereo_v = 0; stereo_v < ksr->voices; stereo_v++)
                    if(ksr->voice[stereo_v].status == VOICE_FREE)
                        break;
                if(stereo_v < ksr->voices)
                {
                    ksr->voice[stereo_v].status                  = VOICE_ON;
                    ksr->voice[stereo_v].channel                 = e->channel;
                    ksr->voice[stereo_v].note                    = e->a;
                    ksr->voice[stereo_v].velocity                = e->b;
                    ksr->voice[stereo_v].sample                  = candidate;
                    ksr->voice[stereo_v].sample_offset           = 0;
                    ksr->voice[stereo_v].sample_increment        = 0;
                    ksr->voice[stereo_v].orig_frequency          = ksr->voice[i].orig_frequency;

                    ksr->voice[stereo_v].tremolo_phase           = 0;
                    ksr->voice[stereo_v].tremolo_phase_increment = candidate->tremolo_phase_increment;
                    ksr->voice[stereo_v].tremolo_sweep           = candidate->tremolo_sweep_increment;
                    ksr->voice[stereo_v].tremolo_sweep_position  = 0;

                    ksr->voice[stereo_v].vibrato_sweep           = candidate->vibrato_sweep_increment;
                    ksr->voice[stereo_v].vibrato_sweep_position  = 0;
                    ksr->voice[stereo_v].vibrato_control_ratio   = candidate->vibrato_control_ratio;
                    ksr->voice[stereo_v].vibrato_control_counter = 0;
                    ksr->voice[stereo_v].vibrato_phase           = 0;
                    for(j = 0; j < VIBRATO_SAMPLE_INCREMENTS; j++)
                        ksr->voice[stereo_v].vibrato_sample_increment[j] = 0;

                    if(ksr->channel[e->channel].panning != NO_PANNING)
                        ksr->voice[stereo_v].panning = ksr->channel[e->channel].panning;
                    else
                        ksr->voice[stereo_v].panning = candidate->panning;

                    recompute_freq(ksr, stereo_v);
                    recompute_amp(ksr, stereo_v);

                    if(candidate->modes & MODES_ENVELOPE)
                    {
                        ksr->voice[stereo_v].envelope_stage  = 0;
                        ksr->voice[stereo_v].envelope_volume = 0;
                        ksr->voice[stereo_v].control_counter = 0;
                        recompute_envelope(ksr, stereo_v);
                        apply_envelope_to_amp(ksr, stereo_v);
                    }
                    else
                    {
                        ksr->voice[stereo_v].envelope_increment = 0;
                        apply_envelope_to_amp(ksr, stereo_v);
                    }
                }
                break;
            }
        }
    }
}

static void kill_note(Kasaria *ksr, int i)
{
    ksr->voice[i].status = VOICE_DIE;
}

/* Only one instance of a note can be playing on a single channel. */
static void note_on(Kasaria *ksr, MidiEvent *e)
{
    int   i = ksr->voices, lowest = -1;
    long lv = 0x7FFFFFFF, v;

    while(i--)
    {
        if(ksr->voice[i].status == VOICE_FREE)
            lowest = i; /* Can't get a lower volume than silence */

        else if(ksr->voice[i].channel == e->channel && (ksr->voice[i].note == e->a || ksr->channel[ksr->voice[i].channel].mono))
            kill_note(ksr, i);
    }

    if(lowest != -1)
    {
        /* Found a free voice. */
        start_note(ksr, e, lowest);
        return;
    }

    /* Look for the decaying note with the lowest volume */
    i = ksr->voices;
    while(i--)
    {
        if((ksr->voice[i].status != VOICE_ON) && (ksr->voice[i].status != VOICE_DIE))
        {
            v = ksr->voice[i].left_mix;
            if((ksr->voice[i].panned == PANNED_MYSTERY) && (ksr->voice[i].right_mix > v))
                v = ksr->voice[i].right_mix;

            if(v < lv)
            {
                lv     = v;
                lowest = i;
            }
        }
    }

    if(lowest != -1)
    {
        /* This can still cause a click, but if we had a free voice to
        spare for ramping down this note, we wouldn't need to kill it
        in the first place... Still, this needs to be fixed. Perhaps
        we could use a reserve of voices to play dying notes only. */

        ksr->cut_notes++;
        ksr->voice[lowest].status = VOICE_FREE;
        start_note(ksr, e, lowest);
    }
    else
        ksr->lost_notes++;
}

static void finish_note(Kasaria *ksr, int i)
{
    if(ksr->voice[i].sample->modes & MODES_ENVELOPE)
    {
        /* We need to get the envelope out of Sustain stage */
        ksr->voice[i].envelope_stage = 3;
        ksr->voice[i].status         = VOICE_OFF;
        recompute_envelope(ksr, i);
        apply_envelope_to_amp(ksr, i);
    }
    else
    {
        /* Set status to OFF so resample_voice() will let this voice out
        of its loop, if any. In any case, this voice dies when it
        hits the end of its data (ofs>=data_length). */
        ksr->voice[i].status = VOICE_OFF;
    }
}

static void note_off(Kasaria *ksr, MidiEvent *e)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status == VOICE_ON && ksr->voice[i].channel == e->channel && ksr->voice[i].note == e->a)
        {
            if(ksr->channel[e->channel].sustain)
                ksr->voice[i].status = VOICE_SUSTAINED;
            else
                finish_note(ksr, i);
        }
}

/* Process the All Notes Off event */
static void all_notes_off(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status == VOICE_ON && ksr->voice[i].channel == c)
        {
            if(ksr->channel[c].sustain)
                ksr->voice[i].status = VOICE_SUSTAINED;
            else
                finish_note(ksr, i);
        }
}

/* Process the All Sounds Off event */
static void all_sounds_off(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].channel == c && ksr->voice[i].status != VOICE_FREE && ksr->voice[i].status != VOICE_DIE)
            kill_note(ksr, i);
}

static void adjust_pressure(Kasaria *ksr, MidiEvent *e)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status == VOICE_ON && ksr->voice[i].channel == e->channel && ksr->voice[i].note == e->a)
        {
            ksr->voice[i].velocity = e->b;
            recompute_amp(ksr, i);
            apply_envelope_to_amp(ksr, i);
            return;
        }
}

static void adjust_panning(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if((ksr->voice[i].channel == c) && (ksr->voice[i].status == VOICE_ON || ksr->voice[i].status == VOICE_SUSTAINED))
        {
            ksr->voice[i].panning = ksr->channel[c].panning;
            recompute_amp(ksr, i);
            apply_envelope_to_amp(ksr, i);
        }
}

static void drop_sustain(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status == VOICE_SUSTAINED && ksr->voice[i].channel == c)
            finish_note(ksr, i);
}

static void adjust_pitchbend(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status != VOICE_FREE && ksr->voice[i].channel == c)
            recompute_freq(ksr, i);
}

static void adjust_volume(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].channel == c && (ksr->voice[i].status == VOICE_ON || ksr->voice[i].status == VOICE_SUSTAINED))
        {
            recompute_amp(ksr, i);
            apply_envelope_to_amp(ksr, i);
        }
}

static void seek_forward(Kasaria *ksr, long until_time)
{
    reset_voices(ksr);
    while(ksr->current_event->time < until_time)
    {
        switch(ksr->current_event->type)
        {
            /* All notes stay off. Just handle the parameter changes. */

        case ME_PITCH_SENS:
            ksr->channel[ksr->current_event->channel].pitchsens   = ksr->current_event->a;
            ksr->channel[ksr->current_event->channel].pitchfactor = 0;
            break;

        case ME_PITCHWHEEL:
            ksr->channel[ksr->current_event->channel].pitchbend   = ksr->current_event->a + ksr->current_event->b * 128;
            ksr->channel[ksr->current_event->channel].pitchfactor = 0;
            break;

        case ME_MAINVOLUME:
            ksr->channel[ksr->current_event->channel].volume = ksr->current_event->a;
            break;

        case ME_PAN:
            ksr->channel[ksr->current_event->channel].panning = ksr->current_event->a;
            break;

        case ME_EXPRESSION:
            ksr->channel[ksr->current_event->channel].expression = ksr->current_event->a;
            break;

        case ME_PROGRAM:
            if(ISDRUMCHANNEL(ksr, ksr->current_event->channel))
                /* Change drum set */
                ksr->channel[ksr->current_event->channel].bank = ksr->current_event->a;
            else
                ksr->channel[ksr->current_event->channel].program = ksr->current_event->a;
            break;

        case ME_SUSTAIN:
            ksr->channel[ksr->current_event->channel].sustain = ksr->current_event->a;
            break;

        case ME_RESET_CONTROLLERS:
            reset_controllers(ksr, ksr->current_event->channel);
            break;

        case ME_MONO:
            ksr->channel[ksr->current_event->channel].mono = 1;
            break;

        case ME_POLY:
            ksr->channel[ksr->current_event->channel].mono = 0;
            break;

        case ME_TONE_BANK:
            if(!ISDRUMCHANNEL(ksr, ksr->current_event->channel))
                ksr->channel[ksr->current_event->channel].bank = ksr->current_event->a;
            break;

        case ME_EOT:
            ksr->current_sample = ksr->current_event->time;
            return;
        }
        ksr->current_event++;
    }
    /*current_sample=current_event->time;*/
    if(ksr->current_event != ksr->event_list)
        ksr->current_event--;

    ksr->current_sample = until_time;
}

static void skip_to(Kasaria *ksr, long until_time)
{
    if(ksr->current_sample > until_time)
        ksr->current_sample = 0;

    reset_midi(ksr);
    ksr->current_event = ksr->event_list;

    if(until_time)
        seek_forward(ksr, until_time);
}

static void do_compute_data(Kasaria *ksr, long count)
{
    int i, samples;
    samples = (ksr->play_mode.encoding & PE_MONO) ? count : (count * 2);
    for(i = 0; i < samples; i++)
        ksr->buffer_pointer[i] = 0;
    for(i = 0; i < ksr->voices; i++)
    {
        if(ksr->voice[i].status != VOICE_FREE)
            mix_voice(ksr, ksr->buffer_pointer, i, count);
    }
}
static void play_midi(Kasaria *ksr, MidiEvent *e)
{
    if(e)
    {
        if(ISQUIETCHANNEL(ksr, e->channel))
            return;

        switch(e->type)
        {

            /* Effects affecting a single note */

        case ME_NOTEON:
            if(!(e->b)) /* Velocity 0? */
                note_off(ksr, e);
            else
                note_on(ksr, e);
            break;

        case ME_NOTEOFF:
            note_off(ksr, e);
            break;

        case ME_KEYPRESSURE:
            adjust_pressure(ksr, e);
            break;

            /* Effects affecting a single channel */

        case ME_PITCH_SENS:
            ksr->channel[e->channel].pitchsens   = e->a;
            ksr->channel[e->channel].pitchfactor = 0;
            break;

        case ME_PITCHWHEEL:
            ksr->channel[e->channel].pitchbend   = e->a + e->b * 128;
            ksr->channel[e->channel].pitchfactor = 0;
            /* Adjust pitch for notes already playing */
            adjust_pitchbend(ksr, e->channel);
            break;

        case ME_MAINVOLUME:
            ksr->channel[e->channel].volume = e->a;
            adjust_volume(ksr, e->channel);
            break;

        case ME_PAN:
            ksr->channel[e->channel].panning = e->a;
            if(ksr->adjust_panning_immediately)
                adjust_panning(ksr, e->channel);

            break;

        case ME_EXPRESSION:
            ksr->channel[e->channel].expression = e->a;
            adjust_volume(ksr, e->channel);
            break;

        case ME_PROGRAM:
            if(ISDRUMCHANNEL(ksr, e->channel))
            {
                /* Change drum set */
                if(ksr->drumset[e->a])
                    ksr->channel[e->channel].bank = e->a;
            }
            else
                ksr->channel[e->channel].program = e->a;

            break;

        case ME_SUSTAIN:
            ksr->channel[e->channel].sustain = e->a;
            if(!e->a)
                drop_sustain(ksr, e->channel);

            break;

        case ME_RESET_CONTROLLERS:
            reset_controllers(ksr, e->channel);
            break;

        case ME_ALL_NOTES_OFF:
            all_notes_off(ksr, e->channel);
            break;

        case ME_ALL_SOUNDS_OFF:
            all_sounds_off(ksr, e->channel);
            break;

        case ME_MONO:
            ksr->channel[e->channel].mono = 1;
            all_notes_off(ksr, e->channel);
            break;

        case ME_POLY:
            ksr->channel[e->channel].mono = 0;
            all_notes_off(ksr, e->channel);
            break;

        case ME_TONE_BANK:
            if(!ISDRUMCHANNEL(ksr, e->channel))
            {
                if(ksr->tonebank[e->a])
                    ksr->channel[e->channel].bank = e->a;
            }
            break;
        }
    }
}

// Adapted from ReadMidiText function in gspmidi.cpp
static void read_midi_text(Kasaria *ksr)
{
    u_long buff;
    u_long read;

    if(!ksr->fp_midi)
        return;

    fseek(ksr->fp_midi, 0, SEEK_SET);

    if(fread(&buff, 1, 4, ksr->fp_midi) != 4 || buff != 0x6468544d)
    {
        fseek(ksr->fp_midi, 0, SEEK_SET);
        return;
    }

    while(fread(&buff, 1, 4, ksr->fp_midi) == 4)
    {
        if(buff == 0x6B72544D)
            break;

        fseek(ksr->fp_midi, -3, SEEK_CUR);
    }

    if(buff != 0x6B72544D)
    {
        fseek(ksr->fp_midi, 0, SEEK_SET);
        return;
    }

    fseek(ksr->fp_midi, 4, SEEK_CUR);

    while(fread(&buff, 1, 4, ksr->fp_midi) == 4)
    {
        // if((buff & 0x0000FFFF) != 0x0000FF00 || buff == 0x6B72544D)
        if((buff & 0x0000FFFF) != 0x0000FF00 || buff == 0x6B72544D || (buff & 0x00FFFF00) == 0x2FFF00)
            break;

        switch(buff & 0x00FFFF00)
        {
        case 0x02FF00: // Copyright
            buff = (buff & 0xFF000000) >> 24;
            if(!strlen(ksr->song_copyright))
            {
                read                         = fread(ksr->song_copyright, 1, buff, ksr->fp_midi);
                *(ksr->song_copyright + read) = '\0';
            }
            else
                fseek(ksr->fp_midi, buff, SEEK_CUR);
            break;
        case 0x03FF00: // Track
            buff = (buff & 0xFF000000) >> 24;
            if(!strlen(ksr->song_title))
            {
                read                     = fread(ksr->song_title, 1, buff, ksr->fp_midi);
                *(ksr->song_title + read) = '\0';
            }
            else
                fseek(ksr->fp_midi, buff, SEEK_CUR);
            break;
        case 0x01FF00: // other text
        case 0x04FF00: // Instrument
        case 0x05FF00: // Lyrics
        case 0x06FF00: // Marker
        case 0x07FF00: // Cue
        default:
            buff = (buff & 0xFF000000) >> 24;
            fseek(ksr->fp_midi, buff, SEEK_CUR);
            break;
        }
    }
    fseek(ksr->fp_midi, 0, SEEK_SET);
}

Kasaria *ksr_init(void)
{
    Kasaria *ksr = (Kasaria *)safe_malloc(sizeof(Kasaria));
    if(!ksr)
        return NULL;

    memset(ksr, 0, sizeof(Kasaria));
    ksr->default_program        = DEFAULT_PROGRAM;
    ksr->antialiasing_allowed   = 1;
    ksr->pre_resampling_allowed = 1;
#ifdef FAST_DECAY
    ksr->fast_decay = 1;
#else
    ksr->fast_decay = 0;
#endif
    ksr->dynamic_loading            = 0;
    ksr->voices                     = DEFAULT_VOICES;
    ksr->play_mode.rate             = DEFAULT_RATE;
    ksr->play_mode.encoding         = 0;
    ksr->control_rate               = CONTROLS_PER_SECOND;
    ksr->control_ratio              = ksr->play_mode.rate / ksr->control_rate;
    ksr->drumchannels               = DEFAULT_DRUMCHANNELS;
    ksr->quietchannels              = 0;
    ksr->adjust_panning_immediately = 1;
    init_tables(ksr);
    reset_midi(ksr);
    adjust_amplification(ksr, DEFAULT_AMPLIFICATION);
    return ksr;
}

int ksr_load_config(Kasaria *ksr, char *filename)
{
    char  directory[256];
    char *separator;
    if(!ksr || !filename)
        return 0;

    ksr_unload_config(ksr);
    if(!ksr->tonebank[0])
    {
        ksr->tonebank[0] = (ToneBank *)safe_malloc(sizeof(ToneBank));
        memset(ksr->tonebank[0], 0, sizeof(ToneBank));
    }
    if(!ksr->drumset[0])
    {
        ksr->drumset[0] = (ToneBank *)safe_malloc(sizeof(ToneBank));
        memset(ksr->drumset[0], 0, sizeof(ToneBank));
    }
    memset(directory, 0, sizeof(directory));
    strncpy(directory, filename, 255);
    directory[255] = '\0';
    separator      = strrchr(directory, PATH_SEP);
    if(separator)
        *separator = '\0';

    add_to_pathlist(ksr, directory);
    if(read_config_file(ksr, filename) == 0)
    {
        if(*ksr->def_instr_name)
            set_default_instrument(ksr, ksr->def_instr_name);

        if(!ksr->dynamic_loading)
            return ksr_force_instrument_load(ksr);
        else
            return 1;
    }
    return 0;
}

void ksr_unload_config(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    free_instruments(ksr);
    free_pathlist(ksr);
    memset(ksr->def_instr_name, 0, sizeof(ksr->def_instr_name));
    memset(ksr->last_config, 0, sizeof(ksr->last_config));
}

int ksr_reload_config(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    if(strlen(ksr->last_config))
    {
        char temp[1024];
        memset(temp, 0, sizeof(temp));
        strncpy(temp, ksr->last_config, 1023);
        temp[1023] = '\0';
        return ksr_load_config(ksr, temp);
    }
    return 0;
}

int ksr_load_soundfont_file(Kasaria *ksr, char *filename)
{
    if(!ksr || !filename)
        return 0;

    if(ksr->sf_loaded)
        free_soundfont(&ksr->sf_info);

    if(load_soundfont(&ksr->sf_info, filename) != 0)
        return 0;

    strncpy(ksr->sf_filename, filename, sizeof(ksr->sf_filename) - 1);
    ksr->sf_filename[sizeof(ksr->sf_filename) - 1] = '\0';
    ksr->sf_loaded                                = 1;

    return 1;
}

void ksr_channel_note_on(Kasaria *ksr, u_char channel, u_char note, u_char velocity)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_NOTEON;
    ev.a       = note & 0x7f;
    ev.b       = velocity & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_note_off(Kasaria *ksr, u_char channel, u_char note)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_NOTEOFF;
    ev.a       = note & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_key_pressure(Kasaria *ksr, u_char channel, u_char note, u_char velocity)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_KEYPRESSURE;
    ev.a       = note & 0x7f;
    ev.b       = velocity & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_volume(Kasaria *ksr, u_char channel, u_char volume)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_MAINVOLUME;
    ev.a       = volume & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_pan(Kasaria *ksr, u_char channel, u_char pan)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_PAN;
    ev.a       = pan & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_expression(Kasaria *ksr, u_char channel, u_char expression)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_EXPRESSION;
    ev.a       = expression & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_sustain(Kasaria *ksr, u_char channel, u_char sustain)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_SUSTAIN;
    ev.a       = sustain & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_pitch_wheel(Kasaria *ksr, u_char channel, u_short pitch)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_PITCHWHEEL;
    ev.a       = pitch & 0x7f;
    ev.b       = (pitch >> 7) & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_pitch_range(Kasaria *ksr, u_char channel, u_char range)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_PITCH_SENS;
    ev.a       = range & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_program(Kasaria *ksr, u_char channel, u_char program)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_PROGRAM;
    ev.a       = program & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_set_bank(Kasaria *ksr, u_char channel, u_char bank)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_TONE_BANK;
    ev.a       = bank & 0x7f;
    play_midi(ksr, &ev);
}

void ksr_channel_mono_mode(Kasaria *ksr, u_char channel)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_MONO;
    play_midi(ksr, &ev);
}

void ksr_channel_poly_mode(Kasaria *ksr, u_char channel)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_POLY;
    play_midi(ksr, &ev);
}

void ksr_channel_all_notes_off(Kasaria *ksr, u_char channel)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_ALL_NOTES_OFF;
    play_midi(ksr, &ev);
}

void ksr_channel_all_sounds_off(Kasaria *ksr, u_char channel)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_ALL_SOUNDS_OFF;
    play_midi(ksr, &ev);
}

void ksr_channel_reset_controllers(Kasaria *ksr, u_char channel)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_RESET_CONTROLLERS;
    play_midi(ksr, &ev);
}

void ksr_channel_control_change(Kasaria *ksr, u_char channel, u_char controller, u_char value)
{
    if(!ksr)
        return;

    channel    = channel & 0x0f;
    controller = controller & 0x7f;
    value      = value & 0x7f;
    switch(controller)
    {
    case 0x00:
        ksr_channel_set_bank(ksr, channel, value);
        break;
    case 0x06:
        switch((ksr->rpn_msb[channel] << 8) | ksr->rpn_lsb[channel])
        {
        case 0x0000:
            ksr_channel_set_pitch_range(ksr, channel, value);
            break;
        case 0x7f7f:
            ksr_channel_set_pitch_range(ksr, channel, 2);
            ksr->rpn_msb[channel] = 0xff;
            ksr->rpn_lsb[channel] = 0xff;
            break;
        }
        break;
    case 0x07:
        ksr_channel_set_volume(ksr, channel, value);
        break;
    case 0x0a:
        ksr_channel_set_pan(ksr, channel, value);
        break;
    case 0x0b:
        ksr_channel_set_expression(ksr, channel, value);
        break;
    case 0x40:
        ksr_channel_set_sustain(ksr, channel, value);
        break;
    case 0x62:
        ksr->rpn_lsb[channel] = 0xff;
        break;
    case 0x63:
        ksr->rpn_msb[channel] = 0xff;
        break;
    case 0x64:
        ksr->rpn_msb[channel] = value;
        break;
    case 0x65:
        ksr->rpn_lsb[channel] = value;
        break;
    case 0x78:
        ksr_channel_all_sounds_off(ksr, channel);
        break;
    case 0x79:
        ksr_channel_reset_controllers(ksr, channel);
        break;
    case 0x7b:
        ksr_channel_all_notes_off(ksr, channel);
        break;
    case 0x7e:
        ksr_channel_mono_mode(ksr, channel);
        break;
    case 0x7f:
        ksr_channel_poly_mode(ksr, channel);
        break;
    }
}

void ksr_write_midi(Kasaria *ksr, u_char byte1, u_char byte2, u_char byte3)
{
    u_char type    = byte1 & 0xf0;
    u_char channel = byte1 & 0x0f;
    if(!ksr)
        return;

    switch(type)
    {
    case 0x80:
        ksr_channel_note_off(ksr, channel, byte2);
        break;
    case 0x90:
        ksr_channel_note_on(ksr, channel, byte2, byte3);
        break;
    case 0xa0:
        ksr_channel_key_pressure(ksr, channel, byte2, byte3);
        break;
    case 0xb0:
        ksr_channel_control_change(ksr, channel, byte2, byte3);
        break;
    case 0xc0:
        ksr_channel_set_program(ksr, channel, byte2);
        break;
    case 0xe0:
        ksr_channel_set_pitch_wheel(ksr, channel, (u_short)((byte3 << 7) | byte2));
        break;
    }
}

void ksr_write_midi_packed(Kasaria *ksr, u_long data)
{
    u_char byte1 = data & 0xff;
    u_char byte2 = (data >> 8) & 0x7f;
    u_char byte3 = (data >> 16) & 0x7f;
    if(!ksr)
        return;

    ksr_write_midi(ksr, byte1, byte2, byte3);
}

void ksr_write_sysex(Kasaria *ksr, u_char *buffer, long count)
{
    const u_char gm_reset_array[6]  = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    const u_char gm2_reset_array[6] = { 0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7 };
    const u_char gs_reset_array[11] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
    const u_char xg_reset_array[9]  = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };
    if(!ksr || !buffer)
        return;

    if(buffer[0] != 0xF0 || buffer[count - 1] != 0xF7)
        return;

    if(count == 6 && memcmp(&gm_reset_array[0], buffer, 6) == 0)
        reset_midi(ksr);
    else if(count == 6 && memcmp(&gm2_reset_array[0], buffer, 6) == 0)
        reset_midi(ksr);
    else if(count == 11 && memcmp(&gs_reset_array[0], buffer, 11) == 0)
        reset_midi(ksr);
    else if(count == 9 && memcmp(&xg_reset_array[0], buffer, 9) == 0)
        reset_midi(ksr);
}

void ksr_render_char(Kasaria *ksr, u_char *buffer, long count)
{
    int curframes, cursamples, i;
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;

        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            ksr->common_buffer[i] = ksr->common_buffer[i] >> (32 - 8 - GUARD_BITS);
            if(ksr->common_buffer[i] > 127)
                ksr->common_buffer[i] = 127;

            else if(ksr->common_buffer[i] < -128)
                ksr->common_buffer[i] = -128;

            buffer[i] = (u_char)ksr->common_buffer[i] ^ 0x80;
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_short(Kasaria *ksr, short *buffer, long count)
{
    int curframes, cursamples, i;
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;
        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            ksr->common_buffer[i] = ksr->common_buffer[i] >> (32 - 16 - GUARD_BITS);
            if(ksr->common_buffer[i] > 32767)
                ksr->common_buffer[i] = 32767;

            else if(ksr->common_buffer[i] < -32768)
                ksr->common_buffer[i] = -32768;

            buffer[i] = (short)ksr->common_buffer[i];
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_24(Kasaria *ksr, int24 *buffer, long count)
{
    int curframes, cursamples, i;
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;

        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            ksr->common_buffer[i] = ksr->common_buffer[i] >> (32 - 24 - GUARD_BITS);
            if(ksr->common_buffer[i] > 8388607)
                ksr->common_buffer[i] = 8388607;

            else if(ksr->common_buffer[i] < -8388608)
                ksr->common_buffer[i] = -8388608;

            buffer[i].data[0] = ksr->common_buffer[i] & 0xff;
            buffer[i].data[1] = (ksr->common_buffer[i] >> 8) & 0xff;
            buffer[i].data[2] = (ksr->common_buffer[i] >> 16) & 0xff;
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_long(Kasaria *ksr, long *buffer, long count)
{
    int   curframes, cursamples, i;
    long maxval = 1 << (31 - GUARD_BITS);
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;
        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            if(ksr->common_buffer[i] > maxval - 1)
                ksr->common_buffer[i] = maxval - 1;
            else if(ksr->common_buffer[i] < maxval * -1)
                ksr->common_buffer[i] = maxval * -1;

            ksr->common_buffer[i] = ksr->common_buffer[i] << GUARD_BITS;
            buffer[i]            = ksr->common_buffer[i];
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_float(Kasaria *ksr, f32 *buffer, long count)
{
    int   curframes, cursamples, i;
    long maxval = 1 << (31 - GUARD_BITS);
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;

        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            if(ksr->common_buffer[i] > maxval - 1)
                ksr->common_buffer[i] = maxval - 1;
            else if(ksr->common_buffer[i] < -maxval)
                ksr->common_buffer[i] = -maxval;
            buffer[i] = ksr->common_buffer[i] / (f32)maxval;
        }

        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_f64(Kasaria *ksr, f64 *buffer, long count)
{
    int   curframes, cursamples, i;
    long maxval = 1 << (31 - GUARD_BITS);
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;
        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            if(ksr->common_buffer[i] > maxval - 1)
                ksr->common_buffer[i] = maxval - 1;
            else if(ksr->common_buffer[i] < -maxval)
                ksr->common_buffer[i] = -maxval;
            buffer[i] = ksr->common_buffer[i] / (f64)maxval;
        }

        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_ulaw(Kasaria *ksr, u_char *buffer, long count)
{
    int curframes, cursamples, i;
    if(!ksr || !buffer)
        return;

    while(count > 0)
    {
        if(count < AUDIO_BUFFER_SIZE)
            curframes = count;
        else
            curframes = AUDIO_BUFFER_SIZE;

        ksr->buffer_pointer = ksr->common_buffer;
        do_compute_data(ksr, curframes);
        cursamples = curframes;
        if(!(ksr->play_mode.encoding & PE_MONO))
            cursamples *= 2;

        for(i = 0; i < cursamples; i++)
        {
            ksr->common_buffer[i] = ksr->common_buffer[i] >> (32 - 13 - GUARD_BITS);
            if(ksr->common_buffer[i] > 4095)
                ksr->common_buffer[i] = 4095;
            else if(ksr->common_buffer[i] < -4096)
                ksr->common_buffer[i] = -4096;

            buffer[i] = _l2u[ksr->common_buffer[i]];
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_all_notes_off(Kasaria *ksr)
{
    int i;
    if(!ksr)
        return;

    for(i = 0; i < 16; i++)
    {
        drop_sustain(ksr, i);
        all_notes_off(ksr, i);
    }
}

void ksr_all_sounds_off(Kasaria *ksr)
{
    int i;
    if(!ksr)
        return;

    for(i = 0; i < 16; i++)
        all_sounds_off(ksr, i);
}

void ksr_reset_controllers(Kasaria *ksr)
{
    int i;
    if(!ksr)
        return;

    for(i = 0; i < 16; i++)
        reset_controllers(ksr, i);
}

void ksr_panic(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_voices(ksr);
}

void ksr_reset(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_midi(ksr);
}

int ksr_load_midi_file(Kasaria *ksr, char *filename)
{
    if(!ksr || !filename)
        return 0;

    ksr_unload_midi(ksr);

    ksr->fp_midi = open_file(ksr, filename, 1, OF_VERBOSE);
    if(!ksr->fp_midi)
        return 0;

    ksr->event_list = read_midi_file(ksr, ksr->fp_midi, &ksr->events_midi, &ksr->sample_count);
    if(!ksr->event_list || !ksr->events_midi || !ksr->sample_count)
    {
        ksr_unload_midi(ksr);
        return 0;
    }

    load_missing_instruments(ksr);

    read_midi_text(ksr);
    skip_to(ksr, 0);
    strncpy(ksr->last_smf, filename, 1023);
    ksr->last_smf[1023] = '\0';
    return 1;
}

void ksr_unload_midi(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_midi(ksr);
    if(ksr->event_list)
    {
        free(ksr->event_list);
        ksr->event_list = NULL;
    }

    ksr->current_event = NULL;

    if(ksr->fp_midi)
    {
        close_file(ksr->fp_midi);
        ksr->fp_midi = NULL;
    }

    ksr->events_midi    = 0;
    ksr->sample_count   = 0;
    ksr->current_sample = 0;
    memset(ksr->song_title, 0, sizeof(ksr->song_title));
    memset(ksr->song_copyright, 0, sizeof(ksr->song_copyright));
    memset(ksr->last_smf, 0, sizeof(ksr->last_smf));
}

int ksr_reload_midi(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    if(strlen(ksr->last_smf))
    {
        char temp[1024];
        memset(temp, 0, sizeof(temp));
        strncpy(temp, ksr->last_smf, 1023);
        temp[1023] = '\0';
        return ksr_load_midi_file(ksr, temp);
    }

    return 0;
}

int ksr_play_midi(Kasaria *ksr, long type, u_char *buffer, long count)
{
    int convert;
    if(!ksr || !buffer || (type > AUDIO_ULAW || type < AUDIO_CHAR) || !ksr->current_event || (ksr->current_event->type == ME_EOT && !ksr_get_active_voices(ksr)))
        return 0;

    while(count > 0)
    {
        /* Handle all events that should happen at this time */
        while(ksr->current_event->time <= ksr->current_sample)
        {
            if(ksr->current_event->type == ME_EOT)
                break;

            play_midi(ksr, ksr->current_event);
            ksr->current_event++;
        }
        convert = ksr->current_event->time - ksr->current_sample;
        if(convert > count || convert <= 0)
            convert = count;

        switch(type)
        {
        case AUDIO_CHAR:
            ksr_render_char(ksr, (u_char *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(u_char);
            else
                buffer += convert * sizeof(u_char);

            break;
        case AUDIO_SHORT:
            ksr_render_short(ksr, (short *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(short);
            else
                buffer += convert * sizeof(short);

            break;
        case AUDIO_24:
            ksr_render_24(ksr, (int24 *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(int24);
            else
                buffer += convert * sizeof(int24);

            break;
        case AUDIO_LONG:
            ksr_render_long(ksr, (long *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(long);
            else
                buffer += convert * sizeof(long);

            break;
        case AUDIO_FLOAT:
            ksr_render_float(ksr, (f32 *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(f32);
            else
                buffer += convert * sizeof(f32);

            break;
        case AUDIO_DOUBLE:
            ksr_render_f64(ksr, (f64 *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(f64);
            else
                buffer += convert * sizeof(f64);

            break;
        case AUDIO_ULAW:
            ksr_render_ulaw(ksr, (u_char *)buffer, convert);
            if(!(ksr->play_mode.encoding & PE_MONO))
                buffer += convert * 2 * sizeof(u_char);
            else
                buffer += convert * sizeof(u_char);

            break;
        }
        if(ksr->current_sample == ksr->sample_count)
            ksr_all_notes_off(ksr);

        ksr->current_sample += convert;
        count              -= convert;
    }
    return 1;
}

int ksr_seek_midi(Kasaria *ksr, long time)
{
    int total_time;
    if(!ksr || !ksr->current_event)
        return 0;

    total_time = ksr_get_duration(ksr);
    if(time > total_time)
        time = total_time;
    else if(time < 0)
        time = 0;

    skip_to(ksr, 0);
    skip_to(ksr, ksr_millis2samples(ksr, time));
    return ksr_get_current_time(ksr);
}

int ksr_fast_forward_midi(Kasaria *ksr, long time)
{
    int new_time;
    if(!ksr)
        return 0;

    new_time = ksr_get_current_time(ksr) + time;
    return ksr_seek_midi(ksr, new_time);
}

int ksr_rewind_midi(Kasaria *ksr, long time)
{
    int new_time;
    if(!ksr)
        return 0;

    new_time = ksr_get_current_time(ksr) - time;
    return ksr_seek_midi(ksr, new_time);
}

int ksr_restart_smf(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr_seek_midi(ksr, 0);
}

int ksr_stop_midi(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr_seek_midi(ksr, ksr_get_duration(ksr));
}

void ksr_set_amplification(Kasaria *ksr, int amplification)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    if(amplification > MAX_AMPLIFICATION)
        amplification = MAX_AMPLIFICATION;
    else if(amplification < 0)
        amplification = 0;

    adjust_amplification(ksr, amplification);
}

void ksr_set_max_voices(Kasaria *ksr, int voices)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    if(voices > MAX_VOICES)
        voices = MAX_VOICES;
    else if(voices < 1)
        voices = 1;

    ksr->voices = voices;
}

void ksr_set_immediate_panning(Kasaria *ksr, int value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->adjust_panning_immediately = value;
}

void ksr_set_mono(Kasaria *ksr, int value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    if(value)
        ksr->play_mode.encoding |= PE_MONO;
    else
        ksr->play_mode.encoding &= ~PE_MONO;
}

void ksr_set_fast_decay(Kasaria *ksr, int value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->fast_decay = value;
    ksr_reload_config(ksr);
}

void ksr_set_antialiasing(Kasaria *ksr, int value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->antialiasing_allowed = value;
    ksr_reload_config(ksr);
}

void ksr_set_pre_resample(Kasaria *ksr, int value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->pre_resampling_allowed = value;
    ksr_reload_config(ksr);
}

void ksr_set_dynamic_instrument_load(Kasaria *ksr, int value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->dynamic_loading = value;
    ksr_reload_config(ksr);
}

void ksr_set_sample_rate(Kasaria *ksr, int rate)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    if(rate > MAX_OUTPUT_RATE)
        rate = MAX_OUTPUT_RATE;

    else if(rate < MIN_OUTPUT_RATE)
        rate = MIN_OUTPUT_RATE;

    ksr->play_mode.rate = rate;

    if(ksr->control_rate > ksr->play_mode.rate)
        ksr->control_rate = ksr->play_mode.rate;
    else if(ksr->control_rate < ksr->play_mode.rate / MAX_CONTROL_RATIO)
        ksr->control_rate = ksr->play_mode.rate / MAX_CONTROL_RATIO;

    ksr->control_ratio = ksr->play_mode.rate / ksr->control_rate;

    if(ksr->control_ratio > MAX_CONTROL_RATIO)
        ksr->control_ratio = MAX_CONTROL_RATIO;

    else if(ksr->control_ratio < 1)
        ksr->control_ratio = 1;

    ksr_reload_config(ksr);
}

void ksr_set_control_rate(Kasaria *ksr, int rate)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->control_rate = rate;
    if(ksr->control_rate > ksr->play_mode.rate)
        ksr->control_rate = ksr->play_mode.rate;
    else if(ksr->control_rate < ksr->play_mode.rate / MAX_CONTROL_RATIO)
        ksr->control_rate = ksr->play_mode.rate / MAX_CONTROL_RATIO;

    ksr->control_ratio = ksr->play_mode.rate / ksr->control_rate;

    if(ksr->control_ratio > MAX_CONTROL_RATIO)
        ksr->control_ratio = MAX_CONTROL_RATIO;

    else if(ksr->control_ratio < 1)
        ksr->control_ratio = 1;

    ksr_reload_config(ksr);
}

void ksr_set_default_program(Kasaria *ksr, int program)
{
    if(!ksr)
        return;

    ksr->default_program = program & 0x7f;
}

void ksr_set_drum_channel(Kasaria *ksr, int channel, int enable)
{
    if(!ksr)
        return;

    channel = channel & 0x0f;

    if(enable)
        ksr->drumchannels |= (1 << channel);
    else
        ksr->drumchannels &= ~(1 << channel);
}

void ksr_set_quiet_channel(Kasaria *ksr, int channel, int enable)
{
    if(!ksr)
        return;

    channel = channel & 0x0f;
    if(enable && !ISQUIETCHANNEL(ksr, channel))
    {
        drop_sustain(ksr, channel);
        all_notes_off(ksr, channel);
        reset_controllers(ksr, channel);
    }
    if(enable)
        ksr->quietchannels |= (1 << channel);
    else
        ksr->quietchannels &= ~(1 << channel);
}

void ksr_restore_defaults(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->default_program        = DEFAULT_PROGRAM;
    ksr->antialiasing_allowed   = 1;
    ksr->pre_resampling_allowed = 1;
#ifdef FAST_DECAY
    ksr->fast_decay = 1;
#else
    ksr->fast_decay = 0;
#endif
    ksr->dynamic_loading            = 0;
    ksr->voices                     = DEFAULT_VOICES;
    ksr->play_mode.rate             = DEFAULT_RATE;
    ksr->play_mode.encoding         = 0;
    ksr->control_rate               = CONTROLS_PER_SECOND;
    ksr->control_ratio              = ksr->play_mode.rate / ksr->control_rate;
    ksr->drumchannels               = DEFAULT_DRUMCHANNELS;
    ksr->quietchannels              = 0;
    ksr->adjust_panning_immediately = 1;
    adjust_amplification(ksr, DEFAULT_AMPLIFICATION);
    ksr_reload_config(ksr);
}

int ksr_force_instrument_load(Kasaria *ksr)
{
    int i;
    if(!ksr)
        return 0;

    reset_voices(ksr);
    for(i = 0; i < 128; i++)
    {
        if(ksr->tonebank[i])
        {
            int j;
            for(j = 0; j < 128; j++)
                if(ksr->tonebank[i]->tone[j].name && !ksr->tonebank[i]->tone[j].instrument)
                    ksr->tonebank[i]->tone[j].instrument = MAGIC_LOAD_INSTRUMENT;
        }
        if(ksr->drumset[i])
        {
            int j;
            for(j = 0; j < 128; j++)
            {
                if(ksr->drumset[i]->tone[j].name && !ksr->drumset[i]->tone[j].instrument)
                    ksr->drumset[i]->tone[j].instrument = MAGIC_LOAD_INSTRUMENT;
            }
        }
    }
    if(load_missing_instruments(ksr) == 0)
        return 1;

    return 0;
}

int ksr_set_default_instrument(Kasaria *ksr, char *filename)
{
    if(!ksr || !filename)
        return 0;

    reset_voices(ksr);
    if(set_default_instrument(ksr, filename) == 0)
        return 1;

    return 0;
}

void ksr_free_default_instrument(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    free_default_instrument(ksr);
}

int ksr_get_config_name(Kasaria *ksr, char *buffer, long count)
{
    int len;
    if(!ksr)
        return 0;

    len = strlen(ksr->last_config);
    if(buffer && len)
        strncpy(buffer, ksr->last_config, count);

    return len;
}

int ksr_get_amplification(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return (int)(ksr->master_volume * 100.0L);
}

int ksr_get_active_voices(Kasaria *ksr)
{
    int count = 0;
    int i;
    if(!ksr)
        return 0;

    for(i = 0; i < ksr->voices; i++)
        if(ksr->voice[i].status != VOICE_FREE)
            count++;

    return count;
}

int ksr_get_max_voices(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->voices;
}

int ksr_get_immediate_panning(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->adjust_panning_immediately;
}

int ksr_get_mono(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    if(ksr->play_mode.encoding & PE_MONO)
        return 1;

    else
        return 0;
}

int ksr_get_fast_decay(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->fast_decay;
}

int ksr_get_antialiasing(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->antialiasing_allowed;
}

int ksr_get_pre_resample(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->pre_resampling_allowed;
}

int ksr_get_dynamic_instrument_load(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->dynamic_loading;
}

int ksr_get_sample_rate(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->play_mode.rate;
}

int ksr_get_control_rate(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->control_rate;
}

int ksr_get_default_program(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->default_program;
}

int ksr_get_drum_channel_enabled(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    if(ISDRUMCHANNEL(ksr, channel))
        return 1;
    else
        return 0;
}

int ksr_get_quiet_channel_enabled(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    if(ISQUIETCHANNEL(ksr, channel))
        return 1;
    else
        return 0;
}

int ksr_get_lost_notes(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->lost_notes;
}

int ksr_get_cut_notes(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->cut_notes;
}

int ksr_channel_get_volume(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].volume;
}

int ksr_channel_get_pan(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].panning;
}

int ksr_channel_get_expression(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].expression;
}

int ksr_channel_get_sustain(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].sustain;
}

int ksr_channel_get_pitch_wheel(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].pitchbend;
}

int ksr_channel_get_pitch_range(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].pitchsens;
}

int ksr_channel_get_program(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    if(ISDRUMCHANNEL(ksr, channel))
        return ksr->channel[channel].bank;

    else
        return ksr->channel[channel].program;
}

int ksr_channel_get_bank(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    if(!ISDRUMCHANNEL(ksr, channel))
        return ksr->channel[channel].bank;

    else
        return -1;
}

int ksr_channel_get_mono(Kasaria *ksr, int channel)
{
    if(!ksr)
        return 0;

    channel = channel & 0x0f;
    return ksr->channel[channel].mono;
}

int ksr_get_smf_name(Kasaria *ksr, char *buffer, long count)
{
    int len;
    if(!ksr)
        return 0;

    len = strlen(ksr->last_smf);
    if(buffer && len)
        strncpy(buffer, ksr->last_smf, count);

    return len;
}

int ksr_get_event_count(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->events_midi;
}

int ksr_get_sample_count(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->sample_count;
}

int ksr_get_current_sample_position(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->current_sample;
}

int ksr_get_duration(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr_samples2millis(ksr, ksr->sample_count);
}

int ksr_get_current_time(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr_samples2millis(ksr, ksr->current_sample);
}

int ksr_get_bitrate(Kasaria *ksr)
{
    int bitrate;
    if(!ksr || !ksr->fp_midi)
        return 0;

    fseek(ksr->fp_midi, 0, SEEK_END);
    bitrate = ftell(ksr->fp_midi) * 8 / ksr_get_duration(ksr);
    fseek(ksr->fp_midi, 0, SEEK_SET);
    if(!bitrate)
        bitrate = 1;

    return bitrate;
}

int ksr_get_song_title(Kasaria *ksr, char *buffer, long count)
{
    int len;
    if(!ksr)
        return 0;

    len = strlen(ksr->song_title);

    if(buffer && len)
        strncpy(buffer, ksr->song_title, count);

    return len;
}

int ksr_get_song_copyright(Kasaria *ksr, char *buffer, long count)
{
    int len;
    if(!ksr)
        return 0;

    len = strlen(ksr->song_copyright);
    if(buffer && len)
        strncpy(buffer, ksr->song_copyright, count);

    return len;
}

int ksr_millis2samples(Kasaria *ksr, long millis)
{
    if(!ksr)
        return 0;

    return (int)((f64)ksr->play_mode.rate * millis / 1000);
}

int ksr_samples2millis(Kasaria *ksr, long samples)
{
    if(!ksr)
        return 0;

    return (int)((f64)samples * 1000 / ksr->play_mode.rate);
}

void ksr_shutdown(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_midi(ksr);
    ksr_unload_midi(ksr);
    ksr_unload_config(ksr);
    free_default_instrument(ksr);
    free_tables(ksr);
    memset(ksr, 0, sizeof(Kasaria));
    free(ksr);
}