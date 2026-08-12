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

#include "ext_deps/ulog/src/ulog.h"

#include "ksr_internal.h"
#include "ksr_sf2.h"




/*

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

#include "ext_deps/ulog/src/ulog.h"

#include "ksr_internal.h"
#include "ksr_sf2.h"

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

static void free_bank(Kasaria *ksr, int dr, int b)
{
    int       i;
    ToneBank *bank = ((dr) ? ksr->drumset[b] : ksr->tonebank[b]);
    for(i = 0; i < 128; i++)
    {
        if(bank->tone[i].instrument)
        {
            if(bank->tone[i].instrument != MAGIC_LOAD_INSTRUMENT)
                free_instrument(bank->tone[i].instrument);

            bank->tone[i].instrument = 0;
        }
        if(bank->tone[i].name)
        {
            free(bank->tone[i].name);
            bank->tone[i].name = 0;
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
                if(!drum_instr)
                    drum_instr = load_soundfont_instrument(ksr, &ksr->sf_info, ksr->sf_filename, b + 128, 0);

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
                bank->tone[i].instrument = load_soundfont_instrument(ksr, &ksr->sf_info, ksr->sf_filename, b, i);
                if(bank->tone[i].instrument)
                    continue;
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

void free_instruments(Kasaria *ksr)
{
    int i = 128;

    while(i--)
    {
        if(ksr->tonebank[i])
        {
            free_bank(ksr, 0, i);
            free(ksr->tonebank[i]);
            ksr->tonebank[i] = 0;
        }
        if(ksr->drumset[i])
        {
            free_bank(ksr, 1, i);
            free(ksr->drumset[i]);
            ksr->drumset[i] = 0;
        }
    }
}

static int sf_find_gen(SFGenLayer *layer, int id, int def)
{
    int i;
    if(!layer)
        return def;
    for(i = 0; i < layer->nlists; i++)
    {
        if(layer->list[i].oper == id)
            return layer->list[i].amount;
    }
    return def;
}

static long sf_timecent_to_msec(int tc)
{
    if(tc <= -12000)
        return 1;
    if(tc >= 5000)
        return 30000;
    return (long)(1000.0 * pow(2.0, tc / 1200.0));
}

static long sf_calc_envelope_rate(Kasaria *ksr, long msec)
{
    long diff = 255;
    f64  rate;
    if(msec < 1)
        msec = 1;
    diff <<= (7 + 15);
    rate   = ((f64)diff / ksr->play_mode.rate) * ksr->control_ratio * 1000.0 / msec;
    if(ksr->fast_decay)
        rate *= 2;
    return (long)rate;
}

Instrument *load_soundfont_instrument(Kasaria *ksr, SFInfo *sf, const char *filename, int bank, int program)
{
    FILE         *fp;
    Instrument   *sf_instr;
    Sample       *sample;
    int           i, j, k;
    int           preset_idx;
    int           inst_idx;
    int           sample_idx;
    int           root_key;
    int           fine_tune;
    int           coarse_tune;
    int           sample_flags;
    int           gen_val;
    int           attack_tc;
    int           decay_tc;
    int           sustain_level;
    int           release_tc;
    int           total_samples;
    int           count;
    int           low_key;
    int           high_key;
    SFPresetHdr  *preset;
    SFInstHdr    *inst_hdr;
    SFGenLayer   *inst_zone;
    SFSampleInfo *sfsample;
    long          start;
    long          end;
    long          loop_start;
    long          loop_end;
    long          loop_mode;
    long          attenuation;
    long          pan;

    if(!sf || !sf->preset || !sf->inst || !sf->sample)
        return NULL;

    preset_idx = -1;
    for(i = 0; i < sf->npresets; i++)
    {
        if(sf->preset[i].bank == bank && sf->preset[i].preset == program)
        {
            preset_idx = i;
            break;
        }
    }

    if(preset_idx == -1)
    {
        for(i = 0; i < sf->npresets; i++)
        {
            if(sf->preset[i].bank == 0 && sf->preset[i].preset == program)
            {
                preset_idx = i;
                break;
            }
        }
    }
    if(preset_idx == -1)
        return NULL;

    preset        = &sf->preset[preset_idx];

    total_samples = 0;
    for(i = 0; i < preset->hdr.nlayers; i++)
    {
        inst_idx = sf_find_gen(&preset->hdr.layer[i], SF_INSTRUMENT, -1);
        if(inst_idx < 0 || inst_idx >= sf->ninsts)
            continue;

        inst_hdr = &sf->inst[inst_idx];

        for(j = 0; j < inst_hdr->hdr.nlayers; j++)
        {
            sample_idx = sf_find_gen(&inst_hdr->hdr.layer[j], SF_SAMPLEID, -1);
            if(sample_idx >= 0 && sample_idx < sf->nsamples)
            {
                sfsample = &sf->sample[sample_idx];
                if(!(sfsample->sampletype & 0x8000))
                    total_samples++;
            }
        }
    }

    if(total_samples == 0)
        return NULL;

    fp = fopen(filename, "rb");
    if(!fp)
    {
        ulog_topic_error("SF2", "Failed to load SoundFont instrument (bank: %d, program: %d) from: '%s'", bank, program, filename);
        return NULL;
    }

    sf_instr          = (Instrument *)safe_malloc(sizeof(Instrument));
    sf_instr->samples = total_samples;
    sf_instr->sample  = (Sample *)safe_malloc(sizeof(Sample) * total_samples);
    memset(sf_instr->sample, 0, sizeof(Sample) * total_samples);

    count = 0;

    for(i = 0; i < preset->hdr.nlayers; i++)
    {
        inst_idx = sf_find_gen(&preset->hdr.layer[i], SF_INSTRUMENT, -1);
        if(inst_idx < 0 || inst_idx >= sf->ninsts)
            continue;

        low_key  = sf_find_gen(&preset->hdr.layer[i], SF_KEYRANGE, 0x7F00) & 0xFF;
        high_key = (sf_find_gen(&preset->hdr.layer[i], SF_KEYRANGE, 0x7F00)) >> 8;

        inst_hdr = &sf->inst[inst_idx];

        for(j = 0; j < inst_hdr->hdr.nlayers; j++)
        {
            inst_zone  = &inst_hdr->hdr.layer[j];
            sample_idx = sf_find_gen(inst_zone, SF_SAMPLEID, -1);
            if(sample_idx < 0 || sample_idx >= sf->nsamples)
                continue;

            sfsample = &sf->sample[sample_idx];

            if(sfsample->sampletype & 0x8000)
                continue;

            sample      = &sf_instr->sample[count];

            start       = sfsample->startsample;
            end         = sfsample->endsample;
            loop_start  = sfsample->startloop;
            loop_end    = sfsample->endloop;

            gen_val     = sf_find_gen(inst_zone, SF_STARTADDRS, 0);
            start      += gen_val;
            gen_val     = sf_find_gen(inst_zone, SF_STARTADDRSHI, 0);
            start      += gen_val * 32768;

            gen_val     = sf_find_gen(inst_zone, SF_ENDADDRS, 0);
            end        += gen_val;
            gen_val     = sf_find_gen(inst_zone, SF_ENDADDRSHI, 0);
            end        += gen_val * 32768;

            gen_val     = sf_find_gen(inst_zone, SF_STARTLOOP, 0);
            loop_start += gen_val;
            gen_val     = sf_find_gen(inst_zone, SF_STARTLOOPHI, 0);
            loop_start += gen_val * 32768;

            gen_val     = sf_find_gen(inst_zone, SF_ENDLOOP, 0);
            loop_end   += gen_val;
            gen_val     = sf_find_gen(inst_zone, SF_ENDLOOPHI, 0);
            loop_end   += gen_val * 32768;

            if(end > sfsample->endsample)
                end = sfsample->endsample;

            if(start < sfsample->startsample)
                start = sfsample->startsample;

            if(end <= start)
                continue;

            sample_flags = sf_find_gen(inst_zone, SF_SAMPLEFLAGS, 0);
            if(sample_flags == 0)
                sample_flags = sfsample->sampletype & 0x3;

            if(sample_flags & 1)
            {
                loop_mode = MODES_LOOPING | MODES_SUSTAIN;
                if(sample_flags & 2)
                    loop_mode |= MODES_PINGPONG;
            }
            else
                loop_mode = 0;

            root_key = sf_find_gen(inst_zone, SF_ROOTKEY, -1);
            if(root_key < 0 || root_key > 127)
                root_key = sfsample->originalPitch;
            if(root_key < 0 || root_key > 127)
                root_key = 60;

            fine_tune           = sf_find_gen(inst_zone, SF_FINETUNE, 0);
            coarse_tune         = sf_find_gen(inst_zone, SF_COARSETUNE, 0);
            attenuation         = sf_find_gen(inst_zone, SF_INITATTEN, 0);
            pan                 = sf_find_gen(inst_zone, SF_PAN, 0);

            int keyrange = sf_find_gen(inst_zone, SF_KEYRANGE, -1);
            if(keyrange == 0x7F00 && bank >= 128)
            {
                low_key  = root_key;
                high_key = root_key;
            }
            else if(keyrange < 0)
            {
                low_key  = 0;
                high_key = 127;
            }
            else
            {
                low_key  = keyrange & 0xFF;
                high_key = (keyrange >> 8) & 0xFF;
            }

            sample->sample_rate = sfsample->samplerate;

            if(sample->sample_rate <= 0)
                sample->sample_rate = 44100;

            sample->root_freq               = (long)(8.176 * pow(2.0, (root_key + coarse_tune + fine_tune / 100.0) / 12.0) * 1000.0);
            sample->low_freq                = (long)(8.176 * pow(2.0, (low_key + coarse_tune) / 12.0) * 1000.0);
            sample->high_freq               = (long)(8.176 * pow(2.0, (high_key + coarse_tune) / 12.0) * 1000.0);

            sample->volume                  = pow(10.0, -attenuation / 200.0);
            sample->panning                 = (char)((pan * 63 / 5000) + 64);
            sample->modes                   = MODES_16BIT | MODES_ENVELOPE | loop_mode;
            sample->note_to_use             = (bank >= 128) ? root_key : 0;

            //sample->tremolo_sweep_increment = 0;
            //sample->tremolo_phase_increment = 0;
            //sample->tremolo_depth           = 0;
            //sample->vibrato_sweep_increment = 0;
            //sample->vibrato_control_ratio   = 0;
            //sample->vibrato_depth           = 0;

            attack_tc                       = sf_find_gen(inst_zone, SF_ATTACKENV1, -12000);
            decay_tc                        = sf_find_gen(inst_zone, SF_DECAYENV1, -12000);
            sustain_level                   = sf_find_gen(inst_zone, SF_SUSTAINENV1, 0);
            release_tc                      = sf_find_gen(inst_zone, SF_RELEASEENV1, -12000);

            sample->envelope_offset[0]      = 255 << (7 + 15);
            sample->envelope_rate[0]        = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(attack_tc));

            {
                f64 sus_amp                = pow(10.0, -sustain_level / 200.0);
                sample->envelope_offset[1] = (long)(sus_amp * 255.0) << (7 + 15);
            }
            sample->envelope_rate[1]   = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(decay_tc)) * (sample->envelope_offset[0] - sample->envelope_offset[1]) / sample->envelope_offset[0];

            sample->envelope_offset[2] = sample->envelope_offset[1];
            sample->envelope_rate[2]   = 0;

            sample->envelope_offset[3] = 0;
            sample->envelope_rate[3]   = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(release_tc)) * sample->envelope_offset[2] / (255 << (7 + 15));

            sample->envelope_offset[4] = 0;
            sample->envelope_rate[4]   = 0;
            sample->envelope_offset[5] = 0;
            sample->envelope_rate[5]   = 0;

            {
                long num_samples = end - start;
                long byte_offset;

                sample->data_length = num_samples;
                sample->loop_start  = loop_start - start;
                sample->loop_end    = loop_end - start;

                if(sample->loop_start < 0)
                    sample->loop_start = 0;

                if(sample->loop_end > num_samples)
                    sample->loop_end = num_samples;

                if(sample->loop_end <= sample->loop_start)
                {
                    sample->loop_start = 0;
                    sample->loop_end   = num_samples;
                }

                sample->data         = (sample_t *)safe_malloc((num_samples + 2) * sizeof(short));
                sample->data_alloced = 1;

                byte_offset          = sf->samplepos + start * sizeof(short);
                fseek(fp, byte_offset, SEEK_SET);

                if(fread(sample->data, sizeof(short), num_samples, fp) != (size_t)num_samples)
                {
                    free(sample->data);
                    sample->data = NULL;
                    continue;
                }

                sample->data[num_samples]     = 0;
                sample->data[num_samples + 1] = 0;

                {
                    long   _i;
                    short  _maxamp = 1, _a;
                    short *_tmp    = (short *)sample->data;
                    for(_i = 0; _i < num_samples; _i++)
                    {
                        _a = *_tmp++;
                        if(_a < 0)
                            _a = -_a;
                        if(_a > _maxamp)
                            _maxamp = _a;
                    }
                    sample->volume = 32768.0 / (f64)_maxamp * sample->volume;
                }

                if(ksr->antialiasing_allowed)
                    antialiasing(sample, ksr->play_mode.rate);

                sample->data_length <<= FRACTION_BITS;
                sample->loop_start  <<= FRACTION_BITS;
                sample->loop_end    <<= FRACTION_BITS;
            }

            count++;
        }
    }

    fclose(fp);

    if(count == 0)
    {
        free(sf_instr->sample);
        free(sf_instr);
        return NULL;
    }

    sf_instr->samples = count;
    return sf_instr;
}

int preload_soundfont_instruments(Kasaria *ksr)
{
    ulog_topic_debug("SF2", "Preloading soundfont instruments for %s", ksr->sf_filename);
    int i, b, p;

    if(!ksr->sf_loaded)
        return 0;

    for(i = 0; i < ksr->sf_info.npresets; i++)
    {
        b = ksr->sf_info.preset[i].bank;
        p = ksr->sf_info.preset[i].preset;

        if(b < 0 || b > 127 || p < 0 || p > 127)
            continue;

        if(!ksr->tonebank[b])
        {
            ksr->tonebank[b] = (ToneBank *)safe_malloc(sizeof(ToneBank));
            memset(ksr->tonebank[b], 0, sizeof(ToneBank));
        }

        if(!ksr->tonebank[b]->tone[p].instrument)
            ksr->tonebank[b]->tone[p].instrument = load_soundfont_instrument(ksr, &ksr->sf_info, ksr->sf_filename, b, p);
    }

    return 1;
}

void free_default_instrument(Kasaria *ksr)
{
    ulog_debug("Clear default instrument");
    if(ksr->default_instrument)
    {
        free_instrument(ksr->default_instrument);
        ksr->default_instrument = 0;
        ksr->default_program    = DEFAULT_PROGRAM;
    }
}

*/


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
			return;
		}
	    memset(b, 0, sizeof(ToneBank));
		if(alloc_tone_bank_element(&b->tone[0]))
		{
			//ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_instrument_bank: ToneBankElement malloc error. drumset");
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
			return;
		}
	    memset(b, 0, sizeof(ToneBank));
		if(alloc_tone_bank_element(&b->tone[0]))
		{
			// ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "alloc_instrument_bank: ToneBankElement malloc error. tonebank");
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
    ulog_debug("Clear default instrument");
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
    
        ulog_topic_debug(
            "SF2",
            "Preloading soundfont instruments"
        );
    
        for(i = 0; i < ksr->sf_info->npresets; i++)
        {
            bank    = ksr->sf_info->preset[i].bank;
            program = ksr->sf_info->preset[i].preset;
    
            if(bank < 0 || bank > 127 ||
               program < 0 || program > 127)
                continue;
    
            if(!ksr->tonebank[bank])
            {
                ksr->tonebank[bank] =
                    (ToneBank *)safe_malloc(sizeof(ToneBank));
    
                memset(
                    ksr->tonebank[bank],
                    0,
                    sizeof(ToneBank)
                );
            }
    
            if(ksr->tonebank[bank]->tone[program].instrument)
                continue;
    
            inst = sndfont_load_instrument(
                ksr,
                bank,
                program
            );
    
            ulog_debug(
                "SF2 PRELOAD: bank=%d program=%d inst=%p",
                bank,
                program,
                (void *)inst
            );
    
            if(inst)
            {
                ksr->tonebank[bank]
                    ->tone[program]
                    .instrument = inst;
            }
            else
            {
                ulog_debug(
                    "SF2: failed to load instrument "
                    "bank=%d program=%d",
                    bank,
                    program
                );
            }
        }
    
        return 1;
}