#include <stdio.h>
#include <string.h>


#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include "kasaria_lib/kasaria.h"

#define SAMPLE_RATE 48000
#define BUFFER_FRAMES 512

static Kasaria *synth;
static int song_finished = 0;

void audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    (void)userdata;
    (void)total_amount;

    if(additional_amount == 0)
        return;

    int frames_needed = additional_amount / (2 * sizeof(float));
    float interleaved[BUFFER_FRAMES * 2];
    int remaining = frames_needed;

    while(remaining > 0)
    {
        int chunk = remaining > BUFFER_FRAMES ? BUFFER_FRAMES : remaining;

        if(song_finished)
        {
            memset(interleaved, 0, chunk * 2 * sizeof(float));
            SDL_PutAudioStreamData(stream, interleaved, chunk * 2 * sizeof(float));
            return;
        }

        int rendered = ksr_play_midi(synth, AUDIO_FLOAT, (uint8_t *)interleaved, chunk);

        if(!rendered)
        {
            song_finished = 1;
            memset(interleaved, 0, chunk * 2 * sizeof(float));
            SDL_PutAudioStreamData(stream, interleaved, chunk * 2 * sizeof(float));
            return;
        }

        SDL_PutAudioStreamData(stream, interleaved, chunk * 2 * sizeof(float));
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

    if(!SDL_Init(SDL_INIT_AUDIO))
    {
        printf("Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    synth = ksr_init();
    if(!synth)
    {
        printf("Failed to initialize Timidity.\n");
        SDL_Quit();
        return 1;
    }

    ksr_set_sample_rate(synth, SAMPLE_RATE);
    ksr_set_max_voices(synth, 5000);
    ksr_load_soundfont_file(synth, "/home/andre/disks/1_TB_1/bm/soundfonts/Full Grand Piano V2.sf2");
    ksr_set_antialiasing(synth, 5);

    if(!ksr_load_config(synth, "timidity.cfg"))
    {
        if(!ksr_load_config(synth, "/etc/timidity.cfg"))
        {
            if(!ksr_load_config(synth, "/etc/timidity/timidity.cfg"))
            {
                printf("WARNING: No timidity.cfg found. Instruments may not load.\n");
                printf("Place timidity.cfg with GUS patch paths next to the executable.\n");
            }
        }
    }

    printf("Loading midi\n");
    if(!ksr_load_midi_file(synth, argv[1]))
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(synth);
        SDL_Quit();
        return 1;
    }
    ksr_preload_instruments(synth);

    printf("Duration: %d ms\n", ksr_get_duration(synth));

    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq     = SAMPLE_RATE;

    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);

    if(!stream)
    {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        ksr_shutdown(synth);
        SDL_Quit();
        return 1;
    }

    SDL_ResumeAudioStreamDevice(stream);
    printf("Playing... Press Enter to stop.\n");
    getchar();

    SDL_DestroyAudioStream(stream);
    ksr_shutdown(synth);
    SDL_Quit();
    return 0;
}