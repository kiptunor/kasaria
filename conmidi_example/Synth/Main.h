#ifndef SYNTH
#define SYNTH

#ifdef _WIN32
    #include <windows.h>
#else
    typedef struct {
        unsigned char* lpData;
        unsigned int dwBufferLength;
        unsigned int dwBytesRecorded;
        void* dwUser;
        unsigned int dwFlags;
    } MIDIHDR;
#endif

void Sound_Init();

extern void (*ShortSubmit)(unsigned long int);
extern void (*SendDirectData)(unsigned long int);
extern int (*SendDirectLongData)(MIDIHDR* a, unsigned int b);
extern int (*PrepareLongData)(MIDIHDR* a, unsigned int b);
extern int (*UnprepareLongData)(MIDIHDR* a, unsigned int b);

#endif