#define _POSIX_C_SOURCE 200809L

#define SOKOL_IMPL
#define SOKOL_AUDIO_IMPL
#include "../../deps/sokol_audio.h"




#include <stdio.h>
#include <string.h>



#include "../../../src/kasaria.h"

#define SAMPLE_RATE   48000
#define BUFFER_FRAMES 512

static Kasaria *synth;
static int      song_finished = 0;

static void     data_callback(float *pOutput, int frameCount, int numChannels)
{
    (void)numChannels;

    if(song_finished)
    {
        memset(pOutput, 0, frameCount * 2 * sizeof(float));
        return;
    }

    float  raw_audio[BUFFER_FRAMES * 2];
    float *out       = pOutput;
    int    remaining = frameCount;

    while(remaining > 0)
    {
        int chunk    = remaining > BUFFER_FRAMES ? BUFFER_FRAMES : remaining;

        // Advance to the next MIDI events and generate the raw audio data as raw PCM in float format
        int rendered = ksr_player_get_stream(synth, AUDIO_FLOAT, (uint8_t *)raw_audio, chunk);

        // Send out silence if no audio from the player is available
        if(!rendered)
        {
            song_finished = 1;
            memset(out, 0, remaining * 2 * sizeof(float));
            return;
        }

        // Write the raw audio data to the output buffer separately for each channel
        for(int i = 0; i < chunk; i++)
        {
            out[i * 2 + 0] = raw_audio[i * 2 + 0];
            out[i * 2 + 1] = raw_audio[i * 2 + 1];
        }

        out       += chunk * 2;
        remaining -= chunk;
    }
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        printf("Usage: %s <midi_file>\n", argv[0]);
        return 1;
    }

    // Initialize Kasaria instance
    synth = ksr_init(0);

    
    if(!synth)
    {
        printf("Failed to initialize Kasaria.\n");
        return 1;
    }

    // Override few default settings
    ksr_config_set_sample_rate(synth, SAMPLE_RATE);
    ksr_config_set_antialiasing(synth, true);
    ksr_config_set_max_voices(synth, 5000);
    
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
    ksr_load_soundfont_file_new(synth, "Full Grand Piano V2.sf2", s2);

    printf("Loading midi\n");

    // As in the previous examples the MIDI file data is stored in RAM
    if(!ksr_load_midi_file(synth, MIDI_MEMORY, argv[1]))
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(synth);
        return 1;
    }

    // Optionally, you can print the time duration of the MIDI Loader
    printf("Duration: %d ms\n", ksr_get_duration(synth));

    // Initialize sokol audio device
    saudio_setup(&(saudio_desc)
    {
        .sample_rate   = SAMPLE_RATE,
        .num_channels  = 2,             // Set stereo audio channels
        .buffer_frames = BUFFER_FRAMES, // Audio frame size (Which cannot be smaller than 512)
        .stream_cb     = data_callback, // Once again, the midi player is ran by the audio thread
    });

    // Try to open the audio device and start the audio thread
    if(!saudio_isvalid())
    {
        printf("Failed to open playback device.\n");
        ksr_shutdown(synth);
        return 1;
    }

    printf("Playing... Press Enter to stop.\n");
    getchar();

    saudio_shutdown();
    ksr_shutdown(synth);
    return 0;
}