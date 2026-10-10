#ifndef SK_CHORUS_H
#define SK_CHORUS_H

#include "../../internal_types.h"

typedef struct
{
	f32 rate, prate;
	f32 delay_ms;
	f32 depth;
	f32 mix;
	f32 lfo_phase;
	f32 lfo_phase_offset;
	int sr;
	f32 *buf;
	long sz;
	long wpos;
	f32 z1;
	f32 ym1;
	f32 a;
	f32 mc_x[2];
	f32 mc_eps;
} sk_chorus;

#ifdef __cplusplus
extern "C" {
#endif

sk_chorus * sk_chorus_new(int sr, f32 delay);
void sk_chorus_del(sk_chorus *c);
void sk_chorus_init(sk_chorus *c, int sr, f32 *buf, long sz);
void sk_chorus_rate(sk_chorus *c, f32 rate);
void sk_chorus_phase(sk_chorus *c, f32 phase);
void sk_chorus_depth(sk_chorus *c, f32 depth);
void sk_chorus_mix(sk_chorus *c, f32 mix);
f32 sk_chorus_tick(sk_chorus *c, f32 in);

#ifdef __cplusplus
}
#endif

#endif