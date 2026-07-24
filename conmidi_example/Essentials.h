#ifndef ESSENTIALS_H
#define ESSENTIALS_H

#include <stdlib.h>
#include <string.h>

int intInput(char* text);
void removeSymbol(char text[], char symbol, char* clean);
void error(char text[]);
char* concat(const char* str1, const char* str2);
double getTimeMsec(void);

#endif
