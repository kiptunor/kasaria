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

playmidi.c -- random stuff in need of rearrangement

*/



#include <string.h>

#include <stdint.h>
#include <time.h>

#include "ext_deps/log_c/log.h"
#include "ksr_internal.h"










void reset_voices(Kasaria *ksr)
{
    log_debug("Reset voices");

    ksr->steal_scan = 0;
   
    for(int i = 0; i < MAX_VOICES; i++)    // ← MAX_VOICES, not ksr->voices
        ksr->voice[i].status = VOICE_FREE;
    
    ksr->free_voice_count = ksr->voices;
    for(int i = 0; i < ksr->voices; i++)
    {
        ksr->voice[i].status = VOICE_FREE;
        ksr->free_voice_stack[i] = i;
    }
    memset(ksr->channel_voice_count, 0, sizeof(ksr->channel_voice_count));
    memset(ksr->voice_by_channel_note, 0, sizeof(ksr->voice_by_channel_note));
}

void free_voice_push(Kasaria *ksr, int i)
{
    //ulog_debug("free_voice_push: i=%d", i);
    ksr->free_voice_stack[ksr->free_voice_count++] = i;
}

void channel_voice_add(Kasaria *ksr, int ch, int vi)
{
    if(ksr->channel_voice_count[ch] < MAX_VOICES)
        ksr->channel_voice_list[ch][ksr->channel_voice_count[ch]++] = vi;
}

void channel_voice_remove(Kasaria *ksr, int ch, int vi)
{
    int n = ksr->channel_voice_count[ch];
    for(int i = 0; i < n; i++)
    {
        if(ksr->channel_voice_list[ch][i] == vi)
        {
            ksr->channel_voice_list[ch][i] = ksr->channel_voice_list[ch][--n];
            break;
        }
    }
    ksr->channel_voice_count[ch] = n;
}

