/*

TiMidity -- Experimental MIDI to WAVE converter
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

instrum.c

Code to load and unload GUS-compatible instrument patches.

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
    Instrument   *ip;
    Sample       *sp;
    int           i, j, k;
    int           preset_idx, inst_idx, sample_idx;
    SFPresetHdr  *preset;
    SFInstHdr    *inst_hdr;
    SFGenLayer   *inst_zone;
    SFSampleInfo *sfsample;
    int           total_samples, count;
    long          start, end, loop_start, loop_end, loop_mode;
    long          attenuation, pan;
    int           root_key, fine_tune, coarse_tune, sample_flags;
    int           gen_val;
    int           attack_tc, decay_tc, sustain_level, release_tc;
    int           low_key, high_key;

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
        return NULL;

    ip          = (Instrument *)safe_malloc(sizeof(Instrument));
    ip->samples = total_samples;
    ip->sample  = (Sample *)safe_malloc(sizeof(Sample) * total_samples);
    memset(ip->sample, 0, sizeof(Sample) * total_samples);

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

            sp          = &ip->sample[count];

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

            fine_tune       = sf_find_gen(inst_zone, SF_FINETUNE, 0);
            coarse_tune     = sf_find_gen(inst_zone, SF_COARSETUNE, 0);
            attenuation     = sf_find_gen(inst_zone, SF_INITATTEN, 0);
            pan             = sf_find_gen(inst_zone, SF_PAN, 0);

            low_key         = sf_find_gen(inst_zone, SF_KEYRANGE, 0x7F00) & 0xFF;
            high_key        = (sf_find_gen(inst_zone, SF_KEYRANGE, 0x7F00)) >> 8;

            sp->sample_rate = sfsample->samplerate;

            if(sp->sample_rate <= 0)
                sp->sample_rate = 44100;

            sp->root_freq               = (long)(8.176 * pow(2.0, (root_key + coarse_tune + fine_tune / 100.0) / 12.0) * 1000.0);
            sp->low_freq                = (long)(8.176 * pow(2.0, (low_key + coarse_tune) / 12.0) * 1000.0);
            sp->high_freq               = (long)(8.176 * pow(2.0, (high_key + coarse_tune) / 12.0) * 1000.0);

            sp->volume                  = pow(10.0, -attenuation / 200.0);
            sp->panning                 = (u_char)((pan + 500) * 127 / 1000);
            sp->modes                   = MODES_16BIT | MODES_ENVELOPE | loop_mode;
            sp->note_to_use             = 0;

            sp->tremolo_sweep_increment = 0;
            sp->tremolo_phase_increment = 0;
            sp->tremolo_depth           = 0;
            sp->vibrato_sweep_increment = 0;
            sp->vibrato_control_ratio   = 0;
            sp->vibrato_depth           = 0;

            attack_tc                   = sf_find_gen(inst_zone, SF_ATTACKENV1, -12000);
            decay_tc                    = sf_find_gen(inst_zone, SF_DECAYENV1, -12000);
            sustain_level               = sf_find_gen(inst_zone, SF_SUSTAINENV1, 0);
            release_tc                  = sf_find_gen(inst_zone, SF_RELEASEENV1, -12000);

            sp->envelope_offset[0]      = 255 << (7 + 15);
            sp->envelope_rate[0]        = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(attack_tc));

            {
                f64 sus_amp            = pow(10.0, -sustain_level / 200.0);
                sp->envelope_offset[1] = (long)(sus_amp * 255.0) << (7 + 15);
            }
            sp->envelope_rate[1]   = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(decay_tc)) * (sp->envelope_offset[0] - sp->envelope_offset[1]) / sp->envelope_offset[0];

            sp->envelope_offset[2] = sp->envelope_offset[1];
            sp->envelope_rate[2]   = 0;

            sp->envelope_offset[3] = 0;
            sp->envelope_rate[3]   = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(release_tc)) * sp->envelope_offset[2] / (255 << (7 + 15));

            sp->envelope_offset[4] = 0;
            sp->envelope_rate[4]   = 0;
            sp->envelope_offset[5] = 0;
            sp->envelope_rate[5]   = 0;

            {
                long num_samples = end - start;
                long byte_offset;

                sp->data_length = num_samples;
                sp->loop_start  = loop_start - start;
                sp->loop_end    = loop_end - start;

                if(sp->loop_start < 0)
                    sp->loop_start = 0;

                if(sp->loop_end > num_samples)
                    sp->loop_end = num_samples;

                if(sp->loop_end <= sp->loop_start)
                {
                    sp->loop_start = 0;
                    sp->loop_end   = num_samples;
                }

                sp->data         = (sample_t *)safe_malloc((num_samples + 2) * sizeof(short));
                sp->data_alloced = 1;

                byte_offset      = sf->samplepos + start * sizeof(short);
                fseek(fp, byte_offset, SEEK_SET);

                if(fread(sp->data, sizeof(short), num_samples, fp) != (size_t)num_samples)
                {
                    free(sp->data);
                    sp->data = NULL;
                    continue;
                }

                sp->data[num_samples]     = 0;
                sp->data[num_samples + 1] = 0;

                {
                    long   _i;
                    short  _maxamp = 1, _a;
                    short *_tmp    = (short *)sp->data;
                    for(_i = 0; _i < num_samples; _i++)
                    {
                        _a = *_tmp++;
                        if(_a < 0)
                            _a = -_a;
                        if(_a > _maxamp)
                            _maxamp = _a;
                    }
                    sp->volume = 32768.0 / (f64)_maxamp * sp->volume;
                }

                if(ksr->antialiasing_allowed)
                    antialiasing(sp, ksr->play_mode.rate);

                sp->data_length <<= FRACTION_BITS;
                sp->loop_start  <<= FRACTION_BITS;
                sp->loop_end    <<= FRACTION_BITS;
            }

            count++;
        }
    }

    fclose(fp);

    if(count == 0)
    {
        free(ip->sample);
        free(ip);
        return NULL;
    }

    ip->samples = count;
    return ip;
}

void free_default_instrument(Kasaria *ksr)
{
    if(ksr->default_instrument)
    {
        free_instrument(ksr->default_instrument);
        ksr->default_instrument = 0;
        ksr->default_program    = DEFAULT_PROGRAM;
    }
}