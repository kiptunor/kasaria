#include <stdio.h>

#define __USE_XOPEN_EXTENDED
#include <unistd.h>


#include "src/kasaria.h"



Kasaria *synth;




int main(int argc, char *argv[])
{
    synth = ksr_init(); // Create a synth instance

    

    // Set some parameters
    ksr_set_fast_decay(synth, true);
    ksr_set_antialiasing(synth, true);
    ksr_set_sample_rate(synth, 48000); // Optional
    
    // Skip notes with velocities in between the low and high specified threasholds
    // And also enable the filter
    ksr_set_note_velocity_skipping(synth, 0, 20, true);

    
    ksr_set_max_voices(synth, 5000); // How many voices the synth can use

    // Initialize and open an audio device
    ksr_init_audio(synth); // Needed for the async midi playback

    // Load 2 soundfonts

    // The first preset of this soundfont overrides the first preset of the second loaded soundfont
    ksr_load_soundfont_file(synth, "/home/andre/disks/1_TB_1/bm/soundfonts/Full Grand Piano V2.sf2", true);

    /*
        But the second soundfont may have more presets than the first loaded soundfont
        which means that if the midi uses multiple banks the second soundfont can provide them if the first soundfont
        doesn't have a preset for the required MIDI bank
    */
    ksr_load_soundfont_file(synth, "/home/andre/disks/1_TB_1/bm/soundfonts/Arachno SoundFont Version 1.0.sf2", true); 


    if(!ksr_load_midi_file(synth, argv[1])) // Try to load a midi file
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(synth);
        return 1;
    }

    // Start the MIDI playback in the background (Audio thread is handled internally) and also wait for the etire midi player to finish
    ksr_play_midi_async(synth, true);

    ksr_shutdown(synth);
    return 0;
}