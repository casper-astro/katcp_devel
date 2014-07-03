#include <stdio.h>
#include <string.h>
#include "cmdhandler.h"

struct message {
    char *cmd;
    char *arg;
    void (*action)(void);
};

static void cmd_test(void)
{
    printf("cmd_test() called!\n");
}

static struct message messageLookup[] = {
    {"#fpga", "down", cmd_test},
    {NULL, NULL, NULL}
};

int cmdhandler(char *cmd, char *arg)
{
    int i = 0;

    /* itterate through the message lookup list */ 
    while (messageLookup[i].cmd != NULL) {
        if (!strcmp(messageLookup[i].cmd, cmd)) {
            if (!strcmp(messageLookup[i].arg, arg)) {
                messageLookup[i].action();
            }
        }
        i++; 
    }
   
    return 0; 
}

