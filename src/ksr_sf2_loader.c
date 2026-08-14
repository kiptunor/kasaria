/*
    Kasaria -- A powerful and High efficiency MIDI Synth based on TiMidity
    Copyright (C) 1999-2005 Masanao Izumo <iz@onicos.co.jp>
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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    This code from awesfx
    Modified by Masanao Izumo <mo@goice.co.jp>

    ================================================================
    parsesf.c
         parse SoundFont layers and convert it to AWE driver patch

    Copyright (C) 1996,1997 Takashi Iwai
    ================================================================
*/


#include <errno.h>
#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef __POCC__
#include <sys/types.h>
#endif //for off_t
#include <ctype.h>
#define __USE_POSIX
#include <stdio.h>
#ifndef NO_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#include <math.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


#include <stdio.h>

#include "ext_deps/ulog/src/ulog.h"
#include "../src/ksr_internal.h"
#include "../src/ksr_sf2.h"














enum
{
	SF_startAddrs,		  /* 0  sample start address -4 (0to*0xffffff)  */
	SF_endAddrs,		  /* 1                                          */
	SF_startloopAddrs,	  /* 2  loop start address -4 (0 to * 0xffffff) */
	SF_endloopAddrs,	  /* 3  loop end address -3 (0 to * 0xffffff)   */
	SF_startAddrsHi,	  /* 4  high word of startAddrs                 */
	SF_lfo1ToPitch,		  /* 5  main fm: lfo1-> pitch                   */
	SF_lfo2ToPitch,		  /* 6  aux fm:  lfo2-> pitch                   */
	SF_env1ToPitch,		  /* 7  pitch env: env1(aux)-> pitch            */
	SF_initialFilterFc,	  /* 8  initial filter cutoff                    */
	SF_initialFilterQ,	  /* 9  filter Q                                 */
	SF_lfo1ToFilterFc,	  /* 10 filter modulation: lfo1->filter*cutoff    */
	SF_env1ToFilterFc,	  /* 11 filter env: env1(aux)->filter * cutoff    */
	SF_endAddrsHi,		  /* 12 high word of endAddrs                   */
	SF_lfo1ToVolume,	  /* 13 tremolo: lfo1-> volume                  */
	SF_env2ToVolume,	  /* 14 Env2Depth: env2-> volume                */
    SF_chorusEffectsSend, /* 15 chorus                                  */
    SF_reverbEffectsSend, /* 16 reverb                                  */
    SF_panEffectsSend,	  /* 17 pan                                     */
    SF_auxEffectsSend,	  /* 18 pan auxdata (internal)                  */
    SF_sampleVolume,	  /* 19 used internally                         */
    SF_unused3,		      /* 20                                         */
    SF_delayLfo1,		  /* 21 delay 0x8000-n*(725us)                  */
    SF_freqLfo1,		  /* 22 frequency                               */
    SF_delayLfo2,		  /* 23 delay 0x8000-n*(725us)                  */
    SF_freqLfo2,		  /* 24 frequency                               */
    SF_delayEnv1,		  /* 25 delay 0x8000 - n(725us)                 */
    SF_attackEnv1,		  /* 26 attack                                  */
    SF_holdEnv1,		  /* 27 hold                                    */
    SF_decayEnv1,		  /* 28 decay                                   */
    SF_sustainEnv1,		  /* 29 sustain                                 */
    SF_releaseEnv1,		  /* 30 release                                 */
    SF_autoHoldEnv1,	  /* 31                                         */
    SF_autoDecayEnv1,	  /* 32                                         */
    SF_delayEnv2,		  /* 33 delay 0x8000 - n(725us)                 */
    SF_attackEnv2,		  /* 34 attack                                  */
    SF_holdEnv2,		  /* 35 hold                                    */
    SF_decayEnv2,		  /* 36 decay                                   */
    SF_sustainEnv2,		  /* 37 sustain                                 */
    SF_releaseEnv2,		  /* 38 release                                 */
    SF_autoHoldEnv2,	  /* 39                                         */
    SF_autoDecayEnv2,	  /* 40                                         */
    SF_instrument,		  /* 41                                         */
    SF_nop,			      /* 42                                         */
    SF_keyRange,		  /* 43                                         */
    SF_velRange,		  /* 44                                         */
    SF_startloopAddrsHi,  /* 45 high word of startloopAddrs             */
    SF_keynum,		      /* 46                                         */
    SF_velocity,		  /* 47                                         */
    SF_initAtten,		  /* 48                                         */
    SF_keyTuning,		  /* 49                                         */
    SF_endloopAddrsHi,	  /* 50 high word of endloopAddrs               */
    SF_coarseTune,		  /* 51                                         */
    SF_fineTune,		  /* 52                                         */
    SF_sampleId,		  /* 53                                         */
    SF_sampleFlags,		  /* 54                                         */
    SF_samplePitch,		  /* 55 SF1 only                                */
    SF_scaleTuning,		  /* 56                                         */
    SF_keyExclusiveClass, /* 57                                         */
    SF_rootKey,		      /* 58                                         */
    SF_EOF			      /* 59                                         */
};

char *sf_gen_text[SF_EOF];

Sample OverrideSample = {0};

typedef struct _LayerTable
{
	short val[SF_EOF];
	char set[SF_EOF];
}LayerTable;

typedef struct _LayerItem
{
	int copy;	/* copy policy */
	int type;	/* conversion type */
	int minv;	/* minimum value */
	int maxv;	/* maximum value */
	int defv;	/* default value */
}LayerItem;

/* copy policy */
enum
{
	L_INHRT,	/* add to global */
	L_OVWRT,	/* overwrite on global */
	L_RANGE,	/* range */
	L_PRSET,	/* preset only */
	L_INSTR		/* instrument only */
};

/* data type */
enum
{
	T_NOP,		/* nothing */
	T_NOCONV,	/* no conversion */
	T_OFFSET,	/* address offset */
	T_HI_OFF,	/* address coarse offset (32k) */
	T_RANGE,	/* range; composite values (0-127/0-127) */

	T_CUTOFF,	/* initial cutoff */
	T_FILTERQ,	/* initial resonance */
	T_TENPCT,	/* effects send */
	T_PANPOS,	/* panning position */
	T_ATTEN,	/* initial attenuation */
	T_SCALE,	/* scale tuning */

	T_TIME,		/* envelope/LFO time */
	T_TM_KEY,	/* time change per key */
	T_FREQ,		/* LFO frequency */
	T_PSHIFT,	/* env/LFO pitch shift */
	T_CSHIFT,	/* env/LFO cutoff shift */
	T_TREMOLO,	/* LFO tremolo shift */
	T_MODSUST,	/* modulation env sustain level */
	T_VOLSUST,	/* volume env sustain level */

	T_EOT		/* end of type */
};

enum EnumOverWriteMode
{ //elion add
	EOWM_ENABLE_VIBRATO = 1, 
	EOWM_ENABLE_TREMOLO = 2,
	EOWM_ENABLE_CUTOFF = 4, 
	EOWM_ENABLE_VEL = 8, 
	EOWM_ENABLE_MOD = 16, 
	EOWM_ENABLE_ENV = 32, 
};

LayerItem layer_items[SF_EOF];

///r
i8 sf_attenuation_neg  = 0;
f64 sf_attenuation_pow = 10.0; // sb xfi
f64 sf_attenuation_mul = 0.005; // sb xfi
f64 sf_attenuation_add = 0;

i32 sf_limit_volenv_attack = 6;
i32 sf_limit_modenv_attack = 6;
i32 sf_limit_modenv_fc     = 1200;
i32 sf_limit_modenv_pitch  = 12000;
i32 sf_limit_modlfo_fc     = 12000;
i32 sf_limit_modlfo_pitch  = 12000;
i32 sf_limit_viblfo_pitch  = 12000;
i32 sf_limit_modlfo_freq   = 100000;
i32 sf_limit_viblfo_freq   = 100000;

i32 sf_default_modlfo_freq = 8176;
i32 sf_default_viblfo_freq = 8176;

i8 sf_config_lfo_swap     = 0; // 0:ModLfo to Tremolo , VibLfo to Vibrato , 1:swap
i8 sf_config_addrs_offset = 0; // 1:on

#define SF2_24BIT 1

#define FILENAME_NORMALIZE(fname) url_unexpand_home_dir(fname)
#define FILENAME_REDUCED(fname)   url_unexpand_home_dir(fname)
#define SFMalloc(rec, count)      new_segment(&(rec)->pool, count)
#define SFStrdup(rec, s)          strdup_mblock(&(rec)->pool, s)

/*----------------------------------------------------------------
 * compile flags
 *----------------------------------------------------------------*/

#define SF_CLOSE_EACH_FILE 1

/* return value */
#define AWE_RET_OK		    0	/* successfully loaded */
#define AWE_RET_ERR		    1	/* some fatal error occurs */
#define AWE_RET_SKIP		2	/* some fonts are skipped */
#define AWE_RET_NOMEM		3	/* out or memory; not all fonts loaded */
#define AWE_RET_NOT_FOUND	4	/* the file is not found */



typedef int (*SBKConv)(int gen, int amount);

/*----------------------------------------------------------------
 * local parameters
 *----------------------------------------------------------------*/

typedef struct _SFPatchRec
{
	int preset, bank, keynote; /* -1 = matches all */
}SFPatchRec;

typedef struct _SampleList
{
	int sfrom;
	Sample v;
	struct _SampleList *next;
	off_size_t start, lowbit;
	off_size_t len;
	i32 cutoff_freq;
	i16 resonance;
	i16 root, tune;
	char low, high;		/* key note range */
	i8 reverb_send, chorus_send;

	/* Depend on play_mode->rate */
	i32 vibrato_freq;
	i32 attack;
	i32 hold;
	i32 sustain;
	i32 decay;
	i32 release;

	i32 modattack;
	i32 modhold;
	i32 modsustain;
	i32 moddecay;
	i32 modrelease;

	int bank, keynote;	/* for drum instruments */
}SampleList;

typedef struct _InstList
{
	SFPatchRec pat;
	int pr_idx;
	int samples;
	int order;
	SampleList *slist;
	struct _InstList *next;
}InstList;

typedef struct _SFExclude
{
	SFPatchRec pat;
	struct _SFExclude *next;
}SFExclude;

typedef struct _SFOrder
{
	SFPatchRec pat;
	int order;
	struct _SFOrder *next;
}SFOrder;

#define INSTHASHSIZE 127
#define INSTHASH(bank, preset, keynote) \
	((int)(((unsigned)bank ^ (unsigned)preset ^ (unsigned)keynote) % INSTHASHSIZE))

typedef struct _SFInsts
{
	FILE *tf;
	char *fname;
	i8 def_order, def_cutoff_allowed, def_resonance_allowed;
	u16 version, minorversion;
	off_size_t samplepos, lowbitpos;
	size_t samplesize;
	InstList *instlist[INSTHASHSIZE];
	char **inst_namebuf;
	SFExclude *sfexclude;
	SFOrder *sforder;
	struct _SFInsts *next;
	f64 amptune;
	MBlockList pool;
}SFInsts;

typedef struct OverrideTiMidityData
{
	short overwriteMode;
	short timRunMode;
	/* nrpn */
	f64 vibrato_rate;
	f64 vibrato_cent;
	f64 vibrato_delay;
	f64 filter_freq;
	f64 filter_reso;
	/* effect */
	char chorus_send, reverb_send;
	f64 gsefx_CustomODLv;
	f64 gsefx_CustomODDrive;
	f64 gsefx_CustomODFreq;
	f64 xgefx_CustomODLv;
	f64 xgefx_CustomODDrive;
	f64 xgefx_CustomODFreq;
	f64 sdefx_CustomODLv;
	f64 sdefx_CustomODDrive;
	f64 sdefx_CustomODFreq;
	f64 gsefx_CustomLFLvIn;
	f64 gsefx_CustomLFLvOut;
	f64 xgefx_CustomLFLvIn;
	f64 xgefx_CustomLFLvOut;
	f64 sdefx_CustomLFLvIn;
	f64 sdefx_CustomLFLvOut;
	f64 efx_CustomHmnLvIn;
	f64 efx_CustomHmnLvOut;
	f64 efx_CustomLmtLvIn;
	f64 efx_CustomLmtLvOut;
	f64 efx_CustomCmpLvIn;
	f64 efx_CustomCmpLvOut;
	f64 efx_CustomWahLvIn;
	f64 efx_CustomWahLvOut;
	f64 efx_CustomGRevLvIn;
	f64 efx_CustomGRevLvOut;
	f64 efx_CustomEnhLvIn;
	f64 efx_CustomEnhLvOut;
	f64 efx_CustomRotLvOut;
	f64 efx_CustomPSLvOut;
	f64 efx_CustomRMLvOut;
	int efx_CustomRevType;

#ifdef CUSTOMIZE_CHORUS_PARAM
	struct tag_chorus {
		char pre_lpf;
		char level;
		char feedback;
		char delay;
		char rate;
		char depth;
		char send_reverb;
		char send_delay;
	}chorus_param;
#endif

	struct tag_delay
	{
		f32 delay;
		f64 level, feedback;
	}delay_param;

	f64 compThr, compSlope, compLook, compWTime, compATime, compRTime;

	unsigned char EnableVolMidCtrl;
	short DriverRVolume;
}OVERRIDETIMIDITYDATA ;


