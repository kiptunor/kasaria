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


   internal.h
*/














#ifndef TIMID_INTERNAL_H
#define TIMID_INTERNAL_H


#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "config.h"
#include "kasaria.h"
#include "ksr_sf2.h"


















#ifndef PI
    #define PI 3.14159265
#endif

#ifdef USE_LDEXP
    #define FSCALE(a, b)    ldexp((double)(a), (b))
    #define FSCALENEG(a, b) ldexp((double)(a), -(b))
#else
    #define FSCALE(a, b)    ((a) * (double)(1 << (b)))
    #define FSCALENEG(a, b) ((a) * (1.0L / (double)(1 << (b))))
#endif

#define ISDRUMCHANNEL(tm, c)  ((tm->drumchannels & (1 << (c))))
#define ISQUIETCHANNEL(tm, c) ((tm->quietchannels & (1 << (c))))
#define MAGIC_LOAD_INSTRUMENT ((Instrument *)(-1))

// Voice status options:
#define VOICE_FREE            0
#define VOICE_ON              1
#define VOICE_SUSTAINED       2
#define VOICE_OFF             3
#define VOICE_DIE             4

// Voice panned options:
#define PANNED_MYSTERY        0
#define PANNED_LEFT           1
#define PANNED_RIGHT          2
#define PANNED_CENTER         3


// Noise modes for open_file
#define OF_SILENT  0
#define OF_NORMAL  1
#define OF_VERBOSE 2

// Order of the FIR filter = 20 should be enough !
#define ORDER      20
#define ORDER2     ORDER / 2

// Midi events
#define ME_NONE              0
#define ME_NOTEON            1
#define ME_NOTEOFF           2
#define ME_KEYPRESSURE       3
#define ME_MAINVOLUME        4
#define ME_PAN               5
#define ME_SUSTAIN           6
#define ME_EXPRESSION        7
#define ME_PITCHWHEEL        8
#define ME_PROGRAM           9
#define ME_MONO              10
#define ME_PITCH_SENS        11
#define ME_ALL_SOUNDS_OFF    12
#define ME_RESET_CONTROLLERS 13
#define ME_ALL_NOTES_OFF     14
#define ME_TONE_BANK         15
#define ME_POLY              16
#define ME_TEMPO             17
#define ME_EOT               99

// Data format encoding bits
#define PE_MONO         0x01 // versus stereo
#define PE_SIGNED       0x02 // versus unsigned
#define PE_16BIT        0x04 // versus 8-bit
#define PE_ULAW         0x08 // versus linear
#define PE_BYTESWAP     0x10 // versus the other way

#define SINE_CYCLE_LENGTH 1024

// Bits in modes:
#define MODES_16BIT    (1 << 0)
#define MODES_UNSIGNED (1 << 1)
#define MODES_LOOPING  (1 << 2)
#define MODES_PINGPONG (1 << 3)
#define MODES_REVERSE  (1 << 4)
#define MODES_SUSTAIN  (1 << 5)
#define MODES_ENVELOPE (1 << 6)

#define SPECIAL_PROGRAM -1

// Causes the instrument's default panning to be used.
#define NO_PANNING -1





// Anything but PANNED_MYSTERY only uses the left volume





typedef uint8_t        u8;
typedef uint16_t       u16;
typedef uint32_t       u32;
typedef uint64_t       u64;
typedef int8_t         i8;
typedef int16_t        i16;
typedef int32_t        i32;
typedef int64_t        i64;
typedef float          f32;
typedef double         f64;
typedef unsigned long  u_long;
typedef unsigned short u_short;
typedef unsigned char  u_char;

#ifdef LOOKUP_SINE
f64 sine(int x);
#else
    #include <math.h>
    #define sine(x) (sin((2 * PI / 1024.0) * (x)))
#endif





#ifdef LOOKUP_HACK
extern short _u2l[];
#endif

extern long    freq_table[];
extern f64     vol_table[];
extern f64     bend_fine[];
extern f64     bend_coarse[];
extern u_char *_l2u;    // 13-bit PCM to 8-bit u-law
extern u_char  _l2u_[]; // used in LOOKUP_HACK


typedef struct
{
    char *path;
    void *next;
} PathList;

typedef struct
{
    f32 envelope;
    f32 gain;
    f32 limiter_threshold;
    f32 limiter_ratio;
    f32 limiter_attack_coeff;
    f32 limiter_release_coeff;
    f32 limiter_makeup_gain;
    f32 limiter_attack_ms;
    f32 limiter_release_ms;
    f32 limiter_sample_rate;
} CompressorSettings;

typedef struct
{
    long      loop_start;
    long      loop_end;
    long      data_length;
    long      sample_rate;
    long      low_freq;
    long      high_freq;
    long      root_freq;
    long      envelope_rate[6];
    long      envelope_offset[6];
    f64       volume;
    sample_t *data;
    long      tremolo_sweep_increment;
    long      tremolo_phase_increment;
    long      vibrato_sweep_increment;
    long      vibrato_control_ratio;
    u_char    tremolo_depth;
    u_char    vibrato_depth;
    u_char    modes;
    char      panning;
    char      note_to_use;
    long      data_alloced;
} Sample;

