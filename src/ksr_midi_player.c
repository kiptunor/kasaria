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









/*
    TODO: Split the basic synth function into another source file (ksr_synth_base.c)
*/




#include <stdio.h>
#include <stdlib.h>


#ifndef _WIN32_WCE
    #include <string.h>
#endif

#include "ext_deps/ulog/src/ulog.h"

#define MINIAUDIO_IMPLEMENTATION
#include "ext_deps/miniaudio/miniaudio.h"

#include "ksr_internal.h"
#include "ksr_sf2.h"
#include "kasaria.h"














Kasaria *async_midi_player; // Required only for the async MIDI player




void _internal_midi_player_cb(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    (void)pInput;

    if(async_midi_player->is_midi_ended)
    {
        memset(pOutput, 0, frameCount * 2 * sizeof(float));
        return;
    }

    float  raw_audio[async_midi_player->buffer_period_size * 2];
    float *out       = (float *)pOutput;
    int    remaining = frameCount;

    while(remaining > 0)
    {
        int chunk    = remaining > async_midi_player->buffer_period_size ? async_midi_player->buffer_period_size : remaining;
        int rendered = ksr_play_midi_raw(async_midi_player, AUDIO_FLOAT, (uint8_t *)raw_audio, chunk); // Play MIDI in realtime and get the generated audio (as Raw PCM)

        if(!rendered)
        {
            memset(out, 0, remaining * 2 * sizeof(float));
            return;
        }

        for(int i = 0; i < chunk; i++)
        {
            out[i * 2 + 0] = raw_audio[i * 2 + 0];
            out[i * 2 + 1] = raw_audio[i * 2 + 1];
        }
        out       += chunk * 2;
        remaining -= chunk;
    }
}

static void seek_forward(Kasaria *ksr, long until_time)
{
    reset_voices(ksr);
    while(ksr->current_event->time < until_time)
    {
        switch(ksr->current_event->type)
        {
            // All notes stay off. Just handle the parameter changes.

        case ME_PITCH_SENS:
            ksr->channel[ksr->current_event->channel].pitchsens   = ksr->current_event->key;
            ksr->channel[ksr->current_event->channel].pitchfactor = 0;
            break;

        case ME_PITCHWHEEL:
            ksr->channel[ksr->current_event->channel].pitchbend   = ksr->current_event->key + ksr->current_event->vel * 128;
            ksr->channel[ksr->current_event->channel].pitchfactor = 0;
            break;

        case ME_MAINVOLUME:
            ksr->channel[ksr->current_event->channel].volume = ksr->current_event->key;
            break;

        case ME_PAN:
            ksr->channel[ksr->current_event->channel].panning = ksr->current_event->key;
            break;

        case ME_EXPRESSION:
            ksr->channel[ksr->current_event->channel].expression = ksr->current_event->key;
            break;

        case ME_PROGRAM:
            if(ISDRUMCHANNEL(ksr, ksr->current_event->channel))
                // Change drum set
                ksr->channel[ksr->current_event->channel].bank = ksr->current_event->key;
            else
                ksr->channel[ksr->current_event->channel].program = ksr->current_event->key;
            break;

        case ME_SUSTAIN:
            ksr->channel[ksr->current_event->channel].sustain = ksr->current_event->key;
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
                ksr->channel[ksr->current_event->channel].bank = ksr->current_event->key;
            break;

        case ME_EOT:
            ksr->current_sample = ksr->current_event->time;
            return;
        }
        ksr->current_event++;
    }
    // current_sample=current_event->time;
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

static void play_midi(Kasaria *ksr, MidiEvent *e)
{
    if(e)
    {
        if(ISQUIETCHANNEL(ksr, e->channel))
            return;

        switch(e->type)
        {

            // Effects affecting a single note

        case ME_NOTEON:
            if(!(e->vel)) // Velocity 0?
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

            // Effects affecting a single channel

        case ME_PITCH_SENS:
            ksr->channel[e->channel].pitchsens   = e->key;
            ksr->channel[e->channel].pitchfactor = 0;
            break;

        case ME_PITCHWHEEL:
            ksr->channel[e->channel].pitchbend   = e->key + e->vel * 128;
            ksr->channel[e->channel].pitchfactor = 0;
            // Adjust pitch for notes already playing
            adjust_pitchbend(ksr, e->channel);
            break;

        case ME_MAINVOLUME:
            ksr->channel[e->channel].volume = e->key;
            adjust_volume(ksr, e->channel);
            break;

        case ME_PAN:
            ksr->channel[e->channel].panning = e->key;
            if(ksr->adjust_panning_immediately)
                adjust_panning(ksr, e->channel);

            break;

        case ME_EXPRESSION:
            ksr->channel[e->channel].expression = e->key;
            adjust_volume(ksr, e->channel);
            break;

        case ME_PROGRAM:
            if(ISDRUMCHANNEL(ksr, e->channel))
            {
                // Change drum set
                if(ksr->drumset[e->key])
                    ksr->channel[e->channel].bank = e->key;
            }
            else
                ksr->channel[e->channel].program = e->key;

            break;

        case ME_SUSTAIN:
            ksr->channel[e->channel].sustain = e->key;
            if(!e->key)
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
                if(ksr->tonebank[e->key])
                    ksr->channel[e->channel].bank = e->key;
            }
            break;
        }
    }
}

