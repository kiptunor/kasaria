#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "Essentials.h"










char ntdllAvailable = 0;
signed long long start = 0;







int intInput(char *text)
{
    int num;
    printf(text);
    scanf("%d", &num);
    fflush(stdin);
    return num;
}
void removeSymbol(char text[], char symbol, char *clean)
{
    int i, j;
    for (i = 0, j = 0; i < strlen(text); i++)
    {
        if (text[i] != symbol)
        {
            clean[j++] = text[i];
        }
    }
    clean[j] = '\0';
}
void error(char text[])
{
    printf("\n[ERROR] %s\n", text);
    exit(0);
}
char *concat(const char *str1, const char *str2)
{
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    char *result = malloc(len1 + len2 + 1);
    if(result == NULL)
        return NULL;
    
    strcpy(result, str1);
    strcat(result, str2);
    return result;
}
double getTimeMsec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}
