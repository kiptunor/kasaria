#ifndef SOUND_H
#define SOUND_H

typedef struct {
    unsigned char *lpData;
    unsigned int dwBytesRecorded;
    char data[1024];  // Placeholder for MIDIHDR equivalent on Linux
} MIDIHDR;

extern void (*SendDirectDataPtr)(unsigned long int a);
extern int (*SendDirectLongDataPtr)(MIDIHDR *a, unsigned int b);
extern int (*PrepareLongDataPtr)(MIDIHDR *a, unsigned int b);
extern int (*UnprepareLongDataPtr)(MIDIHDR *a, unsigned int b);

extern int usable;
void Sound_Setup();
int Sound_Init();

#endif