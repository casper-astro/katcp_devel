/* This is the main entry point for the Gateware Monitor (GMON)
 * application. 
 *
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"

#include "gmon.h"

static void usage(void);
static void print_version(void);

static volatile sig_atomic_t monitor = 1;

static void signalhandler(int signum)
{
    switch (signum) {
        case SIGTERM:
        case SIGINT:
        case SIGPIPE:
            monitor = 0;    
            break;

        default:
            return;
    }
}

int main(int argc, char *argv[])
{
    int opt = 0;
    struct sigaction sa;
    char *server;
    struct gmon_lib gmon = {0};
    int fd;

    /* initialise gmon object */
    gmon.polltime = GMON_POLL_TIME_S;
    gmon.state = GMON_UNKNOWN;

    /* initialize the signal handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signalhandler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    server = getenv("KATCP_SERVER");
    if (server == NULL) {
        server = "localhost";
    }

    /* process command line arguments */
    while ((opt = getopt(argc, argv, "hs:t:v")) != -1) {
        switch (opt) {
            case 's':
                server = optarg;
                break;
            case 't':
                gmon.polltime = atoi(optarg);
                break;
            case 'v':
                print_version();
                exit(EXIT_SUCCESS);
            case 'h':
            case '?':
            default:
                usage();
                exit(EXIT_FAILURE);
                break;
        }
    }

    gmon.log = create_katcl(STDOUT_FILENO);
    if (gmon.log == NULL) {
        fprintf(stderr, "could not create katcl logger\n");
        return EXIT_FAILURE;
    }

    fd = net_connect(server, 0, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
    if (fd < 0) {
        fprintf(stderr, "unable to connect to %s\n", server);
        return EXIT_FAILURE;
    }

    gmon.server = create_katcl(fd);
    if (gmon.server == NULL) {
        fprintf(stderr, "unable to allocate server state\n");
        /// \todo should we disconnect from server?
        return EXIT_FAILURE;
    }

    sync_message_katcl(gmon.log, KATCP_LEVEL_INFO, GMON_PROG, 
                        "starting %s...", GMON_PROG);

    /* main working loop */
    while (monitor) {
        gmon_task(&gmon);
    }

    /* perform clean-up */
    sync_message_katcl(gmon.log, KATCP_LEVEL_INFO, GMON_PROG, 
                        "shutting down %s...", GMON_PROG);

    /* free gmon sensor resources */
    gmon_destroy(&gmon);

    /* free server and logger */
    if (gmon.server) {
        destroy_katcl(gmon.server, 1);
    }

    if (gmon.log) {
        destroy_katcl(gmon.log, 1);
    }

    return EXIT_SUCCESS;
}

static void usage(void)
{
    printf("usage: %s [options]\n", GMON_PROG);
    printf("-h\t\t\tthis help\n");
    printf("-s server:port\t\tspecify server:port\n");
    printf("-t polltime\t\tspecify the polltime in seconds [default %d]\n", GMON_POLL_TIME_S);

    printf("\nenvironment variable(s):\n");
    printf("\tKATCP_SERVER\tdefault server (overriden by -s option)\n");
}

static void print_version(void)
{
    printf("%s version %d.%d.%d\n", GMON_PROG, GMON_VER_MAJOR, GMON_VER_MINOR,
                GMON_VER_BUGFIX);
    printf("build %s %s\n", __DATE__, __TIME__);
}

