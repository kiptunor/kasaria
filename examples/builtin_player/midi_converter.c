#include <stdio.h>

#include "../../src/kasaria.h"


#define MINIAUDIO_IMPLEMENTATION
#include "../deps/miniaudio.h"


Kasaria *converter;

int main(int argc, char *argv[])
{
    converter = ksr_init(0);

    ksr_config_set_fast_decay(converter, true);
    ksr_config_set_antialiasing(converter, true);
    ksr_config_set_sample_rate(converter, 48000); // Optional
    ksr_config_set_max_voices(converter, 8024);
    
    // Skip notes with velocities in between the low and high specified threasholds
    // And also enable the filter
    ksr_config_set_note_skipping(converter, 0, 20, false);

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
    ksr_load_soundfont_file_new(converter, "Arachno SoundFont Version 1.0.sf2", s1);

    // And the second has only 5 presets available which means that the first 5 presets of the first soundfont
    // will be overridden by the presets of the second soundfont
    ksr_load_soundfont_file_new(converter, "Full Grand Piano V2.sf2", s2);


    // ==============[Load MIDI file]==============
    // For this approach file mapping is set which does not store any midi data in RAM
    if(!ksr_load_midi_file(converter, MIDI_MAP, argv[1]))
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(converter);
        return 1;
    }

    // Retrieve sample rate and sound channel distribution
    int rate     = ksr_config_get_sample_rate(converter);
    int channels = ksr_config_get_mono(converter) ? 1 : 2;

    // Initialize the config of the encoder and use WAV audio format
    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, channels, rate);
    ma_encoder encoder;

    // Create the audio file with the name provided in the second CLI argument
    ma_result result = ma_encoder_init_file(argv[2], &config, &encoder);

    // Check for any encoder errors
    if(result != MA_SUCCESS)
    {
        // Error
        printf("Failed to initialize encoder: %s\n", ma_result_description(result));
        ksr_shutdown(converter);
        return 1;
    }

    // Count all samples required for the synthesis process
    long total = ksr_get_sample_count(converter) + ksr_millis2samples(converter, 1000); // +1s tail
    long chunk = 4096;
    float *buf = malloc(chunk * channels * sizeof(float));

    // Initialize a timer for conversion time measurement
    ma_timer timer;
    ma_timer_init(&timer);

    /*
        Because the midi loader was set to create file mapping, the player reads all midi events
        from the virtual memory (Page cache)
    */
    while(total > 0)
    {
        long frames = total < chunk ? total : chunk;

        // Advance to the next MIDI event and generate the synthesized audio frames
        if(!ksr_player_get_stream(converter, AUDIO_FLOAT, (unsigned char*)buf, frames))
            break;

        // Then write each audio frame to the encoder
        ma_encoder_write_pcm_frames(&encoder, buf, frames, NULL);
        total -= frames;
    }

    // Clean up resources after the conversion is complete
    printf("Converted in %.3f s\n", ma_timer_get_time_in_seconds(&timer));
    
    ma_encoder_uninit(&encoder);
    free(buf);
    ksr_shutdown(converter);
    return 0;
}