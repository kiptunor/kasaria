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

*/

#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32_WCE
    #include <errno.h>
#endif

#ifndef _WIN32_WCE
    #include <string.h>
#endif

#include "ksr_internal.h"

/* Computes how many (fractional) samples one MIDI delta-time unit contains */
static void compute_sample_increment(Kasaria *ksr, long tempo, long divisions)
{
    f64 a;
    a                      = (f64)(tempo) * (f64)(ksr->play_mode.rate) * (65536.0 / 1000000.0) / (f64)(divisions);

    ksr->sample_correction = (long)(a) & 0xFFFF;
    ksr->sample_increment  = (long)(a) >> 16;
}


static int compare_events(const void *a, const void *b)
{
    const MidiEventList *ea = *(const MidiEventList * const *)a;
    const MidiEventList *eb = *(const MidiEventList * const *)b;
    long ta = ea->event.time;
    long tb = eb->event.time;
    return (ta > tb) - (ta < tb);
}

// ----------------

static MidiEventList *merge_sorted(MidiEventList *a, MidiEventList *b)
{
    MidiEventList  dummy;
    MidiEventList *tail = &dummy;
    dummy.next = 0;

    while(a && b)
    {
        if(a->event.time <= b->event.time)
        {
            tail->next = a;
            a = (MidiEventList *)a->next;
        }
        else
        {
            tail->next = b;
            b = (MidiEventList *)b->next;
        }
        tail = (MidiEventList *)tail->next;
    }
    tail->next = a ? a : b;
    return (MidiEventList *)dummy.next;
}

static MidiEventList *merge_sort_events(MidiEventList *head)
{
    MidiEventList *slow, *fast, *second;

    if(!head || !head->next)
        return head;

    slow = head;
    fast = (MidiEventList *)head->next;
    while(fast && fast->next)
    {
        slow = (MidiEventList *)slow->next;
        fast = (MidiEventList *)((MidiEventList *)fast->next)->next;
    }

    second = (MidiEventList *)slow->next;
    slow->next = 0;

    head   = merge_sort_events(head);
    second = merge_sort_events(second);

    return merge_sorted(head, second);
}

static void sort_event_list(Kasaria *ksr)
{
    ksr->evlist = merge_sort_events(ksr->evlist);
}

/* Read variable-length number (7 bits per byte, MSB first) */
static long getvl(Kasaria *ksr)
{
    long   l = 0;
    u_char c;
    for(;;)
    {
        if(fread(&c, 1, 1, ksr->fp) != 1)
            return l;

        l += (c & 0x7f);
        if(!(c & 0x80))
            return l;

        l <<= 7;
    }
}

/* Print a string from the file, followed by a newline. Any non-ASCII
or unprintable characters will be converted to periods. */
static int dumpstring(Kasaria *ksr, long len, char *label)
{
    signed char *s = (signed char *)safe_malloc(len + 1);
    if(len != fread(s, 1, len, ksr->fp))
    {
        free(s);
        return -1;
    }
    s[len] = '\0';
    while(len--)
    {
        if(s[len] < 32)
            s[len] = '.';
    }
    free(s);
    return 0;
}

#define MIDIEVENT(at, t, ch, pa, pb)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   \
    newev                = (MidiEventList *)safe_malloc(sizeof(MidiEventList));                                                                                                                                                                                                                                                                                                                                                                                                                                                        \
    newev->event.time    = at;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         \
    newev->event.type    = t;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          \
    newev->event.channel = ch;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         \
    newev->event.a       = pa;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         \
    newev->event.b       = pb;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         \
    newev->next          = 0;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          \
    return newev;

#define MAGIC_EOT ((MidiEventList *)(-1))

