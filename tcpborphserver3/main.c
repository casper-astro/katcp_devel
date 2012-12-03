/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <dirent.h>
#include <sysexits.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#include <katcp.h>
#include <fork-parent.h>

#include "tcpborphserver3.h"

void usage(char *app)
{
  printf("Usage: %s" 
  " [-b bof-dir] [-f] [-h] [-i init-script] [-l log-file] [-m mode] [-p network-port]\n", app);

  printf("-b dir           directory containing bof files\n");
  printf("-f               run in foreground (default is background)\n");
  printf("-h               this help\n");
  printf("-i file          run the specified startup script\n");
  printf("-l file          log file name\n");
  printf("-m mode          mode to enter at startup\n");
  printf("-p port          network port to listen on\n");

}

int main(int argc, char **argv)
{
  struct katcp_dispatch *d;
  int status;
  int i, j, c, foreground, lfd;
  char *port, *mode, *init, *lfile, *bofdir;
  time_t now;

  port = "7147";
  mode = TBS_MODE_RAW_NAME;
  init = NULL;
  lfile = TBS_LOGFILE;
  foreground = 0;
  bofdir = NULL;

  i = 1;
  j = 1;
  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {
        case '\0':
          j = 1;
          i++;
          break;
        case '-':
          j++;
          break;
        case 'h' :
          usage(argv[0]);
          return EX_OK;

        case 'f' :
          j++;
          foreground = 1 - foreground;
          break;

        case 'b' :
        case 'i' :
        case 'l' :
        case 'm' :
        case 'p' :
          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: option -%c requires a parameter\n", argv[0], c);
          }
          switch(c){
            case 'b' :
              bofdir = argv[i] + j;
              break;
            case 'i' :
              init = argv[i] + j;
              break;
            case 'l':
              lfile = argv[i] + j;  
              break;
            case 'm' :
              mode = argv[i] + j;
              break;
            case 'p' :
              port = argv[i] + j;
              break;
          }
          i++;
          j = 1;
          break;
        default:
          fprintf(stderr, "%s: unknown option -%c\n", argv[0], c);
          return 1;
          break;
      }
    } else {
      fprintf(stderr, "%s: extra argument %s\n", argv[0], argv[i]);
      return 1;
    }
  }

  if (!foreground){
    fprintf(stderr,"%s: about to go into background\n", argv[0]);
    if(fork_parent() < 0){
      fprintf(stderr, "%s: unable to fork_parent\n", argv[0]);
      return 1;
    }
  }

  /* create a state handle */
  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "%s: unable to allocate state\n", argv[0]);
    return 1;
  }

  if(setup_raw_tbs(d, bofdir, argc, argv) < 0){
    fprintf(stderr, "%s: unable to initialise logic for raw mode\n", argv[0]);
    return 1;
  }

  /* mode from command line */
  if(mode){
    if(enter_name_mode_katcp(d, mode, NULL) < 0){
      fprintf(stderr, "%s: unable to enter mode %s\n", argv[0], mode);
      return 1;
    }
  }

  signal(SIGPIPE, SIG_DFL);

  if (!foreground){
    lfd = open(lfile, O_WRONLY | O_APPEND | O_CREAT | O_NOCTTY, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP);
      
    if (lfd < 0){
      fprintf(stderr,"%s: unable to open %s: %s\n",argv[0], TBS_LOGFILE, strerror(errno));
      return 1;
    } else {
      fflush(stderr);
      if (dup2(lfd, STDERR_FILENO) >= STDERR_FILENO){
        now = time(NULL);
        fprintf(stderr ,"Logging to file started at %s\n",ctime(&now));
      }
      close(lfd);
    }
  }

#ifdef DEBUG
  fprintf(stderr, "server: about to run config server\n");
#endif

  if(run_config_server_katcp(d, init, TBS_MAX_CLIENTS, port, 0) < 0){
    fprintf(stderr, "server: run failed\n");
    return 1;
  }

  status = exited_katcp(d);

  shutdown_katcp(d);

#ifdef DEBUG
  fprintf(stderr, "server: exit with status %d\n", status);
#endif

  return status;
}
