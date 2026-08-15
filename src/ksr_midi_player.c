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











#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define __USE_POSIX199309
#include <time.h>


#ifndef _WIN32_WCE
    #include <string.h>
#endif

#include "ext_deps/log_c/log.h"

#define MINIAUDIO_IMPLEMENTATION
#include "ext_deps/miniaudio/miniaudio.h"

#include "ksr_internal.h"
#include "ksr_sf2.h"
#include "kasaria.h"














Kasaria *async_midi_player; // Required only for the async MIDI player




void _internal_midi_player_cb(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    (void)pInput;
    
    // Paused / ended: never advance the sequencer or position, just output silence.
    if(!async_midi_player ||
           !async_midi_player->is_midi_loaded ||
           (!async_midi_player->stream && !async_midi_player->current_event) ||
           async_midi_player->is_midi_ended ||
           async_midi_player->is_midi_player_paused)
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
        int rendered = ksr_play_midi_raw(async_midi_player, AUDIO_FLOAT, (uint8_t *)raw_audio, chunk);

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
        //log_debug("play_midi: type=%d chan=%d key=%d vel=%d time=%ld", e->type, e->channel, e->key, e->vel, e->time);
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

void ksr_unmap_file(FileMap *m)
{
    if(!m)
        return;
    
    if(m->data && m->data != MAP_FAILED)
        munmap(m->data, m->len);
    
    if(m->fd >= 0)
        close(m->fd);
    
    m->data = NULL;
    m->fd   = -1;
}

static u32 read_vlq(const u_char **p)
{
    u32 v = 0;
    for(int i = 0; i < 4; i++)
    {
        u_char b = *(*p)++;
        v = (v << 7) | (b & 0x7f);
        if(!(b & 0x80))
            break;
    }
    return v;
}

static int stream_track_event(MidiStream *s, int t, MidiEvent *ev)
{
    const u_char **p = &s->cur[t];
    const u_char *end = s->end[t];
    long at = s->abs_tick[t];

    for(;;)
    {
        if(*p >= end) return 0;
        at += read_vlq(p);
        u_char me = *(*p)++;

        if(me == 0xF0 || me == 0xF7)              /* SysEx: skip */
        {
            *p += read_vlq(p);
            
            if(*p > end)
                *p = end;
            continue;
        }
        if(me == 0xFF)                            /* meta */
        {
            u_char type = *(*p)++;
            u32 len = read_vlq(p);
            
            if(type == 0x2F)
                return 0;            /* EndOfTrack */
            
            if(type == 0x51 && len == 3)          /* tempo */
            {
                u_char a = *(*p)++, b = *(*p)++, c = *(*p)++;
                ev->time = at;
                ev->type = ME_TEMPO;
                ev->channel = c;                  /* low byte */
                ev->key     = a;                  /* high byte */
                ev->vel     = b;                  /* mid byte */
                s->abs_tick[t] = at;
                return 1;
            }
            *p += len;
            if(*p > end) *p = end;
            continue;
        }

        u_char a, b;
        
        if(me & 0x80)                             /* new status byte */
        {
            s->lastchan[t]   = me & 0x0F;
            s->laststatus[t] = (me >> 4) & 0x07;
            if(*p >= end)
                return 0;
            a = *(*p)++ & 0x7F;
        }
        else                                      /* running status */
        {
            if(*p >= end) return 0;
            a = me & 0x7F;
        }

        switch(s->laststatus[t])
        {
        case 0: case 1: case 2: case 6:           /* 2 data bytes */
        
            if(*p >= end)
                return 0;
            
            b = *(*p)++ & 0x7F;
            ev->time = at;
            ev->channel = s->lastchan[t];
            ev->key  = a;
            ev->vel  = b;
            ev->type = s->laststatus[t] == 0 ? ME_NOTEOFF :
                       s->laststatus[t] == 1 ? ME_NOTEON :
                       s->laststatus[t] == 2 ? ME_KEYPRESSURE : ME_PITCHWHEEL;
            s->abs_tick[t] = at;
            return 1;

        case 4:                                   /* program change: 1 byte */
            ev->time = at;
            ev->channel = s->lastchan[t];
            ev->type = ME_PROGRAM;
            ev->key  = a;
            ev->vel  = 0;
            s->abs_tick[t] = at;
            return 1;

        case 5:                                   /* channel pressure: dropped */
            continue;

        case 3:                                   /* control change remap */
        {
            if(*p >= end) return 0;
            b = *(*p)++ & 0x7F;
            int control = 255, chan = s->lastchan[t];
            switch(a)
            {
            case 7:
                control = ME_MAINVOLUME;
            break;
            case 10:
                control = ME_PAN;
            break;
            case 11:
                control = ME_EXPRESSION;
            break;
            case 64:
                control = ME_SUSTAIN; b = (b >= 64);
            break;
            case 120:
                control = ME_ALL_SOUNDS_OFF;
            break;
            case 121:
                control = ME_RESET_CONTROLLERS;
            break;
            case 123:
                control = ME_ALL_NOTES_OFF;
            break;
            case 126:
                control = ME_MONO;
            break;
            case 127:
                control = ME_POLY;
            break;
            case 0:
                control = ME_TONE_BANK;
            break;
            case 32:  break;
            case 100:
                s->nrpn[t] = 0;
                s->rpn_msb[t][chan] = b;
            break;
            case 101:
                s->nrpn[t] = 0;
                s->rpn_lsb[t][chan] = b;
            break;
            case 99:
                s->nrpn[t] = 1;
                s->rpn_msb[t][chan] = b;
            break;
            case 98:
                s->nrpn[t] = 1;
                s->rpn_lsb[t][chan] = b;
                break;
            case 6:
                if(s->nrpn[t])
                    break;
                
                switch((s->rpn_msb[t][chan] << 8) | s->rpn_lsb[t][chan])
                {
                case 0x0000:
                    control = ME_PITCH_SENS;
                    break;
                case 0x7F7F:
                    ev->time    = at;
                    ev->channel = chan;
                    ev->type    = ME_PITCH_SENS;
                    ev->key     = 2; ev->vel = 0;
                    s->abs_tick[t] = at;
                    return 1;
                default: break;
                }
                break;
            default: break;
            }
            if(control != 255)
            {
                ev->time = at;
                ev->channel = chan;
                ev->type = control;
                ev->key = b; ev->vel = 0;
                s->abs_tick[t] = at;
                return 1;
            }
            continue;
        }
        default: continue;
        }
    }
}

static int stream_groom(Kasaria *ksr, MidiEvent *raw)
{
    MidiStream *s = ksr->stream;
    long dt, samples_to_do;
    int skip_this_event = 0;

    if(raw->type == ME_TEMPO)
        skip_this_event = 1;
    else if(ISQUIETCHANNEL(ksr, raw->channel))
        skip_this_event = 1;
    else switch(raw->type)
    {
    case ME_PROGRAM:
        if(ISDRUMCHANNEL(ksr, raw->channel))
        {
            long new_value = ksr->drumset[raw->key] ? raw->key : (raw->key = 0);
            if(s->current_set[raw->channel] != new_value)
                s->current_set[raw->channel] = new_value;
            else
                skip_this_event = 1;
        }
        else
        {
            long new_value = raw->key;
            if(s->current_program[raw->channel] != SPECIAL_PROGRAM && s->current_program[raw->channel] != new_value)
                s->current_program[raw->channel] = new_value;
            else
                skip_this_event = 1;
        }
        break;

    case ME_NOTEON:
        if(s->counting_time)
            s->counting_time = 1;
        
        if(ISDRUMCHANNEL(ksr, raw->channel) && ksr->drumset[s->current_set[raw->channel]])
        {
            if(!(ksr->drumset[s->current_set[raw->channel]]->tone[raw->key].instrument))
                ksr->drumset[s->current_set[raw->channel]]->tone[raw->key].instrument = MAGIC_LOAD_INSTRUMENT;
        }
        else if(ksr->tonebank[s->current_bank[raw->channel]])
        {
            if(s->current_program[raw->channel] == SPECIAL_PROGRAM)
                break;
            if(!(ksr->tonebank[s->current_bank[raw->channel]]->tone[s->current_program[raw->channel]].instrument))
                ksr->tonebank[s->current_bank[raw->channel]]->tone[s->current_program[raw->channel]].instrument = MAGIC_LOAD_INSTRUMENT;
        }
        break;

    case ME_TONE_BANK:
        if(ISDRUMCHANNEL(ksr, raw->channel)) { skip_this_event = 1; break; }
        {
            long new_value = ksr->tonebank[raw->key] ? raw->key : (raw->key = 0);
            if(s->current_bank[raw->channel] != new_value)
                s->current_bank[raw->channel] = new_value;
            else
                skip_this_event = 1;
        }
        break;
    }

    dt = raw->time - s->prev_tick;
    if(dt && !s->counting_time)
    {
        samples_to_do = ksr->sample_increment * dt;
        s->sample_cum += ksr->sample_correction * dt;
        if(s->sample_cum >> 16)
        {
            samples_to_do += (s->sample_cum >> 16);
            s->sample_cum &= 0xFFFF;
        }
        s->st += samples_to_do;
    }
    else if(s->counting_time == 1)
        s->counting_time = 0;

    if(raw->type == ME_TEMPO)
        compute_sample_increment(ksr, raw->channel + raw->vel * 256 + raw->key * 65536, s->division);

    s->prev_tick = raw->time;

    if(!skip_this_event)
    {
        s->ev = *raw;
        s->ev.time = s->st;
        return 1;
    }
    return 0;
}

static u32 be32(const u_char *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void heap_sift_down(MidiStream *s, unsigned pos)
{
    unsigned n = s->heap_size;
    for(;;)
    {
        unsigned l = 2 * pos + 1, r = l + 1, m = pos;
        if(l < n && s->pending[s->heap[l]].time < s->pending[s->heap[m]].time)
            m = l;
        
        if(r < n && s->pending[s->heap[r]].time < s->pending[s->heap[m]].time)
            m = r;
        
        if(m == pos)
            break;
        
        unsigned tmp = s->heap[pos]; s->heap[pos] = s->heap[m]; s->heap[m] = tmp;
        pos = m;
    }
}

static void heap_sift_up(MidiStream *s, unsigned pos)
{
    while(pos > 0)
    {
        unsigned par = (pos - 1) / 2;
        
        if(s->pending[s->heap[par]].time <= s->pending[s->heap[pos]].time)
            break;
        
        unsigned tmp = s->heap[par]; s->heap[par] = s->heap[pos]; s->heap[pos] = tmp;
        pos = par;
    }
}

static void stream_rewind(Kasaria *ksr)
{
    MidiStream *s = ksr->stream;
    const u_char *p = s->track_start;
    s->heap_size = 0;
    
    for(unsigned t = 0; t < s->ntrks; t++)
    {
        while(p + 8 <= s->data + s->len && memcmp(p, "MTrk", 4))
            p++;
        
        if(p + 8 > s->data + s->len)
        {
            s->alive[t] = 0;
            continue;
        }
        
        s->cur[t] = p + 8;
        u32 tlen  = be32(p + 4);
        s->end[t] = p + 8 + tlen;
        
        if(s->end[t] > s->data + s->len)
            s->end[t] = s->data + s->len;
        
        s->laststatus[t] = s->lastchan[t] = s->nrpn[t] = 0;
        s->abs_tick[t] = 0;
        
        memset(s->rpn_msb[t], 0, 16);
        memset(s->rpn_lsb[t], 0, 16);
        
        s->pending_valid[t] = 0;
        s->alive[t]         = 1;

        /* prime this track's first event into the heap */
        if(stream_track_event(s, t, &s->pending[t]))
        {
            s->pending_valid[t] = 1;
            unsigned pos = s->heap_size++;
            s->heap[pos] = t;
            heap_sift_up(s, pos);
        }
        else
            s->alive[t] = 0;

        p = s->end[t];
    }
    
    s->prev_tick       = 0;
    s->st              = 0;
    s->sample_cum      = 0;
    s->counting_time   = ksr->skip_initial_midi_silence ? 2 : 0;
    s->have = s->ended = 0;
    
    for(int i = 0; i < 16; i++)
    {
        s->current_program[i] = ksr->default_program;
        s->current_bank[i] = 0;
        s->current_set[i] = 0;
    }
    compute_sample_increment(ksr, 500000, s->division);
}

static int stream_next(Kasaria *ksr)
{
    MidiStream *s = ksr->stream;
    for(;;)
    {
        if(s->heap_size == 0)
            return 0;

        unsigned best = s->heap[0];
        
        if(!s->alive[best] || !s->pending_valid[best])
        {
            s->heap[0] = s->heap[--s->heap_size];
            heap_sift_down(s, 0);
            continue;
        }

        MidiEvent raw = s->pending[best];
        s->pending_valid[best] = 0;

        /* refill the same track and re-heapify in place */
        if(stream_track_event(s, best, &s->pending[best]))
        {
            s->pending_valid[best] = 1;
            heap_sift_down(s, 0);
        }
        else
        {
            s->alive[best] = 0;
            s->heap[0] = s->heap[--s->heap_size];
            heap_sift_down(s, 0);
        }

        if(stream_groom(ksr, &raw))
            return 1;
    }
}

static MidiEvent *stream_peek(Kasaria *ksr)
{
    MidiStream *s = ksr->stream;
    if(!s->have)
    {
        if(stream_next(ksr))
            s->have = 1;
        else
        {
            s->ev.time = s->st;
            s->ev.type = ME_EOT;
            s->ev.channel = s->ev.key = s->ev.vel = 0;
            s->have = 1;
            s->ended = 1;
        }
    }
    return &s->ev;
}

static void stream_advance(Kasaria *ksr)
{
    ksr->stream->have = 0;
}

static void stream_free(Kasaria *ksr)
{
    MidiStream *s = ksr->stream;
    
    if(!s)
        return;
    
    free(s->cur);
    free(s->end);
    free(s->laststatus);
    free(s->lastchan);
    free(s->abs_tick);
    free(s->nrpn);
    free(s->rpn_msb);
    free(s->rpn_lsb);
    free(s->pending);
    free(s->pending_valid);
    free(s->alive);
    free(s->heap);
    free(s);
    
    ksr->stream = NULL;
}

static int stream_init(Kasaria *ksr, const u_char *data, size_t len)
{
    MidiStream *s;
    if(len < 14 || memcmp(data, "MThd", 4)) return 0;
    
    long chunk = be32(data + 4);
    
    if(chunk < 6)
        return 0;
    
    u_char h[6];
    memcpy(h, data + 8, 6);
    
    short format = (short)((h[0] << 8) | h[1]);
    short tracks = (short)((h[2] << 8) | h[3]);
    short div    = (short)((h[4] << 8) | h[5]);
    
    if(format < 0 || format > 2 || tracks < 1 || tracks > 1024)
        return 0;

    s = ksr->stream = calloc(1, sizeof *s);
    
    if(!s)
        return 0;
    
    s->data        = data;
    s->len         = len;
    s->format      = format;
    s->ntrks       = tracks;
    s->division    = div < 0 ? (long)(-(div / 256)) * (long)(div & 0xFF) : (long)div;
    s->track_start = data + 8 + chunk;
    
    if(s->track_start > data + len)
    {
        free(s);
        ksr->stream = NULL;
        return 0;
    }

    s->cur           = calloc(tracks, sizeof *s->cur);
    s->end           = calloc(tracks, sizeof *s->end);
    s->laststatus    = calloc(tracks, 1);
    s->lastchan      = calloc(tracks, 1);
    s->abs_tick      = calloc(tracks, sizeof *s->abs_tick);
    s->nrpn          = calloc(tracks, 1);
    s->rpn_msb       = calloc(tracks, 16);
    s->rpn_lsb       = calloc(tracks, 16);
    s->pending       = calloc(tracks, sizeof *s->pending);
    s->pending_valid = calloc(tracks, 1);
    s->alive         = calloc(tracks, 1);
    s->heap          = calloc(tracks, sizeof *s->heap);
    
    if(!s->cur || !s->end || !s->laststatus || !s->lastchan || !s->abs_tick ||
       !s->nrpn || !s->rpn_msb || !s->rpn_lsb || !s->pending || !s->pending_valid || !s->alive || !s->heap)
    {
        stream_free(ksr);
        return 0;
    }
    return 1;
}

static void stream_seek(Kasaria *ksr, long until_time)
{
    if(ksr->current_sample > until_time)
        ksr->current_sample = 0;
    
    reset_voices(ksr);
    reset_midi(ksr);
    stream_rewind(ksr);
    
    if(!until_time)
    {
        ksr->current_sample = 0;
        return;
    }

    for(;;)
    {
        MidiEvent *e = stream_peek(ksr);
        
        if(e->type == ME_EOT || e->time >= until_time)
            break;
        
        switch(e->type)
        {
        case ME_PITCH_SENS: case ME_PITCHWHEEL: case ME_MAINVOLUME:
        case ME_PAN: case ME_EXPRESSION: case ME_PROGRAM: case ME_SUSTAIN:
        case ME_RESET_CONTROLLERS: case ME_MONO: case ME_POLY: case ME_TONE_BANK:
            play_midi(ksr, e);
            break;
        default: break;
        }
        stream_advance(ksr);
    }
    ksr->current_sample = until_time;
}

int ksr_load_midi_file(Kasaria *ksr, int loading_mode, const char *filename)
{
    if(!ksr || !filename)
        return 0;

    ksr->midi_loading_mode = loading_mode;

    ksr_unload_midi(ksr);

    if(loading_mode == MIDI_MEMORY)
    {

        ksr->fp_midi = open_file(ksr, filename, 1, OF_VERBOSE);
        if(!ksr->fp_midi)
            return 0;

        ksr->event_list = read_midi_file(ksr, ksr->fp_midi, &ksr->events_midi, &ksr->sample_count);
        if(!ksr->event_list || !ksr->events_midi || !ksr->sample_count)
        {
            ksr_unload_midi(ksr);
            return 0;
        }

        //load_missing_instruments(ksr); // Is this necessary???

        read_midi_text(ksr);
        skip_to(ksr, 0);
        strncpy(ksr->last_smf, filename, 1023);
        ksr->last_smf[1023] = '\0';
        ksr->is_midi_loaded = true;
        log_info("Loaded MIDI: %s", ksr->last_smf);
        return 1;
    }

    if(loading_mode == MIDI_MAP)
    {
        log_debug("Opening file: %s", filename);
        struct stat st;
        ksr->f_mmap->data = NULL;
        ksr->f_mmap->len  = 0;
        ksr->f_mmap->fd   = open(filename, O_RDONLY);
        
        if(ksr->f_mmap->fd < 0)
            return 0;
        
        if(fstat(ksr->f_mmap->fd, &st) < 0 || st.st_size == 0)
        {
            log_error("Failed to check file map. Bad file descriptor or zero size.");
            close(ksr->f_mmap->fd);
            ksr->f_mmap->fd   = -1;
            ksr->f_mmap->data = NULL;
            return 0;
        }

        log_debug("Creating file map...");
        ksr->f_mmap->len = (size_t)st.st_size;
        ksr->f_mmap->data = mmap(NULL, ksr->f_mmap->len, PROT_READ, MAP_PRIVATE, ksr->f_mmap->fd, 0);

        //mlock(ksr->f_mmap->data, ksr->f_mmap->len);

        if(ksr->f_mmap->data == MAP_FAILED)
        {
            log_error("Failed to create file map.");
            close(ksr->f_mmap->fd);
            return 0;
        }

        madvise(ksr->f_mmap->data, ksr->f_mmap->len, MADV_WILLNEED);
        
        if(!stream_init(ksr, ksr->f_mmap->data, ksr->f_mmap->len))
        {
            log_error("Invalid or unsupported MIDI stream.");
            ksr_unmap_file(ksr->f_mmap);
            return 0;
        }
        log_debug("MIDI Stream init");
       
        /* one quick pass: mark instruments for loading + compute duration */
        stream_rewind(ksr);
        
        while(stream_next(ksr)){ }
        
        ksr->sample_count = ksr->stream->st;
        ksr->events_midi  = 0;
    
        stream_seek(ksr, 0);                /* reset to start, current_sample = 0 */
        strncpy(ksr->last_smf, filename, 1023);
        ksr->last_smf[1023] = '\0';
        ksr->is_midi_loaded = true;
        log_info("Loaded MIDI (mapped stream): %s", ksr->last_smf);
        return 1;
    }
    
    return 1;
}

void ksr_unload_midi(Kasaria *ksr)
{
    if(!ksr)
        return;

    log_debug("Unload MIDI");

    if(ksr->midi_loading_mode == MIDI_MAP)
        ksr_unmap_file(ksr->f_mmap);

    stream_free(ksr);

    if(ksr->is_audio_started)
        ma_device_stop(&ksr->audio_device);
    
    if(ksr->event_list)
    {
        free(ksr->event_list);         // freed here, for BOTH modes
        ksr->event_list = NULL;
    }
    
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

    ksr->is_audio_started = false;
    
    ksr->current_event        = NULL;
    ksr->is_midi_loaded       = false;
    ksr->is_midi_ended        = false;
    ksr->is_midi_player_active= false;
    ksr->phase_valid          = 0;

    reset_midi(ksr);

    if(ksr->event_list)
        {
            free(ksr->event_list);
            ksr->event_list = NULL;
        }
    
        if(ksr->fp_midi)
        {
            close_file(ksr->fp_midi);
            ksr->fp_midi = NULL;
        }
    
        ksr->events_midi  = 0;
        ksr->sample_count = 0;
        
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
        return ksr_load_midi_file(ksr, ksr->midi_loading_mode, temp);
    }

    return 0;
}

static void ksr_mark_pos_ns(Kasaria *ksr)
{
    //ksr->wall_clock_last_ns = monotonic_ns();
    u64 now = monotonic_ns();
    f64 y   = (f64)ksr->current_sample / (f64)ksr->play_mode.rate;
    f64 x   = (f64)now / 1e9;
    
    if(!ksr->phase_valid)
    {
        ksr->phase_ema   = y - x;          // first sample: take it as-is
        ksr->phase_valid = 1;
    }
    else
        ksr->phase_ema += 0.2 * ((y - x) - ksr->phase_ema); // one-pole filter on the phase

    ksr->wall_clock_last_ns = now;
}

bool ksr_pause_midi(Kasaria *ksr)
{
    if(!ksr)
        return 1;

    bool pause_ret;

    pause_ret = ksr->is_midi_player_paused = !ksr->is_midi_player_paused;
    if(!ksr->is_midi_player_paused)
    {
        ksr->phase_valid = 0;
        ksr_mark_pos_ns(ksr);
    }

    return pause_ret;
}

int ksr_play_midi_raw(Kasaria *ksr, long type, u_char *buffer, long count)
{
    int convert;
   
    if(!ksr || !buffer || (!ksr->stream && !ksr->current_event) || !ksr->is_midi_loaded || (type > AUDIO_ULAW || type < AUDIO_CHAR))
        return 0;

    ksr->is_midi_player_active = true;

    while(count > 0)
    {
        convert = count;
        if(ksr->midi_loading_mode == MIDI_MAP)
        {
            MidiEvent *e;

            while((e = stream_peek(ksr))->time <= ksr->current_sample)
            {
                if(e->type == ME_EOT)
                {
                    ksr->is_midi_ended = true;
                    ksr->is_midi_player_active = false;
                    break;
                }
    
                play_midi(ksr, e);
                stream_advance(ksr);
            }

            e = stream_peek(ksr);
            convert = e->time - ksr->current_sample;
        }
        
        if(ksr->midi_loading_mode == MIDI_MEMORY)
        {
            while(ksr->current_event->time <= ksr->current_sample)
            {
                if(ksr->current_event->type == ME_EOT)
                {
                    ksr->is_midi_ended = true;
                    ksr->is_midi_player_active = false;
                    break;
                }
                
                play_midi(ksr, ksr->current_event);
                ksr->current_event++;
            }

            convert = ksr->current_event->time - ksr->current_sample;
        }
        
        
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
        ksr_mark_pos_ns(ksr);
        count               -= convert;
    }
    return 1;
}

u64 monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
}