/* Read a MIDI event, returning a freshly allocated element that can
be linked to the event list */
static MidiEventList *read_midi_event(Kasaria *ksr)
{
    static u_char  laststatus, lastchan;
    static u_char  nrpn = 0, rpn_msb[16], rpn_lsb[16];
    u_char         me, type, a, b, c;
    long           len;
    MidiEventList *newev;
    
    for(;;)
    {
        ksr->at += getvl(ksr);
        if(fread(&me, 1, 1, ksr->fp) != 1)
            return 0;
        
        //if(me == 0xF0 || me == 0xF7)
        //{
        //    ksr->at += getvl(ksr);
            //if(fread(&me, 1, 1, ksr->fp) != 1)
            //    return 0;
    
            if(me == 0xF0 || me == 0xF7)
            {
                len = getvl(ksr);
                skip(ksr->fp, len);
            }
            else if(me == 0xFF)
            {
                if(fread(&type, 1, 1, ksr->fp) != 1)
                    return 0;
                len = getvl(ksr);
                if(type > 0 && type < 16)
                {
                    static char *label[] = { "Text event: ", "Text: ", "Copyright: ",
                        "Track name: ", "Instrument: ", "Lyric: ", "Marker: ", "Cue point: " };
                    if(dumpstring(ksr, len, label[(type > 7) ? 0 : type]) < 0)
                        return 0;
                }
                else switch(type)
                {
                case 0x2F:
                    return MAGIC_EOT;
    
                case 0x51:
                    if(len != 3)
                    {
                        skip(ksr->fp, len);
                        break;
                    }
                    if(fread(&a, 1, 1, ksr->fp) != 1 ||
                       fread(&b, 1, 1, ksr->fp) != 1 ||
                       fread(&c, 1, 1, ksr->fp) != 1)
                        return 0;
                    MIDIEVENT(ksr->at, ME_TEMPO, c, a, b);
    
                default:
                    skip(ksr->fp, len);
                    break;
                }
            }
            else
            {
                a = me;
                if(a & 0x80)
                {
                    lastchan   = a & 0x0F;
                    laststatus = (a >> 4) & 0x07;
                    if(fread(&a, 1, 1, ksr->fp) != 1)
                        return 0;
                    a &= 0x7F;
                }
                switch(laststatus)
                {
                case 0: /* Note off */
                    if(fread(&b, 1, 1, ksr->fp) != 1)
                        return 0;
                    b &= 0x7F;
                    MIDIEVENT(ksr->at, ME_NOTEOFF, lastchan, a, b);
    
                case 1: /* Note on */
                    if(fread(&b, 1, 1, ksr->fp) != 1)
                        return 0;
                    b &= 0x7F;
                    MIDIEVENT(ksr->at, ME_NOTEON, lastchan, a, b);
    
                case 2: /* Key Pressure */
                    if(fread(&b, 1, 1, ksr->fp) != 1)
                        return 0;
                    b &= 0x7F;
                    MIDIEVENT(ksr->at, ME_KEYPRESSURE, lastchan, a, b);
    
                case 3: /* Control change */
                    if(fread(&b, 1, 1, ksr->fp) != 1)
                        return 0;
                    b &= 0x7F;
                    {
                        int control = 255;
                        switch(a)
                        {
                        case 7:
                            control = ME_MAINVOLUME;
                            break;
                        case 10:
                            control = ME_PAN;
                            break;
                        case 11:
                            control = ME_EXPRESSION;
                            break;
                        case 64:
                            control = ME_SUSTAIN;
                            b       = (b >= 64);
                            break;
                        case 120:
                            control = ME_ALL_SOUNDS_OFF;
                            break;
                        case 121:
                            control = ME_RESET_CONTROLLERS;
                            break;
                        case 123:
                            control = ME_ALL_NOTES_OFF;
                            break;
                        case 126:
                            control = ME_MONO;
                            break;
                        case 127:
                            control = ME_POLY;
                            break;
                        case 0:
                            control = ME_TONE_BANK;
                            break;
                        case 32:
                            break;
                        case 100:
                            nrpn              = 0;
                            rpn_msb[lastchan] = b;
                            break;
                        case 101:
                            nrpn              = 0;
                            rpn_lsb[lastchan] = b;
                            break;
                        case 99:
                            nrpn              = 1;
                            rpn_msb[lastchan] = b;
                            break;
                        case 98:
                            nrpn              = 1;
                            rpn_lsb[lastchan] = b;
                            break;
                        case 6:
                            if(nrpn)
                                break;
                            switch((rpn_msb[lastchan] << 8) | rpn_lsb[lastchan])
                            {
                            case 0x0000:
                                control = ME_PITCH_SENS;
                                break;
                            case 0x7F7F:
                                MIDIEVENT(ksr->at, ME_PITCH_SENS, lastchan, 2, 0);
                            default:
                                break;
                            }
                            break;
                        default:
                            break;
                        }
                        if(control != 255)
                            MIDIEVENT(ksr->at, control, lastchan, b, 0);
                    }
                    break;
    
                case 4: /* Program change */
                    a &= 0x7f;
                    MIDIEVENT(ksr->at, ME_PROGRAM, lastchan, a, 0);
    
                case 5: /* Channel pressure */
                    break;
    
                case 6: /* Pitch wheel */
                    if(fread(&b, 1, 1, ksr->fp) != 1)
                        return 0;
                    b &= 0x7F;
                    MIDIEVENT(ksr->at, ME_PITCHWHEEL, lastchan, a, b);
    
                default:
                    break;
                }
            }
        //}
    }
    return newev;
}

