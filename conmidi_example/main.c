#include <stdio.h>

//REMOVE FOR LINUX PORTING
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif


#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

#include "Essentials.h"
#include "Synth/Main.h"
#include "MIDI/Parser.h"
#include "MIDI/Player.h"


char title[256];

int main(int argc, char** argv) {
    generateTitle();

    printf("%s\n\n", title);
    setConsoleTitle(title);

    printf("Initializing synth functions...\n");
    Sound_Init();

    /*
    char path[260];
    char fixedPath[260];
    while(TRUE){
        printf("\nSelect a MIDI\n");
        OPENFILENAME ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "MIDI Files\0*.mid\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = sizeof(path);
        ofn.lpstrTitle = "Select a MIDI file";
        ofn.Flags = OFN_DONTADDTORECENT | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        path[0] = '\0';

        if(!GetOpenFileName(&ofn)){
            printf("No file selected.\n");
            //int ch;
            //while ((ch = getchar()) != '\n' && ch != EOF);
            printf("\nEnter file path: ");
            if (fgets(path, sizeof(path), stdin)) {
                // Remove newline if present
                size_t len = strlen(path);
                if (len > 0 && path[len - 1] == '\n') {
                    path[len - 1] = '\0';
                }
            }
        }
        removeSymbol(path,'\"',fixedPath);
        if(access(fixedPath, 0) != -1){
            break;
        } else {
            printf("Invalid path\n");
            memset(path, 0, sizeof(path));
        }
    }
*/
    printf("\nLoading MIDI...\n");
    LoadMIDI(argv[1]);

    PlayMIDI();

    return 0;
}