void select_sample(Kasaria *ksr, int v, Instrument *ip)
{
    long    f, cdiff, diff;
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

    // No suitable sample found! We'll select the sample whose root
    // frequency is closest to the one we want. (Actually we should
    // probably convert the low, high, and root frequencies to MIDI note
    // values and compare those.)

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

void recompute_freq(Kasaria *ksr, int v)
{
    int sign = (ksr->voice[v].sample_increment < 0), // for bidirectional loops
        pb   = ksr->channel[ksr->voice[v].channel].pitchbend;
    f64 a;

    if(!ksr->voice[v].sample->sample_rate)
        return;

    if(ksr->voice[v].vibrato_control_ratio)
    {
        // This instrument has vibrato. Invalidate any precomputed sample_increments.

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
            // Damn. Somebody bent the pitch.
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
        a = -a; // need to preserve the loop direction

    ksr->voice[v].sample_increment = (long)(a);
}

void recompute_amp(Kasaria *ksr, int v)
{
    long vol = ksr->channel[ksr->voice[v].channel].volume;
    long expr = ksr->channel[ksr->voice[v].channel].expression;
    long vel = ksr->voice[v].velocity;

    f64 vel_scaled = vel ? vol_table[vel] * 127.0 : 0.0;
    f64 vol_scaled = vol ? vol_table[vol] * 127.0 : 0.0;
    f64 expr_scaled = expr ? vol_table[expr] * 127.0 : 0.0;
    f64 tempamp = vel_scaled * vol_scaled * expr_scaled;

    f64 base = (f64)(tempamp) * ksr->voice[v].sample->volume * ksr->master_volume;

    if(!(ksr->play_mode.encoding & PE_MONO))
    {
        if(ksr->voice[v].panning > 60 && ksr->voice[v].panning < 68)
        {
            ksr->voice[v].panned   = PANNED_CENTER;
            //ksr->voice[v].left_amp = base / (f64)(1 << 19);
            ksr->voice[v].left_amp = base / (f64)(1 << 20);
        }
        else if(ksr->voice[v].panning < 5)
        {
            ksr->voice[v].panned   = PANNED_LEFT;
            //ksr->voice[v].left_amp = base / (f64)(1 << 18);
            ksr->voice[v].left_amp = base / (f64)(1 << 19);
        }
        else if(ksr->voice[v].panning > 123)
        {
            ksr->voice[v].panned   = PANNED_RIGHT;
            //ksr->voice[v].left_amp = base / (f64)(1 << 18);
            ksr->voice[v].left_amp = base / (f64)(1 << 19);
        }
        else
        {
            ksr->voice[v].panned     = PANNED_MYSTERY;
            ksr->voice[v].left_amp   = base / (f64)(1 << 25);
            ksr->voice[v].right_amp  = ksr->voice[v].left_amp * (ksr->voice[v].panning);
            ksr->voice[v].left_amp  *= (f64)(127 - ksr->voice[v].panning);
        }
    }
    else
    {
        ksr->voice[v].panned   = PANNED_CENTER;
        ksr->voice[v].left_amp = base / (f64)(1 << 19);
    }
}

void start_note(Kasaria *ksr, MidiEvent *e, int i)
{
    Instrument *ip;
    int         j;
    Sample     *stereo_partner = NULL;

    if(ISDRUMCHANNEL(ksr, e->channel))
    {
        ToneBank *db = ksr->drumset[ksr->channel[e->channel].bank];
        if(!db)
            db = ksr->drumset[0];
        
        if(!db)
            return;
        
        if(!IS_VALID_INSTRUMENT(ip = db->tone[e->key].instrument))
        {
            db = ksr->drumset[0];
            if(!db)
                return;
            
            if(!IS_VALID_INSTRUMENT(ip = db->tone[e->key].instrument))
                return;
        }
    
        Sample *best = NULL;
        for(j = 0; j < ip->samples; j++)
        {
            Sample *sp = &ip->sample[j];
            if(e->key >= sp->low_key && e->key <= sp->high_key)
            {
                best = sp;
                break;
            }
        }
        if(!best)
            return;
        ksr->voice[i].sample = best;
            
        if(ksr->voice[i].sample->note_to_use)
            ksr->voice[i].orig_frequency = freq_table[(int)(ksr->voice[i].sample->note_to_use)];
        else
            ksr->voice[i].orig_frequency = freq_table[e->key & 0x7F];
    }
    else
    {
        if(!ksr->tonebank[ksr->channel[e->channel].bank] && !ksr->tonebank[0] && ksr->channel[e->channel].program != SPECIAL_PROGRAM)
            return;

        if(ksr->channel[e->channel].program == SPECIAL_PROGRAM)
        {
            ip = ksr->default_instrument;
            if(!ip)
                return;
        }
        else
        {
            ToneBank *tb = ksr->tonebank[ksr->channel[e->channel].bank];

            ip = NULL;
            if(tb)
                ip = tb->tone[ksr->channel[e->channel].program].instrument;

            if(!IS_VALID_INSTRUMENT(ip))
            {
                tb = ksr->tonebank[0];
                if(!tb)
                    return;

                ip = tb->tone[ksr->channel[e->channel].program].instrument;
                if(!IS_VALID_INSTRUMENT(ip))
                    return;
            }
        }

        if(!ip)
            return;
        

        if(ip->sample->note_to_use)
            ksr->voice[i].orig_frequency = freq_table[(int)(ip->sample->note_to_use)];
        else
            ksr->voice[i].orig_frequency = freq_table[e->key & 0x7F];
        select_sample(ksr, i, ip);
    }

    // FIX 3: Clear all voice_by_channel_note slots before setting new ones
    for(int s = 0; s < 8; s++)
        ksr->voice_by_channel_note[e->channel][e->key][s] = NULL;

    channel_voice_add(ksr, e->channel, i);
    ksr->voice[i].status                            = VOICE_ON;
    ksr->voice[i].channel                           = e->channel;
    ksr->voice[i].note                              = e->key;
    ksr->voice[i].velocity                          = e->vel;
    ksr->voice_by_channel_note[e->channel][e->key][0] = &ksr->voice[i];
    ksr->voice[i].sample_offset                     = 0;
    ksr->voice[i].sample_increment                  = 0;

    ksr->voice[i].tremolo_phase                     = 0;
    ksr->voice[i].tremolo_phase_increment           = ksr->voice[i].sample->tremolo_phase_increment;
    ksr->voice[i].tremolo_sweep                     = ksr->voice[i].sample->tremolo_sweep_increment;
    ksr->voice[i].tremolo_sweep_position            = 0;
    
    ksr->voice[i].vibrato_sweep                     = ksr->voice[i].sample->vibrato_sweep_increment;
    ksr->voice[i].vibrato_sweep_position            = 0;
    ksr->voice[i].vibrato_control_ratio             = ksr->voice[i].sample->vibrato_control_ratio;
    ksr->voice[i].vibrato_control_counter           = ksr->voice[i].vibrato_phase = 0;
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

    // SF2 stereo support: start partner voice for stereo samples
    if(!ISDRUMCHANNEL(ksr, e->channel) && ip && ip->samples > 1)
    {
        Sample *primary = ksr->voice[i].sample;
        int     si;
        for(si = 0; si < ip->samples; si++)
        {
            Sample *candidate = &ip->sample[si];
            if(candidate == primary)
                continue;
    
            if(candidate->sample_type == SF_SAMPLETYPE_RIGHT && candidate->sf_sample_index == primary->sf_sample_link)
            {
                int stereo_v;
                
                if(ksr->free_voice_count > 0)
                {
                    stereo_v = ksr->free_voice_stack[--ksr->free_voice_count];
                    channel_voice_add(ksr, e->channel, stereo_v);
                    ksr->voice[stereo_v].status                     = VOICE_ON;
                    ksr->voice[stereo_v].channel                    = e->channel;
                    ksr->voice[stereo_v].note                       = e->key;
                    ksr->voice[stereo_v].velocity                   = e->vel;
                    ksr->voice_by_channel_note[e->channel][e->key][1] = &ksr->voice[stereo_v];
                    ksr->voice[stereo_v].sample                     = candidate;
                    ksr->voice[stereo_v].sample_offset              = 0;
                    ksr->voice[stereo_v].sample_increment           = 0;
                    ksr->voice[stereo_v].orig_frequency             = ksr->voice[i].orig_frequency;

                    ksr->voice[stereo_v].tremolo_phase              = 0;
                    ksr->voice[stereo_v].tremolo_phase_increment    = candidate->tremolo_phase_increment;
                    ksr->voice[stereo_v].tremolo_sweep              = candidate->tremolo_sweep_increment;
                    ksr->voice[stereo_v].tremolo_sweep_position     = 0;
                    
                    ksr->voice[stereo_v].vibrato_sweep              = candidate->vibrato_sweep_increment;
                    ksr->voice[stereo_v].vibrato_sweep_position     = 0;
                    ksr->voice[stereo_v].vibrato_control_ratio      = candidate->vibrato_control_ratio;
                    ksr->voice[stereo_v].vibrato_control_counter    = 0;
                    ksr->voice[stereo_v].vibrato_phase              = 0;
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

                    // FIX 4: Track the stereo partner so layering code can skip it
                    stereo_partner = candidate;
                    break;
                }
            }
        }

        // Super broken when using Project CF-162.sf2
        for(int li = 0; li < ip->samples; li++)
        {
            Sample *layer = &ip->sample[li];
            
            if(layer == ksr->voice[i].sample)
                continue;
            if(layer == stereo_partner)
                continue;
            
            if(e->key < layer->low_key || e->key > layer->high_key)
                continue;
            
            if(e->vel < layer->low_vel || e->vel > layer->high_vel)
                continue;
            
            if(ksr->free_voice_count > 0)
            {
                int layer_v = ksr->free_voice_stack[--ksr->free_voice_count];
                
                for(int s = 0; s < 8; s++)
                {
                    if(ksr->voice_by_channel_note[e->channel][e->key][s] == NULL)
                    {
                        ksr->voice_by_channel_note[e->channel][e->key][s] = &ksr->voice[layer_v];
                        break;
                    }
                }
                
                channel_voice_add(ksr, e->channel, layer_v);
                ksr->voice[layer_v].status          = VOICE_ON;
                ksr->voice[layer_v].channel         = e->channel;
                ksr->voice[layer_v].note            = e->key;
                ksr->voice[layer_v].velocity        = e->vel;
                ksr->voice[layer_v].sample          = layer;
                ksr->voice[layer_v].sample_offset   = 0;
                ksr->voice[layer_v].sample_increment = 0;
                ksr->voice[layer_v].orig_frequency  = ksr->voice[i].orig_frequency;
                
                ksr->voice[layer_v].tremolo_phase              = 0;
                ksr->voice[layer_v].tremolo_phase_increment    = layer->tremolo_phase_increment;
                ksr->voice[layer_v].tremolo_sweep              = layer->tremolo_sweep_increment;
                ksr->voice[layer_v].tremolo_sweep_position     = 0;
                
                ksr->voice[layer_v].vibrato_sweep              = layer->vibrato_sweep_increment;
                ksr->voice[layer_v].vibrato_sweep_position     = 0;
                ksr->voice[layer_v].vibrato_control_ratio      = layer->vibrato_control_ratio;
                ksr->voice[layer_v].vibrato_control_counter    = 0;
                ksr->voice[layer_v].vibrato_phase              = 0;
                for(j = 0; j < VIBRATO_SAMPLE_INCREMENTS; j++)
                    ksr->voice[layer_v].vibrato_sample_increment[j] = 0;
                
                if(ksr->channel[e->channel].panning != NO_PANNING)
                    ksr->voice[layer_v].panning = ksr->channel[e->channel].panning;
                else
                    ksr->voice[layer_v].panning = layer->panning;
                
                recompute_freq(ksr, layer_v);
                recompute_amp(ksr, layer_v);
                
                if(layer->modes & MODES_ENVELOPE)
                {
                    ksr->voice[layer_v].envelope_stage  = 0;
                    ksr->voice[layer_v].envelope_volume = 0;
                    ksr->voice[layer_v].control_counter = 0;
                    recompute_envelope(ksr, layer_v);
                    apply_envelope_to_amp(ksr, layer_v);
                }
                else
                {
                    ksr->voice[layer_v].envelope_increment = 0;
                    apply_envelope_to_amp(ksr, layer_v);
                }
            }
        }
    }
}

void kill_note(Kasaria *ksr, int i)
{
    ksr->voice[i].status = VOICE_DIE;
    channel_voice_remove(ksr, ksr->voice[i].channel, i);
}

// Only one instance of a note can be playing on a single channel.
// This thing needs some serious oprimizations
void note_on(Kasaria *ksr, MidiEvent *e)
{
    if(!ksr->is_soundfont_loaded)
        return;
    
    if(ksr->note_vel_skipping)
        if(e->vel >= ksr->low_vel_treshold && e->vel <= ksr->high_vel_treshold)
        {
            ksr->skip_note_vel[e->channel][e->key] = e->vel;
            ksr->skip_note_active[e->channel][e->key] = 1;
            return;
        }
    
    ksr->skip_note_active[e->channel][e->key] = 0;
    ksr->skip_note_vel[e->channel][e->key] = 0;
    
    // FIX 1: Retrigger — check ALL 8 slots, clear all after killing
    if(ksr->channel[e->channel].mono)
    {
        int n = ksr->channel_voice_count[e->channel];
        while(n--)
            kill_note(ksr, ksr->channel_voice_list[e->channel][n]);
    }
    else
    {
        for(int k = 0; k < 8; k++)
        {
            Voice *vp = ksr->voice_by_channel_note[e->channel][e->key][k];
            if(vp && vp->channel == e->channel && vp->note == e->key)
                kill_note(ksr, (int)(vp - ksr->voice));
            ksr->voice_by_channel_note[e->channel][e->key][k] = NULL;
        }
    }
    
    if(ksr->free_voice_count > 0)
    {
        start_note(ksr, e, ksr->free_voice_stack[--ksr->free_voice_count]);
        return;
    }

    int i      = ksr->steal_scan;
    int n      = ksr->voices;
    int lowest = -1;
    while(n--)
    {
        int st = ksr->voice[i].status;
        if(st != VOICE_ON && st != VOICE_DIE)
        {
            lowest = i;
            break;
        }
        i = (i + 1) % ksr->voices;
    }
    ksr->steal_scan = lowest >= 0 ? (lowest + 1) % ksr->voices : 0;
    if(lowest != -1)
    {
        ksr->cut_notes++;
        channel_voice_remove(ksr, ksr->voice[lowest].channel, lowest);
        start_note(ksr, e, lowest);
    }
    else
        ksr->lost_notes++;
}

void finish_note(Kasaria *ksr, int i)
{
    if(ksr->voice[i].sample->modes & MODES_ENVELOPE)
    {
        // We need to get the envelope out of Sustain stage
        ksr->voice[i].envelope_stage = 3;
        ksr->voice[i].status         = VOICE_OFF;
        recompute_envelope(ksr, i);
        apply_envelope_to_amp(ksr, i);
    }
    else
    {
        // Set status to OFF so resample_voice() will let this voice out
        // of its loop, if any. In any case, this voice dies when it
        // hits the end of its data (ofs>=data_length).
        ksr->voice[i].status = VOICE_OFF;
    }
    channel_voice_remove(ksr, ksr->voice[i].channel, i);
}

void note_off(Kasaria *ksr, MidiEvent *e)
{
    if(ksr->skip_note_active[e->channel][e->key])
    {
        ksr->skip_note_active[e->channel][e->key] = 0;
        ksr->skip_note_vel[e->channel][e->key] = 0;
        return;
    }
    
    // FIX 2: Process all 8 slots and clear ALL of them
    for(int k = 0; k < 8; k++)
    {
        Voice *v = ksr->voice_by_channel_note[e->channel][e->key][k];
        if(v && v->status == VOICE_ON && v->channel == e->channel && v->note == e->key)
        {
            if(ksr->channel[e->channel].sustain)
                v->status = VOICE_SUSTAINED;
            else
                finish_note(ksr, (int)(v - ksr->voice));
        }
        ksr->voice_by_channel_note[e->channel][e->key][k] = NULL;
    }
}

// Process the All Notes Off event
void all_notes_off(Kasaria *ksr, int c)
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

void drop_sustain(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status == VOICE_SUSTAINED && ksr->voice[i].channel == c)
            finish_note(ksr, i);
}

// Process the All Sounds Off event
void all_sounds_off(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].channel == c && ksr->voice[i].status != VOICE_FREE && ksr->voice[i].status != VOICE_DIE)
            kill_note(ksr, i);
}

