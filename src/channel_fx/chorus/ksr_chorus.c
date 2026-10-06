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

chorus.c -- SKChorus integration

*/

#include <stdio.h>

#ifndef _WIN32_WCE
#include <string.h>
#endif

#include "../../ksr_internal.h"
#include "ksr_sk_chorus.h"

#define CHORUS_LEN 0.01
#define CHORUS_RATE 0.5

void init_chorus(Kasaria *ksr)
{
    ksr->chorus_l = sk_chorus_new(ksr->play_mode.rate, CHORUS_LEN);
    
    if(!ksr->chorus_l)
        return;
    
    ksr->chorus_r = sk_chorus_new(ksr->play_mode.rate, CHORUS_LEN);
    if (!ksr->chorus_r)
    {
        sk_chorus_del(ksr->chorus_l);
        ksr->chorus_l = NULL;
        return;
    }
    sk_chorus_rate(ksr->chorus_l, CHORUS_RATE);
    sk_chorus_mix(ksr->chorus_l, 1.0);
    sk_chorus_rate(ksr->chorus_r, CHORUS_RATE * -1);
    sk_chorus_mix(ksr->chorus_r, 1.0);
    apply_chorus_depth(ksr);
}

void free_chorus(Kasaria *ksr)
{
    if(!ksr->chorus_l && !ksr->chorus_r)
        return;
    
    sk_chorus_del(ksr->chorus_l);
    ksr->chorus_l = NULL;
    sk_chorus_del(ksr->chorus_r);
    ksr->chorus_r = NULL;
}

void reset_chorus(Kasaria *ksr)
{
    free_chorus(ksr);
    init_chorus(ksr);
}

void apply_chorus_depth(Kasaria *ksr)
{
    if(!ksr->chorus_l && !ksr->chorus_r)
        return;
    
    sk_chorus_depth(ksr->chorus_l, (f32)ksr->chorus_depth);
    sk_chorus_depth(ksr->chorus_r, (f32)ksr->chorus_depth);
}

void process_chorus(Kasaria *ksr, f32 *buf, long *send_buf, long count)
{
    
    long scale;
    long i;
    
    if((!ksr->chorus_l && !ksr->chorus_r) || !ksr->chorus_enabled || ksr->chorus_depth <= 0.0 || count <= 0)
        return;
    
    
    scale = 1 << (31 - GUARD_BITS);
    
    if(count > AUDIO_BUFFER_SIZE)
        count = AUDIO_BUFFER_SIZE;
    
    for(i=0; i<count; i++)
    {
        if(!(ksr->play_mode.encoding & PE_MONO))
        {
            // buf[i*2+0] += (long)(sk_chorus_tick(ksr->chorus_l, (f32)send_buf[i] / (f32)scale) * (f32)scale);
            // buf[i*2+1] += (long)(sk_chorus_tick(ksr->chorus_r, (f32)send_buf[i] / (f32)scale) * (f32)scale);
            // buf[i * 2 + 0] += sk_chorus_tick(ksr->chorus_l, send_buf[i]);
            // buf[i * 2 + 1] += sk_chorus_tick(ksr->chorus_r, send_buf[i]);

            buf[i*2+0] += (f32)(sk_chorus_tick(ksr->chorus_l, (f32)send_buf[i] / (f32)scale) * (f32)scale);
            buf[i*2+1] += (f32)(sk_chorus_tick(ksr->chorus_r, (f32)send_buf[i] / (f32)scale) * (f32)scale);
        }
        else
            //buf[i] += (long)(sk_chorus_tick(ksr->chorus_l, (f32)send_buf[i] / (f32)scale) * (f32)scale);
            //buf[i] += sk_chorus_tick(ksr->chorus_l, send_buf[i]);
            buf[i] += (f32)(sk_chorus_tick(ksr->chorus_l, (f32)send_buf[i] / (f32)scale) * (f32)scale);
        
    }
}