typedef struct
{
    int     samples;
    Sample *sample;
} Instrument;

typedef struct
{
    char       *name;
    Instrument *instrument;
    int         note, amp, pan, strip_loop, strip_envelope, strip_tail;
} ToneBankElement;

typedef struct
{
    ToneBankElement tone[128];
} ToneBank;


typedef struct
{
    long   time;
    u_char channel;
    u_char type;
    u_char vel;
    u_char key;
} MidiEvent;

typedef struct
{
    MidiEvent event;
    void     *next;
} MidiEventList;



typedef struct
{
    long rate;
    long encoding;
} PlayMode;



typedef struct
{
    int bank;
    int program;
    int volume;
    int sustain;
    int panning;
    int pitchbend;
    int expression;
    int mono; // one note only on this channel
    int pitchsens;
    // chorus, reverb... Coming soon to a 300-MHz, eight-way superscalar processor near you
    f64 pitchfactor; // precomputed pitch bend factor to save some fdiv's
} Channel;

typedef struct
{
    // Effects common for SF2 standard
    u32 start_addrs_offset;
    u32 end_addrs_offset;
    u32 startloop_addrs_offset;
    u32 endloop_addrs_offset;
    u32 start_addrs_coarse_offset;
    f32 mod_lfo_to_pitch;
    f32 vib_lfo_to_pitch;
    f32 mod_env_to_pitch;
    u16 initial_filter_fc;
    f32 initial_filter_q;
    f32 mod_lfo_to_filter_fc;
    f32 mod_env_to_filter_fc;
    u32 end_addrs_coarse_offset;
    f32 mod_lfo_to_volume;
    f32 chorus_effects_send;
    f32 reverb_effects_send;
    f32 pan;
    f32 delay_mod_LFO;
    f32 freq_mod_LFO;
    f32 delay_vib_LFO;
    f32 freq_vib_LFO;
    f32 delay_mod_env;
    f32 attack_mod_env;
    f32 hold_mod_env;
    f32 decay_mod_env;
    f32 sustain_mod_env;
    f32 release_mod_env;
    f32 keynum_to_mod_env_hold;
    f32 keynum_to_mod_env_decay;
    f32 delay_vol_env;
    f32 attack_vol_env;
    f32 hold_vol_env;
    f32 decay_vol_env;
    f32 sustain_vol_env;
    f32 release_vol_env;
    f32 keynum_to_vol_env_hold;
    f32 keynum_to_vol_env_decay;
    u16 instrument;
    u8  key_range;
    u8  vel_range;
    u32 start_loop_addrs_coarse_offset;
    u8  fixed_key;
    u8  velocity;
    f32 initial_attenuation;
    u32 end_loop_addrs_coarse_offset;
    f32 coarse_tune;
    f32 fine_tune;
    u32 sample_id;
    u8  sample_modes;
    f32 scale_tuning;
    u8  exclusive_class;
    u16 overriding_root_key;
} SoundFontEffects;

typedef struct
{
    u_char           status;
    u_char           channel;
    u_char           note;
    u_char           velocity;
    Sample          *sample;
    long             orig_frequency;
    long             frequency;
    long             sample_offset;
    long             sample_increment;
    long             envelope_volume;
    long             envelope_target;
    long             envelope_increment;
    long             tremolo_sweep;
    long             tremolo_sweep_position;
    long             tremolo_phase;
    long             tremolo_phase_increment;
    long             vibrato_sweep;
    long             vibrato_sweep_position;
    final_volume_t   left_mix;
    final_volume_t   right_mix;
    f64              left_amp;
    f64              right_amp;
    f64              tremolo_volume;
    long             vibrato_sample_increment[VIBRATO_SAMPLE_INCREMENTS];
    int              vibrato_phase;
    int              vibrato_control_ratio;
    int              vibrato_control_counter;
    int              envelope_stage;
    int              control_counter;
    int              panning;
    int              panned;
    SoundFontEffects sf2_effects;
} Voice;