void adjust_amplification(Kasaria *ksr, int amplification)
{
    ksr->master_volume = (f64)(amplification) / 100.0L;
}

void adjust_pressure(Kasaria *ksr, MidiEvent *e)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status == VOICE_ON && ksr->voice[i].channel == e->channel && ksr->voice[i].note == e->key)
        {
            ksr->voice[i].velocity = e->vel;
            recompute_amp(ksr, i);
            apply_envelope_to_amp(ksr, i);
            return;
        }
}

void adjust_panning(Kasaria *ksr, int c)
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

void adjust_pitchbend(Kasaria *ksr, int c)
{
    int i = ksr->voices;
    while(i--)
        if(ksr->voice[i].status != VOICE_FREE && ksr->voice[i].channel == c)
            recompute_freq(ksr, i);
}

void adjust_volume(Kasaria *ksr, int c)
{
    int n = ksr->channel_voice_count[c];
    for(int i = 0; i < n; i++)
    {
        int vi = ksr->channel_voice_list[c][i];
        if(ksr->voice[vi].status == VOICE_ON || ksr->voice[vi].status == VOICE_SUSTAINED || ksr->voice[vi].status == VOICE_OFF)
        {
            recompute_amp(ksr, vi);
            apply_envelope_to_amp(ksr, vi);
        }
    }
}

