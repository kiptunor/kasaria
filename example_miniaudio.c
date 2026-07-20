#include <stdio.h>
#include <string.h>


#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "kasaria_lib/kasaria.h"

#define SAMPLE_RATE 48000
#define BUFFER_FRAMES 512

static Kasaria *synth;
static int song_finished = 0;

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    (void)pInput;

    if(song_finished)
    {
        memset(pOutput, 0, frameCount * 2 * sizeof(float));
        return;
    }

    float interleaved[BUFFER_FRAMES * 2];
    float *out = (float *)pOutput;
    int remaining = frameCount;

    while(remaining > 0)
    {
        int chunk = remaining > BUFFER_FRAMES ? BUFFER_FRAMES : remaining;
        int rendered = ksr_play_midi(synth, AU_FLOAT, (uint8_t *)interleaved, chunk);

        if(!rendered)
        {
            song_finished = 1;
            memset(out, 0, remaining * 2 * sizeof(float));
            return;
        }

        for(int i = 0; i < chunk; i++)
        {
            out[i * 2 + 0] = interleaved[i * 2 + 0];
            out[i * 2 + 1] = interleaved[i * 2 + 1];
        }
        out += chunk * 2;
        remaining -= chunk;

        //printf("Active voices: %d\n", timid_get_active_voices(synth));
    }
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        printf("Usage: %s <midi_file>\n", argv[0]);
        return 1;
    }

    synth = ksr_init();
    if(!synth)
    {
        printf("Failed to initialize Timidity.\n");
        return 1;
    }

    ksr_set_sample_rate(synth, SAMPLE_RATE);
    ksr_set_max_voices(synth, 5000);
    ksr_load_soundfont_file(synth, "/home/andre/disks/1_TB_1/bm/soundfonts/Full Grand Piano V2.sf2");
    ksr_set_antialiasing(synth, 5);

    /* Try loading timidity.cfg from current dir or common locations */

    
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
        return 1;
    }

    //timid_load_soundfont_file(synth, "/home/andre/disks/1_TB_1/bm/soundfonts/Full Grand Piano V2.sf2");
    //printf("Loaded soundfont\n");

    printf("Duration: %d ms\n", ksr_get_duration(synth));
    //printf("Active voices: %d\n", timid_get_active_voices(synth));

    ma_device_config config   = ma_device_config_init(ma_device_type_playback);
    config.playback.format    = ma_format_f32;
    config.playback.channels  = 2;
    config.sampleRate         = SAMPLE_RATE;
    config.dataCallback       = data_callback;
    config.periodSizeInFrames = BUFFER_FRAMES;

    ma_device device;
    
    if(ma_device_init(NULL, &config, &device) != MA_SUCCESS)
    {
        printf("Failed to open playback device.\n");
        ksr_shutdown(synth);
        return 1;
    }

    ma_device_start(&device);
    printf("Playing... Press Enter to stop.\n");
    getchar();

    ma_device_uninit(&device);
    ksr_shutdown(synth);
    return 0;
}