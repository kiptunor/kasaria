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








#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>




#include "ext_deps/log_c/log.h"

#include "ksr_internal.h"









Kasaria *raw_midi_event_ctx;



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

void ksr_print_config(Kasaria *ksr)
{
    log_debug("fast_decay                 = %d",  ksr->fast_decay);
    log_debug("antialiasing_allowed       = %d",  ksr->antialiasing_allowed);
    log_debug("pre_resampling_allowed     = %d",  ksr->pre_resampling_allowed);
    log_debug("sample_rate                = %ld", ksr->play_mode.rate);
    log_debug("control_rate               = %ld", ksr->control_rate);
    log_debug("control_ratio              = %ld", ksr->control_ratio);
    log_debug("master_volume              = %f",  ksr->master_volume);
    log_debug("drum_channels              = %ld", ksr->drumchannels);
    log_debug("quiet_channels             = %ld", ksr->quietchannels);
    log_debug("voice_limit                = %d",  ksr->voices);
    log_debug("adjust_panning_immediately = %d",  ksr->adjust_panning_immediately);
    log_debug("skip_initial_silence       = %d",  ksr->skip_initial_midi_silence);
    

    printf("\n------------- [Filters and audio DSP Effects] -------------\n\n");

    log_debug("note_vel_skipping -> enabled           = %d", ksr->note_vel_skipping);
    log_debug("note_vel_skipping -> low_vel_treshold  = %u", ksr->low_vel_treshold);
    log_debug("note_vel_skipping -> high_vel_treshold = %u\n\n", ksr->high_vel_treshold);

    log_debug("compressor -> envelope      = %f", ksr->compressor_settings.envelope);
    log_debug("compressor -> gain          = %f", ksr->compressor_settings.gain);
    log_debug("compressor -> threshold     = %f", ksr->compressor_settings.threshold);
    log_debug("compressor -> ratio         = %f", ksr->compressor_settings.ratio);
    log_debug("compressor -> attack_coeff  = %f", ksr->compressor_settings.attack_coeff);
    log_debug("compressor -> release_coeff = %f", ksr->compressor_settings.release_coeff);
    log_debug("compressor -> makeup_gain   = %f", ksr->compressor_settings.makeup_gain);
    log_debug("compressor -> attack_ms     = %f", ksr->compressor_settings.attack_ms);
    log_debug("compressor -> release_ms    = %f", ksr->compressor_settings.release_ms);
    log_debug("compressor -> sample_rate   = %f", ksr->compressor_settings.sample_rate);
    log_debug("compressor -> makeup_gain   = %f", ksr->compressor_settings.makeup_gain);
}


void init_internal_state(Kasaria *ksr)
{
    ksr->is_midi_loaded = false;
    ksr->is_midi_ended  = false;
    
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
    // ksr->dynamic_loading                           = 0;
    ksr->voices                        = DEFAULT_VOICES;
    ksr->play_mode.rate                = DEFAULT_RATE;
    ksr->play_mode.encoding            = 0;
    ksr->control_rate                  = CONTROLS_PER_SECOND;
    ksr->control_ratio                 = ksr->play_mode.rate / ksr->control_rate;
    ksr->drumchannels                  = DEFAULT_DRUMCHANNELS;
    ksr->quietchannels                 = 0;
    ksr->adjust_panning_immediately    = 1;
    ksr->preload_soundfont_instruments = 1;
    ksr->buffer_period_size            = 488;
    ksr->skip_initial_midi_silence     = false;

    ksr->is_midi_player_paused = false;
    ksr->is_midi_player_active = false;
    ksr->wall_clock_last_ns = 0;

    default_compressor_settings(ksr);

    ksr->low_vel_treshold  = 0;
    ksr->high_vel_treshold = 32;

    // This might be temporary
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

    init_internal_state(ksr);

    init_tables(ksr);
    reset_midi(ksr);
    adjust_amplification(ksr, DEFAULT_AMPLIFICATION);

    // ulog_color_config(1);
    //ulog_topic_add("SF2", ULOG_OUTPUT_ALL, ULOG_LEVEL_TRACE);
    //ulog_topic_add("MIDI Loader", ULOG_OUTPUT_ALL, ULOG_LEVEL_TRACE);
    log_info("Kasaria Init\n\n");

    return ksr;
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
    // ksr->dynamic_loading            = 0;
    ksr->voices                        = DEFAULT_VOICES;
    ksr->play_mode.rate                = DEFAULT_RATE;
    ksr->play_mode.encoding            = 0;
    ksr->control_rate                  = CONTROLS_PER_SECOND;
    ksr->control_ratio                 = ksr->play_mode.rate / ksr->control_rate;
    ksr->drumchannels                  = DEFAULT_DRUMCHANNELS;
    ksr->quietchannels                 = 0;
    ksr->adjust_panning_immediately    = 1;
    ksr->preload_soundfont_instruments = 1;
    ksr->buffer_period_size            = 488;
    ksr->skip_initial_midi_silence     = false;
    ksr->current_midi_player_position  = 0.0f;

    default_compressor_settings(ksr);

    // This might be temporary
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

    adjust_amplification(ksr, DEFAULT_AMPLIFICATION);
}