struct Kasaria
{
    char           current_filename[1024];
    PathList      *pathlist; // The paths in this list will be tried whenever we're reading a file
    ToneBank      *tonebank[128];
    ToneBank      *drumset[128];
    Instrument    *default_instrument; // This is only used for tracks that don't specify a program
    int            default_program;    // This is a special instrument, used for all melodic programs
    bool           antialiasing_allowed;
    bool           pre_resampling_allowed;
    bool           fast_decay;
    // int            dynamic_loading; // No longer it use
    PlayMode       play_mode;
    f32            common_buffer[AUDIO_BUFFER_SIZE * 2]; // stereo samples
    f32           *buffer_pointer;
    Channel        channel[16];
    Voice          voice[MAX_VOICES];
    Voice         *voice_by_channel_note[16][128][2];
    long           control_rate;
    long           control_ratio;
    f64            master_volume;
    long           drumchannels;
    long           quietchannels;
    long           lost_notes;
    long           cut_notes;
    bool           adjust_panning_immediately;
    int            voices;
    u8             low_vel_treshold;
    u8             high_vel_treshold;
    u_char         rpn_msb[16];
    u_char         rpn_lsb[16];
    MidiEvent     *event_list;
    MidiEvent     *current_event;
    long           sample_count;
    long           current_sample;
    FILE          *fp_midi;
    long           events_midi;
    char           song_title[256];
    char           song_copyright[256];
    char           last_smf[1024];
    // to avoid some unnecessary parameter passing
    MidiEventList *evlist;
    long           event_count;
    FILE          *fp;
    long           at;
    /*
        These would both fit into 32 bits, but they are often added in
        large multiples, so it's simpler to have two roomy ints */
    // samples per MIDI delta-t
    long           sample_increment;
    long           sample_correction;
    sample_t       resample_buffer[AUDIO_BUFFER_SIZE];
#ifdef LOOKUP_HACK
    long *mixup;
    #ifdef LOOKUP_INTERPOLATION
    char *iplookup;
    #endif
#endif
    char               def_instr_name[256];
    int                sf_loaded;
    SFInfo             sf_info;
    char               sf_filename[1024];
    CompressorSettings compressor_settings;
    int                channel_voice_count[16];
    int                channel_voice_list[16][MAX_VOICES * 2];
};













FILE       *open_file(Kasaria *tm, char *name, int decompress, int noise_mode);
void        add_to_pathlist(Kasaria *tm, char *s);
void        free_pathlist(Kasaria *tm);
void        close_file(FILE *fp);
void        skip(FILE *fp, size_t len);
void       *safe_malloc(size_t count);
void        antialiasing(Sample *sp, long output_rate);
int         load_missing_instruments(Kasaria *tm);
void        free_instruments(Kasaria *tm);
//int         set_default_instrument(Kasaria *tm, char *name);
void        free_default_instrument(Kasaria *tm);
void        mix_voice(Kasaria *tm, f32 *buf, int v, long c);
int         recompute_envelope(Kasaria *tm, int v);
void        apply_envelope_to_amp(Kasaria *tm, int v);
MidiEvent  *read_midi_file(Kasaria *tm, FILE *mfp, long *count, long *sp);
sample_t   *resample_voice(Kasaria *tm, int v, long *countptr);
void        pre_resample(Kasaria *tm, Sample *sp);
void        init_tables(Kasaria *tm);
void        free_tables(Kasaria *tm);
Instrument *load_soundfont_instrument(Kasaria *tm, SFInfo *sf, const char *filename, int bank, int program);
int         read_config_file(Kasaria *tm, char *name);
void        reset_midi(Kasaria *ksr);

void        audio_compressor(CompressorSettings *compr_settings, void *buffer, u32 length);


// ------------- Synth Base functions -------------
void channel_voice_add(Kasaria *ksr, int ch, int vi);
void channel_voice_remove(Kasaria *ksr, int ch, int vi);
void select_sample(Kasaria *ksr, int v, Instrument *ip);
void recompute_freq(Kasaria *ksr, int v);
void recompute_amp(Kasaria *ksr, int v);
void start_note(Kasaria *ksr, MidiEvent *e, int i);
void kill_note(Kasaria *ksr, int i);
void note_on(Kasaria *ksr, MidiEvent *e);
void finish_note(Kasaria *ksr, int i);
void note_off(Kasaria *ksr, MidiEvent *e);
void all_notes_off(Kasaria *ksr, int c);
void all_sounds_off(Kasaria *ksr, int c);
void adjust_pressure(Kasaria *ksr, MidiEvent *e);
void adjust_panning(Kasaria *ksr, int c);
void adjust_pitchbend(Kasaria *ksr, int c);
void adjust_volume(Kasaria *ksr, int c);
void do_compute_data(Kasaria *ksr, long count);
void adjust_amplification(Kasaria *ksr, int amplification);
void reset_voices(Kasaria *ksr);
void drop_sustain(Kasaria *ksr, int c);
void reset_controllers(Kasaria *ksr, int c);


// ------------- Output functions -------------
//void ksr_render_char(Kasaria *ksr, u_char *buffer, long count);
//void ksr_render_short(Kasaria *ksr, short *buffer, long count);
//void ksr_render_24(Kasaria *ksr, int24 *buffer, long count);
//void ksr_render_long(Kasaria *ksr, long *buffer, long count);
//void ksr_render_float(Kasaria *ksr, f32 *buffer, long count);
//void ksr_render_f64(Kasaria *ksr, f64 *buffer, long count);
//void ksr_render_ulaw(Kasaria *ksr, u_char *buffer, long count);
#endif