/*

Kasaria -- A powerful and High efficiency MIDI Synth based on TiMidity
Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>
Copyright (C) 2026 Kiptunor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

Previously named: instrum.c

It provided code to load and unload GUS-compatible instrument patches.
With the current changes the old GUS Patch code is replaced by the SF2 soundfont code.

*/







#include <math.h>
#include <stdio.h>

#ifndef _WIN32_WCE
    #include <string.h>
#endif

#if defined(__FreeBSD__) || defined(__WIN32__)
    #include <stdlib.h>
#else
    #include <malloc.h>
#endif

#include "ext_deps/log_c/log.h"

#include "ksr_internal.h"
#include "ksr_sf2.h"



static ToneBank standard_tonebank, standard_drumset;
ToneBank *tonebank[128 + MAP_BANK_COUNT] = {&standard_tonebank};
ToneBank *drumset[128 + MAP_BANK_COUNT] = {&standard_drumset};


static void init_tone_bank_element(ToneBankElement *tone)
{
	tone->note           = -1;
	tone->strip_loop     = -1;
	tone->strip_envelope = -1;
	tone->strip_tail     = -1;
	tone->amp            = -1;
	tone->amp_normalize  = 0;
	tone->lokey          = -1;
	tone->hikey          = -1;
	tone->lovel          = -1;
	tone->hivel          = -1;
	tone->rnddelay       = 0;
	tone->loop_timeout   = 0;
	tone->legato         = 0;
	tone->damper_mode    = 0;
	tone->key_to_fc      = 0;
	tone->vel_to_fc      = 0;
	tone->reverb_send    = DEFALT_REVERB_SEND; // def -1
	tone->chorus_send    = DEFALT_CHORUS_SEND; // def -1
	tone->delay_send     = DEFALT_DELAY_SEND;  // def -1
	tone->lpf_type       = -1;
	tone->rx_note_off    = 1;
	tone->keep_voice     = 0;
	tone->tva_level      = -1;
	tone->play_note      = -1;
	tone->element_num    = 0;
	tone->def_pan        = -1;
	tone->sample_pan     = -1;
	tone->sample_width   = -1;
	tone->vfxe_num       = 0;
	tone->seq_length     = 0;
	tone->seq_position   = 0;
	//tone->lorand = -1;
	//tone->hirand = -1;
}

int alloc_tone_bank_element(ToneBankElement *tone)
{
	tone = (ToneBankElement *)safe_malloc(sizeof(ToneBankElement));
	
	if(tone == NULL)
	{
		// ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_tone_bank_element: ToneBankElement malloc error.");
		log_error("alloc_tone_bank_element: ToneBankElement malloc error.");
		return 1; // error
	}
	
	memset(tone, 0, sizeof(ToneBankElement));
	init_tone_bank_element(tone);
	return 0;
}

void alloc_instrument_bank(int dr, int bk)
{
    ToneBank *b;

    if(dr)
    {
	    if((b = drumset[bk]) == NULL)
	    {
		    b = drumset[bk] = (ToneBank *)safe_malloc(sizeof(ToneBank));
		    
		    if(drumset[bk] == NULL)
		    {
		        //ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_instrument_bank: ToneBank malloc error. drumset");
		        log_error("alloc_instrument_bank: ToneBank malloc error. drumset");
		        return;
		    }
			
		    memset(b, 0, sizeof(ToneBank));
			
		    if(alloc_tone_bank_element(&b->tone[0]))
		    {
		        //ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_instrument_bank: ToneBankElement malloc error. drumset");
		        log_error("alloc_instrument_bank: ToneBankElement malloc error. drumset");
		        return;
		    }
	    }
    }
    else
    {
	    if((b = tonebank[bk]) == NULL)
	    {
		    b = tonebank[bk] = (ToneBank *)safe_malloc(sizeof(ToneBank));
		    
		    if(tonebank[bk] == NULL)
		    {
		        // ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_instrument_bank: ToneBank malloc error. tonebank");
		        log_error("alloc_instrument_bank: ToneBank malloc error. tonebank");
		        return;
		    }
			
		    memset(b, 0, sizeof(ToneBank));
			
		    if(alloc_tone_bank_element(&b->tone[0]))
		    {
		        // ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_instrument_bank: ToneBankElement malloc error. tonebank");
		        log_error("alloc_instrument_bank: ToneBankElement malloc error. tonebank");
		        return;
		    }
	    }
    }
}

