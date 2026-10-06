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
#include "ext_deps/log_c/log.h"



void default_compressor_settings(Kasaria *ksr)
{
    ksr->compressor_settings.envelope      = 0.0f;
    ksr->compressor_settings.gain          = 1.0f;
    ksr->compressor_settings.attack_ms     = 2.0f;
    ksr->compressor_settings.release_ms    = 80.0f;
    ksr->compressor_settings.sample_rate   = ksr->play_mode.rate;
    ksr->compressor_settings.attack_coeff  = expf(-1.0f / (ksr->compressor_settings.attack_ms * 0.001f * ksr->compressor_settings.sample_rate));
    ksr->compressor_settings.release_coeff = expf(-1.0f / (ksr->compressor_settings.release_ms * 0.001f * ksr->compressor_settings.sample_rate));
    ksr->compressor_settings.threshold     = 2000000.0f;
    ksr->compressor_settings.ratio         = 4.0f;
    ksr->compressor_settings.makeup_gain   = 1.0f;
}


void set_default_config(Kasaria *ksr)
{
    if(!ksr)
        return;
    log_trace("setting config");
    ksr->default_program        = DEFAULT_PROGRAM;
    ksr->antialiasing_allowed   = 1;
    ksr->pre_resampling_allowed = 1;
#ifdef FAST_DECAY
    ksr->fast_decay = 1;
#else
    ksr->fast_decay = 0;
#endif

    ksr->voices                        = DEFAULT_VOICES;
    ksr->play_mode.rate                = DEFAULT_RATE;
    ksr->play_mode.encoding            = 0;
    ksr->control_rate                  = CONTROLS_PER_SECOND;
    ksr->control_ratio                 = ksr->play_mode.rate / ksr->control_rate;
    ksr->drumchannels                  = DEFAULT_DRUMCHANNELS;
    ksr->quietchannels                 = 0;
    ksr->adjust_panning_immediately    = 1;
    // ksr->preload_soundfont_instruments = 1; // Unused
    ksr->buffer_period_size            = 488;
    ksr->skip_initial_midi_silence     = false;
    ksr->overlapping_notes             = true;
    ksr->audio_compressor              = true;
    ksr->midi_chunk_limit_enabled      = false;
    ksr->midi_chunk_size               = 64;
    ksr->reverb_only                   = false;
    ksr->reverb_level                  = 8.0;
    // Reverb presets I like
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_GENERIC;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_CASTLE_SHORTPASSAGE; // One of the best
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_CASTLE_LONGPASSAGE;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_SPACESTATION_SHORTPASSAGE;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_SPACESTATION_LONGPASSAGE;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_SPORT_EMPTYSTADIUM;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_SPORT_FULLSTADIUM;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_DOME_TOMB;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_PIPE_RESONANT;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_MOOD_HEAVEN;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_DRIVING_PITGARAGE;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_DRIVING_INCAR_RACER; // not my favorite but still very interesting
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_DRIVING_EMPTYGRANDSTAND;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_DRIVING_TUNNEL;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_CITY_MUSEUM;
    ksr->reverb_preset                 = KSR_REVERB_PRESET_CITY_LIBRARY;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_CITY_ABANDONED;
    //ksr->reverb_preset                 = KSR_REVERB_PRESET_DUSTYROOM;
    ksr->reverb_enabled                = true;

    ksr->chorus_enabled                = false;
    ksr->chorus_depth                  = 0.25;

    default_compressor_settings(ksr);

    ksr->low_vel_treshold  = 0;
    ksr->high_vel_treshold = 32;

    adjust_amplification(ksr, DEFAULT_AMPLIFICATION);
}


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

    
    // Prevent any stale voices from being held when changing overlapping notes
    if(ksr->is_midi_player_active)
        reset_voices(ksr);
    
    ksr->overlapping_notes = value;
}

void ksr_config_set_midi_chunk_limit(Kasaria *ksr, int size, bool enabled)
{
    if(!ksr)
        return;

    ksr->midi_chunk_size = size;
    ksr->midi_chunk_limit_enabled = enabled;
}

void ksr_config_set_audio_frame_size(Kasaria *ksr, int size)
{
    if(!ksr)
        return;

    if(size < 10)
        size = 10;

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

void ksr_config_enable_reverb(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;
    
    ksr->reverb_enabled = value;
    reset_reverb(ksr);
}

void ksr_config_set_reverb_only(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;
    
    ksr->reverb_only = value;
}

void ksr_config_set_reverb_preset(Kasaria *ksr, int preset)
{
    if(!ksr)
        return;
    
    ksr->reverb_preset = preset;
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


KasariaConfig ksr_get_config(Kasaria *ksr)
{
    KasariaConfig config;

    config.amplification           = ksr->master_volume * 100.0L;
    config.voice_limit             = ksr->voices;
    config.audio_frame_size        = ksr->buffer_period_size;
    config.sample_rate             = ksr->play_mode.rate;
    config.control_rate            = ksr->control_rate;
    config.default_program         = ksr->default_program;
    config.low_note_velocity       = ksr->low_vel_treshold;
    config.high_note_velocity      = ksr->high_vel_treshold;
    config.immediate_panning       = ksr->adjust_panning_immediately;
    config.mono_audio              = ksr->play_mode.encoding == 1;
    config.fast_decay              = ksr->fast_decay;
    config.antialiasing            = ksr->antialiasing_allowed;
    config.pre_resample            = ksr->pre_resampling_allowed;
    config.velocity_skipping       = ksr->note_vel_skipping;
    config.audio_compressor        = ksr->audio_compressor;
    config.skip_initial_silence    = ksr->skip_initial_midi_silence;
    config.allow_overlapping_notes = ksr->overlapping_notes;
    config.midi_chunk_limiter      = ksr->midi_chunk_limit_enabled;
    config.midi_chunk_size         = ksr->midi_chunk_size;

    return config;
}

void ksr_set_config(Kasaria *ksr, KasariaConfig config)
{
    if(!ksr)
        return;

    ksr->master_volume              = config.amplification / 100.0L;
    ksr->voices                     = config.voice_limit;
    ksr->buffer_period_size         = config.audio_frame_size;
    ksr->play_mode.rate             = config.sample_rate;
    ksr->control_rate               = config.control_rate;
    ksr->default_program            = config.default_program;
    ksr->low_vel_treshold           = config.low_note_velocity;
    ksr->high_vel_treshold          = config.high_note_velocity;
    ksr->adjust_panning_immediately = config.immediate_panning;
    ksr->play_mode.encoding         = config.mono_audio ? 1 : 0;
    ksr->fast_decay                 = config.fast_decay;
    ksr->antialiasing_allowed       = config.antialiasing;
    ksr->pre_resampling_allowed     = config.pre_resample;
    ksr->note_vel_skipping          = config.velocity_skipping;
    ksr->audio_compressor           = config.audio_compressor;
    ksr->skip_initial_midi_silence  = config.skip_initial_silence;
    ksr->midi_chunk_limit_enabled   = config.midi_chunk_limiter;
    ksr->midi_chunk_size            = config.midi_chunk_size;

    // Again don't let the user disable overlapping notes if the synth is initialized for raw MIDI events
    if(ksr->audio_init_scope == RAW_MIDI_EVENTS)
        ksr->overlapping_notes = true;
    else
        ksr->overlapping_notes          = config.allow_overlapping_notes;
}