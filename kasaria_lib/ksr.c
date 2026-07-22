/*

TiMidity -- Experimental MIDI to WAVE converter
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








#include <math.h>
#include <stdlib.h>
#include <string.h>



#include "ksr_internal.h"






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
    ksr->dynamic_loading                           = 0;
    ksr->voices                                    = DEFAULT_VOICES;
    ksr->play_mode.rate                            = DEFAULT_RATE;
    ksr->play_mode.encoding                        = 0;
    ksr->control_rate                              = CONTROLS_PER_SECOND;
    ksr->control_ratio                             = ksr->play_mode.rate / ksr->control_rate;
    ksr->drumchannels                              = DEFAULT_DRUMCHANNELS;
    ksr->quietchannels                             = 0;
    ksr->adjust_panning_immediately                = 1;

    ksr->compressor_settings.envelope              = 0.0f;
    ksr->compressor_settings.gain                  = 1.0f;
    ksr->compressor_settings.limiter_attack_ms     = 2.0f;
    ksr->compressor_settings.limiter_release_ms    = 80.0f;
    ksr->compressor_settings.limiter_sample_rate   = ksr->play_mode.rate;
    ksr->compressor_settings.limiter_attack_coeff  = expf(-1.0f / (ksr->compressor_settings.limiter_attack_ms * 0.001f * ksr->compressor_settings.limiter_sample_rate));
    ksr->compressor_settings.limiter_release_coeff = expf(-1.0f / (ksr->compressor_settings.limiter_release_ms * 0.001f * ksr->compressor_settings.limiter_sample_rate));
    ksr->compressor_settings.limiter_threshold     = 2000000.0f;
    ksr->compressor_settings.limiter_ratio         = 4.0f;
    ksr->compressor_settings.limiter_makeup_gain   = 1.0f;

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

    const char *ext = strrchr(filename, '.');
    if(ext)
        ext++; // skip the dot

    if(strcasecmp(ext, "sfz") == 0)
    {
        printf("SFZ Support not implelented!\n");
        return 0;
    }

    if(strcasecmp(ext, "sf2") != 0)
    {
        printf("Unsuported soundfont format!\n");
        return 0;
    }

    if(ksr->sf_loaded)
        free_soundfont(&ksr->sf_info);

    if(load_soundfont(&ksr->sf_info, filename) != 0)
        return 0;

    strncpy(ksr->sf_filename, filename, sizeof(ksr->sf_filename) - 1);
    ksr->sf_filename[sizeof(ksr->sf_filename) - 1] = '\0';
    ksr->sf_loaded                                 = 1;

    return 1;
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