#include <stdio.h>

#include "src/kasaria.h"


#define MINIAUDIO_IMPLEMENTATION
#include "src/ext_deps/miniaudio/miniaudio.h"


Kasaria *converter;

int main(int argc, char *argv[])
{
    converter = ksr_init(0);

    ksr_set_fast_decay(converter, true);
    ksr_set_antialiasing(converter, true);
    ksr_set_sample_rate(converter, 48000); // Optional
    ksr_set_max_voices(converter, 2026);
    
    // Skip notes with velocities in between the low and high specified threasholds
    // And also enable the filter
    ksr_set_note_velocity_skipping(converter, 0, 20, true);

    // The first preset of this soundfont overrides the first preset of the second loaded soundfont
    ksr_load_soundfont_file(converter, "Full Grand Piano V2.sf2", true);

    /*
        But the second soundfont may have more presets than the first loaded soundfont
        which means that if the midi uses multiple banks the second soundfont can provide them if the first soundfont
        doesn't have a preset for the required MIDI bank
    */
    ksr_load_soundfont_file(converter, "Arachno SoundFont Version 1.0.sf2", true); 

    if(!ksr_load_midi_file(converter, MIDI_MEMORY, argv[1])) // Try to load a midi file
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(converter);
        return 1;
    }

    int rate     = ksr_get_sample_rate(converter);
    int channels = ksr_get_mono(converter) ? 1 : 2;
    
    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, channels, rate);
    ma_encoder encoder;
    ma_result result = ma_encoder_init_file(argv[2], &config, &encoder);
    if(result != MA_SUCCESS)
    {
        // Error
        printf("Failed to initialize encoder: %s\n", ma_result_description(result));
        ksr_shutdown(converter);
        return 1;
    }
    
    long total = ksr_get_sample_count(converter) + ksr_millis2samples(converter, 1000); /* +1s tail */
    long chunk = 4096;
    float *buf = malloc(chunk * channels * sizeof(float));

    ma_timer timer;
    ma_timer_init(&timer);
    
    while(total > 0)
    {
        long frames = total < chunk ? total : chunk;
        if(!ksr_player_get_stream(converter, AUDIO_FLOAT, (unsigned char*)buf, frames))
            break;
    
        ma_encoder_write_pcm_frames(&encoder, buf, frames, NULL);
        total -= frames;
    }

    printf("Converted in %.3f s\n", ma_timer_get_time_in_seconds(&timer));
    
    ma_encoder_uninit(&encoder);
    free(buf);
    ksr_shutdown(converter);
    return 0;
}