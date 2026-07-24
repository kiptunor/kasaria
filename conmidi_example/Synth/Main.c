#include "KDMAPI.h"
#include "kasaria.h"




#include <stdio.h>
#include <stdlib.h>

void (*ShortSubmit)(unsigned long int);
void (*SendDirectData)(unsigned long int);
int (*SendDirectLongData)(MIDIHDR* a, unsigned int b);
int (*PrepareLongData)(MIDIHDR* a, unsigned int b);
int (*UnprepareLongData)(MIDIHDR* a, unsigned int b);

void Sound_Init() {
    /*
    if(KDMAPI_Setup()==0){ printf("\n[ERROR] This beta requires OmniMIDI to have functioning audio!\n"); exit(1); };

    KDMAPI_InitializeKDMAPIStream();
    SendDirectData = KDMAPI_SendDirectData;
    SendDirectLongData = KDMAPI_SendDirectLongData;
    PrepareLongData = KDMAPI_PrepareLongData;
    UnprepareLongData = KDMAPI_UnprepareLongData;
    */
    KSR_Init();
    SendDirectData = KSR_SendDirectData;
    SendDirectLongData = KSR_SendDirectDataLong;
    PrepareLongData = KSR_PrepareLongData;
    UnprepareLongData = KSR_UnprepareLongData;
}