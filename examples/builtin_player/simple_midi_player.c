#include <stdio.h>

#include "../../src/kasaria.h"



Kasaria *synth;




int main(int argc, char *argv[])
{
    synth = ksr_init(0); // Create a synth instance without disabling logs
    // By default kasaria logs all events to STDOUT

    

    // Override some default settings
    ksr_config_set_fast_decay(synth, true);   // Improves envelope processing efficiency
    ksr_config_set_antialiasing(synth, true);
    ksr_config_set_sample_rate(synth, 48000); // Optional
    ksr_config_set_pre_resample(synth, true); // All soundfont samples will be pre-resampled after applying the common soundfont effects
    
    // Skip notes with velocities in between the low and high specified threasholds
    // And also enable the filter
    ksr_config_set_note_skipping(synth, 0, 20, true);

    
    ksr_config_set_max_voices(synth, 5000); // How many voices the synth can use

    // Initialize and open an audio device (Internally handeled)
    ksr_init_audio(synth, INTERNAL_MIDI_PLAYER); // Needed for the async midi playback

    // Load 2 soundfont files
    // but first set the options for both soundfonts
    KsrSoundfontOpts s1;
    KsrSoundfontOpts s2;

    s1 = (KsrSoundfontOpts)
    {
        .active_presets = 10,         // How many presets to load (Lazy soundfont loading)
        .bank = 0,                    // Set current midi bank
        .preset = 0,                  // Set current preset
        .load_percussion_bank = true, // Load the percussion bank
    };

    // Same options for the second soundfont
    s2 = (KsrSoundfontOpts)
    {
        .active_presets = 10,
        .bank = 0,
        .preset = 0,
        .load_percussion_bank = true,
    };

    

    // ==============[Load the soundfonts in this exact order]==============
    
    // The first soundfont to load has 128 presets
    ksr_load_soundfont_file_new(synth, "Arachno SoundFont Version 1.0.sf2", s1);

    // And the second has only 5 presets available which means that the first 5 presets of the first soundfont
    // will be overridden by the presets of the second soundfont
    //ksr_load_soundfont_file_new(synth, "Full Grand Piano V2.sf2", s2);
    ksr_load_soundfont_file_new(synth, "/home/andre/disks/1_TB_1/bm/soundfonts/Amr's Steinway Dream Piano.sf2", s2);


    // Load a MIDI with file mapping (Set on the second function argument)
    if(!ksr_load_midi_file(synth, MIDI_MAP, argv[1])) // The midi file is memory mapped so no midi data is stored in RAM
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(synth);
        return 1;
    }

    // Start the MIDI playback in the background and also wait for the etire midi player to finish.
    // The midi player reads the midi data from disk and all events are processed
    ksr_player_begin(synth, true);

    ksr_shutdown(synth);
    return 0;
}