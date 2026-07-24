#ifndef BUFFERFILE_H
#define BUFFERFILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern FILE* midi;
extern char* buffer;

extern unsigned long int bufSize;
extern long long bufPos;
extern long long filePos;

void BufferInit(char path[], unsigned long seek, unsigned int bufSizee);

void Seek(long long pos);
void Skip(unsigned long int count);
void ResizeBuffer(unsigned long int size);
unsigned char ReadFast();
unsigned char* ReadRange(int size);
void Copy(unsigned char *target, unsigned long int offset, unsigned long int size);

#endif