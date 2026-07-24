#include <stdio.h>
#include <unistd.h>
#include "Essentials.h"
#include "Sound/Sound.h"
#include "MIDI/LoadMIDI.h"
#include "Playback/MIDIClock.h"
#include "Playback/MainPlayer.h"
#define FALSE 0
#define TRUE 1

FILE *file_ptr;
char version[] = "v2.0.9";
char *title;
int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        int mode = 0;
        for (int i = 1; i < argc; i++)
        {
            switch (mode)
            {
            case 0:
            {
                char *read = argv[i];
                if (strcmp(read, "-cp") == 0)
                {
                    mode = 1;
                }
                else if (strcmp(read, "-hm") == 0 || strcmp(read, "-hidemeta") == 0)
                {
                    for (int a = 0; a < 10; a++)
                    {
                        metaAllow[a] = FALSE;
                    }
                }
                else if (strcmp(read, "-sm") == 0 || strcmp(read, "-showmeta") == 0)
                {
                    for (int a = 0; a < 10; a++)
                    {
                        metaAllow[a] = TRUE;
                    }
                }
                else if (strcmp(read, "-em") == 0)
                {
                    mode = 2;
                }
                else if (strcmp(read, "-dm") == 0)
                {
                    mode = 3;
                }
                else if (strcmp(read, "-fps") == 0)
                {
                    showFpsOutsideLag = TRUE;
                }
                break;
            }
            case 1:
            {
                unsigned int b;
                sscanf(argv[i], "%u", &b);
                mode = 0;
                break;
            }
            case 2:
            {
                unsigned int b;
                sscanf(argv[i], "%u", &b);
                if (b > 9)
                {
                    printf("\nCommand line error, -em parameter above 9 (set to %u)", b);
                    exit(0);
                }
                metaAllow[b] = TRUE;
                mode = 0;
                break;
            }
            case 3:
            {
                unsigned int b;
                sscanf(argv[i], "%u", &b);
                if (b > 9)
                {
                    printf("\nCommand line error, -dm parameter above 9 (set to %u)", b);
                    exit(0);
                }
                metaAllow[b] = FALSE;
                mode = 0;
                break;
            }
            }
        }
    }
    printf("ConMIDI %s\n\n", version);
    title = concat("ConMIDI ", version);
    prgTitle = title;
    // Check for sound engines
    //Sound_Setup();
    // Start sound selection if more than one sound engine is available otherwise auto load only available engine

    Sound_Init();

    // File path input
    char path[260];
    char fixedPath[260];
    /*
    while(TRUE)
    {

        memset(path, 0, sizeof(path));
        printf("\nEnter file path: ");
        scanf("%260[^\n]", path);
        fflush(stdin);
        
        removeSymbol(path, '\"', fixedPath);
        if(access(fixedPath, F_OK) != -1)
        {
            break;
        }
        else
        {
            printf("Invalid path");
            memset(path, 0, sizeof(path));
        }
    }
    */
    
    unsigned int bufSize = 64;
    printf("\nLoading MIDI...");
    LoadMIDI(argv[1], bufSize);
    return 0;
}
