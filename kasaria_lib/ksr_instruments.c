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
            /* Not that this could ever happen, of course */
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

static long convert_envelope_rate(Kasaria *ksr, u_char rate)
{
    long r;

    r  = 3 - ((rate >> 6) & 0x3);
    r *= 3;
    r  = (long)(rate & 0x3f) << r; /* 6.9 fixed point */

    /* 15.15 fixed point. */
    return (((r * 44100) / ksr->play_mode.rate) * ksr->control_ratio) << ((ksr->fast_decay) ? 10 : 9);
}

static long convert_envelope_offset(u_char offset)
{
    /* This is not too good... Can anyone tell me what these values mean?
    Are they GUS-style "exponential" volumes? And what does that mean? */

    /* 15.15 fixed point */
    return offset << (7 + 15);
}

static long convert_tremolo_sweep(Kasaria *ksr, u_char sweep)
{
    if(!sweep)
        return 0;

    return ((ksr->control_ratio * SWEEP_TUNING) << SWEEP_SHIFT) / (ksr->play_mode.rate * sweep);
}

static long convert_vibrato_sweep(Kasaria *ksr, u_char sweep, long vib_control_ratio)
{
    if(!sweep)
        return 0;

    return (long)(FSCALE((f64)(vib_control_ratio)*SWEEP_TUNING, SWEEP_SHIFT) / (f64)(ksr->play_mode.rate * sweep));

    /* this was overflowing with seashore.pat

    ((vib_control_ratio * SWEEP_TUNING) << SWEEP_SHIFT) /
    (tm->play_mode.rate * sweep); */
}

static long convert_tremolo_rate(Kasaria *ksr, u_char rate)
{
    return ((SINE_CYCLE_LENGTH * ksr->control_ratio * rate) << RATE_SHIFT) / (TREMOLO_RATE_TUNING * ksr->play_mode.rate);
}

static long convert_vibrato_rate(Kasaria *ksr, u_char rate)
{
    /* Return a suitable vibrato_control_ratio value */
    return (VIBRATO_RATE_TUNING * ksr->play_mode.rate) / (rate * 2 * VIBRATO_SAMPLE_INCREMENTS);
}

static void reverse_data(short *sp, long ls, long le)
{
    short s, *ep = sp + le;
    sp += ls;
    le -= ls;
    le /= 2;

    while(le--)
    {
        s     = *sp;
        *sp++ = *ep;
        *ep-- = s;
    }
}

