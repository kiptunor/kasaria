#include <stdio.h>

#include "src/kasaria.h"



Kasaria *synth;




int main(int argc, char *argv[])
{
    synth = ksr_init(0); // Create a synth instance without disabling logs

    

    // Set some parameters
    ksr_set_fast_decay(synth, true);
    ksr_set_antialiasing(synth, true);
    ksr_set_sample_rate(synth, 48000); // Optional
    ksr_set_pre_resample(synth, true);
    
    // Skip notes with velocities in between the low and high specified threasholds
    // And also enable the filter
    ksr_set_note_velocity_skipping(synth, 0, 20, true);

    
    ksr_set_max_voices(synth, 5000); // How many voices the synth can use
    //ksr_set_audio_frame_size(synth, 688);

    // Initialize and open an audio device
    ksr_init_audio(synth, INTERNAL_MIDI_PLAYER); // Needed for the async midi playback

    KsrSoundfontOpts s1;
    KsrSoundfontOpts s2;

    s1 = (KsrSoundfontOpts){
        .active_presets = 10,
        .bank = 0,
        .preset = 0,
        .preload_instruments = true,
        .load_percussion_bank = true,
    };

    s2 = (KsrSoundfontOpts){
        .active_presets = 10,
        .bank = 0,
        .preset = 0,
        .preload_instruments = true,
        .load_percussion_bank = true,
    };

    // Load 2 soundfonts

    // The first preset of this soundfont overrides the first preset of the second loaded soundfont

    ksr_load_soundfont_file(synth, "Arachno SoundFont Version 1.0.sf2", true);
    
    ksr_load_soundfont_file(synth, "Full Grand Piano V2.sf2", true);

    //ksr_load_soundfont_file_new(synth, "Full Grand Piano V2.sf2", s1);

    /*
        But the second soundfont may have more presets than the first loaded soundfont
        which means that if the midi uses multiple banks the second soundfont can provide them if the first soundfont
        doesn't have a preset for the required MIDI bank
    */
    
    //ksr_load_soundfont_file_new(synth, "Arachno SoundFont Version 1.0.sf2", s2);


    if(!ksr_load_midi_file(synth, MIDI_MEMORY, argv[1])) // Try to load a midi file
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(synth);
        return 1;
    }

    // Start the MIDI playback in the background (Audio thread is handled internally) and also wait for the etire midi player to finish
    ksr_player_begin(synth, true);

    ksr_shutdown(synth);
    return 0;
}