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

reverb.c -- EAXReverb integration

*/

#include <stdio.h>

#ifndef _WIN32_WCE
#include <string.h>
#endif

#include "../../ksr_internal.h"
#include "efx-presets.h"
#include "ksr_reverb_effect.h"


static const EFXEAXREVERBPROPERTIES reverb_presets[] =
{
    EFX_REVERB_PRESET_GENERIC,
    EFX_REVERB_PRESET_PADDEDCELL,
    EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,
    EFX_REVERB_PRESET_LIVINGROOM,
    EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,
    EFX_REVERB_PRESET_CONCERTHALL,
    EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,
    EFX_REVERB_PRESET_HANGAR,
    EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,
    EFX_REVERB_PRESET_STONECORRIDOR,
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,
    EFX_REVERB_PRESET_CITY,
    EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,
    EFX_REVERB_PRESET_PLAIN,
    EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,
    EFX_REVERB_PRESET_UNDERWATER,
    EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY,
    EFX_REVERB_PRESET_PSYCHOTIC,
    EFX_REVERB_PRESET_CASTLE_SMALLROOM,
    EFX_REVERB_PRESET_CASTLE_SHORTPASSAGE,
    EFX_REVERB_PRESET_CASTLE_MEDIUMROOM,
    EFX_REVERB_PRESET_CASTLE_LARGEROOM,
    EFX_REVERB_PRESET_CASTLE_LONGPASSAGE,
    EFX_REVERB_PRESET_CASTLE_HALL,
    EFX_REVERB_PRESET_CASTLE_CUPBOARD,
    EFX_REVERB_PRESET_CASTLE_COURTYARD,
    EFX_REVERB_PRESET_CASTLE_ALCOVE,
    EFX_REVERB_PRESET_FACTORY_SMALLROOM,
    EFX_REVERB_PRESET_FACTORY_SHORTPASSAGE,
    EFX_REVERB_PRESET_FACTORY_MEDIUMROOM,
    EFX_REVERB_PRESET_FACTORY_LARGEROOM,
    EFX_REVERB_PRESET_FACTORY_LONGPASSAGE,
    EFX_REVERB_PRESET_FACTORY_HALL,
    EFX_REVERB_PRESET_FACTORY_CUPBOARD,
    EFX_REVERB_PRESET_FACTORY_COURTYARD,
    EFX_REVERB_PRESET_FACTORY_ALCOVE,
    EFX_REVERB_PRESET_ICEPALACE_SMALLROOM,
    EFX_REVERB_PRESET_ICEPALACE_SHORTPASSAGE,
    EFX_REVERB_PRESET_ICEPALACE_MEDIUMROOM,
    EFX_REVERB_PRESET_ICEPALACE_LARGEROOM,
    EFX_REVERB_PRESET_ICEPALACE_LONGPASSAGE,
    EFX_REVERB_PRESET_ICEPALACE_HALL,
    EFX_REVERB_PRESET_ICEPALACE_CUPBOARD,
    EFX_REVERB_PRESET_ICEPALACE_COURTYARD,
    EFX_REVERB_PRESET_ICEPALACE_ALCOVE,
    EFX_REVERB_PRESET_SPACESTATION_SMALLROOM,
    EFX_REVERB_PRESET_SPACESTATION_SHORTPASSAGE,
    EFX_REVERB_PRESET_SPACESTATION_MEDIUMROOM,
    EFX_REVERB_PRESET_SPACESTATION_LARGEROOM,
    EFX_REVERB_PRESET_SPACESTATION_LONGPASSAGE,
    EFX_REVERB_PRESET_SPACESTATION_HALL,
    EFX_REVERB_PRESET_SPACESTATION_CUPBOARD,
    EFX_REVERB_PRESET_SPACESTATION_ALCOVE,
    EFX_REVERB_PRESET_WOODEN_SMALLROOM,
    EFX_REVERB_PRESET_WOODEN_SHORTPASSAGE,
    EFX_REVERB_PRESET_WOODEN_MEDIUMROOM,
    EFX_REVERB_PRESET_WOODEN_LARGEROOM,
    EFX_REVERB_PRESET_WOODEN_LONGPASSAGE,
    EFX_REVERB_PRESET_WOODEN_HALL,
    EFX_REVERB_PRESET_WOODEN_CUPBOARD,
    EFX_REVERB_PRESET_WOODEN_COURTYARD,
    EFX_REVERB_PRESET_WOODEN_ALCOVE,
    EFX_REVERB_PRESET_SPORT_EMPTYSTADIUM,
    EFX_REVERB_PRESET_SPORT_SQUASHCOURT,
    EFX_REVERB_PRESET_SPORT_SMALLSWIMMINGPOOL,
    EFX_REVERB_PRESET_SPORT_LARGESWIMMINGPOOL,
    EFX_REVERB_PRESET_SPORT_GYMNASIUM,
    EFX_REVERB_PRESET_SPORT_FULLSTADIUM,
    EFX_REVERB_PRESET_SPORT_STADIUMTANNOY,
    EFX_REVERB_PRESET_PREFAB_WORKSHOP,
    EFX_REVERB_PRESET_PREFAB_SCHOOLROOM,
    EFX_REVERB_PRESET_PREFAB_PRACTISEROOM,
    EFX_REVERB_PRESET_PREFAB_OUTHOUSE,
    EFX_REVERB_PRESET_PREFAB_CARAVAN,
    EFX_REVERB_PRESET_DOME_TOMB,
    EFX_REVERB_PRESET_PIPE_SMALL,
    EFX_REVERB_PRESET_DOME_SAINTPAULS,
    EFX_REVERB_PRESET_PIPE_LONGTHIN,
    EFX_REVERB_PRESET_PIPE_LARGE,
    EFX_REVERB_PRESET_PIPE_RESONANT,
    EFX_REVERB_PRESET_OUTDOORS_BACKYARD,
    EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS,
    EFX_REVERB_PRESET_OUTDOORS_DEEPCANYON,
    EFX_REVERB_PRESET_OUTDOORS_CREEK,
    EFX_REVERB_PRESET_OUTDOORS_VALLEY,
    EFX_REVERB_PRESET_MOOD_HEAVEN,
    EFX_REVERB_PRESET_MOOD_HELL,
    EFX_REVERB_PRESET_MOOD_MEMORY,
    EFX_REVERB_PRESET_DRIVING_COMMENTATOR,
    EFX_REVERB_PRESET_DRIVING_PITGARAGE,
    EFX_REVERB_PRESET_DRIVING_INCAR_RACER,
    EFX_REVERB_PRESET_DRIVING_INCAR_SPORTS,
    EFX_REVERB_PRESET_DRIVING_INCAR_LUXURY,
    EFX_REVERB_PRESET_DRIVING_FULLGRANDSTAND,
    EFX_REVERB_PRESET_DRIVING_EMPTYGRANDSTAND,
    EFX_REVERB_PRESET_DRIVING_TUNNEL,
    EFX_REVERB_PRESET_CITY_STREETS,
    EFX_REVERB_PRESET_CITY_SUBWAY,
    EFX_REVERB_PRESET_CITY_MUSEUM,
    EFX_REVERB_PRESET_CITY_LIBRARY,
    EFX_REVERB_PRESET_CITY_UNDERPASS,
    EFX_REVERB_PRESET_CITY_ABANDONED,
    EFX_REVERB_PRESET_DUSTYROOM,
    EFX_REVERB_PRESET_CHAPEL,
    EFX_REVERB_PRESET_SMALLWATERROOM,
};

