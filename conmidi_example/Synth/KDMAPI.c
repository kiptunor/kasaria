#include <stdio.h>

#include "Main.h"

#ifdef _WIN32
    #include <windows.h>
    HMODULE KDMAPI_libHandle;
#else
    #include <dlfcn.h>
    void *KDMAPI_libHandle;
#endif

typedef int(__stdcall *KDM_INIT)();
typedef void(__stdcall *KDM_SEND)(unsigned long int);
typedef int(__stdcall *KDM_LSEND)(MIDIHDR*, unsigned int);
KDM_INIT KDMAPI_InitializeKDMAPIStream;
KDM_INIT KDMAPI_TerminateKDMAPIStream;
KDM_INIT KDMAPI_ResetKDMAPIStream;
KDM_SEND KDMAPI_SendDirectData;
KDM_LSEND KDMAPI_SendDirectLongData;
KDM_LSEND KDMAPI_PrepareLongData;
KDM_LSEND KDMAPI_UnprepareLongData;

#ifdef _WIN32
    int KDMAPI_Setup(){
        if ((KDMAPI_libHandle = LoadLibrary("OmniMIDI")) == NULL){printf("[KDMAPI] LoadLibrary failed!\n");return 0;}
        if ((KDMAPI_InitializeKDMAPIStream = (KDM_INIT)GetProcAddress(KDMAPI_libHandle, "InitializeKDMAPIStream")) == NULL){printf("[KDMAPI] GetProcAddress failed for InitializeKDMAPIStream.\n");return 0;}
        if ((KDMAPI_SendDirectData = (KDM_SEND)GetProcAddress(KDMAPI_libHandle, "SendDirectData")) == NULL){printf("[KDMAPI] GetProcAddress failed for SendDirectData.\n");return 0;}
        if ((KDMAPI_SendDirectLongData = (KDM_LSEND)GetProcAddress(KDMAPI_libHandle, "SendDirectLongData")) == NULL){printf("[KDMAPI] GetProcAddress failed for SendDirectLongData.\n");return 0;}
        if ((KDMAPI_PrepareLongData = (KDM_LSEND)GetProcAddress(KDMAPI_libHandle, "PrepareLongData")) == NULL){printf("[KDMAPI] GetProcAddress failed for PrepareLongData.\n");return 0;}
        if ((KDMAPI_UnprepareLongData = (KDM_LSEND)GetProcAddress(KDMAPI_libHandle, "UnprepareLongData")) == NULL){printf("[KDMAPI] GetProcAddress failed for UnprepareLongData.\n");return 0;}
        if ((KDMAPI_TerminateKDMAPIStream = (KDM_INIT)GetProcAddress(KDMAPI_libHandle, "TerminateKDMAPIStream")) == NULL){printf("[KDMAPI] GetProcAddress failed for TerminateKDMAPIStream.\n");return 0;}
        if ((KDMAPI_ResetKDMAPIStream = (KDM_INIT)GetProcAddress(KDMAPI_libHandle, "ResetKDMAPIStream")) == NULL){printf("[KDMAPI] GetProcAddress failed for ResetKDMAPIStream.\n");return 0;}
        printf("[KDMAPI] Functions loaded!\n");
        return 1;
    }
#else
    int KDMAPI_Setup(){
        if ((KDMAPI_libHandle = dlopen("libOmniMIDI.so", RTLD_LAZY)) == NULL){printf("[KDMAPI] dlopen failed!\n%s\n", dlerror());return 0;}
        if ((KDMAPI_InitializeKDMAPIStream = (KDM_INIT)dlsym(KDMAPI_libHandle, "InitializeKDMAPIStream")) == NULL){printf("[KDMAPI] GetProcAddress failed for InitializeKDMAPIStream.\n%s\n", dlerror());return 0;}
        if ((KDMAPI_SendDirectData = (KDM_SEND)dlsym(KDMAPI_libHandle, "SendDirectData")) == NULL){printf("[KDMAPI] dlsym failed for SendDirectData.\n%s\n", dlerror());return 0;}
        if ((KDMAPI_SendDirectLongData = (KDM_LSEND)dlsym(KDMAPI_libHandle, "SendDirectLongData")) == NULL){printf("[KDMAPI] GetProcAddress failed for SendDirectLongData.\n%s\n", dlerror());return 0;}
        if ((KDMAPI_PrepareLongData = (KDM_LSEND)dlsym(KDMAPI_libHandle, "PrepareLongData")) == NULL){printf("[KDMAPI] GetProcAddress failed for PrepareLongData.\n%s\n", dlerror());return 0;}
        if ((KDMAPI_UnprepareLongData = (KDM_LSEND)dlsym(KDMAPI_libHandle, "UnprepareLongData")) == NULL){printf("[KDMAPI] GetProcAddress failed for UnprepareLongData.\n%s\n", dlerror());return 0;}
        if ((KDMAPI_TerminateKDMAPIStream = (KDM_INIT)dlsym(KDMAPI_libHandle, "TerminateKDMAPIStream")) == NULL){printf("[KDMAPI] GetProcAddress failed for TerminateKDMAPIStream.\n%s\n", dlerror());return 0;}
        if ((KDMAPI_ResetKDMAPIStream = (KDM_INIT)dlsym(KDMAPI_libHandle, "ResetKDMAPIStream")) == NULL){printf("[KDMAPI] GetProcAddress failed for ResetKDMAPIStream.\n%s\n", dlerror());return 0;}
        printf("[KDMAPI] Functions loaded!\n");
        return 1;
    }
#endif