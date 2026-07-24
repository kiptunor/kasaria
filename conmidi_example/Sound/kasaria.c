#include <pthread.h>
#include <stdio.h>

#include "Sound.h"
#include "../../kasaria_lib/kasaria.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"


static pthread_mutex_t ksr_mutex = PTHREAD_MUTEX_INITIALIZER;



#define SAMPLE_RATE 48000
#define BUFFER_FRAMES 512


static Kasaria *ksr_inst;
static ma_device device;













void audio_callback(ma_device *dev, void *out, const void *in, ma_uint32 frames)
{
    pthread_mutex_lock(&ksr_mutex);
    //printf("Audio callback\n");
    ksr_render_float(ksr_inst, out, frames);  // stereo
    pthread_mutex_unlock(&ksr_mutex);
}


// Required since kasaria does not handle audio callbacks internally currently

void KSR_CreateAudioThread()
{
    printf("Audio thread created\n");
        
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format    = ma_format_f32;
        config.playback.channels  = 2;
        config.sampleRate         = SAMPLE_RATE;
        config.dataCallback       = audio_callback;
        config.periodSizeInFrames = BUFFER_FRAMES;
    
        if(ma_device_init(NULL, &config, &device) != MA_SUCCESS)
        {
            printf("Failed to open playback device.\n");
            ksr_shutdown(ksr_inst);
            return;
        }
    
        ma_device_start(&device);
        printf("Audio device started\n");
}


void KSR_SendDirectData(unsigned long int data)
{
    pthread_mutex_lock(&ksr_mutex);
        printf("MIDI event: 0x%lX\n", data);
        ksr_write_midi_packed(ksr_inst, data);
        pthread_mutex_unlock(&ksr_mutex);
}

int KSR_SendDirectDataLong(MIDIHDR *mid_ev, unsigned int size)
{
    pthread_mutex_lock(&ksr_mutex);
    unsigned char *data = mid_ev->lpData;
    unsigned int len = mid_ev->dwBytesRecorded;
    unsigned int i;
    
    for(i = 0; i + 2 < len; i += 3)
        ksr_write_midi(ksr_inst, data[i], data[i+1], data[i+2]);
    
    
    if (i < len)
    {
        unsigned char pad[3] = {0xF7, 0xF7, 0xF7};
        unsigned int remaining = len - i;
        
        for(unsigned int j = 0; j < remaining; j++)
            pad[j] = data[i + j];
        
        ksr_write_midi(ksr_inst, pad[0], pad[1], pad[2]);
    }

    pthread_mutex_unlock(&ksr_mutex);
    return 0;
}

int KSR_PrepareLongData(MIDIHDR *mid_hdr, unsigned int size)
{
    (void)mid_hdr;
    (void)size;
    return 0;
}

int KSR_UnprepareLongData(MIDIHDR *mid_hdr, unsigned int size)
{
    (void)mid_hdr;
    (void)size;
    return 0;
}

void KSR_Init()
{
    ksr_inst = ksr_init();

    printf("KSR_Init\n");

    ksr_set_amplification(ksr_inst, 100);
        ksr_set_sample_rate(ksr_inst, SAMPLE_RATE);
        ksr_set_control_rate(ksr_inst, SAMPLE_RATE / 4);
        ksr_set_max_voices(ksr_inst, 5000);
        ksr_set_antialiasing(ksr_inst, 1);
        ksr_load_soundfont_file(ksr_inst, "/home/andre/disks/1_TB_1/bm/soundfonts/Full Grand Piano V2.sf2");
        ksr_force_instrument_load(ksr_inst);
}

void KSR_Shutdown()
{
    ksr_shutdown(ksr_inst);
}