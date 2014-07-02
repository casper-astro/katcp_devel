#include <stdio.h>
#include <string.h>
#include "parser.h"

struct message {
    char *commandString;
    void (*action)(void);
};

struct message messageLookup[10];

/*
struct message *parser_register(char *cmdStr, void (*action)(void))
{
      struct message *msg = malloc
}
*/

void parse_test(void)
{
    printf("parse_test() called!\n");
}

int parser_init(void)
{
    memset(messageLookup, 0, sizeof(messageLookup));
    
    messageLookup[0].commandString = "!fpgastatus";
    messageLookup[0].action = parse_test;
  
    return 0;  
}

int parser(char *str)
{
    char *substring;
    int i = 0;

    substring = strtok(str, "\n");
    while (substring != NULL) {
        printf("substring: %s\n", substring); 
        for (i = 0; i < 10; i++) {
            if (messageLookup[i].commandString != NULL) {
                if (strstr(substring, messageLookup[i].commandString)) {
                    messageLookup[i].action();
                } 
            } else {
                break;
            }    
        }
        substring = strtok(NULL, "\n");
    }
    
    return 0;
}
