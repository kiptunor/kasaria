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




#include "ksr_internal.h"




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
            f32 sample = ksr->common_buffer[i] * 127.0f;
            if(sample > 127.0f)
                sample = 127.0f;

            if(sample < -128.0f)
                sample = -128.0f;

            buffer[i] = (u_char)((int)sample ^ 0x80);
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
            f32 sample = ksr->common_buffer[i] * 32767.0f;
            if(sample > 32767.0f)
                sample = 32767.0f;

            if(sample < -32768.0f)
                sample = -32768.0f;

            buffer[i] = (short)((int)sample);
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
            f32 sample = ksr->common_buffer[i] * 8388607.0f;
            if(sample > 8388607.0f)
                sample = 8388607.0f;

            if(sample < -8388608.0f)
                sample = -8388608.0f;

            buffer[i].data[0] = (u_char)((int)sample & 0xff);
            buffer[i].data[1] = (u_char)((int)sample >> 8) & 0xff;
            buffer[i].data[2] = (u_char)((int)sample >> 16) & 0xff;
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_long(Kasaria *ksr, long *buffer, long count)
{
    int  curframes, cursamples, i;
    long maxval = 2147483647L; // 2^31 - 1;
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
            f32 sample = ksr->common_buffer[i] * maxval;
            if(sample > maxval - 1)
                sample = maxval - 1;
            else if(sample < maxval * -1)
                sample = maxval * -1;
            buffer[i] = (long)sample;
        }
        buffer += cursamples;
        count  -= curframes;
    }
}

void ksr_render_float(Kasaria *ksr, f32 *buffer, long count)
{
    int  curframes, cursamples, i;
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
    int  curframes, cursamples, i;
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
            f32 sample = ksr->common_buffer[i] * 4095.0f;
            if(sample > 4095.0f)
                sample = 4095.0f;
            else if(sample < -4096.0f)
                sample = -4096.0f;
            buffer[i] = _l2u[(int)sample];
        }
        buffer += cursamples;
        count  -= curframes;
    }
}