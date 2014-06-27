#include "gmon.h"
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#define BUFF_LEN    (1024)
#define TIMEOUT_S   (5UL)

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
    timeout.tv_sec = TIMEOUT_S;
    timeout.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    retval = select(fd + 1, &readfds, NULL, NULL, &timeout);

    if (retval == -1) {
        perror("select()");
        return 1;
    } else if (retval) {
        printf("data available now.\n");
        /* FD_ISSET(fd, &readfds) will be true */
	    char buff[BUFF_LEN + 1];
        int len = read(fd, buff, BUFF_LEN);
        
       	if (len == -1) {
            perror("read()");
            return 1;
        }

        if (len) {
            buff[len] = '\0';
            printf("read: %s\n", buff);
        }
    } else {
        printf("timeout after %ld seconds.\n", TIMEOUT_S);
    }

    return retval;
}

