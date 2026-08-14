// Minimal test - no threads, no audio device
#include "src/kasaria.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int main(void)
{
    Kasaria *ksr = ksr_init();
    
    ksr_set_amplification(ksr, 100);
    ksr_set_sample_rate(ksr, 48000);
    ksr_set_control_rate(ksr, 12000);
    ksr_set_max_voices(ksr, 256);
    
    int r = ksr_load_soundfont_file(ksr, "Full Grand Piano V2.sf2", true);
    printf("SF2 load: %d\n", r);
    
    //ksr_force_instrument_load(ksr);
    ksr_reset(ksr);
    
    // Send Note On: ch0, note 60 (C4), velocity 100
    ksr_write_midi_ev(ksr, 0x90, 60, 100);
    
    // Render
    float buf[8192];
    memset(buf, 0, sizeof(buf));
    ksr_render_float(ksr, buf, 2048);
    
    float max = 0;
    for (int i = 0; i < 4096; i++) {
        float v = fabsf(buf[i]);
        if (v > max) max = v;
    }
    printf("Max sample: %f\n", max);
    
    // If silence, try setting default instrument
    if (max < 0.001f) {
        printf("Silence - trying with program change\n");
        ksr_channel_set_program(ksr, 0, 0);
        ksr_write_midi_ev(ksr, 0x90, 60, 100);
        memset(buf, 0, sizeof(buf));
        ksr_render_float(ksr, buf, 2048);
        max = 0;
        for (int i = 0; i < 4096; i++) {
            float v = fabsf(buf[i]);
            if (v > max) max = v;
        }
        printf("Max sample with program: %f\n", max);
    }
    
    ksr_shutdown(ksr);
    return 0;
}