OVERRIDETIMIDITYDATA otd = {0};

/*----------------------------------------------------------------*/

/* prototypes */

#define P_GLOBAL	1
#define P_LAYER		2
#ifndef FALSE
#define FALSE       0
#endif /* FALSE */
#ifndef TRUE
#define TRUE 1
#endif /* TRUE */


static SFInsts *find_soundfont(const char *sf_file);
static SFInsts *new_soundfont(const char *sf_file);
static void init_sf(Kasaria *ksr, SFInsts *rec);
static void end_soundfont(SFInsts *rec);
static Instrument *try_load_soundfont(Kasaria *ksr, SFInsts *rec, int order, int bank, int preset, int keynote);
static Instrument *load_from_file(Kasaria *ksr, SFInsts *rec, InstList *ip);
static int is_excluded(SFInsts *rec, int bank, int preset, int keynote);
static int is_ordered(SFInsts *rec, int bank, int preset, int keynote);
//static int load_font(Kasaria *ksr, SFInfo *sf, int pridx);
static int parse_layer(Kasaria *ksr, SFInfo *sf, int pridx, LayerTable *tbl, int level);
static int is_global(SFGenLayer *layer);
static void clear_table(LayerTable *tbl);
static void set_to_table(SFInfo *sf, LayerTable *tbl, SFGenLayer *lay, int level);
static void add_item_to_table(LayerTable *tbl, int oper, int amount, int level);
static void merge_table(SFInfo *sf, LayerTable *dst, LayerTable *src);
static void init_and_merge_table(SFInfo *sf, LayerTable *dst, LayerTable *src);
static int sanity_range(LayerTable *tbl);
static int make_patch(Kasaria *ksr,SFInfo *sf, int pridx, LayerTable *tbl);
static void make_info(Kasaria *ksr, SFInfo *sf, SampleList *vp, LayerTable *tbl);
static f64 calc_volume(LayerTable *tbl);
static void set_sample_info(Kasaria *ksr, SFInfo *sf, SampleList *vp, LayerTable *tbl);
static void set_init_info(Kasaria *ksr, SFInfo *sf, SampleList *vp, LayerTable *tbl);
static void reset_last_sample_info(void);
static int abscent_to_Hz(int abscents);
static void set_rootkey(SFInfo *sf, SampleList *vp, LayerTable *tbl);
static void set_rootfreq(SampleList *vp);
static i32 to_offset(i32 offset);
static i32 to_rate(Kasaria *ksr, i32 diff, int timecent);
static i32 calc_rate(Kasaria *ksr, i32 diff, f64 msec);
static f64 to_msec(int timecent);
static i32 calc_sustain(int sust_cB);
static void convert_volume_envelope(Kasaria *ksr, SampleList *vp, LayerTable *tbl);
static void convert_tremolo(SampleList *vp, LayerTable *tbl);
static void convert_vibrato(SampleList *vp, LayerTable *tbl);

// SFBK Conversions
static int sbk_cutoff(int gen, int val);
static int sbk_filterQ(int gen, int val);
static int sbk_tenpct(int gen, int val);
static int sbk_panpos(int gen, int val);
static int sbk_atten(int gen, int val);
static int sbk_scale(int gen, int val);
static int sbk_time(int gen, int val);
static int sbk_tm_key(int gen, int val);
static int sbk_freq(int gen, int val);
static int sbk_pshift(int gen, int val);
static int sbk_cshift(int gen, int val);
static int sbk_tremolo(int gen, int val);
static int sbk_volsust(int gen, int val);
static int sbk_modsust(int gen, int val);


/*----------------------------------------------------------------*/

#define def_drum_inst 0

int opt_sf_close_each_file    = SF_CLOSE_EACH_FILE;
static SFInsts *sfrecs        = NULL;
static SFInsts *current_sfrec = NULL;

static int sfrom_load       = 0;
static SFInsts *sfrom_sfrec = NULL;
static SFInfo sfrom_sfinfo  = {0};

static void load_sfrom(Kasaria *ksr)
{
	const char *sf_file = NULL;
	FILE *tf = NULL;
	int i;

	if(sfrom_load != 0)
		return;
	
	sf_file = FILENAME_NORMALIZE("SFROM.SF2");
	
	if((tf = open_file(ksr, sf_file, 1, OF_VERBOSE)) == NULL)
	{
		//ctl->cmsg(CMSG_WARNING, VERB_NOISY, "Can't open SFROM.SF2");
		ulog_warn("Can't open SFROM.SF2");
		sfrom_load = -1;
		return;
	}
	
	memset(&sfrom_sfinfo, 0, sizeof(SFInfo));
	if(load_soundfont(&sfrom_sfinfo, tf))
	{
		close_file(tf);
		tf = NULL;
		// ctl->cmsg(CMSG_WARNING, VERB_NOISY, "SFROM : load_soundfont() error");
		ulog_warn("SFROM : load_soundfont() error");
		sfrom_load = -1;
	    return;
	}
	
	correct_samples(&sfrom_sfinfo);
	
	sfrom_sfrec = (SFInsts*) safe_malloc(sizeof(SFInsts));
	
	if(!sfrom_sfrec)
	{
		close_file(tf);
		tf = NULL;
		// ctl->cmsg(CMSG_WARNING, VERB_NOISY, "SFROM : malloc error");
		ulog_warn("SFROM : malloc error");
		sfrom_load = -1;
		return;
	}
	
	memset(sfrom_sfrec, 0, sizeof(SFInsts));
	init_mblock(&sfrom_sfrec->pool);
	
	sfrom_sfrec->tf        = tf;
	sfrom_sfrec->fname     = SFStrdup(sfrom_sfrec, sf_file);
	sfrom_sfrec->def_order = DEFAULT_SOUNDFONT_ORDER;
	sfrom_sfrec->amptune   = 1.0;	
	current_sfrec = sfrom_sfrec;
	
	for(i = 0; i < sfrom_sfinfo.npresets; i++)
		load_font(ksr, &sfrom_sfinfo, i);
	
	sfrom_sfrec->version      = sfrom_sfinfo.version;
	sfrom_sfrec->minorversion = sfrom_sfinfo.minorversion;
	sfrom_sfrec->samplepos    = sfrom_sfinfo.samplepos;
	sfrom_sfrec->lowbitpos    = sfrom_sfinfo.lowbitpos;
	sfrom_sfrec->samplesize   = sfrom_sfinfo.samplesize;
	sfrom_sfrec->inst_namebuf = NULL;
	sfrom_load                = 1;	
	
	return;
}

static void free_sfrom(void)
{
	if(sfrom_load < 1)
	{
		sfrom_load = 0;
		return;
	}
	
	if(!sfrom_sfrec)
	{
		sfrom_load = 0;
		return;
	}
	
	free_soundfont(&sfrom_sfinfo);
	
	if(sfrom_sfrec->tf)
		close_file(sfrom_sfrec->tf);
	
	sfrom_sfrec->tf = NULL;
	
	reuse_mblock(&sfrom_sfrec->pool);
	safe_free(sfrom_sfrec);
	
	sfrom_sfrec = NULL;
	sfrom_load  = 0;
	
	return;
}

/*----------------------------------------------------------------
 * conversion rules for each type
 *----------------------------------------------------------------*/

/* initial cutoff */
static int sbk_cutoff(int gen, int val)
{
	if(val == 127)
		return 14400;
	else
		return 59 * val + 4366;
	/*return 50 * val + 4721;*/
}

/* initial resonance */
static int sbk_filterQ(int gen, int val)
{
	return val * 3 / 2;
}

/* chorus/reverb */
static int sbk_tenpct(int gen, int val)
{
	return val * 1000 / 256;
}

/* pan position */
static int sbk_panpos(int gen, int val)
{
	return val * 1000 / 127 - 500;
}

/* initial attenuation */
static int sbk_atten(int gen, int val)
{
	if(val == 0)
		return 1000;
	
	return (int)(-200.0 * log10((f64)val * DIV_127) * 10);
}

/* scale tuning */
static int sbk_scale(int gen, int val)
{
	return (val ? 50 : 100);
}

/* env/lfo time parameter */
static int sbk_time(int gen, int val)
{
	if(val <= 0)
	    val = 1;
	
	return (int)(log((f64)val * DIV_1000) / log(2.0) * 1200.0);
}

/* time change per key */
static int sbk_tm_key(int gen, int val)
{
	return (int)(val * 5.55);
}

/* lfo frequency */
static int sbk_freq(int gen, int val)
{
	if(val == 0)
	{
		if(gen == SF_freqLfo1)
			return -725;
		else /* SF_freqLfo2*/
			return -15600;
	}
	/*return (int)(3986.0 * log10((double)val) - 7925.0);*/
	return (int)(1200 * log10((f64)val) / log10(2.0) - 7925.0);

}

/* lfo/env pitch shift */
static int sbk_pshift(int gen, int val)
{
	return (1200 * val / 64 + 1) / 2;
}

/* lfo/env cutoff freq shift */
static int sbk_cshift(int gen, int val)
{
	if(gen == SF_lfo1ToFilterFc)
		return (1200 * 3 * val) / 64;
	else
		return (1200 * 6 * val) / 64;
}

/* lfo volume shift */
static int sbk_tremolo(int gen, int val)
{
	return (120 * val) / 64;
}

/* mod env sustain */
static int sbk_modsust(int gen, int val)
{
	if(val < 96)
		return 1000 * (96 - val) / 96;
	else
		return 0;
}

/* vol env sustain */
static int sbk_volsust(int gen, int val)
{
	if(val < 96)
		return (2000 - 21 * val) / 2;
	else
		return 0;
}

static SBKConv sbk_convertors[T_EOT] =
{
	NULL, NULL, NULL, NULL, NULL,

	sbk_cutoff, sbk_filterQ, sbk_tenpct, sbk_panpos, sbk_atten, sbk_scale,

	sbk_time, sbk_tm_key, sbk_freq, sbk_pshift, sbk_cshift,
	sbk_tremolo, sbk_modsust, sbk_volsust,
};

int sbk_to_sf2(int oper, int amount)
{
	LayerItem *item = &layer_items[oper];
	
	if(item->type < 0 || item->type >= T_EOT)
	{
		fprintf(stderr, "illegal gen item type %d\n", item->type);
		return amount;
	}
	
	if(sbk_convertors[item->type])
		return sbk_convertors[item->type](oper, amount);
	
	return amount;
}

static SFInsts *find_soundfont(const char *sf_file)
{
    SFInsts *sf;

    sf_file = FILENAME_NORMALIZE(sf_file);
    
    for(sf = sfrecs; sf; sf = sf->next)
	    if(sf->fname && !strcmp(sf->fname, sf_file))
		    return sf;
    
    return NULL;
}

static SFInsts *new_soundfont(const char *sf_file)
{
	SFInsts *sf, *prev;

	sf_file = FILENAME_NORMALIZE(sf_file);
	for(sf = sfrecs, prev = NULL; sf; prev = sf, sf = sf->next)
	{
		if(!sf->fname)
		{
			/* remove the record from the chain to reuse */
			if(prev)
				prev->next = sf->next;
			else if(sfrecs == sf)
				sfrecs = sf->next;
			
			break;
		}
	}
	if(!sf)
		sf = (SFInsts*) safe_malloc(sizeof(SFInsts));
	
	memset(sf, 0, sizeof(SFInsts));
	init_mblock(&sf->pool);
	
	sf->fname     = SFStrdup(sf, FILENAME_NORMALIZE(sf_file));
	sf->def_order = DEFAULT_SOUNDFONT_ORDER;
	sf->amptune   = 1.0;
	
	return sf;
}

void add_soundfont(char *sf_file, int sf_order, int sf_cutoff, int sf_resonance, int amp)
{
    SFInsts *sf;

    if((sf = find_soundfont(sf_file)) == NULL)
    {
        sf = new_soundfont(sf_file);
        sf->next = sfrecs;
        sfrecs = sf;
    }

    if(sf_order >= 0)
        sf->def_order = sf_order;
    
    if(sf_cutoff >= 0)
        sf->def_cutoff_allowed = sf_cutoff;
    
    if(sf_resonance >= 0)
        sf->def_resonance_allowed = sf_resonance;
    
    if(amp >= 0)
        sf->amptune = (f64)amp * 0.01;
    
    current_sfrec = sf;
}

void remove_soundfont(char *sf_file)
{
    SFInsts *sf;

    if((sf = find_soundfont(sf_file)) != NULL)
	    end_soundfont(sf);
}

void free_soundfonts()
{
	SFInsts *sf, *next;

	for(sf = sfrecs; sf; sf = next)
	{
		// if(sf->tf)
		// 	safe_free(sf->tf->url);
		
		// safe_free(sf->tf);
		close_file(sf->tf);
		
		sf->tf = NULL;
		reuse_mblock(&sf->pool);
		next = sf->next;
		safe_free(sf);
	}
    sfrecs = NULL;//added by Kobarin
	free_sfrom();
}