#undef MIDIEVENT

/* Read a midi track into the linked list, either merging with any previous
tracks or appending to them. */
static int read_track(Kasaria *ksr, MidiEventList **tail, int append)
{
/*
    MidiEventList *meep;
    MidiEventList *next, *newev;
    long           len;
    char           tmp[4];
    long           track_start, track_end;

    meep = ksr->evlist;
    if(append && meep)
    {
        for(; meep->next; meep = (MidiEventList *)meep->next)
            ;
        ksr->at = meep->event.time;
    }
    else
        ksr->at = 0;


    printf("Fread 1 ,4\n");
    if((fread(tmp, 1, 4, ksr->fp) != 4) || (fread(&len, 4, 1, ksr->fp) != 1))
        return -1;

    len = BE_LONG(len);
    if(memcmp(tmp, "MTrk", 4))
        return -2;

    //printf("Ftell\n");
    track_start = ftell(ksr->fp);
    track_end   = track_start + len;
    long track_bytes = len;

    for(;;)
    {
        //if(ftell(ksr->fp) >= track_end)
        //{
        //    printf("Ftell track end\n");
        //    free(newev);
        //    return -2;
        //}

        //if(ftell(ksr->fp) - track_start >= track_bytes)
        //        return -2;
    
        if(!(newev = read_midi_event(ksr)))
            return -2;

        printf("read midi event done\n");

        if(newev == MAGIC_EOT)
            return 0;

        printf("No EOT\n");

        next = (MidiEventList *)meep->next;
        while(next && (next->event.time < newev->event.time))
        {
            meep = next;
            next = (MidiEventList *)meep->next;
        }
    
        newev->next = next;
        meep->next  = newev;

        ksr->event_count++;
        meep = newev;
    }
    */
MidiEventList *newev;
    long           len;
    char           tmp[4];

    if(append && *tail)
        ksr->at = (*tail)->event.time;
    else
        ksr->at = 0;

    if((fread(tmp, 1, 4, ksr->fp) != 4) || (fread(&len, 4, 1, ksr->fp) != 1))
        return -1;

    len = BE_LONG(len);
    if(memcmp(tmp, "MTrk", 4))
        return -2;

    for(;;)
    {
        if(!(newev = read_midi_event(ksr)))
            return -2;

        if(newev == MAGIC_EOT)
            return 0;

        newev->next  = 0;
        (*tail)->next = newev;
        *tail         = newev;

        ksr->event_count++;
    }
}

/* Free the linked event list from memory. */
static void free_midi_list(Kasaria *ksr)
{
    MidiEventList *meep, *next;
    if(!(meep = ksr->evlist))
        return;

    while(meep)
    {
        next = (MidiEventList *)meep->next;
        free(meep);
        meep = next;
    }
    ksr->evlist = 0;
}

