#include <stdio.h>
#include <string.h>

#include "cmdhandler.h"
#include "katpriv.h"
#include "katcl.h"

struct message {
    char *cmd;
    void (*action)(struct katcl_line *l, struct katcl_line *k);
};

static void cmd_test(struct katcl_line *l, struct katcl_line *k)
{
    char *arg = NULL;

    arg = arg_string_katcl(l, 1);
    
    if (arg) {
        if (!strcmp("down", arg)) {
            printf("FPGA not programmed!\n");
        } else if (!strcmp("ready", arg)) {
            printf("FPGA ready!\n");
        }
    }
}

static void cmd_log(struct katcl_line *l, struct katcl_line *k)
{
    struct katcl_parse *p = NULL;

    /* route output to STDOUT */
    p = ready_katcl(l);
    if (p) {
        append_parse_katcl(k, p);
    }
}

static struct message messageLookup[] = {
    {"#fpga", cmd_test},
    {KATCP_LOG_INFORM, cmd_log},
    {NULL, NULL}
};

int cmdhandler(struct katcl_line *l, struct katcl_line *k)
{
    char *cmd;
    int i = 0;

    /* get the command */
    cmd = arg_string_katcl(l, 0);
    if (cmd) {
        /* itterate through the message lookup list */ 
        while (messageLookup[i].cmd != NULL) {
            if (!strcmp(messageLookup[i].cmd, cmd)) {
                messageLookup[i].action(l, k);
            }
            i++; 
        }
    }
   
    return 0; 
}

