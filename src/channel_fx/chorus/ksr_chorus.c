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



#define CHORUS_LEN  0.040f
#define CHORUS_RATE 0.2f


void init_chorus(Kasaria *ksr)
{
    if(!ksr)
        return;

    ksr->chorus_l = NULL;
    ksr->chorus_r = NULL;

    ksr->chorus_l = sk_chorus_new(ksr->play_mode.rate, CHORUS_LEN);

    if(!ksr->chorus_l)
        return;

    ksr->chorus_r = sk_chorus_new(ksr->play_mode.rate, CHORUS_LEN);

    if(!ksr->chorus_r)
    {
        sk_chorus_del(ksr->chorus_l);
        ksr->chorus_l = NULL;
        return;
    }

    sk_chorus_rate(ksr->chorus_l, CHORUS_RATE);
    sk_chorus_rate(ksr->chorus_r, CHORUS_RATE);

   
    sk_chorus_phase(ksr->chorus_l, 0.0f);
    sk_chorus_phase(ksr->chorus_r, 0.5f);


    sk_chorus_mix(ksr->chorus_l, 1.0f);
    sk_chorus_mix(ksr->chorus_r, 1.0f);

    apply_chorus_depth(ksr);
}


void free_chorus(Kasaria *ksr)
{
    if(!ksr)
        return;

    sk_chorus_del(ksr->chorus_l);
    ksr->chorus_l = NULL;

    sk_chorus_del(ksr->chorus_r);
    ksr->chorus_r = NULL;
}


void reset_chorus(Kasaria *ksr)
{
    if(!ksr)
        return;

    free_chorus(ksr);
    init_chorus(ksr);
}


void apply_chorus_depth(Kasaria *ksr)
{
    if(!ksr)
        return;

    if(ksr->chorus_l)
        sk_chorus_depth(ksr->chorus_l, (f32)ksr->chorus_depth);

    if(ksr->chorus_r)
        sk_chorus_depth(ksr->chorus_r, (f32)ksr->chorus_depth);
}


void process_chorus(Kasaria *ksr, f32 *buf, long *send_buf, long count)
{
    long scale;
    long i;

    if(!ksr || !buf || !send_buf)
        return;

    if(!ksr->chorus_enabled || ksr->chorus_depth <= 0.0 || count <= 0)
        return;
    

    if(!ksr->chorus_l || !ksr->chorus_r)
        return;

    if(count > AUDIO_BUFFER_SIZE)
        count = AUDIO_BUFFER_SIZE;
    
    scale = 1L << (31 - GUARD_BITS);

    if(scale <= 0)
        return;

    for(i = 0; i < count; ++i)
    {
        f32 input;
        f32 wet_l;
        f32 wet_r;

        input = (f32)send_buf[i] / (f32)scale;

        wet_l = sk_chorus_tick(ksr->chorus_l, input);

        wet_r = sk_chorus_tick(ksr->chorus_r, input);

        /*
         * Add the wet return to the existing output.
         *
         * The original dry synth signal is already in buf.
         */
        if(!(ksr->play_mode.encoding & PE_MONO))
        {
            buf[i * 2 + 0] += wet_l * (f32)scale;
            buf[i * 2 + 1] += wet_r * (f32)scale;
        }
        else
            buf[i] += wet_l * (f32)scale;
        
    }
}