char *soundfont_preset_name(int bank, int preset, int keynote, const char **sndfile)
{
    SFInsts *rec;
    if(sndfile)
	    *sndfile = NULL;
    
    for(rec = sfrecs; rec; rec = rec->next)
	    if(rec->fname)
	    {
		    int addr;
		    InstList *ip;

		    addr = INSTHASH(bank, preset, keynote);
		    for(ip = rec->instlist[addr]; ip; ip = ip->next)
			    if(ip->pat.bank == bank && ip->pat.preset == preset && (keynote < 0 || keynote == ip->pat.keynote))
				    break;
			    
			if(ip)
			{
			    if(sndfile)
		            *sndfile = rec->fname;
						
			    return rec->inst_namebuf[ip->pr_idx];
			}
	    }
    
    return NULL;
}

Instrument *sndfont_load_instrument(Kasaria *ksr, int bank, int preset)
{
    SFInsts *rec;
    Instrument *inst = NULL;
    int pridx;
    
    if(!ksr || !ksr->sf_loaded || !ksr->sf_info)
        return NULL;
    
    /*
     * Create the private SFInsts record.
     *
     * SFInsts remains completely internal to sndfont.c.
     */
    rec = new_soundfont(ksr->sf_filename);

    if(!rec)
    {
        ulog_error("SF2: failed to create soundfont record");
        return NULL;
    }
    
    /*
     * This is required by make_patch(), is_excluded(),
     * is_ordered(), etc.
     */
    current_sfrec = rec;
    
    /*
     * Open the SF2 again for sample data.
     *
     * load_soundfont() has already parsed the SF2 into
     * ksr->sf_info, but load_from_file() still needs rec->tf
     * to read the actual sample data.
     */
    rec->tf = fopen(ksr->sf_filename, "rb");
    
    if(!rec->tf)
    {
        ulog_error("SF2: failed to open soundfont samples: %s", ksr->sf_filename);

        current_sfrec = NULL;
        end_soundfont(rec);
        return NULL;
    }
    
    /*
     * Copy the information that load_from_file() needs.
     *
     * This mirrors init_sf().
     */
    rec->samplepos  = ksr->sf_info->samplepos;
    rec->lowbitpos  = ksr->sf_info->lowbitpos;
    rec->samplesize = ksr->sf_info->samplesize;
    
    /*
     * load_from_file() accesses:
     *
     *     rec->inst_namebuf[ip->pr_idx]
     *
     * so create it exactly like init_sf() does.
     */
    rec->inst_namebuf = (char **)SFMalloc(rec, ksr->sf_info->npresets * sizeof(char *));
    
    for(pridx = 0; pridx < ksr->sf_info->npresets; pridx++)
        rec->inst_namebuf[pridx] = (char *)SFStrdup(rec, ksr->sf_info->preset[pridx].hdr.name);
    
    
    /*
     * Find the requested preset.
     */
    for(pridx = 0; pridx < ksr->sf_info->npresets; pridx++)
    {
        if(ksr->sf_info->preset[pridx].bank != bank)
            continue;
        
        if(ksr->sf_info->preset[pridx].preset != preset)
            continue;
        
        /*
         * Build the internal InstList/SampleList representation.
         *
         * current_sfrec MUST point to rec here.
         */
        if(load_font(ksr, ksr->sf_info, pridx) != AWE_RET_OK)
        {
            ulog_debug("SF2: load_font failed " "bank=%d preset=%d", bank, preset);
            continue;
        }
        
        /*
         * Convert the internal InstList into the public
         * Instrument structure and load the sample data.
         */
        inst = try_load_soundfont(ksr, rec, -1, bank, preset, -1);
        
        if(inst)
        {
            ulog_debug("SF2: instrument loaded " "bank=%d preset=%d inst=%p", bank, preset, (void *)inst);
            
            /*
             * IMPORTANT:
             *
             * Do not destroy rec here.
             *
             * load_from_file() has used rec to construct the
             * Instrument and its samples, and rec owns the
             * associated SF2 state.
             */
            return inst;
        }
        
        ulog_debug("SF2: try_load_soundfont returned NULL " "bank=%d preset=%d", bank, preset);
    }
    
    /*
     * Nothing was loaded.
     */
    ulog_debug("SF2: no instrument found for bank=%d preset=%d", bank, preset);
    
    return NULL;
}

static void init_sf(Kasaria *ksr, SFInsts *rec)
{
	SFInfo sfinfo = {0};
	int i;

	// ctl->cmsg(CMSG_INFO, VERB_NOISY, "Init soundfonts `%s'", FILENAME_REDUCED(rec->fname));
	ulog_info("Init soundfonts `%s'", FILENAME_REDUCED(rec->fname));
	

	if((rec->tf = open_file(ksr, rec->fname, 1, OF_VERBOSE)) == NULL)
	{
		// ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "Can't open soundfont file %s", FILENAME_REDUCED(rec->fname));
		ulog_error("Can't open soundfont file %s", FILENAME_REDUCED(rec->fname));
		end_soundfont(rec);
		return;
	}

	if(load_soundfont(&sfinfo, rec->tf))
	{
	    end_soundfont(rec);
	    return;
	}
	
	if(sfinfo.use_rom)
		load_sfrom(ksr);
	
	correct_samples(&sfinfo);
	current_sfrec = rec;
	
	for(i = 0; i < sfinfo.npresets; i++)
	{
		int bank = sfinfo.preset[i].bank;
		int preset = sfinfo.preset[i].preset;

		if(bank == 128 && 0 <= preset && preset < 128 + MAP_BANK_COUNT)
		    /* FIXME: why not allow exclusion of drumsets? */
		    alloc_instrument_bank(1, preset);
		else if(0 <= bank && bank < 128 + MAP_BANK_COUNT)
		{
			if(is_excluded(rec, bank, preset, -1))
				continue;
			
			alloc_instrument_bank(0, bank);
		}
		else
		{
			//ctl->cmsg(CMSG_ERROR, VERB_VERBOSE, "%s: bank/preset is out of range [bank = %d, preset = %d]", FILENAME_REDUCED(rec->fname), bank, preset);
			ulog_error("%s: bank/preset is out of range [bank = %d, preset = %d]", FILENAME_REDUCED(rec->fname), bank, preset);
			continue;
		}
		load_font(ksr, &sfinfo, i);
	}

	/* copy header info */
	rec->version      = sfinfo.version;
	rec->minorversion = sfinfo.minorversion;
	rec->samplepos    = sfinfo.samplepos;
	rec->lowbitpos    = sfinfo.lowbitpos;
	rec->samplesize   = sfinfo.samplesize;
	rec->inst_namebuf = (char**)SFMalloc(rec, sfinfo.npresets * sizeof(char*));
	
	for(i = 0; i < sfinfo.npresets; i++)
		rec->inst_namebuf[i] = (char*)SFStrdup(rec, sfinfo.preset[i].hdr.name);
	
	free_soundfont(&sfinfo);

	/*
	if(!opt_sf_close_each_file)
	{
		if(!IS_URL_SEEK_SAFE(rec->tf->url))
		{
			close_file(rec->tf);
			rec->tf = NULL;
		}
	}
	else
	{
		close_file(rec->tf);
		rec->tf = NULL;
	}
	*/
}

void init_load_soundfont(Kasaria *ksr)
{
    SFInsts *rec;
    
    for(rec = sfrecs; rec; rec = rec->next)
	    if(rec->fname)
	        init_sf(ksr, rec);
}

static void end_soundfont(SFInsts *rec)
{
	if(rec->tf)
	{
		close_file(rec->tf);
		rec->tf = NULL;
	}

	rec->fname        = NULL;
	rec->inst_namebuf = NULL;
	rec->sfexclude    = NULL;
	rec->sforder      = NULL;
	
	reuse_mblock(&rec->pool);
}

Instrument *extract_soundfont(Kasaria *ksr, const char *sf_file, int bank, int preset, int keynote)
{
    SFInsts *sf;

    if((sf = find_soundfont(sf_file)) != NULL)
	    return try_load_soundfont(ksr, sf, -1, bank, preset, keynote);
    
    sf = new_soundfont(sf_file);
    sf->next = sfrecs;
    sf->def_order = 2;
    sfrecs = sf;
    init_sf(ksr, sf);
    
    return try_load_soundfont(ksr, sf, -1, bank, preset, keynote);
}

/*----------------------------------------------------------------
 * get converted instrument info and load the wave data from file
 *----------------------------------------------------------------*/

Instrument *try_load_soundfont(Kasaria *ksr, SFInsts *rec, int order, int bank, int preset, int keynote)
{
    ulog_debug("Try load soundfont");
	InstList *ip;
	Instrument *inst = NULL;
	int addr;

	if(!rec->tf)
	{
		if(!rec->fname)
			return NULL;
		
		if((rec->tf = open_file(ksr, rec->fname, 1, OF_VERBOSE)) == NULL)
		{
			// ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "Can't open soundfont file %s", FILENAME_REDUCED(rec->fname));
			ulog_error("Can't open soundfont file %s", FILENAME_REDUCED(rec->fname));
			end_soundfont(rec);
			return NULL;
		}
		// if(!opt_sf_close_each_file)
		// 	if(!IS_URL_SEEK_SAFE(rec->tf->url))
		// 		rec->tf->url = url_cache_open(rec->tf->url, 1);
	}

	addr = INSTHASH(bank, preset, keynote);
	
	for(ip = rec->instlist[addr]; ip; ip = ip->next)
		if(ip->pat.bank == bank && ip->pat.preset == preset && (keynote < 0 || ip->pat.keynote == keynote) && (order < 0 || ip->order == order))
			break;
	

	if(ip && ip->samples)
		inst = load_from_file(ksr, rec, ip);

	if(opt_sf_close_each_file)
	{
		close_file(rec->tf);
		rec->tf = NULL;
	}

	return inst;
}

Instrument *load_soundfont_inst(Kasaria *ksr, int order, int bank, int preset, int keynote)
{
    SFInsts *rec;
    Instrument *ip;
    /*
     * Search through all ordered soundfonts
     */
    int o = order;

    for(rec = sfrecs; rec; rec = rec->next)
    {
	    if(rec->fname)
	    {
		    ip = try_load_soundfont(ksr, rec, o, bank, preset, keynote);
		    
		    if(ip)
		        return ip;
			
		    if(o > 0)
				o++;
	    }
    }
    return NULL;
}

/*----------------------------------------------------------------*/

static f64 calc_volume(LayerTable *tbl)
{
    /*
        sf spec
        vol=10^(-cb/200) 
        table 
        vol=2^(cb*-6/960) 
        16dB : 0.5 : 1/2 , 96dB : 0.015625 : 1/64
    */	
    int v;
	f64 tmp;

    if(!tbl->set[SF_initAtten] || (int)tbl->val[SF_initAtten] == 0)
	    return (f64)1.0;

	v = (int)tbl->val[SF_initAtten];
	
#if 1 // user define
	if(!sf_attenuation_neg)
		if(v < 0) v = 0;
	
	if(v > 1440)
	    v = 1440; // sf spec	
	else if(v < -210)
	    v = -210; // sb xfi
	
	tmp = v;
	tmp += sf_attenuation_add;
	
	if(sf_attenuation_pow <= 0 || sf_attenuation_mul <= 0)
		return pow(10.0, -tmp * DIV_200); // sf spec	
	else
		return pow(sf_attenuation_pow, -tmp * sf_attenuation_mul);
#elif 1 // calc (=table
	if(v > 1440)
		v = 1440;
	else if(v < -1440)
		v = -1440;
	return pow(2.0, (f64)v * -DIV_160); // DIV_160 = -6/960			
#elif 1 // table 
	if (v < 0) { // -att
		v = abs(v);
		if(v > 1440)
			v = 1440;
		if (v > 960)
			return (f64)1.0 / ((f64)cb_to_amp_table[960] * (f64)cb_to_amp_table[v - 960]); 
		else
			return (f64)1.0 / (f64)cb_to_amp_table[v];
    } else { // +att
		if(v > 1440)
			v = 1440;
		if (v > 960)
			return (f64)cb_to_amp_table[960] * (f64)cb_to_amp_table[v - 960]; 
		else
			return (f64)cb_to_amp_table[v]; 
	}	
#else
    if (v < 0) {
		v += 960;
		if (v < 0) { v = 0; }
		return 1.0 + cb_to_amp_table[v];
    }
    else if (v > 960) { v = 960; }
	return cb_to_amp_table[v];
#endif
}

/* convert from 16bit value to fractional offset (15.15) */
static i32 to_offset(i32 offset)
{
	return offset << 14;
}

#define SF_ENVRATE_MAX (0x3FFFFFFFL)
#define SF_ENVRATE_MIN (1L)

/* calculate ramp rate in fractional unit;
 * diff = 16bit, time = msec
 */
static i32 calc_rate(Kasaria *ksr, i32 diff, f64 msec)
{
    f64 rate;

    if(FP_EQ_0(msec))
        return (i32)SF_ENVRATE_MAX + 1;
    
    if(diff <= 0)
        diff = 1;
    
    diff <<= 14;
    rate = ((f64)diff / ksr->play_mode.rate) * ksr->control_ratio * 1000.0 / msec;
    
    if(ksr->fast_decay)
        rate *= 2;
    
	if(rate > SF_ENVRATE_MAX)
	    rate = SF_ENVRATE_MAX;
					
	else if(rate < SF_ENVRATE_MIN)
	    rate = SF_ENVRATE_MIN;
	
    return (i32)rate;
}

/* calculate ramp rate in fractional unit;
 * diff = 16bit, timecent
 */
