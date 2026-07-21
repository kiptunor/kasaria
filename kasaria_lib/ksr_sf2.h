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
 * sffile.h
 *	SoundFont file (SBK/SF2) format defintions
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

#include <stdint.h>


#ifndef SFFILE_H_DEF
    #define SFFILE_H_DEF

/* chunk record header */
typedef struct _SFChunk
{
    char    id[4];
    int32_t size;
} SFChunk;

/* generator record */
typedef struct _SFGenRec
{
    int16_t oper;
    int16_t amount;
} SFGenRec;

/* layered generators record */
typedef struct _SFGenLayer
{
    int       nlists;
    SFGenRec *list;
} SFGenLayer;

/* header record */
typedef struct _SFHeader
{
    char        name[20];
    uint16_t    bagNdx;
    /* layered stuff */
    int         nlayers;
    SFGenLayer *layer;
} SFHeader;

/* preset header record */
typedef struct _SFPresetHdr
{
    SFHeader hdr;
    uint16_t preset, bank;
    /*int32 lib, genre, morphology;*/ /* not used */
} SFPresetHdr;

/* instrument header record */
typedef struct _SFInstHdr
{
    SFHeader hdr;
} SFInstHdr;

/* sample info record */
typedef struct _SFSampleInfo
{
    char     name[20];
    int32_t  startsample, endsample;
    int32_t  startloop, endloop;
    /* ver.2 additional info */
    int32_t  samplerate;
    uint8_t  originalPitch;
    int8_t   pitchCorrection;
    uint16_t samplelink;
    uint16_t sampletype; /*1=mono, 2=right, 4=left, 8=linked, $8000=ROM*/
    /* optional info */
    int32_t  size;     /* sample size */
    int32_t  loopshot; /* short-shot loop size */
} SFSampleInfo;


/*----------------------------------------------------------------
 * soundfont file info record
 *----------------------------------------------------------------*/

typedef struct _SFInfo
{
    /* file name */
    char         *sf_name;

    /* version of this file */
    uint16_t      version, minorversion;
    /* sample position (from origin) & total size (in bytes) */
    long          samplepos;
    int32_t       samplesize;

    /* raw INFO chunk list */
    long          infopos, infosize;

    /* preset headers */
    int           npresets;
    SFPresetHdr  *preset;

    /* sample infos */
    int           nsamples;
    SFSampleInfo *sample;

    /* instrument headers */
    int           ninsts;
    SFInstHdr    *inst;

} SFInfo;

    /* SF2 generator IDs */
    #define SF_STARTADDRS           0
    #define SF_ENDADDRS             1
    #define SF_STARTLOOP            2
    #define SF_ENDLOOP              3
    #define SF_STARTADDRSHI         4
    #define SF_LFO1TOPITCH          5
    #define SF_LFO2TOPITCH          6
    #define SF_ENV1TOPITCH          7
    #define SF_INITFILTERFC         8
    #define SF_INITFILTERQ          9
    #define SF_LFO1TOFILTERFC       10
    #define SF_ENV1TOFILTERFC       11
    #define SF_ENDADDRSHI           12
    #define SF_LFO1TOVOLUME         13
    #define SF_PAN                  17
    #define SF_DELAYLFO1            21
    #define SF_FREQLFO1             22
    #define SF_DELAYLFO2            23
    #define SF_FREQLFO2             24
    #define SF_DELAYENV1            25
    #define SF_ATTACKENV1           26
    #define SF_HOLDENV1             27
    #define SF_DECAYENV1            28
    #define SF_SUSTAINENV1          29
    #define SF_RELEASEENV1          30
    #define SF_DELAYENV2            33
    #define SF_ATTACKENV2           34
    #define SF_HOLDENV2             35
    #define SF_DECAYENV2            36
    #define SF_SUSTAINENV2          37
    #define SF_RELEASEENV2          38
    #define SF_INSTRUMENT           41
    #define SF_KEYRANGE             43
    #define SF_VELRANGE             44
    #define SF_STARTLOOPHI          45
    #define SF_INITATTEN            48
    #define SF_ENDLOOPHI            50
    #define SF_COARSETUNE           51
    #define SF_FINETUNE             52
    #define SF_SAMPLEID             53
    #define SF_SAMPLEFLAGS          54
    #define SF_SCALE_TUNING         56
    #define SF_ROOTKEY              58

    /* SF2 sample types */
    #define SF_SAMPLETYPE_MONO      1
    #define SF_SAMPLETYPE_RIGHT     2
    #define SF_SAMPLETYPE_LEFT      4
    #define SF_SAMPLETYPE_LINKED    8
    #define SF_SAMPLETYPE_ROMMONO   0x8001
    #define SF_SAMPLETYPE_ROMRIGHT  0x8002
    #define SF_SAMPLETYPE_ROMLEFT   0x8004
    #define SF_SAMPLETYPE_ROMLINKED 0x8008


/*----------------------------------------------------------------
 * functions
 *----------------------------------------------------------------*/

/* sffile.c */
extern int  load_soundfont(SFInfo *sf, const char *filename);
extern void free_soundfont(SFInfo *sf);
extern void correct_samples(SFInfo *sf);

#endif