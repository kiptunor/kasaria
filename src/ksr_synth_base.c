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
#include <stdlib.h>

#include <stdint.h>
#include <time.h>

#include "ext_deps/log_c/log.h"
#include "ksr_internal.h"










void reset_voices(Kasaria *ksr)
{
    log_debug("Reset voices");
/*
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
    memset(ksr->note_press, 0, sizeof(ksr->note_press));
    ksr->note_gen = 0;
    */

    ksr->steal_scan = 0;

    for(int i = 0; i < ksr->voices; i++)
    {
        ksr->voice[i].status = VOICE_FREE;
        ksr->free_voice_stack[i] = i;
    }

    ksr->free_voice_count = ksr->voices;

    memset(ksr->channel_voice_count, 0, sizeof(ksr->channel_voice_count));
    memset(ksr->voice_by_channel_note, 0, sizeof(ksr->voice_by_channel_note));
    memset(ksr->note_press, 0, sizeof(ksr->note_press));

    ksr->note_gen = 0;
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
    int sign = (ksr->voice[v].sample_increment < 0); // for bidirectional loops
    int pb   = ksr->channel[ksr->voice[v].channel].pitchbend;
    f64 a;

    if(!ksr->voice[v].sample->sample_rate && !ksr->voice[v].sample_right)
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

    // Apply channel coarse tuning (RPN 0x0002). bend_coarse[n] == 2^(n/12),
    // so whole-semitone offsets don't even need a pow().
    {
        int ct = ksr->channel[ksr->voice[v].channel].coarse_tune;
        if(ct != 64)
        {
            int st = ct - 64; // semitone offset, -64..+63
            if(st > 0)
                ksr->voice[v].frequency = (long)((f64)(ksr->voice[v].frequency) * bend_coarse[st]);
            else
                ksr->voice[v].frequency = (long)((f64)(ksr->voice[v].frequency) / bend_coarse[-st]);
        }
    }

    //a = FSCALE(((f64)(ksr->voice[v].sample->sample_rate) * (f64)(ksr->voice[v].frequency)) / ((f64)(ksr->voice[v].sample->root_freq) * (f64)(ksr->play_mode.rate)), FRACTION_BITS);

    if(ksr->voice[v].sample->sample_rate)
        a = FSCALE(((f64)(ksr->voice[v].sample->sample_rate) * (f64)(ksr->voice[v].frequency)) / ((f64)(ksr->voice[v].sample->root_freq) * (f64)(ksr->play_mode.rate)), FRACTION_BITS);
    else
        a = (1 << FRACTION_BITS); // pre-resampled data plays back one frame per output frame

    if(sign)
        a = -a; // need to preserve the loop direction

    f64 base = ksr->voice[v].sample->sample_rate ? FSCALE(((f64)(ksr->voice[v].sample->sample_rate) * (f64)(ksr->voice[v].frequency)) / ((f64)(ksr->voice[v].sample->root_freq) * (f64)(ksr->play_mode.rate)), FRACTION_BITS) : (f64)(1 << FRACTION_BITS);
    
    a = sign ? -base : base; // need to preserve the loop direction


    ksr->voice[v].sample_increment = (long)(a);

    if(ksr->voice[v].sample_right)
    {
        int signr = (ksr->voice[v].sample_increment_right < 0);
        f64 ar;
        
        if(ksr->voice[v].sample_right->sample_rate)
        {
            if(ksr->voice[v].sample->sample_rate)
                ar = base * ((f64)(ksr->voice[v].sample_right->sample_rate) / (f64)(ksr->voice[v].sample->sample_rate));
            else
                ar = FSCALE(((f64)(ksr->voice[v].sample_right->sample_rate) * (f64)(ksr->voice[v].frequency)) / ((f64)(ksr->voice[v].sample_right->root_freq) * (f64)(ksr->play_mode.rate)), FRACTION_BITS);
        }
        else
            ar = (f64)(1 << FRACTION_BITS); // pre-resampled

        if(signr || sign)
            ar = -ar;

        ksr->voice[v].sample_increment_right = (long)(ar);
    }
}

