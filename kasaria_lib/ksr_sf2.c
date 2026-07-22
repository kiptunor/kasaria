/*
    TiMidity++ -- MIDI to WAVE converter and player
    Copyright (C) 1999-2002 Masanao Izumo <mo@goice.co.jp>
    Copyright (C) 1995 Tuukka Toivonen <tt@cgs.fi>
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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*================================================================
 * sffile.c
 *	read SoundFont file (SBK/SF2) and store the layer lists
 *
 * Copyright (C) 1996,1997 Takashi Iwai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *================================================================*/

/*
 * Modified by Masanao Izumo <mo@goice.co.jp>
 */














#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif /* HAVE_CONFIG_H */
#include <stdio.h>
#ifndef NO_STRING_H
    #include <string.h>
#else
    #include <strings.h>
#endif
#include <stdlib.h>


#include "ksr_internal.h"
#include "ksr_sf2.h"

extern int progbase;

/*================================================================
 * preset / instrument bag record
 *================================================================*/

typedef struct _SFBags
{
    int       nbags;
    uint16_t *bag;
    int       ngens;
    SFGenRec *gen;
} SFBags;

static SFBags prbags, inbags;


/*----------------------------------------------------------------
 * function prototypes
 *----------------------------------------------------------------*/

#define NEW(type, nums) (type *)safe_malloc(sizeof(type) * (nums))


static int READCHUNK(SFChunk *vp, FILE *fp)
{
    if(fread(vp, 8, 1, fp) != 1)
        return -1;

    vp->size = LE_LONG(vp->size);

    return 1;
}

static int READDW(uint32_t *vp, FILE *fp)
{
    if(fread(vp, 4, 1, fp) != 1)
        return -1;

    *vp = LE_LONG(*vp);

    return 1;
}

static int READW(uint16_t *vp, FILE *fp)
{
    if(fread(vp, 2, 1, fp) != 1)
        return -1;

    *vp = LE_SHORT(*vp);

    return 1;
}

static int READSTR(char *str, FILE *fp)
{
    int n;
    if(fread(str, 20, 1, fp) != 1)
        return -1;

    str[19] = '\0';
    n       = strlen(str);

    while(n > 0 && str[n - 1] == ' ')
        n--;

    str[n] = '\0';
    return n;
}

#define READID(var, fp) fread(var, 4, 1, fp)
#define READB(var, fp)  fread(&var, 1, 1, fp)
#define SKIPB(fp)       skip(fp, 1)
#define SKIPW(fp)       skip(fp, 2)
#define SKIPDW(fp)      skip(fp, 4)
#define FSKIP(size, fp) skip(fp, size)


/*----------------------------------------------------------------*/

static int  chunkid(char *id);
static int  process_list(int size, SFInfo *sf, FILE *fp);
static int  process_info(int size, SFInfo *sf, FILE *fp);
static int  process_sdta(int size, SFInfo *sf, FILE *fp);
static int  process_pdta(int size, SFInfo *sf, FILE *fp);
static void load_sample_names(int size, SFInfo *sf, FILE *fp);
static void load_preset_header(int size, SFInfo *sf, FILE *fp);
static void load_inst_header(int size, SFInfo *sf, FILE *fp);
static void load_bag(int size, SFBags *bagp, FILE *fp);
static void load_gen(int size, SFBags *bagp, FILE *fp);
static void load_sample_info(int size, SFInfo *sf, FILE *fp);
static void convert_layers(SFInfo *sf);
static void generate_layers(SFHeader *hdr, SFHeader *next, SFBags *bags);
static void free_layer(SFHeader *hdr);


/*----------------------------------------------------------------
 * id numbers
 *----------------------------------------------------------------*/

