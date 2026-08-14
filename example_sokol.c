#define _POSIX_C_SOURCE 200809L

#define SOKOL_IMPL
#define SOKOL_AUDIO_IMPL
#include "sokol_audio.h"




#include <stdio.h>
#include <string.h>



#include "src/kasaria.h"

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
        int rendered = ksr_play_midi_raw(synth, AUDIO_FLOAT, (uint8_t *)raw_audio, chunk);

        if(!rendered)
        {
            song_finished = 1;
            memset(out, 0, remaining * 2 * sizeof(float));
            return;
        }

        for(int i = 0; i < chunk; i++)
        {
            out[i * 2 + 0] = raw_audio[i * 2 + 0];
            out[i * 2 + 1] = raw_audio[i * 2 + 1];
        }

        out       += chunk * 2;
        remaining -= chunk;

        // printf("Active voices: %d\n", timid_get_active_voices(synth));
    }
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        printf("Usage: %s <midi_file>\n", argv[0]);
        return 1;
    }

    synth = ksr_init(0);
    if(!synth)
    {
        printf("Failed to initialize Kasaria.\n");
        return 1;
    }

    ksr_set_sample_rate(synth, SAMPLE_RATE);
    ksr_set_max_voices(synth, 5000);
    ksr_load_soundfont_file(synth, "Full Grand Piano V2.sf2", true);
    ksr_load_soundfont_file(synth, "SgtPepperArc360.sf2", true);
    ksr_set_antialiasing(synth, 1);

    printf("Loading midi\n");
    if(!ksr_load_midi_file(synth, MIDI_MEMORY, argv[1]))
    {
        printf("Failed to load MIDI file: %s\n", argv[1]);
        ksr_shutdown(synth);
        return 1;
    }

    printf("Duration: %d ms\n", ksr_get_duration(synth));

    saudio_setup(&(saudio_desc) {
        .sample_rate   = SAMPLE_RATE,
        .num_channels  = 2,
        .buffer_frames = BUFFER_FRAMES,
        .stream_cb     = data_callback,
    });

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