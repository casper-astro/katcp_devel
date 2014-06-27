#include <stdlib.h>
#include <stdio.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"

#include "gmon.h"

int main(int argc, char *argv[])
{
    char *server;
    struct katcl_line *l;
    int fd;

    if (argc <= 0) {
        server = getenv("KATCP_SERVER");
    } else {
        server = argv[1];
    }

    if (server == NULL) {
        fprintf(stderr, "need a server as first argument or in the KATCP_SERVER variable\n");
        return EXIT_FAILURE;
    }

    fd = net_connect(server, 0, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
    if (fd < 0) {
        fprintf(stderr, "unable to connect to %s\n", server);
        return EXIT_FAILURE;
    }

    l = create_katcl(fd);
    if (l == NULL) {
        fprintf(stderr, "unable to allocate state\n");
        /// \todo should we disconnect from server?
        return EXIT_FAILURE;
    }

    /* main working loop */
    while (1) {
        gmon_task(l);
    }

    destroy_katcl(l, 1);

    return EXIT_SUCCESS;
}