enum
{
    // level 0; chunk
    UNKN_ID,
    RIFF_ID,
    LIST_ID,
    SFBK_ID,
    // level 1; id only
    INFO_ID,
    SDTA_ID,
    PDTA_ID,
    // info stuff; chunk
    IFIL_ID,
    ISNG_ID,
    IROM_ID,
    INAM_ID,
    IVER_ID,
    IPRD_ID,
    ICOP_ID,
    ICRD_ID,
    IENG_ID,
    ISFT_ID,
    ICMT_ID,
    // sample data stuff; chunk
    SNAM_ID,
    SMPL_ID,
    // preset stuff; chunk
    PHDR_ID,
    PBAG_ID,
    PMOD_ID,
    PGEN_ID,
    // inst stuff; chunk
    INST_ID,
    IBAG_ID,
    IMOD_ID,
    IGEN_ID,
    // sample header; chunk
    SHDR_ID
};

SoundFontEffects default_sf2_effects = {
    .start_addrs_offset             = 0,
    .end_addrs_offset               = 0,
    .startloop_addrs_offset         = 0,
    .endloop_addrs_offset           = 0,
    .start_addrs_coarse_offset      = 0,
    .mod_lfo_to_pitch               = 0.0f,  // cents, range: -12000 to 12000
    .vib_lfo_to_pitch               = 0.0f,  // cents, range: -12000 to 12000
    .mod_env_to_pitch               = 0.0f,  // cents, range: -12000 to 12000
    .initial_filter_fc              = 13500, // Hz, range: 1500 to 13500 (max = no filter)
    .initial_filter_q               = 0.0f,  // centibels, range: 0 to 960 (0 = no resonance)
    .mod_lfo_to_filter_fc           = 0.0f,  // cents, range: -12000 to 12000
    .mod_env_to_filter_fc           = 0.0f,  // cents, range: -12000 to 12000
    .end_addrs_coarse_offset        = 0,
    .mod_lfo_to_volume              = 0.0f,      // centibels, range: -960 to 960
    .chorus_effects_send            = 0.0f,      // range: 0 to 1000 (0 = no send)
    .reverb_effects_send            = 0.0f,      // range: 0 to 1000 (0 = no send)
    .pan                            = 0.0f,      // range: -500 to 500 (0 = center)
    .delay_mod_LFO                  = -12000.0f, // timecents, -12000 = 0 seconds
    .freq_mod_LFO                   = 0.0f,      // mHz, range: 140 to infinity
    .delay_vib_LFO                  = -12000.0f, // timecents, -12000 = 0 seconds
    .freq_vib_LFO                   = 0.0f,      // mHz, range: 140 to infinity
    .delay_mod_env                  = -12000.0f, // timecents, -12000 = 0 seconds
    .attack_mod_env                 = -12000.0f, // timecents, -12000 = 0 seconds
    .hold_mod_env                   = -12000.0f, // timecents, -12000 = 0 seconds
    .decay_mod_env                  = -12000.0f, // timecents, -12000 = 0 seconds
    .sustain_mod_env                = 1000.0f,   // range: 0 to 1000 (1000 = full sustain)
    .release_mod_env                = -12000.0f, // timecents, -12000 = 0 seconds
    .keynum_to_mod_env_hold         = 0.0f,      // timecents/key, range: -1200 to 1200
    .keynum_to_mod_env_decay        = 0.0f,      // timecents/key, range: -1200 to 1200
    .delay_vol_env                  = -12000.0f, // timecents, -12000 = 0 seconds
    .attack_vol_env                 = -12000.0f, // timecents, -12000 = 0 seconds
    .hold_vol_env                   = -12000.0f, // timecents, -12000 = 0 seconds
    .decay_vol_env                  = -12000.0f, // timecents, -12000 = 0 seconds
    .sustain_vol_env                = 0.0f,      // range: 0 to 1000 (0 = full sustain level)
    .release_vol_env                = -12000.0f, // timecents, -12000 = 0 seconds
    .keynum_to_vol_env_hold         = 0.0f,      // timecents/key, range: -1200 to 1200
    .keynum_to_vol_env_decay        = 0.0f,      // timecents/key, range: -1200 to 1200
    .instrument                     = 0,
    .key_range                      = 127, // 0 to 127 (full keyboard)
    .vel_range                      = 127, // 0 to 127 (full velocity)
    .start_loop_addrs_coarse_offset = 0,
    .fixed_key                      = 255,  // 255 = disabled
    .velocity                       = 255,  // 255 = disabled
    .initial_attenuation            = 0.0f, // centibels, 0 to 1440 (0 = no attenuation)
    .end_loop_addrs_coarse_offset   = 0,
    .coarse_tune                    = 0.0f, // semitones, -120 to 120
    .fine_tune                      = 0.0f, // cents, -99 to 99
    .sample_id                      = 0,
    .sample_modes                   = 0,      // 0 = continuous (no loop)
    .scale_tuning                   = 100.0f, // percent, 100 = equal temperament
    .exclusive_class                = 0,      // 0 = not exclusive
    .overriding_root_key            = 255,    // 255 = disabled (use sample's root key)
};


