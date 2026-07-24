#include "MIDIData.h"

BOOL loaded = FALSE;
unsigned int ppq = 0;
unsigned int fakeTracks = 0;
unsigned int format = 0;
unsigned int realTracks = 0;
unsigned long long notes = 0;
unsigned char** tracks = NULL;
unsigned long int* trackSizes = NULL;