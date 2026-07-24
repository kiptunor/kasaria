#ifndef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif
#include "MIDIData.h"
#include "../Synth/kasaria.h"
#include "Clock.h"
#include "../Synth/Main.h"
#include "../Essentials.h"
#include "../Synth/EventThread.h"

double startTime1 = 0;

unsigned long long playedNotes = 0;
unsigned long long playedEvents = 0;

void StartTimeCheck()
{
    double tempT = clock();
    if((long)(tempT-startTime1)>=16){
        char temp[256];
        char* format1 = AddCommas(playedNotes);
        char* format2 = AddCommas(playedEvents);
        snprintf(temp, 256, "%s | Notes: %s | Events: %s | BPM: %.10lg", title, format1, format2, bpm);
        setConsoleTitle(temp);
        free(format1);
        free(format2);
    }
}

unsigned char *tR;

void handleSysEx(){
    unsigned int size = 64;
    unsigned char* arr=(unsigned char*)malloc(size);
    int pos = 1;
    arr[0] = 0xF0;
    //tR++;
    while((arr[pos]=*(tR++))!=0b11110111){
        pos++;
        if(pos>=size){
            size *= 2;
            arr = (unsigned char*)realloc(arr, size);
        }
    }
    MIDIHDR longdata;
    memset(&longdata, 0, sizeof(longdata));
    longdata.lpData = (LPBYTE*)&arr[0];
    longdata.dwBufferLength = pos+1;
    longdata.dwBytesRecorded = pos+1;
    longdata.dwFlags = 0;
    int error = PrepareLongData(&longdata,sizeof(longdata));
    if(!error){
        error = SendDirectLongData(&longdata,sizeof(longdata));
        if(error){
            printf("Could not play SysEx, error %d\n", error);
        }
        while(MIDIERR_STILLPLAYING == UnprepareLongData(&longdata,sizeof(longdata))){
            printf("Failed to unprepare SysEx, retrying...\n");
        }
    } else {
        printf("Failed to prepare SysEx\n");
    }
    free(arr);
}