static i32 to_rate(Kasaria *ksr, i32 diff, int timecent)
{
    f64 rate;

    if(timecent == -12000)	/* instantaneous attack */
	    return (i32)SF_ENVRATE_MAX + 1;
    
    if(diff <= 0)
        diff = 1;
    
    diff <<= 14;
    rate = (f64)diff * ksr->control_ratio / ksr->play_mode.rate / pow(2.0, (f64)timecent * DIV_1200);
    
    if(ksr->fast_decay)
        rate *= 2;
    
	if(rate > SF_ENVRATE_MAX)
	    rate = SF_ENVRATE_MAX;
	else if(rate < SF_ENVRATE_MIN)
	    rate = SF_ENVRATE_MIN;
	
    return (i32)rate;
}

/*
 * convert timecents to sec
 */
static f64 to_msec(int timecent)
{
    return timecent == -12000 ? 0 : 1000.0 * pow(2.0, (f64)timecent * DIV_1200);
}

/*
 * VolEnv Sustain level
 * sf: centibels
 * parm: 0x7f - sustain_level(dB) * 0.75
 */
static i32 calc_volenv_sustain(int sust_cB)
{
#if 1
	if(sust_cB <= 0)
	    return 65533;
	else if(sust_cB >= 1440)
	    return 0;			
	else
	    return (1440 - sust_cB) * 65533 / 1440;
	
	/* ampenv ボリュームカーブ通過で cb変換される */
#else
	if(sust_cB <= 0)
	    return 65533;
	else if(sust_cB >= 1000)
	    return 0;
	else
	    return (1000 - sust_cB) * 65533 / 1000;
#endif
}

/*
 * ModEnv Sustain level
 * sf: 0.1 per cent
 * parm: 1000 ~ 0 {0.1%]
 */
static i32 calc_modenv_sustain(int sust_dcent)
{
	if(sust_dcent <= 0)
	    return 65533;
	else if(sust_dcent >= 1000)
	    return 0;
	else
	
	return ((1000 - sust_dcent) * 65533 / 1000);
}

static int dump_wav_counter = 0;

static void dump_sample_wav(Sample *sample)
{
    char path[128];
    FILE *w;
    long frames;
    u32  rate, data_len;

    frames   = sample->data_length >> FRACTION_BITS;
    rate     = (u32)sample->sample_rate;
    data_len = (u32)(frames * 2);

    snprintf(path, sizeof(path), "/home/andre/Full_Grand_Piano_Samples_pcm/ksr_sample_%02d_%uHz.wav", dump_wav_counter++, rate);

    w = fopen(path, "wb");
    if(!w)
        return;

    fwrite("RIFF", 1, 4, w);
    { u32 c = 36 + data_len; fwrite(&c, 4, 1, w); }
    fwrite("WAVE", 1, 4, w);
    fwrite("fmt ", 1, 4, w);
    { u32 c = 16;         fwrite(&c, 4, 1, w); }
    { u16 c = 1;          fwrite(&c, 2, 1, w); }   /* PCM */
    { u16 c = 1;          fwrite(&c, 2, 1, w); }   /* mono */
    fwrite(&rate, 4, 1, w);
    { u32 c = rate * 2;   fwrite(&c, 4, 1, w); }   /* byte rate */
    { u16 c = 2;          fwrite(&c, 2, 1, w); }   /* block align */
    { u16 c = 16;         fwrite(&c, 2, 1, w); }   /* bits */
    fwrite("data", 1, 4, w);
    fwrite(&data_len, 4, 1, w);

    fwrite(sample->data, 1, data_len, w);
    fclose(w);

    ulog_info("dumped %s (%ld frames, %u Hz)", path, frames, rate);
}

static Instrument *load_from_file(Kasaria *ksr, SFInsts *rec, InstList *ip)
{
    ulog_debug("Load from file");
	SampleList *sp;
	Instrument *inst;
	i32 i;
	i64 j, frames;

	
	//if(ip->pat.bank == 128)
	//    //ctl->cmsg(CMSG_INFO, VERB_NOISY, "Loading SF Drumset %d %d: %s", ip->pat.preset + progbase, ip->pat.keynote, rec->inst_namebuf[ip->pr_idx]);
	//	ulog_info("Loading SF Drumset %d %d: %s", ip->pat.preset + progbase, ip->pat.keynote, rec->inst_namebuf[ip->pr_idx]);
	//else
	//    //ctl->cmsg(CMSG_INFO, VERB_NOISY, "Loading SF Tonebank %d %d: %s", ip->pat.bank, ip->pat.preset + progbase, rec->inst_namebuf[ip->pr_idx]);
	//	ulog_info("Loading SF Tonebank %d %d: %s", ip->pat.bank, ip->pat.preset + progbase, rec->inst_namebuf[ip->pr_idx]);
	
	
	inst           = (Instrument *)safe_malloc(sizeof(Instrument));
	inst->instname = rec->inst_namebuf[ip->pr_idx];
	inst->type     = INST_SF2;
	inst->samples  = ip->samples;
	inst->sample   = (Sample *)safe_malloc(sizeof(Sample) * ip->samples);
	
	memset(inst->sample, 0, sizeof(Sample) * ip->samples);
	
	for(i = 0, sp = ip->slist; i < ip->samples && sp; i++, sp = sp->next)
	{
		FILE *tf;
		Sample *sample = inst->sample + i;
		i32 j;

		//ctl->cmsg(CMSG_INFO, VERB_DEBUG, "Rate=%d LV=%d HV=%d LK=%d HK=%d RK=%d Tune=%f Pan=%f [%d]", sp->v.sample_rate, sp->v.low_vel, sp->v.high_vel, sp->v.low_key, sp->v.high_key,  sp->v.root_key, sp->v.tune, sp->v.sample_pan, sp->start);
		ulog_debug("Rate=%d LV=%d HV=%d LK=%d HK=%d RK=%d Tune=%f Pan=%f [%d]", sp->v.sample_rate, sp->v.low_vel, sp->v.high_vel, sp->v.low_key, sp->v.high_key,  sp->v.root_key, sp->v.tune, sp->v.sample_pan, sp->start);
		
		memcpy(sample, &sp->v, sizeof(Sample));
		sample->data = NULL;
		sample->data_alloced = 0;
				
		if(i > 0 && (!ksr->pre_resampling_allowed || !sample->note_to_use || (sample->modes & MODES_LOOPING)))
		{
			SampleList *sps;
			Sample *found, *s;

			found = NULL;
			for(j = 0, sps = ip->slist, s = inst->sample; j < i && sps; j++, sps = sps->next, s++)
			{
				if(s->data == NULL)
					break;
				
				if(sp->start == sps->start)
				{
					if(ksr->antialiasing_allowed)
					{
						if(sample->data_length != s->data_length || sample->sample_rate != s->sample_rate)
							continue;
					}
					
					if(ksr->pre_resampling_allowed && s->note_to_use && !(s->modes & MODES_LOOPING))
						continue;
					
					found = s;
					break;
				}
			}
			
			if(found)
			{
				sample->data_type    = found->data_type;
				sample->data         = found->data;
				sample->data_alloced = 0;
				
				// ctl->cmsg(CMSG_INFO, VERB_DEBUG, " * Cached");
				ulog_debug(" * Cached");
				continue;
			}
		}

		
		tf = sp->sfrom ? sfrom_sfrec->tf : rec->tf; ///r


		/*
		if(sample->sample_type & SF_SAMPLETYPE_COMPRESSED)
		{
			FILE *ctf = NULL;
            char *compressed_data = (char *)safe_large_malloc(sp->len);
			fseek(tf, sp->start, SEEK_SET);
            fread(compressed_data, sp->len, 1, tf);

            ctf = open_with_mem(compressed_data, sp->len, OF_VERBOSE);

            
            if(ctf)
            {
                SampleDecodeResult sdr = decode_oggvorbis(ctf);
                close_file(ctf);

                // if(sdr.channels > 1)
                // 	ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "error: sf3 contains multichannel sample");
				

				sample->sample_type |= SF_SAMPLETYPE_MONO;
                sample->data         = sdr.data[0];
                sample->data_alloced = sdr.data_alloced[0];
				sdr.data_alloced[0]  = 0;
                sample->data_type    = sdr.data_type;
				sample->data_length  = sdr.data_length;

				if(!(sample->modes & MODES_LOOPING))
				{
					sample->loop_start = sdr.data_length;
					sample->loop_end   = sdr.data_length + (1 << FRACTION_BITS);
				}

				if(sample->loop_end > sample->data_length + (1 << FRACTION_BITS))
					sample->loop_end = sample->data_length + (1 << FRACTION_BITS);
				
				if(sample->loop_start > sample->data_length)
					sample->loop_start = sample->data_length;
				
				if(sample->loop_start < 0)
					sample->loop_start = 0;

				if(sample->loop_start >= sample->loop_end)
				{
					sample->loop_start = sample->data_length;
					sample->loop_end = sample->data_length + (1 << FRACTION_BITS);
				}

				clear_sample_decode_result(&sdr);
			}
			
            // else
            // {
            // 	ctl->cmsg(CMSG_ERROR, VERB_NORMAL, "unable to read compressed sample; open_with_mem() failed");
            // }

			safe_free(compressed_data);
        }
		else
		{
		*/

#if defined(SF2_24BIT) && (defined(DATA_T_DOUBLE) || defined(DATA_T_FLOAT))
#if 1 /* SF2_24BIT_SAMPLE_TYPE_FLOAT */
		if(sp->lowbit > 0 )
		{
		    /* 24 bit */
		    i64 cnt;
		    u8 *lowbit;
			u16 *highbit;
			f32 *tmp_data;

			frames               = divi_2(sp->len);
		    sample->data         = (sample_t*)safe_large_malloc(sizeof(f32) * (frames + 128));
		    sample->data_alloced = 1;
			sample->data_type    = SAMPLE_TYPE_FLOAT;
			highbit              = (u16 *)safe_large_malloc(sizeof(i16) * frames); // 16bit
			lowbit               = (u8 *)safe_large_malloc(sizeof(i8) * frames); // 8bit	
			
			fseek(tf, sp->start, SEEK_SET);
			fread(highbit, sp->len, 1, tf);
		    fseek(tf, sp->lowbit, SEEK_SET);
		    fread(lowbit, frames, 1, tf);
			
			tmp_data = (f32 *)sample->data;
			
		    for(j = 0; j < frames; j++)
			{
				// 24bit to int32full
			    i32 tmp_i = 0; // 1byte 00でいいらしい？
				tmp_i |= (u32)lowbit[j] << 8; // 2byte
			    tmp_i |= (u32)highbit[j] << 16; // 3-4byte
#ifndef LITTLE_ENDIAN
				XCHG_LONG(tmp_i)
#endif
				tmp_data[j] = (f32)tmp_i * DIV_31BIT;
		    }
		    safe_free(highbit);
		    safe_free(lowbit);
			/* set a small blank loop at the tail for avoiding abnormal loop. */	
			memset(&tmp_data[frames], 0, sizeof(f32) * 128);
			
			if(antialiasing_allowed)
			    antialiasing_float((f32 *)sample->data, sample->data_length >> FRACTION_BITS, sample->sample_rate, play_mode->rate);
		}
		else
#else /* SF2_24BIT_SAMPLE_TYPE_INT32 */
		if(sp->lowbit > 0 )
		{
		    /* 24 bit */
		    i64 cnt;
		    u8 *lowbit;
			u16 *highbit;
			u32 *tmp_data;

			frames = divi_2(sp->len);
			sample->data = (i64*)safe_large_malloc(sizeof(int32) * (frames + 128));
			sample->data_alloced = 1;
			sample->data_type = SAMPLE_TYPE_INT32;

			highbit = (u16 *)safe_large_malloc(sizeof(int16) * frames); // 16bit
			lowbit = (u8 *)safe_large_malloc(sizeof(int8) * frames); // 8bit

				fseek(tf, sp->start, SEEK_SET);
				fread(highbit, sp->len, 1, tf);
				fseek(tf, sp->lowbit, SEEK_SET);
				fread(lowbit, frames, 1, tf);

				tmp_data = (u32 *)sample->data;
				for (j = 0; j < frames; j++) {
					// 24bit to int32full
					u32 tmp_i = 0; // 1byte 00でいいらしい？
					tmp_i |= (u32)lowbit[j] << 8; // 2byte
					tmp_i |= (u32)highbit[j] << 16; // 3-4byte
#ifndef LITTLE_ENDIAN
					XCHG_LONG(tmp_i)
#endif
						tmp_data[j] = tmp_i;
				}
				safe_free(highbit);
				safe_free(lowbit);

				/* set a small blank loop at the tail for avoiding abnormal loop. */
			//	tmp_data[frames] = tmp_data[frames + 1] = tmp_data[frames + 2] = 0;			
				memset(&tmp_data[frames], 0, sizeof(int32) * 128);

				if(antialiasing_allowed)
					antialiasing_int32((i32 *)sample->data, sample->data_length >> FRACTION_BITS, sample->sample_rate, play_mode->rate);

			}
			else
#endif
#endif
			{
				/* 16 bit */
				frames               = divi_2(sp->len);
				sample->data_length  = frames << FRACTION_BITS;
				sample->data         = (sample_t *)safe_large_malloc(sizeof(sample_t) * (frames + 128));
				
				memset(sample->data, 0, sizeof(sample_t) * (frames + 128));
				
				sample->data_alloced = 1;
				sample->data_type    = SAMPLE_TYPE_INT16;

							

				//fseek(tf, sp->start, SEEK_SET);
				fseek(tf, (long)sp->start, SEEK_SET);
  
				//fread(sample->data, sp->len, 1, tf);
				size_t got = fread(sample->data, 1, sp->len, tf);

				long   n;
                short  maxamp = 1, a;
                short *p      = (short *)sample->data;
                for(n = 0; n < frames; n++)
                {
                    a = *p++;
                    if(a < 0) a = -a;
                    if(a > maxamp) maxamp = a;
                }
                sample->volume = 32768.0 / (f64)maxamp * sample->volume;

				//if(dump_wav_counter < 40)   // Could be an useful feature to add in the API
                //    dump_sample_wav(sample);
				

#ifndef LITTLE_ENDIAN
				for(j = 0; j < frames; j++)
					sample->data[j] = (i16)(LE_SHORT(tmp_data[j]));
#endif
				/* set a small blank loop at the tail for avoiding abnormal loop. */
			    //sample->data[frames] = sample->data[frames + 1] = sample->data[frames + 2] = 0;
				//memset(&sample->data[frames], 0, sizeof(sample_t) * 128);

				if(ksr->antialiasing_allowed)
					//antialiasing((i16 *)sample->data, sample->data_length >> FRACTION_BITS, sample->sample_rate, ksr->play_mode.rate);
					antialiasing(sample, ksr->play_mode.rate);
			}
		//}

		/* resample it if possible */
		if(ksr->opt_pre_resamplation && sample->note_to_use && !(sample->modes & MODES_LOOPING))
			pre_resample(ksr, sample);
		
#ifdef LOOKUP_HACK
		squash_sample_16to8(sample);
#endif
	}

	return inst;
}


