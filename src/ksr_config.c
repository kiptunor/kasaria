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

*/


#include "kasaria.h"
#include "ksr_internal.h"




void ksr_config_set_overlapping_notes(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    // This MUST be enforced if the user wants to use Kasaria for raw MIDI events
    // Without it the audio becomes trashy
    if(ksr->is_init_raw_midi_events)
    {
        ksr->overlapping_notes = true;
        return; // Ignore the user XD
    }
    
    ksr->overlapping_notes = value;
}

void ksr_config_set_audio_frame_size(Kasaria *ksr, int size)
{
    if(!ksr)
        return;

    ksr->buffer_period_size = size;
}

void ksr_config_set_amplification(Kasaria *ksr, int amplification)
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

void ksr_config_set_max_voices(Kasaria *ksr, int voices)
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

void ksr_config_set_immediate_panning(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->adjust_panning_immediately = value;
}

void ksr_config_set_mono(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    if(value)
        ksr->play_mode.encoding |= PE_MONO;
    else
        ksr->play_mode.encoding &= ~PE_MONO;
}

void ksr_config_set_fast_decay(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->fast_decay = value;
}

void ksr_config_set_antialiasing(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->antialiasing_allowed = value;
}

void ksr_config_set_pre_resample(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->pre_resampling_allowed = value;
}

void ksr_config_set_sample_rate(Kasaria *ksr, int rate)
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
}

void ksr_config_set_control_rate(Kasaria *ksr, int rate)
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
}

void ksr_config_set_default_program(Kasaria *ksr, int program)
{
    if(!ksr)
        return;

    ksr->default_program = program & 0x7f;
}

void ksr_config_set_drum_channel(Kasaria *ksr, int channel, bool enable)
{
    if(!ksr)
        return;

    channel = channel & 0x0f;

    if(enable)
        ksr->drumchannels |= (1 << channel);
    else
        ksr->drumchannels &= ~(1 << channel);
}

void ksr_config_set_quiet_channel(Kasaria *ksr, int channel, bool enable)
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

void ksr_config_set_note_skipping(Kasaria *ksr, uint8_t low_vel, uint8_t high_vel, bool enabled)
{
    if(!ksr)
        return;

    ksr->note_vel_skipping = enabled;
    ksr->low_vel_treshold  = low_vel;
    ksr->high_vel_treshold = high_vel;
}

int ksr_config_get_amplification(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return (int)(ksr->master_volume * 100.0L);
}

int ksr_config_get_max_voices(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->voices;
}

int ksr_config_get_immediate_panning(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->adjust_panning_immediately;
}

int ksr_config_get_mono(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    if(ksr->play_mode.encoding & PE_MONO)
        return 1;

    else
        return 0;
}

int ksr_config_get_fast_decay(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->fast_decay;
}

int ksr_config_get_antialiasing(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->antialiasing_allowed;
}

int ksr_config_get_pre_resample(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->pre_resampling_allowed;
}

int ksr_config_get_sample_rate(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->play_mode.rate;
}

int ksr_config_get_control_rate(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->control_rate;
}

int ksr_config_get_default_program(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->default_program;
}

void ksr_config_set_audio_compressor(Kasaria *ksr, bool enabled)
{
    if(!ksr)
        return;

    ksr->audio_compressor = enabled;
}