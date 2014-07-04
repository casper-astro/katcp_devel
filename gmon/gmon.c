#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "gmon.h"
#include "cmdhandler.h"

#define TIMEOUT_S   (5UL)

static int checkfpga(struct katcl_line *l)
{
    int retval = 0;

    if (l == NULL) {
        return -1;
    }

    retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING 
                    | KATCP_FLAG_LAST, "?fpgastatus");

    return retval;
}


int gmon_init(void)
{
    /* not currently used */
    return 0;
}

int gmon_task(struct katcl_line *l, struct katcl_line *k)
{
    int fd;
    fd_set readfds, writefds;
    struct timeval timeout;
    int retval;

    if (l == NULL) {
        return -1;
    }

    fd = fileno_katcl(l);
    timeout.tv_sec = TIMEOUT_S;
    timeout.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    
    FD_SET(fd, &readfds);

    /* check if there is socket data to write out */
    if (flushing_katcl(l)) {
        FD_SET(fd, &writefds);
    }

    /* check if there is log data to write out */
    if (flushing_katcl(k)) {
        FD_SET(fileno_katcl(k), &writefds);
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
            retval = read_katcl(l);

            if (retval) {
                fprintf(stderr, "%s: read failed.", GMON_PROG);
            }

            while (have_katcl(l) > 0) {
                cmdhandler(l, k);
            }
        }

        if (FD_ISSET(fd, &writefds)) {
            retval = write_katcl(l);       
        }

        if (FD_ISSET(fileno_katcl(k), &writefds)) {
            retval = write_katcl(k);
        }

    } else {
        printf("timeout after %ld seconds.\n", TIMEOUT_S);
        retval = checkfpga(l);
    }

    return retval;
}