/*================================================================
 * load a soundfont file
 *================================================================*/

int load_soundfont(SFInfo *sf, const char *filename)
{
    SFChunk chunk;
    FILE   *fp;

    sf->preset  = NULL;
    sf->sample  = NULL;
    sf->inst    = NULL;
    sf->sf_name = NULL;

    prbags.bag = inbags.bag = NULL;
    prbags.gen = inbags.gen = NULL;

    fp                      = fopen(filename, "rb");
    if(!fp)
    {
        fprintf(stderr, "%s: could not open soundfont file\n", filename);
        return -1;
    }

    // check RIFF file header
    READCHUNK(&chunk, fp);
    if(chunkid(chunk.id) != RIFF_ID)
    {
        fprintf(stderr, "%s: *** not a RIFF file", filename);
        fclose(fp);
        return -1;
    }
    // check file id
    READID(chunk.id, fp);
    if(chunkid(chunk.id) != SFBK_ID)
    {
        fprintf(stderr, "%s: *** not a SoundFont file", filename);
        fclose(fp);
        return -1;
    }

    for(;;)
    {
        if(READCHUNK(&chunk, fp) <= 0)
            break;
        else if(chunkid(chunk.id) == LIST_ID)
        {
            if(process_list(chunk.size, sf, fp))
                break;
        }
        else
        {
            fprintf(stderr, "%s: *** illegal id in level 0: %4.4s %4d", filename, chunk.id, chunk.size);
            FSKIP(chunk.size, fp);
        }
    }

    fclose(fp);

    // parse layer structure
    convert_layers(sf);

    // free private tables
    if(prbags.bag)
        free(prbags.bag);

    if(prbags.gen)
        free(prbags.gen);

    if(inbags.bag)
        free(inbags.bag);

    if(inbags.gen)
        free(inbags.gen);

    prbags.bag = NULL;
    prbags.gen = NULL;
    inbags.bag = NULL;
    inbags.gen = NULL;

    return 0;
}


/*================================================================
 * free buffer
 *================================================================*/

void free_soundfont(SFInfo *sf)
{
    int i;
    if(sf->preset)
    {
        for(i = 0; i < sf->npresets; i++)
            free_layer(&sf->preset[i].hdr);
        free(sf->preset);
    }
    if(sf->inst)
    {
        for(i = 0; i < sf->ninsts; i++)
            free_layer(&sf->inst[i].hdr);

        free(sf->inst);
    }

    if(sf->sample)
        free(sf->sample);

    if(sf->sf_name)
        free(sf->sf_name);
}


/*----------------------------------------------------------------
 * get id value from 4bytes ID string
 *----------------------------------------------------------------*/

