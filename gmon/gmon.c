#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "gmon.h"
#include "cmdhandler.h"

//#define BUFF_LEN    (1024)
#define TIMEOUT_S   (5UL)

static int checkfpga(struct katcl_line *l)
{
    int retval = 0;

    if (l == NULL) {
        return -1;
    }

    retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING 
                    | KATCP_FLAG_LAST, "?fpgastatus");

    /* write katcl command out */
    while ((retval = write_katcl(l)) == 0);

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
    fd_set readfds;
    struct timeval timeout;
    int retval;
    char *cmd, *arg;
    struct katcl_parse *p = NULL;

    if (l == NULL) {
        return -1;
    }

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
        retval = read_katcl(l);

        if (retval) {
            fprintf(stderr, "%s: read failed.", GMON_PROG);
        }

        while (have_katcl(l) > 0) {
            cmd = arg_string_katcl(l, 0);
            /* route log messages to STDOUT, else pass them to
               the command handler */
            if (!strcmp(cmd, KATCP_LOG_INFORM)) {
                p = ready_katcl(l);
                append_parse_katcl(k, p);
                /* write the log data out */
                while ((retval = write_katcl(k)) == 0); 
            } else {
                arg = arg_string_katcl(l, 1);
                cmdhandler(cmd, arg);
            }
        }

    } else {
        printf("timeout after %ld seconds.\n", TIMEOUT_S);
        retval = checkfpga(l);
    }

    return retval;
}