#define REVERB_PRESET_COUNT (sizeof(reverb_presets)/sizeof(reverb_presets[0]))

static void apply_reverb_preset(Kasaria *ksr)
{
    int preset;
    
    if(!ksr->reverb_enabled)
        return;
    
    preset = ksr->reverb_preset;
    
    if(preset >= REVERB_PRESET_COUNT || preset < 0)
        preset = KSR_REVERB_PRESET_GENERIC;
    
    ReverbEffectLoadPreset(&ksr->reverb, (EFXEAXREVERBPROPERTIES *)&reverb_presets[preset]);
    ReverbEffectUpdate(&ksr->reverb, ksr->play_mode.rate);
}

void init_reverb(Kasaria *ksr)
{
    ReverbEffectCreate(&ksr->reverb, (u32)ksr->play_mode.rate);
    apply_reverb_preset(ksr);
}

void free_reverb(Kasaria *ksr)
{
    ReverbEffectDestroy(&ksr->reverb);
}

void reset_reverb(Kasaria *ksr)
{
    free_reverb(ksr);
    init_reverb(ksr);
}

void set_reverb_preset(Kasaria *ksr, int preset)
{
    if(preset >= REVERB_PRESET_COUNT || preset < 0)
        return;
    
    ksr->reverb_preset = preset;
    apply_reverb_preset(ksr);
}

void process_reverb(Kasaria *ksr, f32 *buf, long *send_buf, long count)
{
    f32 mono_in[AUDIO_BUFFER_SIZE];
        f32 stereo_out[AUDIO_BUFFER_SIZE * 2];
        long scale;
        long i;
        
        if(!ksr->reverb_enabled || ksr->reverb_level <= 0.0 || count <= 0)
            return;
        
        scale = 1 << (31 - GUARD_BITS);
        
        if(count > AUDIO_BUFFER_SIZE)
            count = AUDIO_BUFFER_SIZE;
        
        for(i=0; i<count; i++)
            mono_in[i] = (f32)send_buf[i] / (f32)scale;
        
        memset(stereo_out, 0, count * 2 * sizeof(f32));
        ReverbEffectProcess(&ksr->reverb, (u32)count, mono_in, stereo_out);
        
        for(i=0; i<count; i++)
        {
            // f32 wl = stereo_out[i*2+0] * (f32)scale * (f32)ksr->reverb_level;
            // f32 wr = stereo_out[i*2+1] * (f32)scale * (f32)ksr->reverb_level;
            f32 wl = stereo_out[i*2+0] * (f32)ksr->reverb_level;
            f32 wr = stereo_out[i*2+1] * (f32)ksr->reverb_level;
            
            if(ksr->reverb_only)
            {
                if(!(ksr->play_mode.encoding & PE_MONO))
                {
                    buf[i*2+0] = wl;
                    buf[i*2+1] = wr;
                }
                else
                    buf[i] = (wl + wr) / 2.0f;
            }
            else
            {
                if(!(ksr->play_mode.encoding & PE_MONO))
                {
                    buf[i*2+0] += wl;
                    buf[i*2+1] += wr;
                }
                else
                    buf[i] += (wl + wr) / 2.0f;
            }
        }
}