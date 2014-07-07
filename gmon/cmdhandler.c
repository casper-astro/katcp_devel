#include <stdio.h>
#include <string.h>

#include "cmdhandler.h"
#include "katpriv.h"
#include "katcl.h"

struct message {
    char *cmd;
    void (*action)(struct gmon_lib *g);
};

static void cmd_fpga(struct gmon_lib *g)
{
    char *arg = NULL;

    arg = arg_string_katcl(g->server, 1);
    
    if (arg) {
        if (!strcmp("down", arg)) {
            printf("FPGA not programmed!\n");
            g->f_status = FPGA_DOWN;
        } else if (!strcmp("ready", arg)) {
            printf("FPGA ready!\n");
            g->f_status = FPGA_READY;
        }
    }
}

static void cmd_log(struct gmon_lib *g)
{
    struct katcl_parse *p = NULL;

    /* route output to STDOUT */
    p = ready_katcl(g->server);
    if (p) {
        append_parse_katcl(g->log, p);
    }
}

static struct message messageLookup[] = {
    {"#fpga", cmd_fpga},
    {KATCP_LOG_INFORM, cmd_log},
    {NULL, NULL}
};

int cmdhandler(struct gmon_lib *g)
{
    char *cmd;
    int i = 0;

    /* get the command */
    cmd = arg_string_katcl(g->server, 0);
    if (cmd) {
        /* itterate through the message lookup list */ 
        while (messageLookup[i].cmd != NULL) {
            if (!strcmp(messageLookup[i].cmd, cmd)) {
                messageLookup[i].action(g);
            }
            i++; 
        }
    }
   
    return 0; 
}

