#include "../BufferFile.h"
#include "MIDIData.h"
#include <stdlib.h>
#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

int TextSearch(char text[]){
    unsigned char* str = ReadRange(strlen(text));
    int res = strcmp(str,text);
    free(str);
    return 1-res;
}

long long int lastPos = 0;
unsigned long int lastSize = 0;

char CopyTrack(unsigned long int id)
{
    if (lastPos != 0)
    {
        Seek(lastPos + lastSize + 8);
    }
    lastPos = filePos + bufPos;
    if (TextSearch("MTrk") != 1)
    {
        //printf("\nTextSearch failed at %llu\n",lastPos);
        return 1;
    }
    realTracks++;
    lastSize = (ReadFast() * 16777216) + (ReadFast() * 65536) + (ReadFast() * 256) + ReadFast();
    //printf("Track %hu/%hu - %lu bytes\n", id + 1, fakeTracks, lastSize);
    unsigned long int offset = 0;
    unsigned long int sz = lastSize;
    tracks[id] = malloc(sz);
    trackSizes[id] = sz;
    while (sz > 0)
    {
        unsigned long int use = bufSize;
        if (sz < bufSize)
        {
            use = sz;
        }
        Copy(tracks[id], offset, use);
        offset += use;
        sz -= use;
    }
    return 0;
}

void progressBar(double progress, unsigned int trackProg) {
    const int barWidth = 50;
    int pos = (int)(progress * barWidth);
    printf("\r[");
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3.0f%% (%u/%u)", progress * 100, trackProg, fakeTracks);
    fflush(stdout);
}

int LoadMIDI(char* path) {
    BufferInit(path, 0, 256); // initial buffer for reading header
    if(TextSearch("MThd")==0){
        perror("Not a valid MIDI file!");
        return 0;
    }
    Skip(4); // header size
    format = ReadFast() * 256 + ReadFast();
    fakeTracks = ReadFast() * 256 + ReadFast();
    ppq = ReadFast() * 256 + ReadFast();
    printf("Format: %i\n",format);
    printf("Tracks: %i\n",fakeTracks);
    printf("PPQ: %i\n",ppq);

    ResizeBuffer(100000000);

    printf("Loading tracks into memory...\n");

    tracks = (unsigned char **)malloc(fakeTracks * sizeof(unsigned char *));
    trackSizes = (unsigned long int *)malloc(fakeTracks * sizeof(unsigned long int));
    unsigned int i = 0;
    
    unsigned int div = (fakeTracks/100);
    if(div == 0) div = 1;
    for (i = 0; i < fakeTracks; i++)
    {
        if((i%div)==0) {
            progressBar((double)(i+1)/(double)fakeTracks, i+1);
        }
        if (CopyTrack(i) == 1)
        {
            break;
        }
    }
    progressBar(1, realTracks);

    realTracks = i;
    tracks = realloc(tracks, realTracks * sizeof(unsigned char *));
    trackSizes = realloc(trackSizes, realTracks * sizeof(unsigned long int));
    printf("\n\nFreeing file buffer...", realTracks);
    free(buffer);
    fclose(midi);

    loaded = TRUE;
    printf("\nSuccessfully loaded!\n\n");
    return 1;
}