void recompute_amp(Kasaria *ksr, int v)
{
    long vol = ksr->channel[ksr->voice[v].channel].volume;
    long expr = ksr->channel[ksr->voice[v].channel].expression;
    long vel = ksr->voice[v].velocity;

    //f64 vel_scaled = vel ? vol_table[vel] * 127.0 : 0.0;
    //f64 vel_scaled = vel ? pow(vol_table[vel], 1.40) * 127.0 : 0.0;
    //f64 vel_scaled = vel ? pow(vol_table[vel] / vol_table[127], 1.10) * 127.0 : 0.0;
    //f64 vel_scaled = vel ? (f64)vel * (f64)vel / 127.0 : 0.0;
    //f64 vol_scaled = vol ? vol_table[vol] * 127.0 : 0.0;
    //f64 vol_scaled = vol ? (f64)vol * (f64)vol / 127.0 : 0.0;
    //f64 expr_scaled = expr ? vol_table[expr] * 127.0 : 0.0;
    //f64 expr_scaled = expr ? (f64)expr * (f64)expr / 127.0 : 0.0;

    f64 vel_scaled = vel ? (f64)vel * (f64)vel / 127.0 : 0.0;
    f64 vol_scaled = vol ? (f64)vol * (f64)vol / 127.0 : 0.0;
    f64 expr_scaled = expr ? (f64)expr * (f64)expr / 127.0 : 0.0;

    
    f64 tempamp = vel_scaled * vol_scaled * expr_scaled;

    f64 base = (f64)(tempamp) * ksr->voice[v].sample->volume * ksr->master_volume * MIX_HEADROOM;

    if(!(ksr->play_mode.encoding & PE_MONO))
    {
        if(ksr->voice[v].sample_right)
        {
            int pan  = ksr->voice[v].panning;
            f64 base_r = (f64)(tempamp) * ksr->voice[v].sample_right->volume * ksr->master_volume * MIX_HEADROOM;
            f64 lamp = base / (f64)(1 << 20);
            f64 ramp = base_r / (f64)(1 << 20);
            
            if(pan < 0)
                pan = 0;
            else if(pan > 127)
                pan = 127;
            
            if(pan < 64)
                ramp *= (f64)pan / 64.0;
            else if(pan > 64)
                lamp *= (f64)(127 - pan) / 63.0;
        
            ksr->voice[v].panned    = PANNED_MYSTERY;
            ksr->voice[v].left_amp  = lamp;
            ksr->voice[v].right_amp = ramp;
        }
        else if(ksr->voice[v].panning > 60 && ksr->voice[v].panning < 68)
        {
            ksr->voice[v].panned   = PANNED_CENTER;
            ksr->voice[v].left_amp = base / (f64)(1 << 20);
        }
        else if(ksr->voice[v].panning < 5)
        {
            ksr->voice[v].panned   = PANNED_LEFT;
            ksr->voice[v].left_amp = base / (f64)(1 << 19);
        }
        else if(ksr->voice[v].panning > 123)
        {
            ksr->voice[v].panned   = PANNED_RIGHT;
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
    ksr->voice[i].sample_right                      = NULL;
    ksr->voice[i].sample_offset_right               = 0;
    ksr->voice[i].sample_increment_right            = 0;

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

    // SF2 stereo support: mix both samples into a single voice
    if(!ISDRUMCHANNEL(ksr, e->channel) && ip && ip->samples > 1)
    {
        Sample *primary = ksr->voice[i].sample;
        Sample *left    = NULL;
        Sample *right   = NULL;
        int     si;
        
        if(primary->sample_type & SF_SAMPLETYPE_LEFT)
            left = primary;
        else if(primary->sample_type & SF_SAMPLETYPE_RIGHT)
            right = primary;
        
        if(left || right)
        {
            Sample *best       = NULL;
            long    best_score = -1;

            for(si = 0; si < ip->samples; si++)
            {
                Sample *candidate = &ip->sample[si];
                long    score;

                if(candidate == primary)
                    continue;

                if(left && !(candidate->sample_type & SF_SAMPLETYPE_RIGHT))
                    continue;
                if(right && !(candidate->sample_type & SF_SAMPLETYPE_LEFT))
                    continue;

                if(left && left->sf_sample_link == candidate->sf_sample_index)
                    score = 0;
                else if(right && right->sf_sample_link == candidate->sf_sample_index)
                    score = 0;
                else
                {
                    score = 1000000;

                    if(candidate->low_key != primary->low_key || candidate->high_key != primary->high_key)
                        score += 100000;
                    if(candidate->low_vel != primary->low_vel || candidate->high_vel != primary->high_vel)
                        score += 10000;

                    score += labs((long)candidate->data_length - (long)primary->data_length) >> 8;

                    if(candidate->root_key != primary->root_key)
                        score += 1000;
                }

                if(best == NULL || score < best_score)
                {
                    best       = candidate;
                    best_score = score;
                }
            }

            // Require a plausible match.  A structural match with a wildly
            // different length is almost certainly a different layer.
            if(best && (best_score < 1000000 || labs((long)best->data_length - (long)primary->data_length) < (primary->data_length / 2)))
            {
                if(left)
                    right = best;
                else
                    left = best;

                ksr->voice[i].sample                 = left;
                ksr->voice[i].sample_right           = right;
                ksr->voice[i].sample_offset          = 0;
                ksr->voice[i].sample_increment       = 0;
                ksr->voice[i].sample_offset_right    = 0;
                ksr->voice[i].sample_increment_right = 0;
                recompute_freq(ksr, i);
                recompute_amp(ksr, i);
                stereo_partner = right;
            }
        }
    }

    for(int li = 0; li < ip->samples; li++)
    {
        Sample *layer = &ip->sample[li];
        Sample *layer_left  = NULL;
        Sample *layer_right = NULL;

        if(layer == ksr->voice[i].sample)
            continue;
        if(layer == stereo_partner)
            continue;

        if(e->key < layer->low_key || e->key > layer->high_key)
            continue;

        if(e->vel < layer->low_vel || e->vel > layer->high_vel)
            continue;

        if(layer->sample_type & SF_SAMPLETYPE_LEFT)
            layer_left = layer;
        else if(layer->sample_type & SF_SAMPLETYPE_RIGHT)
            layer_right = layer;

        if(layer_left || layer_right)
        {
            Sample *best = NULL;
            long best_score = -1;

            for(int sj = 0; sj < ip->samples; sj++)
            {
                Sample *candidate = &ip->sample[sj];
                long score;

                if(candidate == layer)
                    continue;

                if(layer_left && !(candidate->sample_type & SF_SAMPLETYPE_RIGHT))
                    continue;

                if(layer_right && !(candidate->sample_type & SF_SAMPLETYPE_LEFT))
                    continue;

                if((layer_left && layer_left->sf_sample_link == candidate->sf_sample_index) ||
                    (layer_right && layer_right->sf_sample_link == candidate->sf_sample_index))
                {
                    score = 0;
                }
                else
                {
                    score = 1000000;

                    if(candidate->low_key != layer->low_key || candidate->high_key != layer->high_key)
                        score += 100000;

                    if(candidate->low_vel != layer->low_vel || candidate->high_vel != layer->high_vel)
                        score += 10000;

                    score += labs((long)candidate->data_length - (long)layer->data_length) >> 8;

                    if(candidate->root_key != layer->root_key)
                        score += 1000;
                }

                if(best == NULL || score < best_score)
                {
                    best = candidate;
                    best_score = score;
                }
            }

            if(best && (best_score < 1000000 || labs((long)best->data_length - (long)layer->data_length) < layer->data_length / 2))
            {
                if(layer_left)
                    layer_right = best;
                else
                    layer_left = best;
            }
        }
        
        if(layer_left)
        {
            if(layer != layer_left)
                continue;

            layer = layer_left;
        }
        
        if(layer == ksr->voice[i].sample || layer == stereo_partner)
            continue;


         // Only start a layered voice if we can register it in a trackable
         // slot (2..7); unregistered voices would never be released and would
         // leak the voice pool. 
        int layer_slot = -1;
        for(int s = 2; s < 8; s++)
        {
            if(ksr->voice_by_channel_note[e->channel][e->key][s] == NULL)
            {
                layer_slot = s;
                break;
            }
        }

        if(layer_slot >= 0 && ksr->free_voice_count > 0)
        {
            int layer_v = ksr->free_voice_stack[--ksr->free_voice_count];
            Voice *lv   = &ksr->voice[layer_v];
            ksr->voice_by_channel_note[e->channel][e->key][layer_slot] = &ksr->voice[layer_v];

            channel_voice_add(ksr, e->channel, layer_v);
            ksr->voice[layer_v].status          = VOICE_ON;
            ksr->voice[layer_v].channel         = e->channel;
            ksr->voice[layer_v].note            = e->key;
            ksr->voice[layer_v].velocity        = e->vel;
            ksr->voice[layer_v].sample          = layer;
            ksr->voice[layer_v].sample_right    = layer_right;
            ksr->voice[layer_v].sample_offset   = 0;
            ksr->voice[layer_v].sample_offset_right = 0;
            ksr->voice[layer_v].sample_increment = 0;
            ksr->voice[layer_v].sample_increment_right = 0;
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

static void invalidate_press_for_voice(Kasaria *ksr, int vi)
{
    if(!ksr->overlapping_notes)
        return;
    
    int ch  = ksr->voice[vi].channel;
    int key = ksr->voice[vi].note;
    
    if(ch < 0 || key < 0)
        return;
    
    for(int s = 0; s < MAX_NOTE_PRESSES; s++)
    {
        NotePress *p = &ksr->note_press[ch][key][s];
        
        if(p->active)
        {
            if(p->voice == vi)
                p->voice = -1;
            
            if(p->voice2 == vi)
                p->voice2 = -1;

            for(int l = 0; l < MAX_NOTE_LAYERS; l++)
                if(p->layer[l] == vi)
                    p->layer[l] = -1;

            int any = (p->voice >= 0 || p->voice2 >= 0);
            for(int l = 0; l < MAX_NOTE_LAYERS; l++)
                if(p->layer[l] >= 0)
                    any = 1;

            if(!any)
                p->active = 0;
        }
    }
}

void kill_note(Kasaria *ksr, int i)
{
    invalidate_press_for_voice(ksr, i);
    ksr->voice[i].status = VOICE_DIE;
    channel_voice_remove(ksr, ksr->voice[i].channel, i);
}


static int count_held(Kasaria *ksr, int ch, int key)
{
    int n = 0;
    for(int s = 0; s < MAX_NOTE_PRESSES; s++)
        n += ksr->note_press[ch][key][s].active;
    return n;
}

static int grab_voice(Kasaria *ksr)
{
    if(ksr->free_voice_count > 0)
        return ksr->free_voice_stack[--ksr->free_voice_count];

    int i = ksr->steal_scan, n = ksr->voices;
    
    while(n--)
    {
        if(ksr->voice[i].status != VOICE_ON && ksr->voice[i].status != VOICE_DIE)
            break;
        
        i = (i + 1) % ksr->voices;
    }
    
    if(ksr->voice[i].status == VOICE_ON || ksr->voice[i].status == VOICE_DIE)
    {
        ksr->lost_notes++;
        return -1;
    }
    
    ksr->steal_scan = (i + 1) % ksr->voices;
    ksr->cut_notes++;
    
    invalidate_press_for_voice(ksr, i);
    channel_voice_remove(ksr, ksr->voice[i].channel, i);
    
    return i;
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
    ksr->skip_note_vel[e->channel][e->key]    = 0;

    // Retrigger — check ALL 8 slots, clear all after killing
    if(ksr->channel[e->channel].mono)
    {
        int n = ksr->channel_voice_count[e->channel];
        while(n--)
            kill_note(ksr, ksr->channel_voice_list[e->channel][n]);
    }
    else
    {
        if(!ksr->overlapping_notes)
        {
            for(int k = 0; k < 8; k++)
            {
                Voice *vp = ksr->voice_by_channel_note[e->channel][e->key][k];
                
                if(vp && vp->channel == e->channel && vp->note == e->key)
                    kill_note(ksr, (int)(vp - ksr->voice));
                
                ksr->voice_by_channel_note[e->channel][e->key][k] = NULL;
            }
        }
    }

    if(ksr->overlapping_notes)
    {
        for(int s = 0; s < 8; s++)
            ksr->voice_by_channel_note[e->channel][e->key][s] = NULL;

        int v = grab_voice(ksr);
        
        if(v < 0)
            return;
    
        start_note(ksr, e, v);
    
        NotePress *ring = ksr->note_press[e->channel][e->key];
        int slot        = -1;
        int oldest      = -1;
        u32 oldest_gen  = UINT32_MAX;
    
        for(int s = 0; s < MAX_NOTE_PRESSES; s++)
        {
            if(!ring[s].active)
            {
                slot = s;
                break;
            }
            
            if(ring[s].gen < oldest_gen)
            {
                oldest_gen = ring[s].gen;
                oldest = s;
            }
        }
    
        if(slot < 0)
        {
            slot = oldest;
            
            if(ring[slot].voice  >= 0)
                kill_note(ksr, ring[slot].voice);
            
            if(ring[slot].voice2 >= 0)
                kill_note(ksr, ring[slot].voice2);

            for(int s = 0; s < MAX_NOTE_LAYERS; s++)
                if(ring[slot].layer[s] >= 0)
                    kill_note(ksr, ring[slot].layer[s]);
        }
    
        ring[slot].active = 1;
        ring[slot].gen    = ++ksr->note_gen;
        ring[slot].voice  = v;
        ring[slot].voice2 = -1;
        for(int s = 0; s < MAX_NOTE_LAYERS; s++)
            ring[slot].layer[s] = -1;
        
        ring[slot].voice2 = -1;

        // Capture the layered voices start_note registered (slots 2..7) so they
        // can be released/evicted together with this press.
        {
            int n = 0;
            for(int s = 2; s < 8 && n < MAX_NOTE_LAYERS; s++)
            {
                Voice *lv = ksr->voice_by_channel_note[e->channel][e->key][s];
                if(lv && lv->channel == e->channel && lv->note == e->key)
                    ring[slot].layer[n++] = (int)(lv - ksr->voice);
            }
        }
    }
    else
    {
        // No overlapping notes


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
}

void finish_note(Kasaria *ksr, int i)
{
    invalidate_press_for_voice(ksr, i); // Only works if overlapping_notes is enabled
    
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

static NotePress *oldest_active(Kasaria *ksr, int ch, int key)
{
    NotePress *best = NULL;
    uint32_t bg = UINT32_MAX;
    for(int s = 0; s < MAX_NOTE_PRESSES; s++)
    {
        NotePress *p = &ksr->note_press[ch][key][s];
        if(p->active && p->gen < bg) { bg = p->gen; best = p; }
    }
    return best;
}

void note_off(Kasaria *ksr, MidiEvent *e)
{

    if(ksr->skip_note_active[e->channel][e->key])
    {
        ksr->skip_note_active[e->channel][e->key] = 0;
        ksr->skip_note_vel[e->channel][e->key] = 0;
        return;
    }
    
    if(ksr->overlapping_notes)
    {
        NotePress *p = oldest_active(ksr, e->channel, e->key);

        if(!p)
            return;             // OFF-before-ON or duplicate OFF → safe no-op
    
        if(p->voice >= 0)
        {
            Voice *v = &ksr->voice[p->voice];
            if(v->status == VOICE_ON || v->status == VOICE_SUSTAINED)
            {
                if(ksr->channel[e->channel].sustain && v->status == VOICE_ON)
                    v->status = VOICE_SUSTAINED;
                else
                    finish_note(ksr, p->voice);
            }
        }
    
        if(p->voice2 >= 0)
        {
            Voice *v = &ksr->voice[p->voice2];
            if(v->status == VOICE_ON || v->status == VOICE_SUSTAINED)
            {
                if(ksr->channel[e->channel].sustain && v->status == VOICE_ON)
                    v->status = VOICE_SUSTAINED;
                else
                    finish_note(ksr, p->voice2);
            }
        }
    
        // Release this press's layered voices. They are tracked per-press (not
        // via the shared voice_by_channel_note slots, which a later retrigger
        // may have overwritten), so overlapping notes release the right ones.
        for(int s = 0; s < MAX_NOTE_LAYERS; s++)
        {
            int idx = p->layer[s];
            p->layer[s] = -1;
            if(idx < 0)
                continue;

            Voice *v = &ksr->voice[idx];
            if(v->channel != e->channel || v->note != e->key)
                continue;

            if(v->status == VOICE_ON)
            {
                if(ksr->channel[e->channel].sustain)
                    v->status = VOICE_SUSTAINED;
                else
                    finish_note(ksr, idx);
            }
        }

        p->active = 0;
        p->voice  = -1;
        p->voice2 = -1;

        // Drop the shared slot pointers for this key. Any live layered voice is
        // owned by its press record above, so this is bookkeeping only.
        for(int s = 0; s < 8; s++)
            ksr->voice_by_channel_note[e->channel][e->key][s] = NULL;
    }
    else
    {
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
}

// Process the All Notes Off event
void all_notes_off(Kasaria *ksr, int c)
{
    if(ksr->overlapping_notes)
    {
        for(int n = 0; n < 128; n++)
        {
            for(int s = 0; s < MAX_NOTE_PRESSES; s++)
            {
                for(int l = 0; l < MAX_NOTE_LAYERS; l++)
                    ksr->note_press[c][n][s].layer[l] = -1;
                ksr->note_press[c][n][s].active = 0;
                ksr->note_press[c][n][s].voice  = -1;
                ksr->note_press[c][n][s].voice2 = -1;
            }
            
            for(int k = 0; k < 8; k++)
                ksr->voice_by_channel_note[c][n][k] = NULL;
        }
    }
    
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
    if(ksr->overlapping_notes)
    {
        for(int n = 0; n < 128; n++)
        {
            for(int s = 0; s < MAX_NOTE_PRESSES; s++)
            {
                for(int l = 0; l < MAX_NOTE_LAYERS; l++)
                    ksr->note_press[c][n][s].layer[l] = -1;
                ksr->note_press[c][n][s].active = 0;
                ksr->note_press[c][n][s].voice  = -1;
                ksr->note_press[c][n][s].voice2 = -1;
            }
            
            for(int k = 0; k < 8; k++)
                ksr->voice_by_channel_note[c][n][k] = NULL;
        }
    }
    
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
    ksr->channel[c].coarse_tune = 64; // RPN 0x0002 center (no tuning offset)
    ksr->channel[c].reverb      = 40;
    ksr->channel[c].chorus      = 0;
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

    if(ksr->reverb_enabled)
        reset_reverb(ksr);

    if(ksr->chorus_enabled)
        reset_chorus(ksr);
    
    
    ksr->lost_notes = 0;
    ksr->cut_notes  = 0;
}

void do_compute_data(Kasaria *ksr, long count)
{
    int i;
    int samples;

    samples = (ksr->play_mode.encoding & PE_MONO) ? count : (count * 2);

    if(ksr->reverb_enabled)
        // memset(ksr->reverb_send_buffer, 0, count * 4);
        memset(ksr->reverb_send_buffer, 0, count * sizeof(ksr->reverb_send_buffer[0]));

    if(ksr->chorus_enabled)
        memset(ksr->chorus_send_buffer, 0, count * sizeof(ksr->chorus_send_buffer[0]));
        //memset(ksr->chorus_send_buffer, 0, count * 4);
        //memset(ksr->reverb_send_buffer, 0, count * sizeof(ksr->reverb_send_buffer[0]));
    
    

    for(i = 0; i < samples; i++)
        ksr->buffer_pointer[i] = 0;

    for(i = 0; i < ksr->voices; i++)
    {
        if(ksr->voice[i].status != VOICE_FREE)
            mix_voice(ksr, ksr->buffer_pointer, i, count);
    }

    if(ksr->reverb_enabled)
        process_reverb(ksr, ksr->buffer_pointer, ksr->reverb_send_buffer, count);

    if(ksr->chorus_enabled)
        process_chorus(ksr, ksr->buffer_pointer, ksr->chorus_send_buffer, count);
    
    

    if(ksr->audio_compressor)
        audio_compressor(&ksr->compressor_settings, (f32 *)ksr->buffer_pointer, samples * sizeof(f32));
}