#include <stdio.h>

#include "Sound.h"
#include "../../src/kasaria.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../../src/ext_deps/miniaudio/miniaudio.h"






#define SAMPLE_RATE 48000



static Kasaria *ksr_inst;






void KSR_SendDirectData(unsigned long int data)
{
    ksr_write_midi_ev_packed(ksr_inst, data);
}

int KSR_SendDirectDataLong(MIDIHDR *mid_ev, unsigned int size)
{
    unsigned char *data = mid_ev->lpData;
    unsigned int len = mid_ev->dwBytesRecorded;
    unsigned int i;
    
    for(i = 0; i + 2 < len; i += 3)
        ksr_write_midi_ev(ksr_inst, data[i], data[i+1], data[i+2]);
    
    
    if(i < len)
    {
        unsigned char pad[3] = {0xF7, 0xF7, 0xF7};
        unsigned int remaining = len - i;
        
        for(unsigned int j = 0; j < remaining; j++)
            pad[j] = data[i + j];
        
        ksr_write_midi_ev(ksr_inst, pad[0], pad[1], pad[2]);
    }
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

    ksr_set_amplification(ksr_inst, 100);
    ksr_set_sample_rate(ksr_inst, SAMPLE_RATE);
    //ksr_set_control_rate(ksr_inst, SAMPLE_RATE / 4);
    ksr_set_max_voices(ksr_inst, 5000);
    ksr_set_antialiasing(ksr_inst, 0);
    ksr_set_note_velocity_skipping(ksr_inst, 0, 32, true);
    ksr_print_config(ksr_inst);
    ksr_load_soundfont_file(ksr_inst, "Full Grand Piano V2.sf2", true);
    ksr_load_soundfont_file(ksr_inst, "SgtPepperArc360.sf2", true);

    ksr_init_audio(ksr_inst, RAW_MIDI_EVENTS);
    ksr_start_audio(ksr_inst);
}

void KSR_Shutdown()
{
    ksr_stop_audio(ksr_inst);
    ksr_shutdown(ksr_inst);
}