static int chunkid(char *id)
{
    static struct idstring
    {
        char *str;
        int   id;
    } idlist[] = {
        { "RIFF", RIFF_ID },
        { "LIST", LIST_ID },
        { "sfbk", SFBK_ID },
        { "INFO", INFO_ID },
        { "sdta", SDTA_ID },
        { "snam", SNAM_ID },
        { "smpl", SMPL_ID },
        { "pdta", PDTA_ID },
        { "phdr", PHDR_ID },
        { "pbag", PBAG_ID },
        { "pmod", PMOD_ID },
        { "pgen", PGEN_ID },
        { "inst", INST_ID },
        { "ibag", IBAG_ID },
        { "imod", IMOD_ID },
        { "igen", IGEN_ID },
        { "shdr", SHDR_ID },
        { "ifil", IFIL_ID },
        { "isng", ISNG_ID },
        { "irom", IROM_ID },
        { "iver", IVER_ID },
        { "INAM", INAM_ID },
        { "IPRD", IPRD_ID },
        { "ICOP", ICOP_ID },
        { "ICRD", ICRD_ID },
        { "IENG", IENG_ID },
        { "ISFT", ISFT_ID },
        { "ICMT", ICMT_ID },
    };

    int i;

    for(i = 0; i < sizeof(idlist) / sizeof(idlist[0]); i++)
        if(strncmp(id, idlist[i].str, 4) == 0)
            return idlist[i].id;


    return UNKN_ID;
}


/*================================================================
 * process a list chunk
 *================================================================*/

static int process_list(int size, SFInfo *sf, FILE *fp)
{
    SFChunk chunk;

    READID(chunk.id, fp);
    size -= 4;
    switch(chunkid(chunk.id))
    {
    case INFO_ID:
        return process_info(size, sf, fp);
    case SDTA_ID:
        return process_sdta(size, sf, fp);
    case PDTA_ID:
        return process_pdta(size, sf, fp);
    default:
        FSKIP(size, fp);
        return 0;
    }
}


static int process_info(int size, SFInfo *sf, FILE *fp)
{
    sf->infopos  = ftell(fp);
    sf->infosize = size;

    while(size > 0)
    {
        SFChunk chunk;

        if(READCHUNK(&chunk, fp) <= 0)
            return -1;
        size -= 8;

        switch(chunkid(chunk.id))
        {
        case IFIL_ID:
            READW(&sf->version, fp);
            READW(&sf->minorversion, fp);
            break;
        case INAM_ID:
            sf->sf_name = (char *)safe_malloc(chunk.size + 1);
            fread(sf->sf_name, 1, chunk.size, fp);
            sf->sf_name[chunk.size] = 0;
            break;
        default:
            FSKIP(chunk.size, fp);
            break;
        }
        size -= chunk.size;
    }
    return 0;
}


static int process_sdta(int size, SFInfo *sf, FILE *fp)
{
    while(size > 0)
    {
        SFChunk chunk;

        if(READCHUNK(&chunk, fp) <= 0)
            return -1;
        size -= 8;

        switch(chunkid(chunk.id))
        {
        case SNAM_ID:
            load_sample_names(chunk.size, sf, fp);
            break;
        case SMPL_ID:
            sf->samplepos  = ftell(fp);
            sf->samplesize = chunk.size;
            FSKIP(chunk.size, fp);
            break;
        default:
            FSKIP(chunk.size, fp);
            break;
        }
        size -= chunk.size;
    }
    return 0;
}


static int process_pdta(int size, SFInfo *sf, FILE *fp)
{
    while(size > 0)
    {
        SFChunk chunk;

        if(READCHUNK(&chunk, fp) <= 0)
            return -1;
        size -= 8;

        switch(chunkid(chunk.id))
        {
        case PHDR_ID:
            load_preset_header(chunk.size, sf, fp);
            break;
        case PBAG_ID:
            load_bag(chunk.size, &prbags, fp);
            break;
        case PGEN_ID:
            load_gen(chunk.size, &prbags, fp);
            break;
        case INST_ID:
            load_inst_header(chunk.size, sf, fp);
            break;
        case IBAG_ID:
            load_bag(chunk.size, &inbags, fp);
            break;
        case IGEN_ID:
            load_gen(chunk.size, &inbags, fp);
            break;
        case SHDR_ID:
            load_sample_info(chunk.size, sf, fp);
            break;
        case PMOD_ID:
        case IMOD_ID:
        default:
            FSKIP(chunk.size, fp);
            break;
        }
        size -= chunk.size;
    }
    return 0;
}


