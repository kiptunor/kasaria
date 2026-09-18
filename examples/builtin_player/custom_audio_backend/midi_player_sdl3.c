#include <stdio.h>
#include <string.h>


#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include "../../../src/kasaria.h"

#define SAMPLE_RATE   48000
#define BUFFER_FRAMES 512

static Kasaria *synth;
static int      song_finished = 0;

void            audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    (void)userdata;
    (void)total_amount;

    if(additional_amount == 0)
        return;

    int   frames_needed = additional_amount / (2 * sizeof(float));
    float raw_audio[BUFFER_FRAMES * 2];
    int   remaining = frames_needed;

    while(remaining > 0)
    {
        int chunk = remaining > BUFFER_FRAMES ? BUFFER_FRAMES : remaining;

        if(song_finished)
        {
            memset(raw_audio, 0, chunk * 2 * sizeof(float));
            SDL_PutAudioStreamData(stream, raw_audio, chunk * 2 * sizeof(float));
            return;
        }

        // Play the midi in realtime and get the generated audio frames to feed into the audio device (as raw PCM)
        int rendered = ksr_player_get_stream(synth, AUDIO_FLOAT, (uint8_t *)raw_audio, chunk);

        // Send out silence if no audio from the player is available
        if(!rendered)
        {
            song_finished = 1;
            memset(raw_audio, 0, chunk * 2 * sizeof(float));
            SDL_PutAudioStreamData(stream, raw_audio, chunk * 2 * sizeof(float));
            return;
        }

        // If not feed the generated audio frames from the midi player to the SDL audio stream as raw PCM in float format
        SDL_PutAudioStreamData(stream, raw_audio, chunk * 2 * sizeof(float));
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

    // Try to initialize the SDL audio subsystem
    if(!SDL_Init(SDL_INIT_AUDIO))
    {
        printf("Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    // Initialize and create an instance synth
    synth = ksr_init(0);

    
    if(!synth)
    {
        printf("Failed to initialize Kasaria.\n");
        SDL_Quit();
        return 1;
    }

    // Override few default settings
    ksr_config_set_sample_rate(synth, SAMPLE_RATE);
    ksr_config_set_max_voices(synth, 5000);
    ksr_config_set_antialiasing(synth, 1);
    ksr_config_set_fast_decay(synth, true);
    ksr_config_set_note_skipping(synth, 0, 20, true);
    
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
        SDL_Quit();
        return 1;
    }

    // Optionally, you can print the time duration of the MIDI Loader
    printf("Duration: %d ms\n", ksr_get_duration(synth));

    // Initialize an audio stream handler
    SDL_AudioSpec spec;
    spec.format             = SDL_AUDIO_F32;
    spec.channels           = 2;
    spec.freq               = SAMPLE_RATE;

    // Open an audio device for streaming and just like the miniaudio example, the midi player is also ran by the audio thread
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);

    if(!stream)
    {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        ksr_shutdown(synth);
        SDL_Quit();
        return 1;
    }

    // Start the audio thread streaming
    SDL_ResumeAudioStreamDevice(stream);

    
    printf("Playing... Press Enter to stop.\n");
    getchar();

    SDL_DestroyAudioStream(stream);
    ksr_shutdown(synth);
    SDL_Quit();
    return 0;
}