/*----------------------------------------------------------------
 * excluded samples
 *----------------------------------------------------------------*/

int exclude_soundfont(int bank, int preset, int keynote)
{
	SFExclude *exc;
	if(!current_sfrec)
	    return 1;
	
	exc                      = (SFExclude*)SFMalloc(current_sfrec, sizeof(SFExclude));
	exc->pat.bank            = bank;
	exc->pat.preset          = preset;
	exc->pat.keynote         = keynote;
	exc->next                = current_sfrec->sfexclude;
	current_sfrec->sfexclude = exc;
	
	return 0;
}

/* check the instrument is specified to be excluded */
static int is_excluded(SFInsts *rec, int bank, int preset, int keynote)
{
	SFExclude *p;
	
	for(p = rec->sfexclude; p; p = p->next)
	{
		if(p->pat.bank == bank && (p->pat.preset < 0 || p->pat.preset == preset) && (p->pat.keynote < 0 || p->pat.keynote == keynote))
			return 1;
	}
	
	return 0;
}


/*----------------------------------------------------------------
 * ordered samples
 *----------------------------------------------------------------*/

int order_soundfont(int bank, int preset, int keynote, int order)
{
	SFOrder *p;
	if(!current_sfrec)
	    return 1;
	
	p                      = (SFOrder*)SFMalloc(current_sfrec, sizeof(SFOrder));
	p->pat.bank            = bank;
	p->pat.preset          = preset;
	p->pat.keynote         = keynote;
	p->order               = order;
	p->next                = current_sfrec->sforder;
	current_sfrec->sforder = p;
	
	return 0;
}

/* check the instrument is specified to be ordered */
static int is_ordered(SFInsts *rec, int bank, int preset, int keynote)
{
	SFOrder *p;
	
	for(p = rec->sforder; p; p = p->next)
	{
		if(p->pat.bank == bank && (p->pat.preset < 0 || p->pat.preset == preset) && (p->pat.keynote < 0 || p->pat.keynote == keynote))
			return p->order;
	}
	
	return -1;
}


/*----------------------------------------------------------------*/

int load_font(Kasaria *ksr, SFInfo *sf, int pridx)
{
	SFPresetHdr *preset = &sf->preset[pridx];
	int rc, j, nlayers;
	SFGenLayer *layp, *globalp;

	/* if layer is empty, skip it */
	if((nlayers = preset->hdr.nlayers) <= 0 || (layp = preset->hdr.layer) == NULL)
		return AWE_RET_SKIP;
	
	/* check global layer */
	globalp = NULL;
	if(is_global(layp))
	{
		globalp = layp;
		layp++;
		nlayers--;
	}
	
	/* parse for each preset layer */
	for(j = 0; j < nlayers; j++, layp++)
	{
		LayerTable tbl;

		/* set up table */
		clear_table(&tbl);
		
		if(globalp)
			set_to_table(sf, &tbl, globalp, P_GLOBAL);
		
		set_to_table(sf, &tbl, layp, P_LAYER);

		/* parse the instrument */
		rc = parse_layer(ksr, sf, pridx, &tbl, 0);
		if(rc == AWE_RET_ERR || rc == AWE_RET_NOMEM)
			return rc;
	}

	return AWE_RET_OK;
}


/*----------------------------------------------------------------*/

/* parse a preset layer and convert it to the patch structure */
static int parse_layer(Kasaria *ksr, SFInfo *sf, int pridx, LayerTable *tbl, int level)
{
	SFInstHdr *inst;
	int rc, i, nlayers;
	SFGenLayer *lay, *globalp;
#if 0
	SFPresetHdr *preset = &sf->preset[pridx];
#endif

	if(level >= 2)
	{
	//	fprintf(stderr, "parse_layer: too deep instrument level\n");
		//ctl->cmsg(CMSG_INFO, VERB_DEBUG, "parse_layer: too deep instrument level :%d", pridx);///r c214
		ulog_debug("parse_layer: too deep instrument level :%d", pridx);
		return AWE_RET_ERR;
	}

	/* instrument must be defined */
	if(!tbl->set[SF_instrument])
		return AWE_RET_SKIP;

	inst = &sf->inst[tbl->val[SF_instrument]];

	/* Here, TiMidity makes the reference of the data.  The real data
	 * is loaded after.  So, duplicated data is allowed */
#if 0
	/* if non-standard drumset includes standard drum instruments,
	   skip it to avoid duplicate the data */
	if(def_drum_inst >= 0 && preset->bank == 128 && preset->preset != 0 &&
	    tbl->val[SF_instrument] == def_drum_inst)
			return AWE_RET_SKIP;
#endif

	/* if layer is empty, skip it */
	if((nlayers = inst->hdr.nlayers) <= 0 || (lay = inst->hdr.layer) == NULL)
		return AWE_RET_SKIP;

	reset_last_sample_info();

	/* check global layer */
	globalp = NULL;
	if(is_global(lay))
	{
		globalp = lay;
		lay++;
		nlayers--;
	}

	/* parse for each layer */
	for(i = 0; i < nlayers; i++, lay++)
	{
		LayerTable ctbl;
		clear_table(&ctbl);
		
		if(globalp)
			set_to_table(sf, &ctbl, globalp, P_GLOBAL);
		
		set_to_table(sf, &ctbl, lay, P_LAYER);

		if(!ctbl.set[SF_sampleId])
		{
			/* recursive loading */
			merge_table(sf, &ctbl, tbl);
			
			if(!sanity_range(&ctbl))
				continue;
			
			rc = parse_layer(ksr, sf, pridx, &ctbl, level + 1);
			
			if(rc != AWE_RET_OK && rc != AWE_RET_SKIP)
				return rc;

			reset_last_sample_info();
		}
		else
		{
			init_and_merge_table(sf, &ctbl, tbl);
			
			if(!sanity_range(&ctbl))
				continue;

			/* load the info data */
			if((rc = make_patch(ksr, sf, pridx, &ctbl)) == AWE_RET_ERR)
				return rc;
		}
	}
	return AWE_RET_OK;
}


static int is_global(SFGenLayer *layer)
{
	int i;
	
	for(i = 0; i < layer->nlists; i++)
	{
		if(layer->list[i].oper == SF_instrument || layer->list[i].oper == SF_sampleId)
			return 0;
	}
	
	return 1;
}


/*----------------------------------------------------------------
 * layer table handlers
 *----------------------------------------------------------------*/

/* initialize layer table */
static void clear_table(LayerTable *tbl)
{
	memset(tbl->val, 0, sizeof(tbl->val));
	memset(tbl->set, 0, sizeof(tbl->set));
}

/* set items in a layer to the table */
static void set_to_table(SFInfo *sf, LayerTable *tbl, SFGenLayer *lay, int level)
{
	int i;
	for(i = 0; i < lay->nlists; i++)
	{
		SFGenRec *gen       = &lay->list[i];
		
		/* copy the value regardless of its copy policy */
		tbl->val[gen->oper] = gen->amount;
		tbl->set[gen->oper] = level;
	}
}

/* add an item to the table */
static void add_item_to_table(LayerTable *tbl, int oper, int amount, int level)
{
	LayerItem *item = &layer_items[oper];
	int o_lo, o_hi, lo, hi;

	switch(item->copy)
	{
	case L_INHRT:
		{ ///r
			i32 tmp = tbl->val[oper];
			tmp += amount;
			if(tmp > INT16_MAX)
				tmp = INT16_MAX;
			else if(tmp < INT16_MIN)
				tmp = INT16_MIN;
			tbl->val[oper] = (short)tmp;
		}
	break;
	case L_OVWRT:
		tbl->val[oper] = amount;
	break;
	case L_PRSET:
	case L_INSTR:
		/* do not overwrite */
		if(!tbl->set[oper])
			tbl->val[oper] = amount;
	break;
	case L_RANGE:
		if(!tbl->set[oper])
		{
			tbl->val[oper] = amount;
		}
		else
		{
			o_lo = LOWNUM(tbl->val[oper]);
			o_hi = HIGHNUM(tbl->val[oper]);
			lo   = LOWNUM(amount);
			hi   = HIGHNUM(amount);
			
			if(lo < o_lo)
			    lo = o_lo;
			
			if(hi > o_hi)
			    hi = o_hi;
			
			tbl->val[oper] = RANGE(lo, hi);
		}
		break;
	}
}

/* merge two tables */
static void merge_table(SFInfo *sf, LayerTable *dst, LayerTable *src)
{
	int i;
	for(i = 0; i < SF_EOF; i++)
	{
		if(src->set[i])
		{
			if(sf->version == 1)
			{
				if(!dst->set[i] || i == SF_keyRange || i == SF_velRange)
					/* just copy it */
					dst->val[i] = src->val[i];
			}
			else
				add_item_to_table(dst, i, src->val[i], P_GLOBAL);
			
			dst->set[i] = P_GLOBAL;
		}
	}
}

/* merge and set default values */
static void init_and_merge_table(SFInfo *sf, LayerTable *dst, LayerTable *src)
{
	int i;

	/* default value is not zero */
	if (sf->version == 1)
	{
		layer_items[SF_sustainEnv1].defv = 1000;
		layer_items[SF_sustainEnv2].defv = 1000;
		layer_items[SF_freqLfo1].defv    = -725;
		layer_items[SF_freqLfo2].defv    = -15600;
	}
	else
	{
		layer_items[SF_sustainEnv1].defv = 0;
		layer_items[SF_sustainEnv2].defv = 0;
		layer_items[SF_freqLfo1].defv    = 0;
		layer_items[SF_freqLfo2].defv    = 0;
	}

	/* set default */
	for(i = 0; i < SF_EOF; i++)
	{
		if(!dst->set[i])
			dst->val[i] = layer_items[i].defv;
	}
	
	merge_table(sf, dst, src);
	/* convert from SBK to SF2 */
	if(sf->version == 1)
	{
		for(i = 0; i < SF_EOF; i++)
		{
			if(dst->set[i])
				dst->val[i] = sbk_to_sf2(i, dst->val[i]);
		}
	}
}


/*----------------------------------------------------------------
 * check key and velocity range
 *----------------------------------------------------------------*/

static int sanity_range(LayerTable *tbl)
{
	int lo, hi;

	lo = LOWNUM(tbl->val[SF_keyRange]);
	hi = HIGHNUM(tbl->val[SF_keyRange]);
	
	if(lo < 0 || lo > 127 || hi < 0 || hi > 127 || hi < lo)
		return 0;

	lo = LOWNUM(tbl->val[SF_velRange]);
	hi = HIGHNUM(tbl->val[SF_velRange]);
	
	if(lo < 0 || lo > 127 || hi < 0 || hi > 127 || hi < lo)
		return 0;

	return 1;
}


/*----------------------------------------------------------------
 * create patch record from the stored data table
 *----------------------------------------------------------------*/

static SFSampleInfo *get_sampleinfo(SFInfo *sf, LayerTable *tbl)
{	
    SFSampleInfo *sample;

    sample = &sf->sample[tbl->val[SF_sampleId]];
	if(sfrom_load < 1)
		return sample;
	
	if(!(sample->sampletype & SF_SAMPLETYPE_ROM))
		return sample;
	
	return &(sfrom_sfinfo.sample[tbl->val[SF_sampleId]]);
}

