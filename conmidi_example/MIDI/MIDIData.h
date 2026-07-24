#ifndef MIDIDATA_H
#define MIDIDATA_H

#include <stddef.h>
#include <stdint.h>

typedef int BOOL;
#define FALSE 0
#define TRUE 1

extern BOOL loaded;
extern unsigned int ppq;
extern unsigned int fakeTracks;
extern unsigned int format;
extern unsigned int realTracks;
extern unsigned long long notes;
extern unsigned char** tracks;
extern unsigned long int* trackSizes;

#endif