// Process the Reset All Controllers event
void reset_controllers(Kasaria *ksr, int c)
{
    // ulog_debug("Reset controllers for channel %d", c);
    ksr->channel[c].volume      = 90;  // Some standard says, although the SCC docs say 0.
    ksr->channel[c].expression  = 127; // SCC-1 does this.
    ksr->channel[c].sustain     = 0;
    ksr->channel[c].mono        = 0;
    ksr->channel[c].pitchbend   = 0x2000;
    ksr->channel[c].pitchfactor = 0; // to be computed
}

void reset_midi(Kasaria *ksr)
{
    log_debug("Reset MIDI state");
    int i;
    for(i = 0; i < 16; i++)
    {
        reset_controllers(ksr, i);
        // The rest of these are unaffected by the Reset All Controllers event
        ksr->channel[i].program   = ksr->default_program;
        //ksr->channel[i].panning   = NO_PANNING;
        ksr->channel[i].panning   = 64;
        ksr->channel[i].pitchsens = 2;
        ksr->channel[i].bank      = 0; // tone bank or drum set
        ksr->rpn_msb[i]           = 0xff;
        ksr->rpn_lsb[i]           = 0xff;
    }
    reset_voices(ksr);
    ksr->lost_notes = 0;
    ksr->cut_notes  = 0;
}

void do_compute_data(Kasaria *ksr, long count)
{
    int i;
    int samples;

    samples = (ksr->play_mode.encoding & PE_MONO) ? count : (count * 2);

    u64 t0 = 0;
    if(ksr->profiling_enabled)
        t0 = monotonic_ns();

    for(i = 0; i < samples; i++)
        ksr->buffer_pointer[i] = 0;

    for(i = 0; i < ksr->voices; i++)
    {
        if(ksr->voice[i].status != VOICE_FREE)
            mix_voice(ksr, ksr->buffer_pointer, i, count);
    }

    audio_compressor(&ksr->compressor_settings, (f32 *)ksr->buffer_pointer, samples * sizeof(f32));
}