#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "gmon.h"
#include "cmdhandler.h"
#include "fpga.h"

#define TIMEOUT_S   (5UL)

int gmon_task(struct gmon_lib *g)
{
    fd_set readfds, writefds;
    struct timeval timeout;
    int retval;

    if (g->server == NULL) {
        return -1;
    }

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    timeout.tv_sec = TIMEOUT_S;
    timeout.tv_usec = 0;

    /* check if there is server data to process */    
    FD_SET(fileno_katcl(g->server), &readfds);

    /* check if there is server data to write out */
    if (flushing_katcl(g->server)) {
        FD_SET(fileno_katcl(g->server), &writefds);
    }

    /* check if there is log data to write out */
    if (flushing_katcl(g->log)) {
        FD_SET(fileno_katcl(g->log), &writefds);
    }

    /* we know (fileno_katcl(g->server) + 1) will be the highest since fileno_katcl(k) 
       return STDOUT_FILENO */
    retval = select(fileno_katcl(g->server) + 1, &readfds, &writefds, NULL, &timeout);

    if (retval == -1) {
        perror("select()");
        return 1;
    } else if (retval) {
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
        printf("timeout after %ld seconds.\n", TIMEOUT_S);
        retval = fpga_requeststatus(g->server);
    }

    return retval;
}

