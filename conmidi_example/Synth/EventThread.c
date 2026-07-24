#include "EventThread.h"
#include "../Essentials.h"
#include "Main.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdbool.h>

int RUN = 1;
unsigned long int writes = 0;
unsigned long int eventBuffer[BUFFER_SIZE];
unsigned long int* initialPtr2 = &eventBuffer[0];
unsigned long int* endPtr2 = &eventBuffer[BUFFER_SIZE];
volatile unsigned long int* writePtr = &eventBuffer[0];

static bool running = true;

#ifdef _WIN32
    static DWORD WINAPI EventThreadFunc(LPVOID param)
#else
    static void* EventThreadFunc(void* param)
#endif
{
    unsigned long int* readPtr = &eventBuffer[0];
    unsigned long int* endPtr = &eventBuffer[BUFFER_SIZE];
    unsigned long int* initialPtr = readPtr;
    while (running) {
        while (readPtr != writePtr) {
            SendDirectData(*(readPtr++));
            if(readPtr == endPtr)
                readPtr = initialPtr;
        }
        uniSleep(1);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

void EventThread_Init() {
    initialPtr2 = &eventBuffer[0];
    writePtr = initialPtr2;
#ifdef _WIN32
    CreateThread(NULL, 0, EventThreadFunc, NULL, 0, NULL);
#else
    pthread_t threadId;
    pthread_create(&threadId, NULL, EventThreadFunc, NULL);
#endif
}

void EventThread_Shutdown() {
    running = false;

}
