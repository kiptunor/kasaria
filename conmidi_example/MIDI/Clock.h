#ifndef CLOCK_H
#define CLOCK_H

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#endif

extern char* prgTitle;
extern unsigned int cppq;
extern double bpm;

double getTimeMsec(void);
void Clock_Start();
void Clock_SubmitBPM(double pos, unsigned long int b);
double Clock_GetTick();

#endif