static void load_sample_names(int size, SFInfo *sf, FILE *fp)
{
    int i, nsamples;
    if(sf->version > 1)
    {
        FSKIP(size, fp);
        return;
    }

    nsamples = size / 20;
    if(sf->sample == NULL)
    {
        sf->nsamples = nsamples;
        sf->sample   = NEW(SFSampleInfo, sf->nsamples);
    }
    else if(sf->nsamples != nsamples)
    {
        FSKIP(size, fp);
        return;
    }

    for(i = 0; i < sf->nsamples; i++)
        READSTR(sf->sample[i].name, fp);
}


static void load_preset_header(int size, SFInfo *sf, FILE *fp)
{
    int i;

    sf->npresets = size / 38;
    sf->preset   = NEW(SFPresetHdr, sf->npresets);
    for(i = 0; i < sf->npresets; i++)
    {
        READSTR(sf->preset[i].hdr.name, fp);
        READW(&sf->preset[i].preset, fp);
        READW(&sf->preset[i].bank, fp);
        READW(&sf->preset[i].hdr.bagNdx, fp);
        SKIPDW(fp);
        SKIPDW(fp);
        SKIPDW(fp);
        sf->preset[i].hdr.nlayers = 0;
        sf->preset[i].hdr.layer   = NULL;
    }
}


static void load_inst_header(int size, SFInfo *sf, FILE *fp)
{
    int i;

    sf->ninsts = size / 22;
    sf->inst   = NEW(SFInstHdr, sf->ninsts);
    for(i = 0; i < sf->ninsts; i++)
    {
        READSTR(sf->inst[i].hdr.name, fp);
        READW(&sf->inst[i].hdr.bagNdx, fp);
        sf->inst[i].hdr.nlayers = 0;
        sf->inst[i].hdr.layer   = NULL;
    }
}


static void load_bag(int size, SFBags *bagp, FILE *fp)
{
    int i;

    size      /= 4;
    bagp->bag  = NEW(u_short, size);
    for(i = 0; i < size; i++)
    {
        READW(&bagp->bag[i], fp);
        SKIPW(fp);
    }
    bagp->nbags = size;
}


static void load_gen(int size, SFBags *bagp, FILE *fp)
{
    int i;

    size      /= 4;
    bagp->gen  = NEW(SFGenRec, size);
    for(i = 0; i < size; i++)
    {
        READW((u_short *)&bagp->gen[i].oper, fp);
        READW((u_short *)&bagp->gen[i].amount, fp);
    }
    bagp->ngens = size;
}


static void load_sample_info(int size, SFInfo *sf, FILE *fp)
{
    int i;
    int in_rom;

    if(sf->version > 1)
    {
        sf->nsamples = size / 46;
        sf->sample   = NEW(SFSampleInfo, sf->nsamples);
    }
    else
    {
        int nsamples = size / 16;
        if(sf->sample == NULL)
        {
            sf->nsamples = nsamples;
            sf->sample   = NEW(SFSampleInfo, sf->nsamples);
        }
        else if(sf->nsamples != nsamples)
        {
            sf->nsamples = nsamples;
        }
    }

    in_rom = 1;
    for(i = 0; i < sf->nsamples; i++)
    {
        if(sf->version > 1)
            READSTR(sf->sample[i].name, fp);

        READDW((uint32_t *)&sf->sample[i].startsample, fp);
        READDW((uint32_t *)&sf->sample[i].endsample, fp);
        READDW((uint32_t *)&sf->sample[i].startloop, fp);
        READDW((uint32_t *)&sf->sample[i].endloop, fp);
        if(sf->version > 1)
        {
            READDW((uint32_t *)&sf->sample[i].samplerate, fp);
            READB(sf->sample[i].originalPitch, fp);
            READB(sf->sample[i].pitchCorrection, fp);
            READW(&sf->sample[i].samplelink, fp);
            READW(&sf->sample[i].sampletype, fp);
        }
        else
        {
            sf->sample[i].samplerate      = 44100;
            sf->sample[i].originalPitch   = 60;
            sf->sample[i].pitchCorrection = 0;
            sf->sample[i].samplelink      = 0;
            if(sf->sample[i].startsample == 0)
                in_rom = 0;
            if(in_rom)
                sf->sample[i].sampletype = 0x8001;
            else
                sf->sample[i].sampletype = 1;
        }
    }
}