KasariaConfig ksr_get_config(Kasaria *ksr)
{
    KasariaConfig config;

    config.amplification        = ksr->master_volume * 100.0L;
    config.voice_limit          = ksr->voices;
    config.audio_frame_size     = ksr->buffer_period_size;
    config.sample_rate          = ksr->play_mode.rate;
    config.control_rate         = ksr->control_rate;
    config.default_program      = ksr->default_program;
    config.low_note_velocity    = ksr->low_vel_treshold;
    config.high_note_velocity   = ksr->high_vel_treshold;
    config.immediate_panning    = ksr->adjust_panning_immediately;
    config.mono_audio           = ksr->play_mode.encoding == 1;
    config.fast_decay           = ksr->fast_decay;
    config.antialiasing         = ksr->antialiasing_allowed;
    config.pre_resample         = ksr->pre_resampling_allowed;
    config.velocity_skipping    = ksr->note_vel_skipping;
    config.audio_compressor     = ksr->audio_compressor;
    config.skip_initial_silence = ksr->skip_initial_midi_silence;
    
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
}

int ksr_load_soundfont_file(Kasaria *ksr, const char *filename, bool preload_instruments)
{
    if(!ksr || !filename)
        return 0;

    FILE *fp;
    const char *ext;
    
    if(!ksr || !filename)
        return 0;
    
    log_debug("Loading soundfont file: %s", filename);
    
    ext = strrchr(filename, '.');
    
    if(ext)
        ext++;
    
    if(!ext || strcasecmp(ext, "sf2") != 0)
    {
        log_error("Unsupported soundfont format!");
        return 0;
    }
    
    fp = fopen(filename, "rb");
    
    if(!fp)
    {
        log_error("Can't open soundfont file: %s", filename);
        return 0;
    }
    
    if(ksr->sf_loaded)
    {
        free_soundfont(ksr->sf_info);
        ksr->sf_loaded = 0;
    }
    
    if(!ksr->sf_info)
    {
        ksr->sf_info = safe_malloc(sizeof(SFInfo));
    
        if(!ksr->sf_info)
        {
            log_error("Failed to allocate SFInfo");
            fclose(fp);
            return 0;
        }
    
        memset(ksr->sf_info, 0, sizeof(SFInfo));
    }
    if(load_soundfont(ksr->sf_info, fp) != 0)
    {
        fclose(fp);
        return 0;
    }
    
    fclose(fp);

    strncpy(ksr->sf_filename, filename, sizeof(ksr->sf_filename) - 1);
    
    ksr->sf_filename[sizeof(ksr->sf_filename) - 1] = '\0';
    
    ksr->sf_loaded = 1;
    
    if(preload_instruments)
        preload_soundfont_instruments(ksr);
    
    return 1;
}

void ksr_set_audio_frame_size(Kasaria *ksr, int size)
{
    if(!ksr)
        return;

    ksr->buffer_period_size = size;
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

void ksr_set_immediate_panning(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->adjust_panning_immediately = value;
}

void ksr_set_mono(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    if(value)
        ksr->play_mode.encoding |= PE_MONO;
    else
        ksr->play_mode.encoding &= ~PE_MONO;
}

void ksr_set_fast_decay(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->fast_decay = value;
}

void ksr_set_antialiasing(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->antialiasing_allowed = value;
}

void ksr_set_pre_resample(Kasaria *ksr, bool value)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    ksr->pre_resampling_allowed = value;
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
}

void ksr_set_default_program(Kasaria *ksr, int program)
{
    if(!ksr)
        return;

    ksr->default_program = program & 0x7f;
}

void ksr_set_drum_channel(Kasaria *ksr, int channel, bool enable)
{
    if(!ksr)
        return;

    channel = channel & 0x0f;

    if(enable)
        ksr->drumchannels |= (1 << channel);
    else
        ksr->drumchannels &= ~(1 << channel);
}

void ksr_set_quiet_channel(Kasaria *ksr, int channel, bool enable)
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