/*
If panning or note_to_use != -1, it will be used for all samples,
instead of the sample-specific values in the instrument file.

For note_to_use, any value <0 or >127 will be forced to 0.

For other parameters, 1 means yes, 0 means no, other values are
undefined.

TODO: do reverse loops right */
static Instrument *load_instrument(Kasaria *ksr, char *name, int percussion, int panning, int amp, int note_to_use, int strip_loop, int strip_envelope, int strip_tail)
{
    Instrument *ip;
    Sample     *sp;
    FILE       *fp;
    u_char      tmp[1024];
    int         i, j, noluck = 0;
#ifdef PATCH_EXT_LIST
    static char *patch_ext[] = PATCH_EXT_LIST;
#endif

    if(!name)
        return 0;

    /* Open patch file */
    if(!(fp = open_file(ksr, name, 1, OF_NORMAL)))
    {
        noluck = 1;
#ifdef PATCH_EXT_LIST
        /* Try with various extensions */
        for(i = 0; patch_ext[i]; i++)
        {
            if(strlen(name) + strlen(patch_ext[i]) < 1024)
            {
                strcpy((char *)tmp, name);
                strcat((char *)tmp, patch_ext[i]);
                if((fp = open_file(ksr, (char *)tmp, 1, OF_NORMAL)))
                {
                    noluck = 0;
                    break;
                }
            }
        }
#endif
    }

    if(noluck)
        return 0;


    /* Read some headers and do cursory sanity checks. There are loads
    of magic offsets. This could be rewritten... */

    if((239 != fread(tmp, 1, 239, fp)) || (memcmp(tmp, "GF1PATCH110\0ID#000002", 22) && memcmp(tmp, "GF1PATCH100\0ID#000002", 22)))
        return 0;


    if(tmp[82] != 1 && tmp[82] != 0)
        return 0;


    if(tmp[151] != 1 && tmp[151] != 0) /* layers. What's a layer? */
        return 0;


    ip          = (Instrument *)safe_malloc(sizeof(Instrument));
    ip->samples = tmp[198];
    ip->sample  = (Sample *)safe_malloc(sizeof(Sample) * ip->samples);

    for(i = 0; i < ip->samples; i++)
    {

        u_char  fractions;
        long    tmplong;
        u_short tmpshort;
        u_char  tmpchar;

#define READ_CHAR(thing)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    if(1 != fread(&tmpchar, 1, 1, fp))                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 \
        goto fail;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     \
    thing = tmpchar;
#define READ_SHORT(thing)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              \
    if(1 != fread(&tmpshort, 2, 1, fp))                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                \
        goto fail;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     \
    thing = LE_SHORT(tmpshort);
#define READ_LONG(thing)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    if(1 != fread(&tmplong, 4, 1, fp))                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 \
        goto fail;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     \
    thing = LE_LONG(tmplong);

        skip(fp, 7); /* Skip the wave name */

        if(1 != fread(&fractions, 1, 1, fp))
        {
        fail:
            for(j = 0; j < i; j++)
                free(ip->sample[j].data);

            free(ip->sample);
            free(ip);
            return 0;
        }

        sp = &(ip->sample[i]);

        READ_LONG(sp->data_length);
        READ_LONG(sp->loop_start);
        READ_LONG(sp->loop_end);
        READ_SHORT(sp->sample_rate);
        READ_LONG(sp->low_freq);
        READ_LONG(sp->high_freq);
        READ_LONG(sp->root_freq);
        skip(fp, 2); /* Why have a "root frequency" and then "tuning"?? */

        READ_CHAR(tmp[0]);

        if(panning == -1)
            sp->panning = (tmp[0] * 8 + 4) & 0x7f;
        else
            sp->panning = (u_char)(panning & 0x7F);

        /* envelope, tremolo, and vibrato */
        if(18 != fread(tmp, 1, 18, fp))
            goto fail;

        if(!tmp[13] || !tmp[14])
        {
            sp->tremolo_sweep_increment = sp->tremolo_phase_increment = sp->tremolo_depth = 0;
        }
        else
        {
            sp->tremolo_sweep_increment = convert_tremolo_sweep(ksr, tmp[12]);
            sp->tremolo_phase_increment = convert_tremolo_rate(ksr, tmp[13]);
            sp->tremolo_depth           = tmp[14];
        }

        if(!tmp[16] || !tmp[17])
        {
            sp->vibrato_sweep_increment = sp->vibrato_control_ratio = sp->vibrato_depth = 0;
        }
        else
        {
            sp->vibrato_control_ratio   = convert_vibrato_rate(ksr, tmp[16]);
            sp->vibrato_sweep_increment = convert_vibrato_sweep(ksr, tmp[15], sp->vibrato_control_ratio);
            sp->vibrato_depth           = tmp[17];
        }

        READ_CHAR(sp->modes);

        skip(fp, 40); /* skip the useless scale frequency, scale factor
        (what's it mean?), and reserved space */

        /* Mark this as a fixed-pitch instrument if such a deed is desired. */
        if(note_to_use != -1)
            sp->note_to_use = (u_char)(note_to_use);
        else
            sp->note_to_use = 0;

        /* seashore.pat in the Midia patch set has no Sustain. I don't
        understand why, and fixing it by adding the Sustain flag to
        all looped patches probably breaks something else. We do it
        anyway. */

        if(sp->modes & MODES_LOOPING)
            sp->modes |= MODES_SUSTAIN;

        /* Strip any loops and envelopes we're permitted to */
        if((strip_loop == 1) && (sp->modes & (MODES_SUSTAIN | MODES_LOOPING | MODES_PINGPONG | MODES_REVERSE)))
            sp->modes &= ~(MODES_SUSTAIN | MODES_LOOPING | MODES_PINGPONG | MODES_REVERSE);


        if(strip_envelope == 1)
            sp->modes &= ~MODES_ENVELOPE;
        else if(strip_envelope != 0)
        {
            /* Have to make a guess. */
            if(!(sp->modes & (MODES_LOOPING | MODES_PINGPONG | MODES_REVERSE)))
            {
                /* No loop? Then what's there to sustain? No envelope needed
                either... */
                sp->modes &= ~(MODES_SUSTAIN | MODES_ENVELOPE);
            }
            else if(!memcmp(tmp, "??????", 6) || tmp[11] >= 100)
            {
                /* Envelope rates all maxed out? Envelope end at a high "offset"?
                That's a weird envelope. Take it out. */
                sp->modes &= ~MODES_ENVELOPE;
            }
            else if(!(sp->modes & MODES_SUSTAIN))
            {
                /* No sustain? Then no envelope.  I don't know if this is
                justified, but patches without sustain usually don't need the
                envelope either... at least the Gravis ones. They're mostly
                drums.  I think. */
                sp->modes &= ~MODES_ENVELOPE;
            }
        }

        for(j = 0; j < 6; j++)
        {
            sp->envelope_rate[j]   = convert_envelope_rate(ksr, tmp[j]);
            sp->envelope_offset[j] = convert_envelope_offset(tmp[6 + j]);
        }

        /* Then read the sample data */
        sp->data = (sample_t *)safe_malloc(sp->data_length);
        if(1 != fread(sp->data, sp->data_length, 1, fp))
            goto fail;

        if(!(sp->modes & MODES_16BIT)) /* convert to 16-bit data */
        {
            long     i  = sp->data_length;
            u_char  *cp = (u_char *)(sp->data);
            u_short *tmp, *newdata;
            tmp = newdata = (u_short *)safe_malloc(sp->data_length * 2);
            while(i--)
                *tmp++ = (u_short)(*cp++) << 8;

            cp       = (u_char *)(sp->data);
            sp->data = (sample_t *)newdata;
            free(cp);
            sp->data_length *= 2;
            sp->loop_start  *= 2;
            sp->loop_end    *= 2;
        }
#ifndef LITTLE_ENDIAN
        else
        /* convert to machine byte order */
        {
            int32  i   = sp->data_length / 2;
            int16 *tmp = (int16 *)sp->data, s;
            while(i--)
            {
                s      = LE_SHORT(*tmp);
                *tmp++ = s;
            }
        }
#endif

        if(sp->modes & MODES_UNSIGNED) /* convert to signed data */
        {
            long   i   = sp->data_length / 2;
            short *tmp = (short *)sp->data;
            while(i--)
                *tmp++ ^= 0x8000;
        }

        /* Reverse reverse loops and pass them off as normal loops */
        if(sp->modes & MODES_REVERSE)
        {
            long t;
            /* The GUS apparently plays reverse loops by reversing the
            whole sample. We do the same because the GUS does not SUCK. */
            reverse_data((short *)sp->data, 0, sp->data_length / 2);

            t               = sp->loop_start;
            sp->loop_start  = sp->data_length - sp->loop_end;
            sp->loop_end    = sp->data_length - t;

            sp->modes      &= ~MODES_REVERSE;
            sp->modes      |= MODES_LOOPING; /* just in case */
        }

        /* If necessary do some anti-aliasing filtering  */

        if(ksr->antialiasing_allowed)
            antialiasing(sp, ksr->play_mode.rate);

#ifdef ADJUST_SAMPLE_VOLUMES
        if(amp != -1)
            sp->volume = (f64)(amp) / 100.0;
        else
        {
            /* Try to determine a volume scaling factor for the sample.
            This is a very crude adjustment, but things sound more
            balanced with it. Still, this should be a runtime option. */
            long   i      = sp->data_length / 2;
            short  maxamp = 0, a;
            short *tmp    = (short *)sp->data;
            while(i--)
            {
                a = *tmp++;
                if(a < 0)
                    a = -a;
                if(a > maxamp)
                    maxamp = a;
            }
            sp->volume = 32768.0 / (f64)(maxamp);
        }
#else
        if(amp != -1)
            sp->volume = (f64)(amp) / 100.0;
        else
            sp->volume = 1.0;
#endif

        sp->data_length  /= 2; /* These are in bytes. Convert into samples. */
        sp->loop_start   /= 2;
        sp->loop_end     /= 2;

        /* Then fractional samples */
        sp->data_length <<= FRACTION_BITS;
        sp->loop_start  <<= FRACTION_BITS;
        sp->loop_end    <<= FRACTION_BITS;

        /* Adjust for fractional loop points. This is a guess. Does anyone
        know what "fractions" really stands for? */
        sp->loop_start   |= (fractions & 0x0F) << (FRACTION_BITS - 4);
        sp->loop_end     |= ((fractions >> 4) & 0x0F) << (FRACTION_BITS - 4);

        /* If this instrument will always be played on the same note,
        and it's not looped, we can resample it now. */
        if(ksr->pre_resampling_allowed && sp->note_to_use && !(sp->modes & MODES_LOOPING))
            pre_resample(ksr, sp);

#ifdef LOOKUP_HACK
        /* Squash the 16-bit data into 8 bits. */
        {
            uint8 *gulp, *ulp;
            int16 *swp;
            int    l = sp->data_length >> FRACTION_BITS;
            gulp = ulp = (uint8 *)safe_malloc(l + 1);
            swp        = (int16 *)sp->data;
            while(l--)
                *ulp++ = (*swp++ >> 8) & 0xFF;
            free(sp->data);
            sp->data = (sample_t *)gulp;
        }
#endif

        if(strip_tail == 1) /* Let's not really, just say we did. */
            sp->data_length = sp->loop_end;
    }

    close_file(fp);
    return ip;
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
            /* Try SoundFont first */
            if(ksr->sf_loaded && !bank->tone[i].name)
            {
                bank->tone[i].instrument = load_soundfont_instrument(ksr, &ksr->sf_info, ksr->sf_filename, b, i);
                if(bank->tone[i].instrument)
                    continue;
            }

            if(!(bank->tone[i].name))
            {
                if(b != 0 && ksr->tonebank[0] && ksr->drumset[0])
                {
                    // Mark the corresponding instrument in the default
                    // bank / drumset for loading (if it isn't already)
                    if(!dr)
                    {
                        if(!(ksr->tonebank[0]->tone[i].instrument))
                            ksr->tonebank[0]->tone[i].instrument = MAGIC_LOAD_INSTRUMENT;
                    }
                    else
                    {
                        if(!(ksr->drumset[0]->tone[i].instrument))
                            ksr->drumset[0]->tone[i].instrument = MAGIC_LOAD_INSTRUMENT;
                    }
                }
                bank->tone[i].instrument = 0;
                errors++;
            }
            else if(!(bank->tone[i].instrument = load_instrument(ksr, bank->tone[i].name, (dr) ? 1 : 0, bank->tone[i].pan, bank->tone[i].amp, (bank->tone[i].note != -1) ? bank->tone[i].note : ((dr) ? i : -1), (bank->tone[i].strip_loop != -1) ? bank->tone[i].strip_loop : ((dr) ? 1 : -1), (bank->tone[i].strip_envelope != -1) ? bank->tone[i].strip_envelope : ((dr) ? 1 : -1), bank->tone[i].strip_tail)))
            {
                errors++;
            }
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

int set_default_instrument(Kasaria *ksr, char *name)
{
    Instrument *ip;
    if(!(ip = load_instrument(ksr, name, 0, -1, -1, -1, 0, 0, 0)))
        return -1;

    if(ksr->default_instrument)
        free_instrument(ksr->default_instrument);

    ksr->default_instrument = ip;
    ksr->default_program    = SPECIAL_PROGRAM;
    return 0;
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

/* SF2 time cent to milliseconds: 1200 cents = octave, 0 cents = 1 sec */
static long sf_timecent_to_msec(int tc)
{
    if(tc <= -12000)
        return 1;
    if(tc >= 5000)
        return 30000;
    return (long)(1000.0 * pow(2.0, tc / 1200.0));
}

static long sf_tc_to_offset(int val)
{
    /* Map SF2 attenuation (centibels) to GUS-style offset (0-255 range) */
    /* 0 cB = full volume, 1440 cB = silence */
    int attenuation = val;
    if(attenuation < 0)
        attenuation = 0;
    if(attenuation > 1440)
        attenuation = 1440;
    return (long)((1.0 - (f64)attenuation / 1440.0) * 255.0);
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

/*================================================================
 * SoundFont (SF2) sample loading
 *================================================================*/

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

    /* Find matching preset */
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
        /* fallback: try bank 0 */
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

    /* Count sample zones */
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

        /* preset zone key range */
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

            /* skip ROM samples */
            if(sfsample->sampletype & 0x8000)
                continue;

            sp          = &ip->sample[count];

            /* Calculate sample offsets */
            start       = sfsample->startsample;
            end         = sfsample->endsample;
            loop_start  = sfsample->startloop;
            loop_end    = sfsample->endloop;

            /* Apply instrument zone generators */
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

            /* Clamp */
            if(end > sfsample->endsample)
                end = sfsample->endsample;

            if(start < sfsample->startsample)
                start = sfsample->startsample;

            if(end <= start)
                continue;

            /* Sample flags and loop mode */
            sample_flags = sf_find_gen(inst_zone, SF_SAMPLEFLAGS, 0);
            if(sample_flags == 0)
                sample_flags = sfsample->sampletype & 0x3;

            if(sample_flags & 1)
            {
                /* Continuous loop */
                loop_mode = MODES_LOOPING | MODES_SUSTAIN;
                if(sample_flags & 2)
                    loop_mode |= MODES_PINGPONG;
            }
            else
                loop_mode = 0;


            /* Root key */
            root_key = sf_find_gen(inst_zone, SF_ROOTKEY, -1);
            if(root_key < 0 || root_key > 127)
                root_key = sfsample->originalPitch;
            if(root_key < 0 || root_key > 127)
                root_key = 60;

            /* Fine/coarse tune */
            fine_tune       = sf_find_gen(inst_zone, SF_FINETUNE, 0);
            coarse_tune     = sf_find_gen(inst_zone, SF_COARSETUNE, 0);

            /* Attenuation */
            attenuation     = sf_find_gen(inst_zone, SF_INITATTEN, 0);

            /* Panning */
            pan             = sf_find_gen(inst_zone, SF_PAN, 0);

            /* Instrument zone key range - override preset key range for this sample */
            low_key         = sf_find_gen(inst_zone, SF_KEYRANGE, 0x7F00) & 0xFF;
            high_key        = (sf_find_gen(inst_zone, SF_KEYRANGE, 0x7F00)) >> 8;

            /* Frequency calculations */
            sp->sample_rate = sfsample->samplerate;

            if(sp->sample_rate <= 0)
                sp->sample_rate = 44100;

            /* Calculate MIDI note to frequency mapping */
            sp->root_freq               = (long)(8.176 * pow(2.0, (root_key + coarse_tune + fine_tune / 100.0) / 12.0) * 1000.0);
            sp->low_freq                = (long)(8.176 * pow(2.0, (low_key + coarse_tune) / 12.0) * 1000.0);
            sp->high_freq               = (long)(8.176 * pow(2.0, (high_key + coarse_tune) / 12.0) * 1000.0);

            /* Volume */
            sp->volume                  = pow(10.0, -attenuation / 200.0);

            /* Panning: SF2 uses 0=left, 500=center, 1000=right */
            sp->panning                 = (u_char)((pan + 500) * 127 / 1000);

            /* Modes */
            sp->modes                   = MODES_16BIT | MODES_ENVELOPE | loop_mode;

            /* Note to use: 0 = any key */
            sp->note_to_use             = 0;

            /* Tremolo / vibrato: disabled for simplicity */
            sp->tremolo_sweep_increment = 0;
            sp->tremolo_phase_increment = 0;
            sp->tremolo_depth           = 0;
            sp->vibrato_sweep_increment = 0;
            sp->vibrato_control_ratio   = 0;
            sp->vibrato_depth           = 0;

            /* Set up envelope (SF2 vol envelope: attack, decay, sustain, release) */
            attack_tc                   = sf_find_gen(inst_zone, SF_ATTACKENV1, -12000);
            decay_tc                    = sf_find_gen(inst_zone, SF_DECAYENV1, -12000);
            sustain_level               = sf_find_gen(inst_zone, SF_SUSTAINENV1, 0);
            release_tc                  = sf_find_gen(inst_zone, SF_RELEASEENV1, -12000);

            /* Stage 0: attack (silence to peak) */
            sp->envelope_offset[0]      = 255 << (7 + 15);
            sp->envelope_rate[0]        = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(attack_tc));

            /* Stage 1: decay (peak to sustain) */
            /* SF2 sustain: 0 cB = full volume, 1000 cB = silence (centibels) */
            {
                f64 sus_amp            = pow(10.0, -sustain_level / 200.0);
                sp->envelope_offset[1] = (long)(sus_amp * 255.0) << (7 + 15);
            }
            sp->envelope_rate[1]   = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(decay_tc)) * (sp->envelope_offset[0] - sp->envelope_offset[1]) / sp->envelope_offset[0];

            /* Stage 2: sustain hold */
            sp->envelope_offset[2] = sp->envelope_offset[1];
            sp->envelope_rate[2]   = 0;

            /* Stage 3: release */
            sp->envelope_offset[3] = 0;
            sp->envelope_rate[3]   = sf_calc_envelope_rate(ksr, sf_timecent_to_msec(release_tc)) * sp->envelope_offset[2] / (255 << (7 + 15));

            /* Stage 4-5: done */
            sp->envelope_offset[4] = 0;
            sp->envelope_rate[4]   = 0;
            sp->envelope_offset[5] = 0;
            sp->envelope_rate[5]   = 0;

            /* Read sample data from file */
            {
                long num_samples = end - start;
                long num_loops   = loop_end - loop_start;
                long byte_offset;

                sp->data_length = num_samples;
                sp->loop_start  = loop_start - start;
                sp->loop_end    = loop_end - start;

                /* Sanity check loops */
                if(sp->loop_start < 0)
                    sp->loop_start = 0;

                if(sp->loop_end > num_samples)
                    sp->loop_end = num_samples;

                if(sp->loop_end <= sp->loop_start)
                {
                    sp->loop_start = 0;
                    sp->loop_end   = num_samples;
                }

                /* Allocate and read */
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

                /* Pad 2 extra samples for interpolation safety */
                sp->data[num_samples]     = 0;
                sp->data[num_samples + 1] = 0;

                /* Peak normalize sample volume */
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

                /* Anti-aliasing filter */
                if(ksr->antialiasing_allowed)
                    antialiasing(sp, ksr->play_mode.rate);

                /* Convert from samples to fractional samples */
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