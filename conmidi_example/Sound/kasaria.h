
#include "Sound.h"



void KSR_CreateAudioThread();

void KSR_Init();
void KSR_Shutdown();
void KSR_SendDirectData(unsigned long int data);
int KSR_SendDirectDataLong(MIDIHDR *mid_ev, unsigned int size);
int KSR_PrepareLongData(MIDIHDR *mid_hdr, unsigned int size);
int KSR_UnprepareLongData(MIDIHDR *mid_hdr, unsigned int size);