double ksr_get_midi_player_pos(Kasaria *ksr)
{
    if(!ksr)
        return 0.0;

    f64 base = (f64)ksr->current_sample / (f64)ksr->play_mode.rate;

    if(ksr->is_midi_player_active && !ksr->is_midi_player_paused && !ksr->is_midi_ended && ksr->phase_valid)
        return (f64)monotonic_ns() / 1e9 + ksr->phase_ema;

    return base; // paused / ended / inactive → exact, frozen
}

int ksr_play_midi(Kasaria *ksr, bool wait_midi_ending)
{
    if(!ksr)
        return 0;

    if(!ksr->is_audio_init)
    {
        log_error("Audio device not initialized");
        return 0;
    }

    if(!ksr->is_midi_loaded)
    {
        log_error("No MIDI File was loaded!");
        return 0;
    }

    async_midi_player = ksr; // Get the current synth context for the async player

    ksr->is_midi_ended         = false;
    ksr->is_midi_player_paused = false;
    ksr->phase_valid           = 0;

    log_info("Starting MIDI playback...");
    ma_device_start(&async_midi_player->audio_device); // This may cause the midi player to get stuck when trying to play the midi again

    // Wait for the MIDI playback to finish (This is required if this function is called on the main thread so it won't exit)
    if(wait_midi_ending)
        while(!ksr->is_midi_ended)
            ma_sleep(1000); // This may probably extend the position

    // In any case the while loop above is not used
    if(ksr->is_midi_ended)
        ma_device_stop(&ksr->audio_device);
    
    return 1;
}

bool ksr_is_midi_player_active(Kasaria *ksr)
{
    if(!ksr)
        return 0;
    
    return ksr->is_midi_player_active;
}

bool ksr_is_midi_ended(Kasaria *ksr)
{
    return ksr->is_midi_ended;
}

int ksr_seek_midi(Kasaria *ksr, long time)
{
    int total_time;
    if(!ksr || (!ksr->stream && !ksr->current_event))
        return 0;

    total_time = ksr_get_duration(ksr);
    
    if(time > total_time)
        time = total_time;
    else if(time < 0)
        time = 0;

    if(ksr->midi_loading_mode == MIDI_MEMORY)
    {
        skip_to(ksr, 0);
        skip_to(ksr, ksr_millis2samples(ksr, time));
    }

    if(ksr->midi_loading_mode == MIDI_MAP)
        stream_seek(ksr, ksr_millis2samples(ksr, time));

    ksr->phase_valid = 0;
    
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