static int make_patch(Kasaria *ksr, SFInfo *sf, int pridx, LayerTable *tbl)
{
    int bank, preset, keynote;
    int keynote_from, keynote_to, done;
    int addr, order;
    InstList *ip;
    SFSampleInfo *sample = &sf->sample[tbl->val[SF_sampleId]];
    SampleList *sp;

    if(sample->sampletype & SF_SAMPLETYPE_ROM && sfrom_load < 1) /* is ROM sample? */
    {
	    // ctl->cmsg(CMSG_INFO, VERB_DEBUG, "preset %d is ROM sample: 0x%x", pridx, sample->sampletype);
		ulog_debug("preset %d is ROM sample: 0x%x", pridx, sample->sampletype);
	    return AWE_RET_SKIP;
    }
    
    bank   = sf->preset[pridx].bank;
    preset = sf->preset[pridx].preset;
    
    if(bank == 128)
    {
		keynote_from = LOWNUM(tbl->val[SF_keyRange]);
		keynote_to   = HIGHNUM(tbl->val[SF_keyRange]);
    }
    else
	    keynote_from = keynote_to = -1;

	done = 0;
	for(keynote = keynote_from; keynote <= keynote_to; keynote++)
	{
	    int pat_keynote = (bank == 128) ? -1 : keynote;

        // ctl->cmsg(CMSG_INFO, VERB_DEBUG_SILLY, "SF make inst pridx=%d bank=%d preset=%d keynote=%d", pridx, bank, preset, keynote);
        ulog_debug("SF make inst pridx=%d bank=%d preset=%d keynote=%d", pridx, bank, preset, keynote);
        if(is_excluded(current_sfrec, bank, preset, keynote))
        {
            // ctl->cmsg(CMSG_INFO, VERB_DEBUG_SILLY, " * Excluded");
            ulog_debug(" * Excluded");
            continue;
        }
        else
            done++;

        order = is_ordered(current_sfrec, bank, preset, keynote);
    
        if(order < 0)
            order = current_sfrec->def_order;
        
        addr = INSTHASH(bank, preset, pat_keynote);
        
        for(ip = current_sfrec->instlist[addr]; ip; ip = ip->next)
            if(ip->pat.bank == bank && ip->pat.preset == preset && (pat_keynote < 0 || pat_keynote == ip->pat.keynote))
                break;
        
        if(!ip)
        {
            ip                            = (InstList*)SFMalloc(current_sfrec, sizeof(InstList));
            memset(ip, 0, sizeof(InstList));
            ip->pr_idx                    = pridx;
            ip->pat.bank                  = bank;
            ip->pat.preset                = preset;
            ip->pat.keynote               = pat_keynote;
            ip->order                     = order;
            ip->samples                   = 0;
            ip->slist                     = NULL;
            ip->next                      = current_sfrec->instlist[addr];
            current_sfrec->instlist[addr] = ip;
        }

        /* new sample */
        sp = (SampleList*)SFMalloc(current_sfrec, sizeof(SampleList));
        memset(sp, 0, sizeof(SampleList));
	
        if(sample->sampletype & SF_SAMPLETYPE_ROM)
            sp->sfrom = 1;

	    sp->bank = bank;
	    sp->keynote = keynote;

	    if(tbl->set[SF_keynum])
			sp->v.note_to_use = (int)tbl->val[SF_keynum];
	    else if(bank == 128)
			sp->v.note_to_use = keynote;
		
        make_info(ksr, sf, sp, tbl);

        /* add a sample */
        if(!ip->slist)
            ip->slist = sp;
        else
        {
            SampleList *cur, *prev;
            i32 start;
            
            /* Insert sample */
            start = sp->start;
            cur = ip->slist;
            prev = NULL;
            while(cur && cur->start <= start)
            {
                prev = cur;
                cur = cur->next;
            }
            if(!prev)
            {
                sp->next = ip->slist;
                ip->slist = sp;
            }
            else
            {
                prev->next = sp;
                sp->next = cur;
            }
        }
    
        ip->samples++;
	} /* for (;;) */


	if(done == 0)
	    return AWE_RET_SKIP;
	else
	    return AWE_RET_OK;
}

/*----------------------------------------------------------------
 *
 * Modified for TiMidity
 */

/* conver to Sample parameter */
static void make_info(Kasaria *ksr, SFInfo *sf, SampleList *vp, LayerTable *tbl)
{
	set_sample_info(ksr, sf, vp, tbl);
	set_init_info(ksr, sf, vp, tbl);
	set_rootkey(sf, vp, tbl);
	set_rootfreq(vp);

	/* tremolo & vibrato */
#ifndef SF_SUPPRESS_TREMOLO
	convert_tremolo(vp, tbl);
#endif /* SF_SUPPRESS_TREMOLO */

#ifndef SF_SUPPRESS_VIBRATO
	convert_vibrato(vp, tbl);
#endif /* SF_SUPPRESS_VIBRATO */


#ifndef CFG_FOR_SF //elion chg
	if(otd.overwriteMode & EOWM_ENABLE_MOD)
	{
		vp->v.modenv_to_fc    = OverrideSample.modenv_to_fc;
		vp->v.modenv_to_pitch = OverrideSample.modenv_to_pitch;
		vp->v.modenv_delay    = OverrideSample.modenv_delay;
	}

	if(otd.overwriteMode & EOWM_ENABLE_TREMOLO)
	{
		vp->v.tremolo_delay    = ksr->play_mode.rate / 1000 * OverrideSample.tremolo_delay;
		vp->v.tremolo_to_amp   = OverrideSample.tremolo_to_amp;
		vp->v.tremolo_to_fc    = OverrideSample.tremolo_to_fc;
		vp->v.tremolo_to_pitch = OverrideSample.tremolo_to_pitch;
	}

	vp->reverb_send = otd.reverb_send;
	vp->chorus_send = otd.chorus_send;

	if(otd.overwriteMode & EOWM_ENABLE_CUTOFF)
	{
		vp->cutoff_freq = vp->v.cutoff_freq = OverrideSample.cutoff_freq;
		vp->resonance   = vp->v.resonance   = OverrideSample.resonance;
	}

	if(otd.overwriteMode & EOWM_ENABLE_ENV)
		vp->v.envelope_delay = OverrideSample.envelope_delay;
	

	if(otd.overwriteMode & EOWM_ENABLE_VIBRATO)
	{
		vp->v.vibrato_delay    = OverrideSample.vibrato_delay;
		vp->v.vibrato_to_pitch = OverrideSample.vibrato_to_pitch;
		vp->v.vibrato_sweep    = OverrideSample.vibrato_sweep;
	}

	if(otd.overwriteMode & EOWM_ENABLE_VEL)
	{
		vp->v.vel_to_fc           = OverrideSample.vel_to_fc;
		vp->v.vel_to_fc_threshold = OverrideSample.vel_to_fc_threshold;
		vp->v.vel_to_resonance    = OverrideSample.vel_to_resonance;
	}
#endif
}

static void set_envelope_parameters(SampleList *vp)
{
	/* convert envelope parameters */
	vp->v.envelope_offset[0] = to_offset(65535);
	vp->v.envelope_rate[0]   = vp->attack;

	vp->v.envelope_offset[1] = to_offset(65534);
	vp->v.envelope_rate[1]   = vp->hold;

	if(vp->sustain > 65533)
		vp->sustain = 65533;
		
	vp->v.envelope_offset[2] = to_offset(vp->sustain);
	vp->v.envelope_rate[2]   = vp->decay;

	vp->v.envelope_offset[3] = 0;
	vp->v.envelope_rate[3]   = vp->release;

	vp->v.envelope_offset[4] = 0;
	vp->v.envelope_rate[4]   = vp->release;

	vp->v.envelope_offset[5] = 0;
	vp->v.envelope_rate[5]   = vp->release;

	/* convert modulation envelope parameters */
	vp->v.modenv_offset[0] = to_offset(65535);
	vp->v.modenv_rate[0]   = vp->modattack;

	vp->v.modenv_offset[1] = to_offset(65534);
	vp->v.modenv_rate[1]   = vp->modhold;

	if(vp->modsustain > 65533)
		vp->modsustain = 65533;
		
	if(vp->modsustain > 65533)
		vp->modsustain = 65533;
		
	vp->v.modenv_offset[2] = to_offset(vp->modsustain);
	vp->v.modenv_rate[2]   = vp->moddecay;

	vp->v.modenv_offset[3] = 0;
	vp->v.modenv_rate[3]   = vp->modrelease;

	vp->v.modenv_offset[4] = 0;
	vp->v.modenv_rate[4]   = vp->modrelease;

	vp->v.modenv_offset[5] = 0;
	vp->v.modenv_rate[5]   = vp->modrelease;
}

/* set sample address */

static void set_sample_info(Kasaria *ksr, SFInfo *sf, SampleList *vp, LayerTable *tbl)
{
    SFSampleInfo *sp = &sf->sample[tbl->val[SF_sampleId]];
	int is_rom = 0;

	if((sp->sampletype & SF_SAMPLETYPE_ROM) && sfrom_load >= 1)
	{
		sp = &(sfrom_sfinfo.sample[tbl->val[SF_sampleId]]);
		is_rom = 1;
	}
	
    /* set sample position */
    vp->start = sp->startsample;
	if(sf_config_addrs_offset)
		vp->start += (tbl->val[SF_startAddrsHi] << 15) + tbl->val[SF_startAddrs];
	
    vp->len  = sp->endsample - vp->start;
	vp->len += (tbl->val[SF_endAddrsHi] << 15)	+ tbl->val[SF_endAddrs];

	vp->start = llabs(vp->start);
	vp->len   = llabs(vp->len);

	vp->v.offset = 0;

	vp->v.offset = 0;

    /* set loop position */
	vp->v.loop_start = sp->startloop;
	vp->v.loop_end   = sp->endloop;

	if(!(sp->sampletype & SF_SAMPLETYPE_COMPRESSED))
	{
		vp->v.loop_start -= vp->start;
		vp->v.loop_end -= vp->start;
	}

	vp->v.loop_start += (tbl->val[SF_startloopAddrsHi] << 15) + tbl->val[SF_startloopAddrs];
	vp->v.loop_end   += (tbl->val[SF_endloopAddrsHi] << 15)   + tbl->val[SF_endloopAddrs];

    /* set data length */
    vp->v.data_length = vp->len + 1;

	if(!(sp->sampletype & SF_SAMPLETYPE_COMPRESSED))
	{
		/* fix loop position */
		if(vp->v.loop_end > vp->len + 1)
			vp->v.loop_end = vp->len + 1;
		
		if(vp->v.loop_start > vp->len)
			vp->v.loop_start = vp->len;
		
		if(vp->v.loop_start < 0)
			vp->v.loop_start = 0;
		
		if(vp->v.loop_start >= vp->v.loop_end)
		{
			vp->v.loop_start = vp->len;
			vp->v.loop_end = vp->len + 1;
		}
	}

    /* Sample rate */
	if(sp->samplerate > SF_SAMPLERATE_MAX)
	    sp->samplerate = SF_SAMPLERATE_MAX;
	else if(sp->samplerate < SF_SAMPLERATE_MIN)
	    sp->samplerate = SF_SAMPLERATE_MIN;

	
    vp->v.sample_rate = sp->samplerate;

    /* sample mode */
//    vp->v.modes = vp->lowbit > 0 ? MODES_24BIT : MODES_16BIT;
    vp->v.modes = MODES_16BIT;

    /* volume envelope & total volume */
    vp->v.volume  = calc_volume(tbl) * current_sfrec->amptune;
	vp->v.cfg_amp = 1.0;

#ifndef SF_SUPPRESS_ENVELOPE
	convert_volume_envelope(ksr, vp, tbl);
#endif /* SF_SUPPRESS_ENVELOPE */
	set_envelope_parameters(vp);

	switch(tbl->val[SF_sampleFlags])
	{
	default:
	case 0: /* no looping */
	case 2: /* unused , no looping */
		/* set a small blank loop at the tail for avoiding abnormal loop. */
		vp->v.loop_start = vp->len;
		vp->v.loop_end = vp->len + 1;
		break;
	case 1: /* looping */
		vp->v.modes |= MODES_LOOPING | MODES_SUSTAIN;
		vp->v.data_length = vp->v.loop_end; /* strip the tail */
		break;
	case 3: /* looping , release sample */
		vp->v.modes |= MODES_LOOPING | MODES_SUSTAIN | MODES_RELEASE;
		break;	
	}

    /* convert to fractional samples */
    vp->v.offset      <<= FRACTION_BITS;
    vp->v.data_length <<= FRACTION_BITS;
    vp->v.loop_start  <<= FRACTION_BITS;
    vp->v.loop_end    <<= FRACTION_BITS;

    /* point to the file position */
	if(sp->sampletype & SF_SAMPLETYPE_COMPRESSED)
	{
		vp->start += (is_rom ? sfrom_sfinfo.samplepos : sf->samplepos); ///r
		vp->lowbit = is_rom ? sfrom_sfinfo.lowbitpos  : sf->lowbitpos; ///r
	}
	else
	{
		vp->start  = vp->start * 2 + (is_rom ? sfrom_sfinfo.samplepos : sf->samplepos); ///r
		vp->lowbit = is_rom ? sfrom_sfinfo.lowbitpos : sf->lowbitpos; ///r
		vp->len   *= 2;
	}

	vp->v.vel_to_fc           = -2400; /* SF2 default value */
	vp->v.vel_to_fc_threshold = 0; ///r c214  def64
	vp->v.key_to_fc           = vp->v.vel_to_resonance = 0;
	vp->v.envelope_velf_bpo   = vp->v.modenv_velf_bpo = 64;	
	vp->v.envelope_keyf_bpo   = vp->v.modenv_keyf_bpo = 60;
	vp->v.key_to_fc_bpo       = 60;
	
	memset(vp->v.envelope_velf, 0, sizeof(vp->v.envelope_velf));
	memset(vp->v.modenv_velf, 0, sizeof(vp->v.modenv_velf));
	
	vp->v.modenv_to_pitch = (tbl->set[SF_env1ToPitch]) ? tbl->val[SF_env1ToPitch] : 0;
	
	if(vp->v.modenv_to_pitch > sf_limit_modenv_pitch)
		vp->v.modenv_to_pitch = sf_limit_modenv_pitch;
	else if(vp->v.modenv_to_pitch < -sf_limit_modenv_pitch)
		vp->v.modenv_to_pitch = -sf_limit_modenv_pitch;
	
	vp->v.modenv_to_fc = (tbl->set[SF_env1ToFilterFc]) ? tbl->val[SF_env1ToFilterFc] : 0;
	
	if(vp->v.modenv_to_fc > sf_limit_modenv_fc)
		vp->v.modenv_to_fc = sf_limit_modenv_fc;
	else if(vp->v.modenv_to_fc < -sf_limit_modenv_fc)
		vp->v.modenv_to_fc = -sf_limit_modenv_fc;
///r	
	vp->v.cutoff_low_limit  = -1; 
	vp->v.cutoff_low_keyf   = 0; // cent
	vp->v.inst_type         = INST_SF2;
	vp->v.lpf_type          = -1;	
	vp->v.keep_voice        = 0;
	vp->v.hpf[0]            = -1; // opt_hpf_def
	vp->v.hpf[1]            = 10;
	vp->v.hpf[2]            = 0;
	vp->v.def_pan           = 64;
	vp->v.vibrato_to_amp    = vp->v.vibrato_to_fc = 0;
	vp->v.pitch_envelope[0] = 0; // 0cent init
	vp->v.pitch_envelope[1] = 0; // 0cent atk
	vp->v.pitch_envelope[2] = 0; // 125ms atk
	vp->v.pitch_envelope[3] = 0; // 0cent dcy1
	vp->v.pitch_envelope[4] = 0; // 125ms dcy1
	vp->v.pitch_envelope[5] = 0; // 0cent dcy2
	vp->v.pitch_envelope[6] = 0; // 125ms dcy3
	vp->v.pitch_envelope[7] = 0; // 0cent rls
	vp->v.pitch_envelope[8] = 0; // 125ms rls
	vp->v.offset            = 0;
	vp->v.seq_length        = 0;
	vp->v.seq_position      = 0;
	// vp->v.lorand            = -1;
	// vp->v.hirand            = -1;
	vp->v.rt_decay          = 0;
}

