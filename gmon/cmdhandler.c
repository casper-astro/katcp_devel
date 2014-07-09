#include <stdio.h>
#include <string.h>

#include "gmon.h"
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
            g->g_status = GMON_FPGA_DOWN;
            log_message_katcl(g->log, KATCP_LEVEL_INFO, GMON_PROG,
                                "fpga down", NULL);
        } else if (!strcmp("ready", arg)) {
            g->g_status = GMON_FPGA_READY; 
            log_message_katcl(g->log, KATCP_LEVEL_INFO, GMON_PROG,
                                "fpga ready", NULL);
        }
        //log_message_katcl(g->log, KATCP_LEVEL_INFO, GMON_PROG,
        //        "fpga status is %s", fpga_status_string(g->f_status)); 
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

static void cmd_meta(struct gmon_lib *g)
{
    unsigned int argcount = 0;
    unsigned int i = 0;
    char *arg = NULL;

    argcount = arg_count_katcl(g->server);

    for (i = 1; i <= argcount; i++) {
        arg = arg_string_katcl(g->server, i);
        printf("arg %d : %s\n", i, arg);
    }
}

static struct message messageLookup[] = {
    {KATCP_LOG_INFORM, cmd_log},
    {"#fpga", cmd_fpga},
    {"#meta", cmd_meta},
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

