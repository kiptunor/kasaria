#include <string.h>
#include "../BufferFile.h"
#include "DataStorage.h"
#include "../Playback/MainPlayer.h"
#include "../Essentials.h"
#include "../Sound/kasaria.h"


long long int lastPos = 0;
unsigned long int lastSize = 0;
double lastPrint = 0;

int TextSearch(char text[])
{
    unsigned char *str = ReadRange(strlen(text));
    int res = strcmp(str, text);
    free(str);
    return 1 - res;
}
char CopyTrack(unsigned long int id)
{
    if(lastPos != 0)
        Seek(lastPos + lastSize + 8);
    
    lastPos = filePos + bufPos;
    if(TextSearch("MTrk") != 1)
    {
        // printf("\nTextSearch failed at %lu",filePos+bufPos);
        return 1;
    }
    realTracks++;
    double t = getTimeMsec();
    lastSize = (ReadFast() * 16777216) + (ReadFast() * 65536) + (ReadFast() * 256) + ReadFast();
    if(id + 1 == fakeTracks || t - lastPrint > 250)
    {
        printf("\nTrack %hu / %hu | Size %lu", id + 1, fakeTracks, lastSize);
        lastPrint = t;
    }
    unsigned long int offset = 0;
    unsigned long int sz = lastSize;
    tracks[id] = malloc(sz);
    while(sz > 0)
    {
        unsigned long int use = bufSize;
        if(sz < bufSize)
            use = sz;
        
        Copy(tracks[id], offset, use);
        offset += use;
        sz -= use;
    }
    return 0;
}

void LoadMIDI(char path[], unsigned int bs)
{
    BufferInit(path, 0, bs);
    if(TextSearch("MThd") == 0)
        error("MIDI header not found");
    
    Skip(4);
    format = ReadFast() * 256 + ReadFast();
    fakeTracks = ReadFast() * 256 + ReadFast();
    ppq = ReadFast() * 256 + ReadFast();
    printf("\nFormat: %d", format);
    printf("\nExpected Track Count: %d", fakeTracks);
    printf("\nPPQ: %d", ppq);
    ResizeBuffer(100000000);
    printf("\nCopying tracks to memory...");
    tracks = (unsigned char **)malloc(fakeTracks * sizeof(unsigned char *));
    int i = 0;
    for(i = 0; i < fakeTracks; i++)
        if(CopyTrack(i) == 1)
            break;
        
    
    realTracks = i;
    tracks = realloc(tracks, realTracks * sizeof(unsigned char *));
    printf("\nLoaded %d tracks", realTracks);
    free(buffer);
    fclose(midi);
    printf("\nBeginning playback...\n");
    KSR_CreateAudioThread();
    StartPlayback();
}
