#include <stdio.h>
#include <dlfcn.h>
#include "KDMAPI.h"
#include "Sound.h"

void *KDMAPI_libHandle;
KDM_INIT KDMAPI_InitializeKDMAPIStream;
KDM_INIT KDMAPI_TerminateKDMAPIStream;
KDM_INIT KDMAPI_ResetKDMAPIStream;
KDM_SEND KDMAPI_SendDirectData;
KDM_LSEND KDMAPI_SendDirectLongData;
KDM_LSEND KDMAPI_PrepareLongData;
KDM_LSEND KDMAPI_UnprepareLongData;

int KDMAPI_Setup()
{
    // Try to load the KDMAPI library (using .so extension for Linux)
    if ((KDMAPI_libHandle = dlopen("libOmniMIDI.so", RTLD_LAZY)) == NULL)
    {
        printf("KDMAPI not available: %s\n", dlerror());
        return 0;
    }

    // Get function pointers using dlsym instead of GetProcAddress
    if ((KDMAPI_InitializeKDMAPIStream = (KDM_INIT)dlsym(KDMAPI_libHandle, "InitializeKDMAPIStream")) == NULL)
    {
        printf("dlsym failed for KDMAPI InitializeKDMAPIStream: %s\n", dlerror());
        return 0;
    }
    
    if ((KDMAPI_SendDirectData = (KDM_SEND)dlsym(KDMAPI_libHandle, "SendDirectData")) == NULL)
    {
        printf("dlsym failed for KDMAPI SendDirectData: %s\n", dlerror());
        return 0;
    }

    
    printf("KDMAPI FUNCTIONAL\n");
    return 1;
}