// Adapted from ReadMidiText function in gspmidi.cpp
static void read_midi_text(Kasaria *ksr)
{
    u_long buff = 0;
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
                read                          = fread(ksr->song_copyright, 1, buff, ksr->fp_midi);
                *(ksr->song_copyright + read) = '\0';
            }
            else
                fseek(ksr->fp_midi, buff, SEEK_CUR);
            break;
        case 0x03FF00: // Track
            buff = (buff & 0xFF000000) >> 24;
            if(!strlen(ksr->song_title))
            {
                read                      = fread(ksr->song_title, 1, buff, ksr->fp_midi);
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

void ksr_channel_note_on(Kasaria *ksr, u_char channel, u_char note, u_char velocity)
{
    MidiEvent ev;
    if(!ksr)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.channel = channel & 0x0f;
    ev.type    = ME_NOTEON;
    ev.key     = note & 0x7f;
    ev.vel     = velocity & 0x7f;
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
    ev.key     = note & 0x7f;
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
    ev.key     = note & 0x7f;
    ev.vel     = velocity & 0x7f;
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
    ev.key     = volume & 0x7f;
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
    ev.key     = pan & 0x7f;
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
    ev.key     = expression & 0x7f;
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
    ev.key     = sustain & 0x7f;
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
    ev.key     = pitch & 0x7f;
    ev.vel     = (pitch >> 7) & 0x7f;
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
    ev.key     = range & 0x7f;
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
    ev.key     = program & 0x7f;
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
    ev.key     = bank & 0x7f;
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

void ksr_write_midi_ev(Kasaria *ksr, u_char byte1, u_char byte2, u_char byte3)
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

void ksr_write_midi_ev_packed(Kasaria *ksr, u_long data)
{
    u_char byte1 = data & 0xff;
    u_char byte2 = (data >> 8) & 0x7f;
    u_char byte3 = (data >> 16) & 0x7f;
    if(!ksr)
        return;

    ksr_write_midi_ev(ksr, byte1, byte2, byte3);
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
    ksr->is_midi_loaded = true;
    ulog_info("Loaded MIDI: %s", ksr->last_smf);
    
    return 1;
}

void ksr_unload_midi(Kasaria *ksr)
{
    if(!ksr)
        return;

    ulog_debug("Unload MIDI");
    
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

int ksr_play_midi_raw(Kasaria *ksr, long type, u_char *buffer, long count)
{
    int convert;
    if(!ksr || !buffer || (type > AUDIO_ULAW || type < AUDIO_CHAR))
        return 0;

    while(count > 0)
    {
        // Handle all events that should happen at this time
        while(ksr->current_event->time <= ksr->current_sample)
        {
            if(ksr->current_event->type == ME_EOT)
            {
                ksr->is_midi_ended = true;
                break;
            }

            play_midi(ksr, ksr->current_event);
            ksr->current_event++;
        }

        // ksr->current_midi_player_position = (double)ksr->current_event->time / (double)ksr->play_mode.rate;
        // ksr->current_midi_player_position = (double)ksr->current_sample / (double)ksr->play_mode.rate;
        ksr->current_midi_player_position = (double)(ksr->current_sample - ksr->dev_config.periodSizeInFrames) / (double)ksr->play_mode.rate; // idfk if it's correct
        convert = ksr->current_event->time - ksr->current_sample;
        if(convert > count || convert <= 0) // I could prob count the number of events here ??
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
        case AUDIO_INT24:
            ksr_render_int24(ksr, (int24 *)buffer, convert);
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
            ksr_render_double(ksr, (f64 *)buffer, convert);
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
        count               -= convert;
    }
    return 1;
}

int ksr_play_midi(Kasaria *ksr, bool wait_midi_ending)
{
    if(!ksr)
        return 0;

    if(!ksr->is_audio_init)
    {
        ulog_error("Audio device not initialized");
        return 0;
    }

    if(!ksr->is_midi_loaded)
    {
        ulog_error("No MIDI File was loaded!");
        return 0;
    }

    async_midi_player = ksr; // Get the current synth context for the async player

    ulog_info("Starting MIDI playback...");
    ma_device_start(&async_midi_player->audio_device);

    // Wait for the MIDI playback to finish (This is required if this function is called on the main thread so it won't exit)
    if(wait_midi_ending)
        while(!ksr->is_midi_ended)
            ma_sleep(1000); // This may probably extend the position

    // In any case the while loop above is not used
    if(ksr->is_midi_ended)
        ma_device_stop(&ksr->audio_device);
    
    return 1;
}

bool ksr_is_midi_ended(Kasaria *ksr)
{
    return ksr->is_midi_ended;
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