/* Allocate an array of MidiEvents and fill it from the linked list of
events, marking used instruments for loading. Convert event times to
samples: handle tempo changes. Strip unnecessary events from the list.
Free the linked list. */
static MidiEvent *groom_list(Kasaria *ksr, long divisions, long *eventsp, long *samplesp)
{
    MidiEvent     *groomed_list, *lp;
    MidiEventList *meep;
    long           i, our_event_count, tempo, skip_this_event, new_value;
    long           sample_cum, samples_to_do, at, st, dt, counting_time;

    int            current_bank[16], current_set[16], current_program[16];
    /* Or should each bank have its own current program? */

    for(i = 0; i < 16; i++)
    {
        current_bank[i]    = 0;
        current_set[i]     = 0;
        current_program[i] = ksr->default_program;
    }

    tempo = 500000;
    compute_sample_increment(ksr, tempo, divisions);

    /* This may allocate a bit more than we need */
    groomed_list = lp = (MidiEvent *)safe_malloc(sizeof(MidiEvent) * (ksr->event_count + 1));
    meep              = ksr->evlist;

    our_event_count   = 0;
    st = at = sample_cum = 0;
    counting_time        = 2; /* We strip any silence before the first NOTE ON. */

    for(i = 0; i < ksr->event_count && meep != NULL; i++)
    {
        skip_this_event = 0;

        // This now causes a seg fault
        if(meep->event.type == ME_TEMPO)
            skip_this_event = 1;

        else if(ISQUIETCHANNEL(ksr, meep->event.channel))
            skip_this_event = 1;
        else
            switch(meep->event.type)
            {
            case ME_PROGRAM:
                if(ISDRUMCHANNEL(ksr, meep->event.channel))
                {
                    if(ksr->drumset[meep->event.a]) /* Is this a defined drumset? */
                        new_value = meep->event.a;
                    else
                        new_value = meep->event.a = 0;

                    if(current_set[meep->event.channel] != new_value)
                        current_set[meep->event.channel] = new_value;
                    else
                        skip_this_event = 1;
                }
                else
                {
                    new_value = meep->event.a;
                    if((current_program[meep->event.channel] != SPECIAL_PROGRAM) && (current_program[meep->event.channel] != new_value))
                        current_program[meep->event.channel] = new_value;
                    else
                        skip_this_event = 1;
                }
                break;

            case ME_NOTEON:
                if(counting_time)
                    counting_time = 1;
                if(ISDRUMCHANNEL(ksr, meep->event.channel) && ksr->drumset[current_set[meep->event.channel]])
                {
                    /* Mark this instrument to be loaded */
                    if(!(ksr->drumset[current_set[meep->event.channel]]->tone[meep->event.a].instrument))
                        ksr->drumset[current_set[meep->event.channel]]->tone[meep->event.a].instrument = MAGIC_LOAD_INSTRUMENT;
                }
                else if(ksr->tonebank[current_bank[meep->event.channel]])
                {
                    if(current_program[meep->event.channel] == SPECIAL_PROGRAM)
                        break;
                    /* Mark this instrument to be loaded */
                    if(!(ksr->tonebank[current_bank[meep->event.channel]]->tone[current_program[meep->event.channel]].instrument))
                        ksr->tonebank[current_bank[meep->event.channel]]->tone[current_program[meep->event.channel]].instrument = MAGIC_LOAD_INSTRUMENT;
                }
                break;

            case ME_TONE_BANK:
                if(ISDRUMCHANNEL(ksr, meep->event.channel))
                {
                    skip_this_event = 1;
                    break;
                }
                if(ksr->tonebank[meep->event.a]) /* Is this a defined tone bank? */
                    new_value = meep->event.a;
                else
                    new_value = meep->event.a = 0;

                if(current_bank[meep->event.channel] != new_value)
                    current_bank[meep->event.channel] = new_value;
                else
                    skip_this_event = 1;
                break;
            }

        /* Recompute time in samples*/
        if((dt = meep->event.time - at) && !counting_time)
        {
            samples_to_do  = ksr->sample_increment * dt;
            sample_cum    += ksr->sample_correction * dt;
            if(sample_cum & 0xFFFF0000)
            {
                samples_to_do += ((sample_cum >> 16) & 0xFFFF);
                sample_cum    &= 0x0000FFFF;
            }
            st += samples_to_do;
        }
        else if(counting_time == 1)
            counting_time = 0;

        if(meep->event.type == ME_TEMPO)
        {
            tempo = meep->event.channel + meep->event.b * 256 + meep->event.a * 65536;
            compute_sample_increment(ksr, tempo, divisions);
        }
        if(!skip_this_event)
        {
            /* Add the event to the list */
            *lp      = meep->event;
            lp->time = st;
            lp++;
            our_event_count++;
        }
        at   = meep->event.time;
        meep = (MidiEventList *)meep->next;
    }
    /* Add an End-of-Track event */
    lp->time = st;
    lp->type = ME_EOT;
    our_event_count++;
    free_midi_list(ksr);

    *eventsp  = our_event_count;
    *samplesp = st;
    return groomed_list;
}

