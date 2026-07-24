#ifndef ESSENTIALS
#define ESSENTIALS

#if defined(_WIN32)
    #define WINDOWS 1
#else
    #define WINDOWS 0
#endif
#if defined(__linux__)
    #define LINUX 1
#else
    #define LINUX 0
#endif
#if defined(__APPLE__)
    #define MACOS 1
#else
    #define MACOS 0
#endif

#if defined(_WIN64)
    #define OS "Win64"
#elif defined(_WIN32)
    #define OS "Win32"
#elif defined(__linux__)
    #if defined(__x86_64__) || defined(__aarch64__)
        #define OS "Linux64"
    #else
        #define OS "Linux32"
    #endif
#elif defined(__APPLE__)
    #if defined(__x86_64__) || defined(__aarch64__)
        #define OS "MacOS64"
    #else
        #define OS "MacOS32"
    #endif
#else
    #define OS "Unknown"
#endif

typedef unsigned char byte;
#define MIDIERR_STILLPLAYING 65
#define LPBYTE unsigned char

void setConsoleTitle(const char* title);

char* AddCommas(unsigned long long num);

void uniSleep(unsigned long int duration);

void generateTitle();

void removeSymbol(char text[], char symbol, char* clean);

extern char title[256];

#endif