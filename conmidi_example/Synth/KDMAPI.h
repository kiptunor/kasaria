#ifndef KDMAPI_H
#define KDMAPI_H

#include "Main.h"

typedef int (*KDM_INIT)();
typedef void (*KDM_SEND)(unsigned long int);
typedef int (*KDM_LSEND)(MIDIHDR*, unsigned int);

extern KDM_INIT KDMAPI_InitializeKDMAPIStream;
extern KDM_INIT KDMAPI_TerminateKDMAPIStream;
extern KDM_INIT KDMAPI_ResetKDMAPIStream;
extern KDM_SEND KDMAPI_SendDirectData;
extern KDM_LSEND KDMAPI_SendDirectLongData;
extern KDM_LSEND KDMAPI_PrepareLongData;
extern KDM_LSEND KDMAPI_UnprepareLongData;

int KDMAPI_Setup();

#endif