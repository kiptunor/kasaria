#ifndef EVENTTHREAD_H
#define EVENTTHREAD_H

#include <stdint.h>

#define BUFFER_SIZE 2097152

extern unsigned long int eventBuffer[BUFFER_SIZE];
extern unsigned long int* initialPtr2;
extern unsigned long int* endPtr2;
extern volatile unsigned long int* writePtr;
extern unsigned long int writes;
extern int RUN;

void EventThread_Init();

inline void EventThread_QueueEvent(unsigned long int event) {
    if(++writes == BUFFER_SIZE) { RUN = 0; writes = 0; return; }
    *(writePtr++) = event;
    if(writePtr == endPtr2)
        writePtr = initialPtr2;
}

void EventThread_Shutdown();

#endif