#ifndef REVERBEFFECT_H
#define REVERBEFFECT_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "efx-presets.h"

typedef int BOOL;

#define OUTPUT_CHANNELS 2
#define TRUE 1
#define FALSE 0
#define REVERB_BUFFERSIZE (2048u)
#define F_2PI (6.28318530717958647692f)
#define FLT_EPSILON 1.19209290E-07F

#define  MAX_AMBI_COEFFS 4

#define SPEEDOFSOUNDMETRESPERSEC 343.3f

#define GAIN_SILENCE_THRESHOLD  0.00001f

/* This is a user config option for modifying the overall output of the reverb effect. */
#define ReverbBoost 1.0f

/* Effect parameter ranges and defaults. */
#define EAXREVERB_MIN_DENSITY                 (0.0f)
#define EAXREVERB_MAX_DENSITY                 (1.0f)
#define EAXREVERB_DEFAULT_DENSITY             (1.0f)

#define EAXREVERB_MIN_DIFFUSION               (0.0f)
#define EAXREVERB_MAX_DIFFUSION               (1.0f)
#define EAXREVERB_DEFAULT_DIFFUSION           (1.0f)

#define EAXREVERB_MIN_GAIN                    (0.0f)
#define EAXREVERB_MAX_GAIN                    (1.0f)
#define EAXREVERB_DEFAULT_GAIN                (0.32f)

#define EAXREVERB_MIN_GAINHF                  (0.0f)
#define EAXREVERB_MAX_GAINHF                  (1.0f)
#define EAXREVERB_DEFAULT_GAINHF              (0.89f)

#define EAXREVERB_MIN_GAINLF                  (0.0f)
#define EAXREVERB_MAX_GAINLF                  (1.0f)
#define EAXREVERB_DEFAULT_GAINLF              (1.0f)

#define EAXREVERB_MIN_DECAY_TIME              (0.1f)
#define EAXREVERB_MAX_DECAY_TIME              (20.0f)
#define EAXREVERB_DEFAULT_DECAY_TIME          (1.49f)

#define EAXREVERB_MIN_DECAY_HFRATIO           (0.1f)
#define EAXREVERB_MAX_DECAY_HFRATIO           (2.0f)
#define EAXREVERB_DEFAULT_DECAY_HFRATIO       (0.83f)

#define EAXREVERB_MIN_DECAY_LFRATIO           (0.1f)
#define EAXREVERB_MAX_DECAY_LFRATIO           (2.0f)
#define EAXREVERB_DEFAULT_DECAY_LFRATIO       (1.0f)

#define EAXREVERB_MIN_REFLECTIONS_GAIN        (0.0f)
#define EAXREVERB_MAX_REFLECTIONS_GAIN        (3.16f)
#define EAXREVERB_DEFAULT_REFLECTIONS_GAIN    (0.05f)

#define EAXREVERB_MIN_REFLECTIONS_DELAY       (0.0f)
#define EAXREVERB_MAX_REFLECTIONS_DELAY       (0.3f)
#define EAXREVERB_DEFAULT_REFLECTIONS_DELAY   (0.007f)

#define EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ (0.0f)

#define EAXREVERB_MIN_LATE_REVERB_GAIN        (0.0f)
#define EAXREVERB_MAX_LATE_REVERB_GAIN        (10.0f)
#define EAXREVERB_DEFAULT_LATE_REVERB_GAIN    (1.26f)

#define EAXREVERB_MIN_LATE_REVERB_DELAY       (0.0f)
#define EAXREVERB_MAX_LATE_REVERB_DELAY       (0.1f)
#define EAXREVERB_DEFAULT_LATE_REVERB_DELAY   (0.011f)

#define EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ (0.0f)

#define EAXREVERB_MIN_ECHO_TIME               (0.075f)
#define EAXREVERB_MAX_ECHO_TIME               (0.25f)
#define EAXREVERB_DEFAULT_ECHO_TIME           (0.25f)

#define EAXREVERB_MIN_ECHO_DEPTH              (0.0f)
#define EAXREVERB_MAX_ECHO_DEPTH              (1.0f)
#define EAXREVERB_DEFAULT_ECHO_DEPTH          (0.0f)

#define EAXREVERB_MIN_MODULATION_TIME         (0.04f)
#define EAXREVERB_MAX_MODULATION_TIME         (4.0f)
#define EAXREVERB_DEFAULT_MODULATION_TIME     (0.25f)

#define EAXREVERB_MIN_MODULATION_DEPTH        (0.0f)
#define EAXREVERB_MAX_MODULATION_DEPTH        (1.0f)
#define EAXREVERB_DEFAULT_MODULATION_DEPTH    (0.0f)

#define EAXREVERB_MIN_AIR_ABSORPTION_GAINHF   (0.892f)
#define EAXREVERB_MAX_AIR_ABSORPTION_GAINHF   (1.0f)
#define EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF (0.994f)

#define EAXREVERB_MIN_HFREFERENCE             (1000.0f)
#define EAXREVERB_MAX_HFREFERENCE             (20000.0f)
#define EAXREVERB_DEFAULT_HFREFERENCE         (5000.0f)

#define EAXREVERB_MIN_LFREFERENCE             (20.0f)
#define EAXREVERB_MAX_LFREFERENCE             (1000.0f)
#define EAXREVERB_DEFAULT_LFREFERENCE         (250.0f)

#define EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR     (0.0f)
#define EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR     (10.0f)
#define EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR (0.0f)

#define EAXREVERB_MIN_DECAY_HFLIMIT           FALSE
#define EAXREVERB_MAX_DECAY_HFLIMIT           TRUE
#define EAXREVERB_DEFAULT_DECAY_HFLIMIT       TRUE