/*================================================================
 * convert from bags to layers
 *================================================================*/

static void convert_layers(SFInfo *sf)
{
    int i;

    if(prbags.bag == NULL || prbags.gen == NULL || inbags.bag == NULL || inbags.gen == NULL)
    {
        // ctl->cmsg(CMSG_WARNING, VERB_NORMAL, "%s: *** illegal bags / gens", current_filename);
        return;
    }

    for(i = 0; i < sf->npresets - 1; i++)
        generate_layers(&sf->preset[i].hdr, &sf->preset[i + 1].hdr, &prbags);

    for(i = 0; i < sf->ninsts - 1; i++)
        generate_layers(&sf->inst[i].hdr, &sf->inst[i + 1].hdr, &inbags);
}


/*----------------------------------------------------------------
 * generate layer lists from stored bags
 *----------------------------------------------------------------*/

static void generate_layers(SFHeader *hdr, SFHeader *next, SFBags *bags)
{
    int         i;
    SFGenLayer *layp;

    hdr->nlayers = next->bagNdx - hdr->bagNdx;
    if(hdr->nlayers < 0)
    {
        // ctl->cmsg(CMSG_WARNING, VERB_NORMAL, "%s: illegal layer numbers %d", current_filename, hdr->nlayers);
        return;
    }
    if(hdr->nlayers == 0)
        return;

    hdr->layer = (SFGenLayer *)safe_malloc(sizeof(SFGenLayer) * hdr->nlayers);
    layp       = hdr->layer;
    for(layp = hdr->layer, i = hdr->bagNdx; i < next->bagNdx; layp++, i++)
    {
        int genNdx   = bags->bag[i];
        layp->nlists = bags->bag[i + 1] - genNdx;
        if(layp->nlists < 0)
        {
            // ctl->cmsg(CMSG_WARNING, VERB_NORMAL, "%s: illegal list numbers %d", current_filename, layp->nlists);
            return;
        }
        layp->list = (SFGenRec *)safe_malloc(sizeof(SFGenRec) * layp->nlists);
        memcpy(layp->list, &bags->gen[genNdx], sizeof(SFGenRec) * layp->nlists);
    }
}

/*----------------------------------------------------------------
 * free a layer
 *----------------------------------------------------------------*/

static void free_layer(SFHeader *hdr)
{
    int i;
    for(i = 0; i < hdr->nlayers; i++)
    {
        SFGenLayer *layp = &hdr->layer[i];
        if(layp->nlists >= 0)
            free(layp->list);
    }
    if(hdr->nlayers > 0)
        free(hdr->layer);
}

// add blank loop for each data
const static int auto_add_blank = 0;
void             correct_samples(SFInfo *sf)
{
    int           i;
    SFSampleInfo *sp;
    int           prev_end;

    prev_end = 0;
    for(sp = sf->sample, i = 0; i < sf->nsamples; i++, sp++)
    {
        // correct sample positions for SBK file
        if(sf->version == 1)
        {
            sp->startloop++;
            sp->endloop += 2;
        }

        // calculate sample data size
        if(sp->sampletype & 0x8000)
            sp->size = 0;
        else if(sp->startsample < prev_end && sp->startsample != 0)
            sp->size = 0;
        else
        {
            sp->size = -1;
            if(!auto_add_blank && i != sf->nsamples - 1)
                sp->size = sp[1].startsample - sp->startsample;

            if(sp->size < 0)
                sp->size = sp->endsample - sp->startsample + 48;
        }
        prev_end = sp->endsample;

        // calculate short-shot loop size
        if(auto_add_blank || i == sf->nsamples - 1)
            sp->loopshot = 48;
        else
        {
            sp->loopshot = sp[1].startsample - sp->endsample;
            if(sp->loopshot < 0 || sp->loopshot > 48)
                sp->loopshot = 48;
        }
    }
}