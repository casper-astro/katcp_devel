#include <stdio.h>
#include <string.h>
#include "parser.h"

struct message {
    char *commandString;
    void (*action)(void);
};

static void parse_test(void)
{
    printf("parse_test() called!\n");
}

static struct message messageLookup[] = {
    {"!fpgastatus", parse_test},
    {NULL, NULL}
};

int parser(char *str)
{
    char *substring;
    int i = 0;

    /* parse the newline characters to form substrings */
    substring = strtok(str, "\n");
    while (substring != NULL) {
        i = 0;
        printf("substring: %s\n", substring);
        /* itterate through the message lookup list to find commandstrings */ 
        while (messageLookup[i].commandString != NULL) {
            /* find the needle in the haystack */
            if (strstr(substring, messageLookup[i].commandString)) {
                messageLookup[i].action();
            }
            i++; 
        }
        substring = strtok(NULL, "\n");
    }
    
    return 0;
}
