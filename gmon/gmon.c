#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include "gmon.h"
#include "fpga.h"
#include "cmdhandler.h"
#include "reg.h"

static void gmon_poll_registers(struct gmon_lib *g)
{
    while (((g->readdispatch - g->readcollect) < GMON_POLL_QUEUE_LEN) &&
            (g->readdispatch < g->numsensors)) {
        reg_req_wordread(g->server, g->sensorlist[g->readdispatch]->name);
        g->readdispatch++;
    }

    if (g->readdispatch >= g->numsensors) {
        g->readdispatch = 0;
    }        
}

static int gmon_state(struct gmon_lib *g)
{
    int retval = 0;

    switch (g->state) {
        case GMON_UNKNOWN:
            fpga_req_cmd(g->server, FPGA_REQ_STATUS);
            g->state = GMON_IDLE;
            break;
        case GMON_IDLE:
        case GMON_FPGA_DOWN:
            /* do nothing, as one does when idle/down... */
            break;
        case GMON_FPGA_READY:
            fpga_req_cmd(g->server, FPGA_REQ_LISTDEV);
            g->state = GMON_IDLE; 
            break; 
        case GMON_REQ_META:
            g->state = GMON_POLL;
            break;
        case GMON_POLL:
            gmon_poll_registers(g);
            /* only return to IDLE once all the registers have been dispatched */
            if (g->readdispatch == 0) {
                g->state = GMON_IDLE;
            }
            break;

        default:
            fprintf(stderr, "gmon in unknown state, setting to initial state");
            g->state = GMON_UNKNOWN;
            break;
    }

    return retval;
}

int gmon_task(struct gmon_lib *g)
{
    fd_set readfds, writefds;
    struct timeval timeout;
    int retval, fd, mfd;

    if (g->server == NULL) {
        return -1;
    }

    /* gmon state machine */
    gmon_state(g);

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

    timeout.tv_sec = g->polltime;;
    timeout.tv_usec = 0;

    fd = fileno_katcl(g->server);
    mfd = fd;

    /* check if there is server data to process */    
    FD_SET(fd, &readfds);

    /* check if there is server data to write out */
    if (flushing_katcl(g->server)) {
        FD_SET(fd, &writefds);
    }

    /* check if there is log data to write out */
    if (flushing_katcl(g->log)) {
        fd = fileno_katcl(g->log);
       
        FD_SET(fd, &writefds);

        if(mfd < fd){
            mfd = fd;
        }
    }

    if (g->state == GMON_FPGA_DOWN) {
        /* sleep indefinitely if the FPGA is not ready */
        retval = select(mfd + 1, &readfds, &writefds, NULL, NULL);
    } else {
        retval = select(mfd + 1, &readfds, &writefds, NULL, &timeout);
    }

    if(retval < 0){
        switch(errno){
            case EAGAIN :
            case EINTR  :
                return 1;
            default :
                perror("select()");
                exit(EXIT_FAILURE);
                return -1;
        }
    }

    if (retval) {
        /* server data to process */
        if (FD_ISSET(fileno_katcl(g->server), &readfds)) {
            retval = read_katcl(g->server);

            if (retval) {
                fprintf(stderr, "%s: read failed.", GMON_PROG);
            }

            while (have_katcl(g->server) > 0) {
                cmdhandler(g);
            }
        }

        if (FD_ISSET(fileno_katcl(g->server), &writefds)) {
            retval = write_katcl(g->server);       
        }

        if (FD_ISSET(fileno_katcl(g->log), &writefds)) {
            retval = write_katcl(g->log);
        }

    } else {
        log_message_katcl(g->log, KATCP_LEVEL_TRACE, GMON_PROG, 
                            "timeout after %ld seconds, polling...", g->polltime);
        g->state = GMON_POLL;
    }

    return retval;
}

void gmon_destroy(struct gmon_lib *g)
{
    int i = 0;
    int num = g->numsensors;

    /* free sensors */
    for (i = 0; i < num; i++) {
        if (g->sensorlist[i]) {
            sensor_destroy(g->sensorlist[i]);
            g->numsensors--;
        }
    }

    /* free sensorlist */
    if (g->sensorlist) {
        free(g->sensorlist);
        g->sensorlist = NULL;
    }
}
