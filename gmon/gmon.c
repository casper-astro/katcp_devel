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
    int fd;
    fd_set readfds, writefds;
    struct timeval timeout;
    int retval;

    if (g->server == NULL) {
        return -1;
    }

    fd = fileno_katcl(g->server);
    timeout.tv_sec = TIMEOUT_S;
    timeout.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    
    FD_SET(fd, &readfds);

    /* check if there is socket data to write out */
    if (flushing_katcl(g->server)) {
        FD_SET(fd, &writefds);
    }

    /* check if there is log data to write out */
    if (flushing_katcl(g->log)) {
        FD_SET(fileno_katcl(g->log), &writefds);
    }

    /* we know (fd + 1) will be the highest since fileno_katcl(k) 
       return STDOUT_FILENO */
    retval = select(fd + 1, &readfds, &writefds, NULL, &timeout);

    if (retval == -1) {
        perror("select()");
        return 1;
    } else if (retval) {
        /* data to process */
        if (FD_ISSET(fd, &readfds)) {
            retval = read_katcl(g->server);

            if (retval) {
                fprintf(stderr, "%s: read failed.", GMON_PROG);
            }

            while (have_katcl(g->server) > 0) {
                cmdhandler(g);
            }
        }

        if (FD_ISSET(fd, &writefds)) {
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

