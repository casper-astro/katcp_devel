#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "gmon.h"
#include "cmdhandler.h"
#include "katpriv.h"
#include "katcl.h"
#include "sensor.h"

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
            g->state = GMON_FPGA_DOWN;
            /* free sensor resources */
            gmon_destroy(g);
            log_message_katcl(g->log, KATCP_LEVEL_INFO, GMON_PROG,
                                "fpga down", NULL);
        } else if (!strcmp("ready", arg)) {
            g->state = GMON_FPGA_READY; 
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

static void cmd_listdev(struct gmon_lib *g)
{
    char *reg = NULL;
    char *description = "Temporary description";
    struct sensor *regsensor = NULL;
    struct sensor **tmpsensorlist = NULL;

    /* obtain the register name */
    reg = arg_string_katcl(g->server, 1);
    if (reg) {
        /* create the sensor using the register name */
        regsensor = sensor_create(reg, description, NULL, "integer", "nominal");
        if (regsensor) { 
            /* increase the size of the sensorlist */
            tmpsensorlist = realloc(g->sensorlist, (g->numsensors + 1) * sizeof(regsensor));
            if (tmpsensorlist) {
                /* add the sensor to the sensorlist */
                g->sensorlist = tmpsensorlist;
                g->sensorlist[g->numsensors] = regsensor;
                g->numsensors++;
                log_message_katcl(g->log, KATCP_LEVEL_INFO, GMON_PROG,
                    "added %s register to sensorlist, total = %d", reg, g->numsensors);
                /* register the katcp sensor for ?sensor-list */
                sensor_katcp_add(g->log, regsensor);
            } else {
                /* sensorlist size could not be increased, destroy the sensor */ 
                sensor_destroy(regsensor);
                log_message_katcl(g->log, KATCP_LEVEL_WARN, GMON_PROG,
                    "could not add %s registers to the sensorlist", reg);
            }
        }
    }    
}

static void cmd_meta(struct gmon_lib *g)
{
    unsigned int argcount = 0;
    unsigned int i = 0;
    char *arg = NULL;

    argcount = arg_count_katcl(g->server);

    printf("\n");
    for (i = 1; i < argcount; i++) {
        arg = arg_string_katcl(g->server, i);
        printf("arg %d : %s ", i, arg);
    }
}

static void cmd_wordread(struct gmon_lib *g)
{
    char *arg = NULL;
    uint32_t val = 0;
    char *endptr;
    bool parse = true;

    arg = arg_string_katcl(g->server, 1);
    if (!strcmp("ok", arg)) {
        arg = arg_string_katcl(g->server, 2);
        /* convert ascii hex string to int, also refer to 'man strtol' */
        errno = 0;
        val = strtol(arg, &endptr, 16);
        /* check for various strtol errors */
        if ((errno == ERANGE) || (errno != 0 && val == 0)) {
            log_message_katcl(g->log, KATCP_LEVEL_WARN, GMON_PROG,
                "could not convert ascii string %s", arg);
            parse = false;
        }
        if (endptr == arg) {
            log_message_katcl(g->log, KATCP_LEVEL_WARN, GMON_PROG,
                "no digits found %s", arg);
            parse = false;
        }
        if (parse) {
            g->sensorlist[g->readcollect]->val = val;
#if 0
            printf("reg %s, value = %s\n", g->sensorlist[g->readcollect]->name, arg);
#endif
            /* update the katcp sensorlist */
            sensor_katcp_update(g->log, g->sensorlist[g->readcollect]);
        }
    } else {
        log_message_katcl(g->log, KATCP_LEVEL_WARN, GMON_PROG,
            "could not read reg %s", g->sensorlist[g->readcollect]->name);
    } 
    
    g->readcollect++;   
    if (g->readcollect >= g->numsensors) {
        g->readcollect = 0;
    }
}

static struct message messageLookup[] = {
    {KATCP_LOG_INFORM, cmd_log},
    {"#fpga", cmd_fpga},
    {"#listdev", cmd_listdev},
    {"#meta", cmd_meta},
    {"!wordread", cmd_wordread},
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