MidiEvent *read_midi_file(Kasaria *ksr, FILE *mfp, long *count, long *sp)
{
    /*
    long  len, divisions;
    short format, tracks, divisions_tmp;
    int   i;
    char  tmp[4];

    ksr->fp          = mfp;
    ksr->event_count = 0;
    ksr->at          = 0;
    ksr->evlist      = 0;

    if((fread(tmp, 1, 4, ksr->fp) != 4) || (fread(&len, 4, 1, ksr->fp) != 1))
        return 0;

    len = BE_LONG(len);

    if(memcmp(tmp, "MThd", 4) || len < 6)
        return 0;


    //fread(&format, 2, 1, ksr->fp);
    //fread(&tracks, 2, 1, ksr->fp);
    //fread(&divisions_tmp, 2, 1, ksr->fp);
    if(fread(&format, 2, 1, ksr->fp) != 1 || fread(&tracks, 2, 1, ksr->fp) != 1 || fread(&divisions_tmp, 2, 1, ksr->fp) != 1)
        return 0;
    
    format        = BE_SHORT(format);
    tracks        = BE_SHORT(tracks);
    divisions_tmp = BE_SHORT(divisions_tmp);

    if(divisions_tmp < 0)
    {
        // SMPTE time -- totally untested. Got a MIDI file that uses this?
        divisions = (long)(-(divisions_tmp / 256)) * (long)(divisions_tmp & 0xFF);
    }
    else
        divisions = (long)(divisions_tmp);

    if(len > 6)
        skip(ksr->fp, len - 6); // skip the excess

    if(format < 0 || format > 2)
        return 0;


    // Put a do-nothing event first in the list for easier processing
    ksr->evlist             = (MidiEventList *)safe_malloc(sizeof(MidiEventList));
    ksr->evlist->event.time = 0;
    ksr->evlist->event.type = ME_NONE;
    ksr->evlist->next       = 0;
    ksr->event_count++;

    switch(format)
    {
    case 0:
        if(read_track(ksr, 0))
        {
            printf("Read Track Case 0\n");
            free_midi_list(ksr);
            return 0;
        }
        break;

    case 1:
        for(i = 0; i < tracks; i++)
            if(read_track(ksr, 0))
            {
                free_midi_list(ksr);
                return 0;
            }
        break;

    case 2: // We simply play the tracks sequentially
        for(i = 0; i < tracks; i++)
            if(read_track(ksr, 1))
            {
                printf("Read Track Case 2\n");
                free_midi_list(ksr);
                return 0;
            }
        break;
    }
    printf("Put groom list\n");

    //sort_event_list(ksr);
    sort_event_list(ksr);
    return groom_list(ksr, divisions, count, sp);

    */
    long  len, divisions;
        short format, tracks, divisions_tmp;
        int   i;
        char  tmp[4];
    
        ksr->fp          = mfp;
        ksr->event_count = 0;
        ksr->at          = 0;
        ksr->evlist      = 0;
    
        if((fread(tmp, 1, 4, ksr->fp) != 4) || (fread(&len, 4, 1, ksr->fp) != 1))
            return 0;
    
        len = BE_LONG(len);
    
        if(memcmp(tmp, "MThd", 4) || len < 6)
            return 0;
    
        if(fread(&format, 2, 1, ksr->fp) != 1 || fread(&tracks, 2, 1, ksr->fp) != 1 || fread(&divisions_tmp, 2, 1, ksr->fp) != 1)
            return 0;
    
        format        = BE_SHORT(format);
        tracks        = BE_SHORT(tracks);
        divisions_tmp = BE_SHORT(divisions_tmp);
    
        if(divisions_tmp < 0)
            divisions = (long)(-(divisions_tmp / 256)) * (long)(divisions_tmp & 0xFF);
        else
            divisions = (long)(divisions_tmp);
    
        if(len > 6)
            skip(ksr->fp, len - 6);
    
        if(format < 0 || format > 2)
            return 0;
    
        ksr->evlist             = (MidiEventList *)safe_malloc(sizeof(MidiEventList));
        ksr->evlist->event.time = 0;
        ksr->evlist->event.type = ME_NONE;
        ksr->evlist->next       = 0;
        ksr->event_count++;
    
        MidiEventList *tail = ksr->evlist;
    
        switch(format)
        {
        case 0:
            if(read_track(ksr, &tail, 0))
            {
                free_midi_list(ksr);
                return 0;
            }
            break;
    
        case 1:
            for(i = 0; i < tracks; i++)
                if(read_track(ksr, &tail, 0))
                {
                    free_midi_list(ksr);
                    return 0;
                }
            break;
    
        case 2:
            for(i = 0; i < tracks; i++)
                if(read_track(ksr, &tail, 1))
                {
                    free_midi_list(ksr);
                    return 0;
                }
            break;
        }
    
        sort_event_list(ksr);
        return groom_list(ksr, divisions, count, sp);
}