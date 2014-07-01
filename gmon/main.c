#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"

#include "gmon.h"

static volatile sig_atomic_t monitor = 1;

static void signalhandler(int signum)
{
    switch (signum) {
        case SIGTERM:
        case SIGINT:
            monitor = 0;    
            break;

        default:
            return;
    }
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    char *server;
    struct katcl_line *l;
    int fd;

    /* initialize the signal handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signalhandler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

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

    gmon_init();

    /* main working loop */
    while (monitor) {
        gmon_task(l);
    }

    printf("shutting down...\n");

    destroy_katcl(l, 1);

    return EXIT_SUCCESS;
}