static int fill_bank(Kasaria *ksr, int dr, int b)
{
    int       i, errors = 0;
    ToneBank *bank = ((dr) ? ksr->drumset[b] : ksr->tonebank[b]);
    
    if(!bank)
        return 0;
    
    if(dr && ksr->sf_loaded)
    {
        Instrument *drum_instr = NULL;
        for(i = 0; i < 128; i++)
        {
            if(bank->tone[i].instrument == MAGIC_LOAD_INSTRUMENT)
            {
                // if(!drum_instr)
                //     drum_instr = load_soundfont_instrument(ksr, &ksr->sf_info, ksr->sf_filename, b + 128, 0);

                bank->tone[i].instrument = drum_instr;
                if(!drum_instr)
                    errors++;
            }
        }
        return errors;
    }
    
    for(i = 0; i < 128; i++)
    {
        if(bank->tone[i].instrument == MAGIC_LOAD_INSTRUMENT)
        {
            if(ksr->sf_loaded)
            {
                //bank->tone[i].instrument = load_soundfont_instrument(ksr, &ksr->sf_info, ksr->sf_filename, b, i);
                //if(bank->tone[i].instrument)
                //    continue;
            }

            bank->tone[i].instrument = 0;
            errors++;
        }
    }
    return errors;
}

int load_missing_instruments(Kasaria *ksr)
{
    int i = 128, errors = 0;
    while(i--)
    {
        if(ksr->tonebank[i])
            errors += fill_bank(ksr, 0, i);

        if(ksr->drumset[i])
            errors += fill_bank(ksr, 1, i);
    }
    return errors;
}

static void free_instrument(Instrument *ip)
{
    Sample *sp;
    int     i;
    if(!ip)
        return;

    for(i = 0; i < ip->samples; i++)
    {
        sp = &(ip->sample[i]);
        free(sp->data);
    }

    free(ip->sample);
    free(ip);
}

void free_default_instrument(Kasaria *ksr)
{
    log_debug("Clear default instrument");
    if(ksr->default_instrument)
    {
        free_instrument(ksr->default_instrument);
        ksr->default_instrument = 0;
        ksr->default_program    = DEFAULT_PROGRAM;
    }
}

int preload_soundfont_instruments(Kasaria *ksr)
{
    int i;
    int bank;
    int program;
    Instrument *inst;
    
    if(!ksr || !ksr->sf_loaded || !ksr->sf_info)
        return 0;

    log_debug("SF2 Preset count: %d", ksr->sf_info->npresets);
    // for(int i = 0; i < ksr->sf_info->npresets; i++)
    //     log_trace("preset %d: bank=%d preset=%d name=%s", i, ksr->sf_info->preset[i].bank, ksr->sf_info->preset[i].preset, ksr->sf_info->preset[i].hdr.name);
    
    log_debug("Preloading soundfont instruments");
    
    for(i = 0; i < ksr->sf_info->npresets; i++)
    {
        bank    = ksr->sf_info->preset[i].bank;
        program = ksr->sf_info->preset[i].preset;

        if(program < 0 || program > 127)
            continue;

        if(bank == 128)   // SF2 percussion bank
        {
            inst = sndfont_load_instrument(ksr, bank, program);
        
            if(!inst)
            {
                log_error("SF2: drum load failed bank=%d program=%d", bank, program);
                continue;
            }
            
            if(!ksr->drumset[0])
            {
                ksr->drumset[0] = (ToneBank *)safe_malloc(sizeof(ToneBank));
                memset(ksr->drumset[0], 0, sizeof(ToneBank));
            }
            
            for(int k = 0; k < inst->samples; k++)
            {
                Sample *sp = &inst->sample[k];
                for(int key = sp->low_key; key <= sp->high_key && key < 128; key++)
                    if(key >= 0)
                        ksr->drumset[0]->tone[key].instrument = inst;
            }
            continue;
        }
        
        if(bank < 0 || bank > 127)
            continue;
    
        if(!ksr->tonebank[bank])
        {
            ksr->tonebank[bank] = (ToneBank *)safe_malloc(sizeof(ToneBank));
            memset(ksr->tonebank[bank], 0, sizeof(ToneBank));
        }
        
        if(ksr->tonebank[bank]->tone[program].instrument)
            continue;
        
        inst = sndfont_load_instrument(ksr, bank, program);
        
        if(inst)
            ksr->tonebank[bank]->tone[program].instrument = inst;
        else
            log_error("SF2: failed to load instrument bank=%d program=%d", bank, program);
    }
    
    return 1;
}