typedef enum FilterType {
    /** EFX-style low-pass filter, specifying a gain and reference frequency. */
    Filter_HighShelf,
    /** EFX-style high-pass filter, specifying a gain and reference frequency. */
    Filter_LowShelf,
} FilterType;

typedef struct
{
    // The delay lines use sample lengths that are powers of 2 to allow the
    // use of bit-masking instead of a modulus for wrapping.
    u32   Mask;
    f32 *Line;
} DelayLine;

typedef struct
{
    f32 x[2]; // History of two last input samples
    f32 y[2]; // History of two last output samples
    f32 a[3]; // Transfer function coefficients "a"
    f32 b[3]; // Transfer function coefficients "b"
} FilterState;

typedef struct {
    // Shared Reverb Properties
    f32 Density;
    f32 Diffusion;
    f32 Gain;
    f32 GainHF;
    f32 DecayTime;
    f32 DecayHFRatio;
    f32 ReflectionsGain;
    f32 ReflectionsDelay;
    f32 LateReverbGain;
    f32 LateReverbDelay;
    f32 AirAbsorptionGainHF;
    f32 RoomRolloffFactor;
    BOOL DecayHFLimit;

    // Additional EAX Reverb Properties
    f32 GainLF;
    f32 DecayLFRatio;
    f32 ReflectionsPan[3];
    f32 LateReverbPan[3];
    f32 EchoTime;
    f32 EchoDepth;
    f32 ModulationTime;
    f32 ModulationDepth;
    f32 HFReference;
    f32 LFReference;

} ReverbSettings;

typedef struct {
    // Modulator delay line.
    DelayLine Delay;

    // The vibrato time is tracked with an index over a modulus-wrapped
    // range (in samples).
    u32    Index;
    u32    Range;

    // The depth of frequency change (also in samples) and its filter.
    f32   Depth;
    f32   Coeff;
    f32   Filter;
} Modulator;

typedef struct {
    // Output gain for early reflections.
    f32   Gain;

    // Early reflections are done with 4 delay lines.
    f32   Coeff[4];
    DelayLine Delay[4];
    u32    Offset[4];

    // The gain for each output channel based on 3D panning (only for the
    // EAX path).
    f32   PanGain[OUTPUT_CHANNELS];
} EarlyDelay;

typedef struct {
    // Output gain for late reverb.
    f32   Gain;

    // Attenuation to compensate for the modal density and decay rate of
    // the late lines.
    f32   DensityGain;

    // The feed-back and feed-forward all-pass coefficient.
    f32   ApFeedCoeff;

    // Mixing matrix coefficient.
    f32   MixCoeff;

    // Late reverb has 4 parallel all-pass filters.
    f32   ApCoeff[4];
    DelayLine ApDelay[4];
    u32    ApOffset[4];

    // In addition to 4 cyclical delay lines.
    f32   Coeff[4];
    DelayLine Delay[4];
    u32    Offset[4];

    // The cyclical delay lines are 1-pole low-pass filtered.
    f32   LpCoeff[4];
    f32   LpSample[4];

    // The gain for each output channel based on 3D panning (only for the
    // EAX path).
    f32   PanGain[OUTPUT_CHANNELS];
} LateDelay;

typedef struct {
    // Attenuation to compensate for the modal density and decay rate of
    // the echo line.
    f32   DensityGain;

    // Echo delay and all-pass lines.
    DelayLine Delay;
    DelayLine ApDelay;

    f32   Coeff;
    f32   ApFeedCoeff;
    f32   ApCoeff;

    u32    Offset;
    u32    ApOffset;

    // The echo line is 1-pole low-pass filtered.
    f32   LpCoeff;
    f32   LpSample;

    // Echo mixing coefficients.
    f32   MixCoeff[2];
} ReverbEcho;

typedef struct {
    ReverbSettings settings;

    // All delay lines are allocated as a single buffer to reduce memory
    // fragmentation and management code.
    f32  *SampleBuffer;
    u32    TotalSamples;

    // Master effect filters
    FilterState LpFilter;
    FilterState HpFilter; // EAX only

    Modulator Mod;

    // Initial effect delay.
    DelayLine Delay;
    // The tap points for the initial delay.  First tap goes to early
    // reflections, the last to late reverb.
    u32    DelayTap[2];

    EarlyDelay Early;

    // Decorrelator delay line.
    DelayLine Decorrelator;
    // There are actually 4 decorrelator taps, but the first occurs at the
    // initial sample.
    u32    DecoTap[3];

    LateDelay Late;

    ReverbEcho Echo;

    // The current read offset for all delay lines.
    u32 Offset;

        /* Temporary storage used when processing, before deinterlacing. */
    f32 ReverbSamples[REVERB_BUFFERSIZE][4];
    f32 EarlySamples[REVERB_BUFFERSIZE][4];

    f32 ambiCoeffs[OUTPUT_CHANNELS][MAX_AMBI_COEFFS];
} ReverbEffect;

#ifdef __cplusplus
extern "C" {
#endif

void ReverbEffectCreate(ReverbEffect *effect, uint32_t frequency);
void ReverbEffectDestroy(ReverbEffect *effect);
void ReverbEffectProcess(ReverbEffect *effect, uint32_t SamplesToDo, const float *SamplesIn, float *SamplesOut);
void ReverbEffectUpdate(ReverbEffect *effect, int frequency);
void ReverbEffectLoadPreset(ReverbEffect *effect, EFXEAXREVERBPROPERTIES *properties);

#ifdef __cplusplus
}
#endif

#endif // REVERBEFFECT_H