/*----------------------------------------------------------------*/

/* set global information */
static int last_sample_type;
static int last_sample_instrument;
static int last_sample_keyrange;
static SampleList *last_sample_list;

///r
static void set_init_info(Kasaria *ksr, SFInfo *sf, SampleList *vp, LayerTable *tbl)
{
    int val;
    SFSampleInfo *sample = &sf->sample[tbl->val[SF_sampleId]];

    /* key range */
    if(tbl->set[SF_keyRange])
    {
	    vp->low = LOWNUM(tbl->val[SF_keyRange]);
	    vp->high = HIGHNUM(tbl->val[SF_keyRange]);
    }
    else
    {
	    vp->low = 0;
	    vp->high = 127;
    }
    
    vp->v.low_key = vp->low;
    vp->v.high_key = vp->high;
    
    /* velocity range */
    if(tbl->set[SF_velRange])
    {
		vp->v.low_vel = LOWNUM(tbl->val[SF_velRange]);
		vp->v.high_vel = HIGHNUM(tbl->val[SF_velRange]);
    }
    else
    {
		vp->v.low_vel = 0;
		vp->v.high_vel = 127;
	}

    /* fixed key & velocity */
    if(tbl->set[SF_keynum])
		vp->v.note_to_use = (int)tbl->val[SF_keynum];
    
	if(tbl->set[SF_velocity] && (int)tbl->val[SF_velocity] != 0)
	{
		//ctl->cmsg(CMSG_INFO, VERB_DEBUG, "error: fixed-velocity is not supported.");
		ulog_debug("error: fixed-velocity is not supported.");
	}
	
	vp->v.sample_type     = sample->sampletype;
	vp->v.sf_sample_index = tbl->val[SF_sampleId];
	vp->v.sf_sample_link  = sample->samplelink;

	if((sample->sampletype & SF_SAMPLETYPE_ROM) && sfrom_load > 0)
	{
		if(sfrom_sfinfo.nsamples < vp->v.sf_sample_link)
			vp->v.sf_sample_link = 0;
	}
	else if(sf->nsamples < vp->v.sf_sample_link)
		vp->v.sf_sample_link = 0;
	

	/* Some sf2 files don't contain valid sample links, so see if the
	   previous sample was a matching Left / Right sample with the
	   link missing and add it */
	if(sample->sampletype & SF_SAMPLETYPE_LEFT)
	{
		if(vp->v.sf_sample_link == 0 && (last_sample_type & SF_SAMPLETYPE_RIGHT) && last_sample_instrument == tbl->val[SF_instrument] && last_sample_keyrange == tbl->val[SF_keyRange])
		{
		    	/* The previous sample was a matching right sample
		    	   set the link */
		    	vp->v.sf_sample_link = last_sample_list->v.sf_sample_index;
		}
	}
	else if(sample->sampletype & SF_SAMPLETYPE_RIGHT)
	{
		if (last_sample_list &&
		    last_sample_list->v.sf_sample_link == 0 &&
		    (last_sample_type & SF_SAMPLETYPE_LEFT) &&
		    last_sample_instrument == tbl->val[SF_instrument] &&
		    last_sample_keyrange == tbl->val[SF_keyRange])
		{
		    /* The previous sample was a matching left sample
		       set the link on the previous sample*/
		    last_sample_list->v.sf_sample_link = tbl->val[SF_sampleId];
		}
	}
	if(sample->sampletype & SF_SAMPLETYPE_LEFT)
	{

		if(vp->v.sf_sample_link == 0 &&
		    (last_sample_type & SF_SAMPLETYPE_RIGHT) &&
		    last_sample_instrument == tbl->val[SF_instrument] &&
		    last_sample_keyrange == tbl->val[SF_keyRange])
		{
		    /* The previous sample was a matching right sample
		       set the link */
		    vp->v.sf_sample_link = last_sample_list->v.sf_sample_index;
		}
	}
	else if(sample->sampletype & SF_SAMPLETYPE_RIGHT)
	{
		if(last_sample_list &&
		    last_sample_list->v.sf_sample_link == 0 &&
		    (last_sample_type & SF_SAMPLETYPE_LEFT) &&
		    last_sample_instrument == tbl->val[SF_instrument] &&
		    last_sample_keyrange == tbl->val[SF_keyRange])
		{
		    /* The previous sample was a matching left sample
		       set the link on the previous sample*/
		    last_sample_list->v.sf_sample_link = tbl->val[SF_sampleId];
		}
	}

	/* Remember this sample in case the next one is a match */
	last_sample_type       = sample->sampletype;
	last_sample_instrument = tbl->val[SF_instrument];
	last_sample_keyrange   = tbl->val[SF_keyRange];
	last_sample_list       = vp;
	
	/* panning position: -0.5 to 0.5 */
	vp->v.sample_pan = 0.0;
	val = 0;	
	if(tbl->set[SF_panEffectsSend])
	{
		val = (int)tbl->val[SF_panEffectsSend];// elion add
		if(val < -500)
			val = -500;
		else if(val > 500)
			val = 500;
	}
	
    if(sample->sampletype & SF_SAMPLETYPE_MONO)
    {
        /* monoSample = 1 */
		if(val != 0)
			vp->v.sample_pan = (f64)val * DIV_1000;
	}
	else if(sample->sampletype & SF_SAMPLETYPE_RIGHT)
	{
	    /* rightSample = 2 */
		val += 500;
		if(val > 500)
			val = 500;
		vp->v.sample_pan = (f64)val * DIV_1000;	
	}
	else if(sample->sampletype & SF_SAMPLETYPE_LEFT)
	{
	    /* leftSample = 4 */
		val -= 500;
		if(val < -500)
			val = -500;
		vp->v.sample_pan = (f64)val * DIV_1000;
	}
	else if(sample->sampletype & SF_SAMPLETYPE_LINKED)
	{	
	    /* linkedSample = 8 */
		if(val != 0)
			vp->v.sample_pan = (f64)val * DIV_1000;
		
		// ctl->cmsg(CMSG_ERROR, VERB_NOISY, "error: linkedSample is not supported.");
		ulog_debug("error: linkedSample is not supported.");
	}

	memset(vp->v.envelope_keyf, 0, sizeof(vp->v.envelope_keyf));
	memset(vp->v.modenv_keyf, 0, sizeof(vp->v.modenv_keyf));
	if(tbl->set[SF_autoHoldEnv2])
		vp->v.envelope_keyf[1] = (i16)tbl->val[SF_autoHoldEnv2];
	
	if(tbl->set[SF_autoDecayEnv2])
		vp->v.envelope_keyf[2] = (i16)tbl->val[SF_autoDecayEnv2];
	
	if(tbl->set[SF_autoHoldEnv1])
		vp->v.modenv_keyf[1] = (i16)tbl->val[SF_autoHoldEnv1];
	
	if(tbl->set[SF_autoDecayEnv1])
		vp->v.modenv_keyf[2] = (i16)tbl->val[SF_autoDecayEnv1];
	

#ifndef CFG_FOR_SF
	current_sfrec->def_cutoff_allowed    = 1;
	current_sfrec->def_resonance_allowed = 1;
#endif

    /* initial cutoff & resonance */
    vp->cutoff_freq = 20005;

    if((int)tbl->val[SF_initialFilterFc] < 0)tbl->set[SF_initialFilterFc] = tbl->val[SF_initialFilterFc] = 0;
		if(tbl->val[SF_initialFilterFc] < 1500)
		   tbl->val[SF_initialFilterFc] = 1500;
		
   if(tbl->val[SF_initialFilterFc] > 13500)
       tbl->val[SF_initialFilterFc] = 13500;

    if(current_sfrec->def_cutoff_allowed && tbl->set[SF_initialFilterFc] && (int)tbl->val[SF_initialFilterFc] >= 1500 && (int)tbl->val[SF_initialFilterFc] <= 13500)
    {
        val = (int)tbl->val[SF_initialFilterFc];
        val = abscent_to_Hz(val);

#ifndef CFG_FOR_SF
	    if(!ksr->opt_modulation_envelope)
		{
		    if(tbl->set[SF_env1ToFilterFc] && (int)tbl->val[SF_env1ToFilterFc] > 0)
			{
			    val *= pow(2.0, (f64)tbl->val[SF_env1ToFilterFc] * DIV_1200);
			    if(val > 20000)
				    val = 20000;
			}
	    }
#endif

	vp->cutoff_freq = val;
	}
	else
	{
		//elion add start
		val = abscent_to_Hz(13500);
		vp->cutoff_freq = val;
		//elion add end
	}
    
	vp->v.cutoff_freq = vp->cutoff_freq;
	vp->resonance = 0;
	
    if(current_sfrec->def_resonance_allowed && tbl->set[SF_initialFilterQ])
    {
	    val = (int)tbl->val[SF_initialFilterQ];
	    vp->resonance = val;
	}
    
	vp->v.resonance = vp->resonance;

#if 0 /* Not supported */
    /* exclusive class key */
    vp->exclusiveClass = tbl->val[SF_keyExclusiveClass];
#endif
}

static void reset_last_sample_info(void)
{
    last_sample_list = NULL;
    last_sample_type = 0;
    /* Set last instrument and keyrange to a value which cannot be represented
       by LayerTable.val (which is a short) */
    last_sample_instrument = 0x80000000;
    last_sample_keyrange   = 0x80000000;
}

static int abscent_to_Hz(int abscents)
{
	return (int)(8.176 * pow(2.0, (f64)abscents * DIV_1200));
}

/*----------------------------------------------------------------*/

#define SF_MODENV_CENT_MAX 1200	/* Live! allows only +-1200cents. */

/* calculate root key & fine tune */
static void set_rootkey(SFInfo *sf, SampleList *vp, LayerTable *tbl)
{
	SFSampleInfo *sp = &sf->sample[tbl->val[SF_sampleId]];
	int temp;
	int is_rom = 0;
	
	/* scale factor */
	vp->v.scale_factor = 1024 * (f64) tbl->val[SF_scaleTuning] / 100 + 0.5;
	/* set initial root key & fine tune */
	if(sf->version == 1 && tbl->set[SF_samplePitch])
	{
		/* set from sample pitch */
		vp->root = tbl->val[SF_samplePitch] / 100;
		vp->tune = -tbl->val[SF_samplePitch] % 100;
		if(vp->tune <= -50)
			vp->root++, vp->tune += 100;
	}
	else
	{
		/* from sample info */
		vp->root = sp->originalPitch;
		vp->tune = (i8) sp->pitchCorrection;
	}
	/* orverride root key */
	if(tbl->set[SF_rootKey])
		vp->root = tbl->val[SF_rootKey];
	else if(vp->bank == 128 && vp->v.scale_factor != 0)
		vp->tune += (vp->keynote - sp->originalPitch) * 100 * (f64) vp->v.scale_factor * DIV_1024;

//	vp->root += (int32)tbl->val[SF_coarseTune];
//	vp->tune += (int32)tbl->val[SF_fineTune];
	if(tbl->set[SF_coarseTune])
		vp->tune += (i32)tbl->val[SF_coarseTune] * 100;
	
	if(tbl->set[SF_fineTune])
		vp->tune += (i32)tbl->val[SF_fineTune];

	/* correct too high pitch */
	if(vp->root >= (i32)(vp->high) + 60)
		vp->root -= 60;
	
}