void PlayMIDI()
{
    printf("Playing MIDI...\n");
    KSR_CreateAudioThread();
    if(!loaded) {
        perror("Cannot play while no MIDI is loaded!");
        return;
    }

    EventThread_Init();

    double Clock = 0;

    unsigned int aliveTracks = realTracks;

    startTime1 = clock();
    BOOL *trackFinished = (BOOL *)calloc(realTracks, sizeof(BOOL));
    byte *prevEvent = (byte *)calloc(realTracks, sizeof(byte));
    unsigned long long *trackPosition = (unsigned long long *)calloc(realTracks, sizeof(unsigned long long));
    unsigned char **trackReader = (unsigned char **)malloc(realTracks * sizeof(unsigned char *));
    for (unsigned int i = 0; i < realTracks; i++)
    {
        trackReader[i] = &tracks[i][0];
    }

    cppq = ppq;
    Clock_Start();
    while(TRUE)
    {
        //printf("Send events\n");
        StartTimeCheck();
        double newClock = Clock_GetTick();
        if(newClock == Clock) { uniSleep(1); continue; }
        unsigned long long clockUInt64 = (unsigned long long)Clock;
        Clock = newClock;

        unsigned long long *tPos = &trackPosition[0];
        unsigned long int *tSize = &trackSizes[0];
        BOOL *tF1 = &trackFinished[0];
        byte *prevE = &prevEvent[0];

        unsigned int curTracks = aliveTracks;

        RUN = 1;
        writes = 0;
        for (unsigned int i = 0; i < curTracks; i++)
        {
            if(*tF1) { tF1++; prevE++; tPos++; tSize++; continue; }
            tR = trackReader[i];
            unsigned char *trackBounds = tracks[i] + *tSize;
            unsigned long long tempPos = *tPos;
            byte tempPrev = *prevE;
            BOOL doloop = TRUE;
            while(doloop)
            {
                //printf("[Track %u] pos=%llu clock=%llu event=0x%02X\n", i, tempPos, clockUInt64, prevEvent);
                if(tR == trackBounds) {
                    printf("[WARN] Track %u abruptly ended with no End of Track event!\n",i+1);
                    *tF1 = TRUE;
                    aliveTracks--;
                    break;
                }
                unsigned char* startPos = tR;
                unsigned long val = 0;
                unsigned char temp = 0;

                do {
                    temp = *(tR++);
                    val = (val << 7) | (temp & 0x7F);
                } while (temp & 0x80);

                tempPos += val;
                if (tempPos > clockUInt64) {
                    tR = startPos;
                    tempPos -= val;
                    break;
                }

                byte readEvent = *(tR++);
                if (readEvent < 0x80)
                {
                    tR--;
                    readEvent = tempPrev;
                }
                tempPrev = readEvent;
                int trackEvent = readEvent & 0b11110000;
                if(trackEvent == 0x90)
                {
                    playedNotes++; playedEvents++;
                    byte note = *(tR++);
                    byte vel = *(tR++);
                    if(RUN)
                        EventThread_QueueEvent(readEvent | (note << 8) | (vel << 16));
                } else if(trackEvent == 0x80)
                {
                    playedEvents++;
                    byte note = *(tR++);
                    byte vel = *(tR++);
                    if(RUN)
                        EventThread_QueueEvent(readEvent | (note << 8) | (vel << 16));
                } else if(trackEvent == 0xA0 || trackEvent == 0xE0 || trackEvent == 0xB0)
                {
                    playedEvents++;
                    byte note = *(tR++);
                    byte vel = *(tR++);
                    SendDirectData(readEvent | (note << 8) | (vel << 16));
                } else if(trackEvent == 0xC0 || trackEvent == 0xD0)
                {
                    playedEvents++;
                    SendDirectData((readEvent | (*(tR++) << 8)));
                } else if(trackEvent == 0)
                {
                    break;
                } else {
                    switch (readEvent)
                    {
                        case 0b11110000: {
                            handleSysEx();
                            break;
                        }
                        case 0b11110010:
                            tR += 2;
                            break;
                        case 0b11110011:
                            tR++;
                            break;
                        case 0xF7: {
                            handleSysEx(trackBounds);
                            break;
                        }
                        case 0x2F:
                        {
                            printf("[Track %u] End of Track at pos=%llu\n", i+1, tempPos);
                            break;
                        }
                        case 0xFF: {
                            readEvent = *(tR++);
                            switch(readEvent){
                                case 0x51:
                                {
                                    tR++;
                                    unsigned long int event = 0;
                                    for (int i = 0; i != 3; i++) {
                                        byte temp = *(tR++);
                                        event = (event << 8) | temp;
                                    }
                                    Clock_SubmitBPM(tempPos, event);
                                    break;
                                }
                                case 0x2F:
                                {
                                    doloop = FALSE;
                                    *tF1 = TRUE;
                                    aliveTracks--;
                                    break;
                                }
                                default:
                                {
                                    tR += *(tR++);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            *tPos = tempPos;
            *prevE = tempPrev;
            trackReader[i] = tR;

            tF1++; prevE++; tPos++; tSize++;
        }

        if(aliveTracks != curTracks)
        {
            //printf("Unloading %u dead tracks, %u remain.\n",(curTracks-aliveTracks),aliveTracks);

            BOOL *trackFinished_new = (BOOL *)malloc(aliveTracks * sizeof(BOOL));
            byte *prevEvent_new = (byte *)malloc(aliveTracks * sizeof(byte));
            unsigned long long *trackPosition_new = (unsigned long long *)malloc(aliveTracks * sizeof(unsigned long long));
            unsigned char **trackReader_new = (unsigned char **)malloc(aliveTracks * sizeof(unsigned char *));
            unsigned char **tracks_new = (unsigned char **)malloc(aliveTracks * sizeof(unsigned char *));
            unsigned long int *trackSizes_new = (unsigned long int *)malloc(aliveTracks * sizeof(unsigned long int));
            unsigned int i2 = 0;
            for (unsigned int i = 0; i < curTracks; i++)
            {
                if(trackFinished[i])
                {
                    free(tracks[i]);
                    tracks[i] = NULL;
                } else {
                    trackReader_new[i2] = trackReader[i];
                    trackPosition_new[i2] = trackPosition[i];
                    prevEvent_new[i2] = prevEvent[i];
                    trackFinished_new[i2] = trackFinished[i];
                    tracks_new[i2] = tracks[i];
                    trackSizes_new[i2] = trackSizes[i];
                    i2++;
                }
            }

            free(trackReader);
            free(trackPosition);
            free(prevEvent);
            free(trackFinished);
            free(tracks);
            free(trackSizes);

            trackReader = trackReader_new;
            trackPosition = trackPosition_new;
            prevEvent = prevEvent_new;
            trackFinished = trackFinished_new;
            tracks = tracks_new;
            trackSizes = trackSizes_new;
        }

        if(aliveTracks == 0)
        {
            printf("\nRan out of tracks, playback finished.\n");
            uniSleep(3000);
            exit(0);
        }
    }
}