void ksr_set_note_velocity_skipping(Kasaria *ksr, uint8_t low_vel, uint8_t high_vel, bool enabled)
{
    if(!ksr)
        return;

    ksr->note_vel_skipping = enabled;
    ksr->low_vel_treshold  = low_vel;
    ksr->high_vel_treshold = high_vel;
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

/*
int ksr_set_default_instrument(Kasaria *ksr, char *filename)
{
    if(!ksr || !filename)
        return 0;

    reset_voices(ksr);
    if(set_default_instrument(ksr, filename) == 0)
        return 1;

    return 0;
}
*/

void ksr_free_default_instrument(Kasaria *ksr)
{
    if(!ksr)
        return;

    reset_voices(ksr);
    free_default_instrument(ksr);
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

/*
int ksr_get_dynamic_instrument_load(Kasaria *ksr)
{
    if(!ksr)
        return 0;

    return ksr->dynamic_loading;
}
*/

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

void audio_callback(ma_device *dev, void *out, const void *in, ma_uint32 frames)
{
    // Sooo simple
    // XD
    ksr_render_float(raw_midi_event_ctx, out, frames);
}

int ksr_init_audio(Kasaria *ksr, int init_scope)
{
    if(!ksr)
        return 1;

    if(ksr->is_audio_init)
    {
        log_warn("Audio device is already initialized");
        return 0;
    }

    ksr->audio_init_scope = init_scope;

    ksr->dev_config                    = ma_device_config_init(ma_device_type_playback);

    ksr->dev_config.playback.format    = ma_format_f32;
    ksr->dev_config.playback.channels  = 2;
    ksr->dev_config.sampleRate         = ksr->play_mode.rate;
    ksr->dev_config.periodSizeInFrames = ksr->buffer_period_size;

    if(ksr->audio_init_scope == INTERNAL_MIDI_PLAYER)
        ksr->dev_config.dataCallback = _internal_midi_player_cb;

    if(ksr->audio_init_scope == RAW_MIDI_EVENTS)
    {
        raw_midi_event_ctx = ksr;
        ksr->dev_config.dataCallback = audio_callback;
        ksr->is_init_raw_midi_events = true;
    }

    if(ma_device_init(NULL, &ksr->dev_config, &ksr->audio_device) != MA_SUCCESS)
    {
        log_error("Failed to open audio device");
        return 1;
    }

    ksr->is_audio_init = true;

    log_info("Audio device initialized");

    return 0;
}

void ksr_set_audio_compressor(Kasaria *ksr, bool enabled)
{
    if(!ksr)
        return;
    
    ksr->audio_compressor = enabled;
}

int ksr_start_audio(Kasaria *ksr)
{
    if(!ksr)
        return 1;

    if(ksr->is_init_raw_midi_events)
    {
        if(ma_device_start(&raw_midi_event_ctx->audio_device) != MA_SUCCESS)
        {
            log_error("Failed to start audio device for raw MIDI events");
            return 1;
        }
        log_info("Audio device started for raw MIDI events");
        return 0;
    }
        

    if(ma_device_start(&ksr->audio_device) != MA_SUCCESS)
    {
        log_error("Failed to start audio device for internal midi player");
        return 1;
    }

    log_info("Audio device started for internal midi player");
    //ksr->is_audio_started = true;
    
    return 0;
}

int ksr_stop_audio(Kasaria *ksr)
{
    if(!ksr)
        return 1;

    if(ksr->is_init_raw_midi_events)
    {
        if(ma_device_stop(&raw_midi_event_ctx->audio_device) != MA_SUCCESS)
        {
            log_error("Failed to stop audio device for raw MIDI events");
            return 1;
        }
        return 0;
    }
        

    if(ma_device_stop(&ksr->audio_device) != MA_SUCCESS)
    {
        log_error("Failed to stop audio device for internal midi player");
        return 1;
    }

    //ksr->is_audio_started = true;
    
    return 0;
}

// And this one too (It may prob dissapear)
void ksr_preload_instruments(Kasaria *ksr)
{
    MidiEvent *e = ksr->event_list;
    int        i;
    for(i = 0; i < ksr->events_midi; i++)
    {
        if(e[i].type == ME_PROGRAM)
        {
            int ch = e[i].channel;
            if(!ISDRUMCHANNEL(ksr, ch))
            {
                int bank = ksr->channel[ch].bank;
                int prog = e[i].key;
                // trigger load if not already loaded
                if(ksr->tonebank[bank] && ksr->tonebank[bank]->tone[prog].name && !ksr->tonebank[bank]->tone[prog].instrument)
                {
                    ksr->tonebank[bank]->tone[prog].instrument = MAGIC_LOAD_INSTRUMENT;
                    load_missing_instruments(ksr);
                }
            }
        }
    }
}

void ksr_shutdown(Kasaria *ksr)
{
    if(!ksr)
        return;

    log_info("Kasaria shutdown...");

    free_sf2_sample_cache();

    if(ksr->is_audio_init)
        ma_device_uninit(&ksr->audio_device);

    reset_midi(ksr);
    ksr_unload_midi(ksr); // This also calls reset_midi()
    free_default_instrument(ksr);
    free_tables(ksr);
    memset(ksr, 0, sizeof(Kasaria));
    free(ksr);
}