static void set_rootfreq(SampleList *vp)
{	
	vp->v.scale_freq = vp->root;
	vp->v.root_key = vp->root;
	vp->v.tune = pow(2.0, (f64)vp->tune * DIV_1200);
#if 1 // use tune
	vp->v.root_freq = freq_table[vp->root];
#else // not use tune , include root_freq
	vp->v.root_freq = (f64)freq_table[vp->root] / vp->v.tune + 0.5;
#endif

#if 0
	int root = vp->root;
	int tune = 0.5 - 256.0 * (f64)vp->tune * DIV_100;

	/* 0 <= tune < 255 */
	while(tune < 0)
		root--, tune += 256;
	
	while(tune > 255)
		root++, tune -= 256;
	
	if(root < 0)
	{
		vp->v.root_freq = (f64)freq_table[0] * (f64) bend_fine[tune] / bend_coarse[-root] + 0.5;
		vp->v.scale_freq = 0;		/* scale freq */
	}
	else if(root > 127)
	{
		vp->v.root_freq = (f64)freq_table[127] * (f64) bend_fine[tune] * bend_coarse[root - 127] + 0.5;
		vp->v.scale_freq = 127;		/* scale freq */
	}
	else
	{
		vp->v.root_freq = (f64)freq_table[root] * (f64) bend_fine[tune] + 0.5;
		vp->v.scale_freq = root;	/* scale freq */
	}
/*
	if (root || root < 0 || root > 127) {
//		vp->v.root_freq = (double)freq_table[s] * (double) bend_fine[tune] + 0.5;
//		vp->v.root_freq = (440.0 * pow(2.0, ((double)(root - 69) * DIV_12)) * 1000.0) * bend_fine[tune] + 0.5;
		vp->v.root_freq = freq_table[vp->root] * pow(2.0, -(double)vp->tune * DIV_100 * DIV_12) + 0.5;
		if (root < 0) {
			vp->v.scale_freq = 0;
//			vp->v.root_freq = (double)vp->v.root_freq * pow(2.0, -(double)root * DIV_100 * DIV_12) + 0.5;
		}
		else if (root > 127) {
			vp->v.scale_freq = 127;
//			vp->v.root_freq = (double)vp->v.root_freq * pow(2.0, -(double)(root - 127) * DIV_100 * DIV_12) + 0.5;
		}
		else vp->v.scale_freq = root;
	}
*/
#endif
}

/*----------------------------------------------------------------*/


/*Pseudo Reverb*/
i32 modify_release;

i32 get_sf_release(Kasaria *ksr, i32 v)
{
	i32 m = calc_rate(ksr, 65535, (f64)modify_release),
		s = to_rate(ksr, 65535, v);

	if(modify_release == 0)
		return s;
	
	if(calc_rate(ksr, 65535, 800.0) > s)
		return s;

//	if (m > s)
//		return s;
	return m;
}

static i32 abscent_to_mHz(int abscents)
{
	return ((f64)8176 * pow(2.0, (f64)abscents * DIV_1200) + 0.5);
}

/* volume envelope parameters */
static void convert_volume_envelope(Kasaria *ksr, SampleList *vp, LayerTable *tbl)
{
	f64 tmp;

	// convert amp envelope
	tmp = to_msec(tbl->set[SF_attackEnv2] ? tbl->val[SF_attackEnv2] : -12000);
	
	if(tmp < sf_limit_volenv_attack)
		tmp = sf_limit_volenv_attack;
	
	vp->attack           = calc_rate(ksr, 65535, tmp);
    vp->hold             = to_rate(ksr, 1, tbl->set[SF_holdEnv2] ? tbl->val[SF_holdEnv2] : -12000);
    vp->sustain          = calc_volenv_sustain(tbl->set[SF_sustainEnv2] ? tbl->val[SF_sustainEnv2] : 0);
    vp->decay            = to_rate(ksr, 65534 - vp->sustain, tbl->set[SF_decayEnv2] ? tbl->val[SF_decayEnv2] : -12000);
    //vp->release          = get_sf_release(ksr, tbl->set[SF_releaseEnv2] ? tbl->val[SF_releaseEnv2] : -12000); // This fucks up the performance
    vp->v.envelope_delay = (ksr->play_mode.rate * to_msec((f64)(tbl->set[SF_delayEnv2] ? tbl->val[SF_delayEnv2] : -12000)) * 0.001);


    vp->release = to_rate(ksr, 65535, -12000); // Temporarily because I'm too stupid to figure everything out
	// convert modulation envelope
	tmp = to_msec(tbl->set[SF_attackEnv1] ? tbl->val[SF_attackEnv1] : -12000);
	
	if(tmp < sf_limit_modenv_attack)
		tmp = sf_limit_modenv_attack;
	
	vp->modattack      = calc_rate(ksr, 65535, tmp);
    vp->modhold        = to_rate(ksr, 1, tbl->set[SF_holdEnv1] ? tbl->val[SF_holdEnv1] : -12000);
    vp->modsustain     = calc_modenv_sustain(tbl->set[SF_sustainEnv1] ? tbl->val[SF_sustainEnv1] : 0);
    vp->moddecay       = to_rate(ksr, 65534 - vp->modsustain, tbl->set[SF_decayEnv1] ? tbl->val[SF_decayEnv1] : -12000);
    vp->modrelease     = get_sf_release(ksr, tbl->set[SF_releaseEnv1] ? tbl->val[SF_releaseEnv1] : -12000); // elion chg
    vp->v.modenv_delay = (ksr->play_mode.rate * to_msec((f64)(tbl->set[SF_delayEnv1] ? tbl->val[SF_delayEnv1] : -12000)) * 0.001);

    vp->v.modes |= MODES_ENVELOPE;
}


#ifndef SF_SUPPRESS_TREMOLO
/*----------------------------------------------------------------
 * ModLFO (LFO1) conversion
 *----------------------------------------------------------------*/
///r
static void convert_tremolo(SampleList *vp, LayerTable *tbl)
{
    i32 freq;
	f64 level;
	
	if(sf_config_lfo_swap)
	{		
		vp->v.vibrato_to_pitch = !tbl->set[SF_lfo1ToPitch] ? 0 : tbl->val[SF_lfo1ToPitch]; // cent
		
		if(vp->v.vibrato_to_pitch > sf_limit_viblfo_pitch)
			vp->v.vibrato_to_pitch = sf_limit_viblfo_pitch;
		else if(vp->v.vibrato_to_pitch < -sf_limit_viblfo_pitch)
			vp->v.vibrato_to_pitch = -sf_limit_viblfo_pitch;
		
		vp->v.vibrato_to_fc = (tbl->set[SF_lfo1ToFilterFc]) ? tbl->val[SF_lfo1ToFilterFc] : 0; // cent
		
		if(vp->v.vibrato_to_fc > sf_limit_modlfo_fc)
			vp->v.vibrato_to_fc = sf_limit_modlfo_fc;
		else if(vp->v.vibrato_to_fc < -sf_limit_modlfo_fc)
			vp->v.vibrato_to_fc = -sf_limit_modlfo_fc;
		
		level = !tbl->set[SF_lfo1ToVolume] ? 1.0 : pow(10.0, (f64)abs(tbl->val[SF_lfo1ToVolume]) * -DIV_200);
		vp->v.vibrato_to_amp = 10000 * (1.0 - level); // 0.01%
		
		if((int)tbl->val[SF_lfo1ToVolume] < 0)
		    vp->v.tremolo_to_amp = -vp->v.tremolo_to_amp;
		
		freq = !tbl->set[SF_freqLfo1] ? sf_default_modlfo_freq : abscent_to_mHz((int)tbl->val[SF_freqLfo1]);
		
		if(freq > sf_limit_modlfo_freq)
			freq = sf_limit_modlfo_freq;
		
		if(freq < 1)
			freq = 1;
		
		vp->v.vibrato_freq  = freq; // mHz
		vp->v.vibrato_delay = to_msec(tbl->val[SF_delayLfo1]); // ms
		vp->v.vibrato_sweep = 0; // ms
	}
	else
	{		
		vp->v.tremolo_to_pitch = (tbl->set[SF_lfo1ToPitch]) ? tbl->val[SF_lfo1ToPitch] : 0; // cent
		
		if(vp->v.tremolo_to_pitch > sf_limit_modlfo_pitch)
			vp->v.tremolo_to_pitch = sf_limit_modlfo_pitch;
		else if(vp->v.tremolo_to_pitch < -sf_limit_modlfo_pitch)
			vp->v.tremolo_to_pitch = -sf_limit_modlfo_pitch;
		
		vp->v.tremolo_to_fc = (tbl->set[SF_lfo1ToFilterFc]) ? tbl->val[SF_lfo1ToFilterFc] : 0; // cent
		
		if(vp->v.tremolo_to_fc > sf_limit_modlfo_fc)
			vp->v.tremolo_to_fc = sf_limit_modlfo_fc;
		else if(vp->v.tremolo_to_fc < -sf_limit_modlfo_fc)
			vp->v.tremolo_to_fc = -sf_limit_modlfo_fc;
		
		level = !tbl->set[SF_lfo1ToVolume] ? 1.0 : pow(10.0, (double)abs(tbl->val[SF_lfo1ToVolume]) * -DIV_200);
		vp->v.tremolo_to_amp = 10000 * (1.0 - level); // 0.01%
		
		if((int)tbl->val[SF_lfo1ToVolume] < 0)
		    vp->v.tremolo_to_amp = -vp->v.tremolo_to_amp;
		
		freq = !tbl->set[SF_freqLfo1] ? sf_default_modlfo_freq : abscent_to_mHz((int)tbl->val[SF_freqLfo1]);
		if(freq > sf_limit_modlfo_freq)
			freq = sf_limit_modlfo_freq;
		
		if(freq < 1)
		    freq = 1;
		
		vp->v.tremolo_freq  = freq + 0.5; // mHz
		vp->v.tremolo_delay = to_msec(tbl->val[SF_delayLfo1]); // ms
		vp->v.tremolo_sweep = 0; // ms
	}
}
#endif

#ifndef SF_SUPPRESS_VIBRATO
/*----------------------------------------------------------------
 * VibLFO (LFO2) conversion
 *----------------------------------------------------------------*/
///r
static void convert_vibrato(SampleList *vp, LayerTable *tbl)
{
    i32 freq;
	
	if(sf_config_lfo_swap)
	{
		vp->v.tremolo_to_pitch = (tbl->set[SF_lfo2ToPitch]) ? tbl->val[SF_lfo2ToPitch] : 0; // cent
		
		if(vp->v.tremolo_to_pitch > sf_limit_modlfo_pitch)
			vp->v.tremolo_to_pitch = sf_limit_modlfo_pitch;
		else if(vp->v.tremolo_to_pitch < -sf_limit_modlfo_pitch)
			vp->v.tremolo_to_pitch = -sf_limit_modlfo_pitch;
		
		vp->v.tremolo_to_fc  = 0; // cent
		vp->v.tremolo_to_amp = 0; // 0.01%
		freq = !tbl->set[SF_freqLfo2] ? sf_default_viblfo_freq : abscent_to_mHz((int)tbl->val[SF_freqLfo2]);
		
		if(freq > sf_limit_viblfo_freq)
			freq = sf_limit_viblfo_freq;
		
		if(freq < 1)
			freq = 1;
		
		vp->v.tremolo_freq  = freq; // mHz
		vp->v.tremolo_delay = to_msec(tbl->val[SF_delayLfo2]); // ms
		vp->v.tremolo_sweep = 0; // ms
	}
	else
	{
		vp->v.vibrato_to_pitch = !tbl->set[SF_lfo2ToPitch] ? 0 : tbl->val[SF_lfo2ToPitch]; // cent
		
		if(vp->v.vibrato_to_pitch > sf_limit_viblfo_pitch)
			vp->v.vibrato_to_pitch = sf_limit_viblfo_pitch;
		else if(vp->v.vibrato_to_pitch < -sf_limit_viblfo_pitch)
			vp->v.vibrato_to_pitch = -sf_limit_viblfo_pitch;
		
		vp->v.vibrato_to_fc  = 0; // cent
		vp->v.vibrato_to_amp = 0; // 0.01%
		freq = !tbl->set[SF_freqLfo2] ? sf_default_viblfo_freq : abscent_to_mHz((int)tbl->val[SF_freqLfo2]);
		
		if(freq > sf_limit_viblfo_freq)
			freq = sf_limit_viblfo_freq;
		
		if(freq < 1)
		    freq = 1;
		
		vp->v.vibrato_freq  = freq; // mHz
		vp->v.vibrato_delay = to_msec(tbl->val[SF_delayLfo2]); // ms
		vp->v.vibrato_sweep = 0; // ms
	}
}
#endif