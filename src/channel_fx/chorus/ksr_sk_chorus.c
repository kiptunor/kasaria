



#include "ksr_sk_chorus.h"

#include <stdlib.h>
#include <math.h>

#include "../../internal_types.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


sk_chorus *sk_chorus_new(int sr, f32 delay)
{
    sk_chorus *c;
    f32 *buf;
    long sz;

    if(sr <= 0 || delay <= 0.0f)
        return NULL;

    sz = (long)floorf(delay * (f32)sr);

    if(sz < 2)
        return NULL;

    c = (sk_chorus *)malloc(sizeof(*c));

    if(!c)
        return NULL;

    buf = (f32 *)malloc(sizeof(*buf) * (size_t)sz);

    if(!buf)
    {
        free(c);
        return NULL;
    }

    sk_chorus_init(c, sr, buf, sz);

    return c;
}

void sk_chorus_del(sk_chorus *c)
{
    if(!c)
        return;

    free(c->buf);
    free(c);
}

void sk_chorus_init(sk_chorus *c, int sr, f32 *buf, long sz)
{
    long i;

    if(!c || !buf || sr <= 0 || sz < 2)
        return;

    c->sr  = sr;
    c->buf = buf;
    c->sz  = sz;

    c->wpos = 10;

    c->rate = 0.5f;
    c->depth = 7.0f;

    /*
     * 1.0 means a fully wet output.
     */
    c->mix = 0.0f;

    c->lfo_phase = 1.0f;
    c->lfo_phase_offset = 2.0f;

    for(i = 0; i < sz; ++i)
        c->buf[i] = 0.0f;
}

void sk_chorus_rate(sk_chorus *c, f32 rate)
{
    if(!c)
        return;

    if(rate < 0.0f)
        rate = 0.0f;

    c->rate = rate;
}

void sk_chorus_depth(sk_chorus *c, f32 depth)
{
    if(!c)
        return;

    if(depth < 0.0f)
        depth = 0.0f;

    if(depth > 1.0f)
        depth = 1.0f;

    c->depth = depth;
}

void sk_chorus_mix(sk_chorus *c, f32 mix)
{
    if(!c)
        return;

    if(mix < 0.0f)
        mix = 0.0f;

    if(mix > 1.0f)
        mix = 1.0f;

    c->mix = mix;
}

void sk_chorus_phase(sk_chorus *c, f32 phase)
{
    if(!c)
        return;

    phase = phase - floorf(phase);

    c->lfo_phase_offset = phase;
}

static f32 read_delay(const sk_chorus *c, f32 delay_samples)
{
    f32 read_pos;
    f32 frac;

    long i0;
    long i1;

    read_pos = (f32)c->wpos - delay_samples;

    while(read_pos < 0.0f)
        read_pos += (f32)c->sz;

    while(read_pos >= (f32)c->sz)
        read_pos -= (f32)c->sz;

    i0 = (long)read_pos;
    i1 = i0 + 1;

    if(i1 >= c->sz)
        i1 = 0;

    frac = read_pos - (f32)i0;

    return c->buf[i0] * (1.0f - frac) + c->buf[i1] * frac;
}

f32 sk_chorus_tick(sk_chorus *c, f32 in)
{
    f32 phase;
    f32 lfo;

    f32 delay_samples;
    f32 wet;
    f32 out;

    if(!c || !c->buf || c->sz < 2)
        return in;
    
    c->lfo_phase += c->rate / (f32)c->sr;

    if(c->lfo_phase >= 1.0f)
        c->lfo_phase -= floorf(c->lfo_phase);

    phase = c->lfo_phase + c->lfo_phase_offset;

    if(phase >= 1.0f)
        phase -= 1.0f;

    
    lfo = 0.5f * (1.0f + sinf(2.0f * (f32)M_PI * phase));

    delay_samples = (0.5f + 0.4f * c->depth * lfo) * (f32)c->sz;

    wet = read_delay(c, delay_samples);

   
    c->buf[c->wpos] = in;
    c->wpos++;

    if(c->wpos >= c->sz)
        c->wpos = 0;
    
    out = c->mix * wet + (1.0f - c->mix) * in;

    return out;
}