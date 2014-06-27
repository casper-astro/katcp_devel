#include "gmon.h"
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>

int gmon_task(struct katcl_line *l);

int gmon_init(struct katcl_line *l)
{

    return 1;
}

int gmon_task(struct katcl_line *l)
{
    int fd;
    fd_set readfds;
    struct timeval timeout;
    int retval;

    fd = fileno_katcl(l);
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    retval = select(fd + 1, &readfds, NULL, NULL, &timeout);

    if (retval == -1) {
        perror("select()");
    } else if (retval) {
        printf("data available now.\n");
        /* FD_ISSET(fd, &readfds) will be true */
    } else {
        printf("timeout after %ld seconds.\n", timeout.tv_sec);
    }

    return retval;
}
