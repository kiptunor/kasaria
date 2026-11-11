#include <stdio.h>

#define __USE_XOPEN_EXTENDED
#include <unistd.h>


#include "../src/kasaria.h"



Kasaria *synth_ctx1;
Kasaria *synth_ctx2;
Kasaria *synth_ctx3;
Kasaria *synth_ctx4;


KasariaConfig ksr_settings =
{
    .amplification      = 100,
    .voice_limit        = 1024,
    .sample_rate        = 48000,
    .control_rate       = 100,
    .low_note_velocity  = 0,
    .high_note_velocity = 0,
    .audio_frame_size   = 96,
    .fast_decay         = 1,
    .antialiasing       = 1,
    .pre_resample       = 1,
    .velocity_skipping  = 0,
    .audio_compressor   = 1,
};


KsrSoundfontOpts s1 = (KsrSoundfontOpts)
{
    .active_presets = 128,         // How many presets to load (Lazy soundfont loading)
    .bank = 0,                    // Set current midi bank
    .preset = 0,                  // Set current preset
    .load_percussion_bank = true, // Load the percussion bank
};



int main()
{
    synth_ctx1 = ksr_init(0);
    synth_ctx2 = ksr_init(0);
    synth_ctx3 = ksr_init(0);
    synth_ctx4 = ksr_init(0);
    
    printf("===================[ 4 Instances initialized ]===================\n\n");

    ksr_set_config(synth_ctx1, ksr_settings);
    ksr_set_config(synth_ctx2, ksr_settings);
    ksr_set_config(synth_ctx3, ksr_settings);
    ksr_set_config(synth_ctx4, ksr_settings);
    printf("===================[ 4 instances config set ]===================\n\n");
    
    ksr_load_soundfont_file(synth_ctx1, "Arachno SoundFont Version 1.0.sf2", s1);
    ksr_load_soundfont_file(synth_ctx2, "Arachno SoundFont Version 1.0.sf2", s1);
    ksr_load_soundfont_file(synth_ctx3, "Arachno SoundFont Version 1.0.sf2", s1);
    ksr_load_soundfont_file(synth_ctx4, "Arachno SoundFont Version 1.0.sf2", s1);


    printf("===================[ Soundfonts loaded for 4 instances ]===================\n\n");


    ksr_init_audio(synth_ctx1, RAW_MIDI_EVENTS);
    ksr_start_audio(synth_ctx1);

    
    ksr_init_audio(synth_ctx2, RAW_MIDI_EVENTS);
    ksr_start_audio(synth_ctx2);
    ksr_init_audio(synth_ctx3, RAW_MIDI_EVENTS);
    ksr_start_audio(synth_ctx3);

    ksr_init_audio(synth_ctx4, RAW_MIDI_EVENTS);
    ksr_start_audio(synth_ctx4);

    printf("===================[ 4 instances init audio ]===================\n\n");

    
    // Play some chord
    printf("===================[ Play chord ]===================\n\n");
    ksr_channel_note_on(synth_ctx1, 0, 60, 96);
    ksr_channel_note_on(synth_ctx2, 0, 64, 96);
    ksr_channel_note_on(synth_ctx3, 0, 67, 96);
    ksr_channel_note_on(synth_ctx4, 0, 72, 96);

    sleep(1);

   ksr_channel_note_off(synth_ctx1, 0, 60);
   ksr_channel_note_off(synth_ctx2, 0, 64);
   ksr_channel_note_off(synth_ctx3, 0, 67);
   ksr_channel_note_off(synth_ctx4, 0, 72);


    sleep(2);


    ksr_shutdown(synth_ctx1);
    ksr_shutdown(synth_ctx2);
    ksr_shutdown(synth_ctx3);
    ksr_shutdown(synth_ctx4);

    printf("===================[ 4 instances closed ]===================\n");
    
    return 0;
}