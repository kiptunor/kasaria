#include <stdio.h>
#include "KDMAPI.h"
#include "Sound.h"
#include "kasaria.h"

void (*SendDirectDataPtr)(unsigned long int a);
int (*SendDirectLongDataPtr)(MIDIHDR *a, unsigned int b);
int (*PrepareLongDataPtr)(MIDIHDR *a, unsigned int b);
int (*UnprepareLongDataPtr)(MIDIHDR *a, unsigned int b);

int usable = 0;

void Sound_Setup()
{
    if (KDMAPI_Setup() == 1)
    {
        usable = 1;
        printf("KDMAPI available\n");
    }
    else
    {
        printf("No sound system available\n");
    }
}

int Sound_Init()
{
    

    KSR_Init();
    SendDirectDataPtr = KSR_SendDirectData;
    SendDirectLongDataPtr = KSR_SendDirectDataLong;
    PrepareLongDataPtr = KSR_PrepareLongData;
    UnprepareLongDataPtr = KSR_UnprepareLongData;


/*
if (usable)
   {
       int res = KDMAPI_InitializeKDMAPIStream();
       if (res == 1)
       {
           printf("KDMAPI Initialized\n");
           SendDirectDataPtr = KDMAPI_SendDirectData;
           SendDirectLongDataPtr = KDMAPI_SendDirectLongData;
           PrepareLongDataPtr = KDMAPI_PrepareLongData;
           UnprepareLongDataPtr = KDMAPI_UnprepareLongData;
           return 1;
       }
       else
       {
           printf("KDMAPI failed to initialize\n");
           return 0;
       }
   }
   else
   {
       printf("KDMAPI is not available\n");
       return 0;
   }
*/
    return 0;
}