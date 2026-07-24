#include "Essentials.h"
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
    void setConsoleTitle(const char* title) {
        SetConsoleTitleA(title);
    }
    void uniSleep(unsigned long int duration)
    {
        Sleep(duration);
    }
#elif __linux__
    #include <unistd.h>
    void setConsoleTitle(const char* title) {
        printf("\033]0;%s\007", title);
        fflush(stdout);
    }
    void uniSleep(unsigned long int duration)
    {
        sleep(duration*1000);
    }
#else
    void setConsoleTitle(const char* title) {
        print("[WARN] Cannot set console title on this OS!\n")
    }
    void uniSleep(unsigned long int duration) {}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void generateTitle() {
    snprintf(title, 256, "ConMIDI "); // everything except OS is defined in xmake
}

static int count_digits(unsigned long long n) {
    int digits = 1;
    while (n >= 10) {
        n /= 10;
        digits++;
    }
    return digits;
}

char* AddCommas(unsigned long long num) {
    int digits = count_digits(num);
    int commas = (digits - 1) / 3;
    int len_with_commas = digits + commas;

    char* result = (char*)malloc(len_with_commas + 1);

    result[len_with_commas] = '\0';

    int i = digits - 1;
    int j = len_with_commas - 1;
    int k = 0;

    while (i >= 0) {
        unsigned long long digit = num % 10;
        num /= 10;
        result[j--] = '0' + (char)digit;
        k++;
        if (k == 3 && i != 0) {
            result[j--] = ',';
            k = 0;
        }
        i--;
    }

    return result;
}

void removeSymbol(char text[], char symbol, char* clean){
    int i,j;
    for(i = 0, j = 0; i < strlen(text); i++){
        if(text[i] != symbol){
            clean[j++] = text[i];
        }
    }
    clean[j] = '\0';
}