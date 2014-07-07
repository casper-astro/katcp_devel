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
    struct katcl_line *l = NULL;
    struct katcl_line *k = NULL;
    int fd;

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
    while ((opt = getopt(argc, argv, "hs:v")) != -1) {
        switch (opt) {
            case 's':
                server = optarg;
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

    k = create_katcl(STDOUT_FILENO);
    if (k == NULL) {
        fprintf(stderr, "could not create katcl\n");
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

    sync_message_katcl(k, KATCP_LEVEL_INFO, GMON_PROG, 
                        "starting %s...", GMON_PROG);

    /* main working loop */
    while (monitor) {
        gmon_task(l, k);
    }

    sync_message_katcl(k, KATCP_LEVEL_INFO, GMON_PROG, 
                        "shutting down %s...", GMON_PROG);

    if (l) {
        destroy_katcl(l, 1);
    }

    if (k) {
        destroy_katcl(k, 1);
    }

    return EXIT_SUCCESS;
}

static void usage(void)
{
    printf("usage: %s [options]\n", GMON_PROG);
    printf("-h\t\t\tthis help\n");
    printf("-s server:port\t\tspecify server:port\n");

    printf("\nenvironment variable(s):\n");
    printf("\tKATCP_SERVER\tdefault server (overriden by -s option)\n");
}

static void print_version(void)
{
    printf("%s version %d.%d.%d\n", GMON_PROG, GMON_VER_MAJOR, GMON_VER_MINOR,
                GMON_VER_BUGFIX);
    printf("build %s %s\n", __DATE__, __TIME__);
}

