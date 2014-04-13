#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <netc.h>
#include <katcl.h>
#include <katcp.h>
#include <katpriv.h>
#include <fork-parent.h>

#define NAME "kcplog"

static volatile int log_level = KATCP_LEVEL_INFO;
static volatile int log_changed = 0;
static volatile int log_reload = 0;

void usage(char *app)
{
  printf("usage: %s [-v] [-h] [-l level] [-o logfile] [-a reconnect-attempts] [-d] [-t] [-s server:port]\n", app);
  printf("-v              be more verbose\n");
  printf("-h              this help\n");
  printf("-l level        select a log level (trace, debug, info, warn, ... to log at)\n");
  printf("-o logfile      write log messages to the specified log file instead of stdout\n");
  printf("-d              run in the background\n");
  printf("-f              run in the foreground\n");
  printf("-t              truncate the logfile when opening it\n");
  printf("-s server:port  connect to the specified server rather than localhost:7147\n");
  printf("-a attempts     make the given number attempts to connect to the server before giving up\n");
  printf("signals: HUP USR1 USR2\n");
  printf(" HUP            re-open the logfile (if -o is given)\n");
  printf(" USR1           change log level one level more detailed (eg from DEBUG to TRACE)\n");
  printf(" USR2           change log level one level less detailed (eg from INFO to WARN)\n");
}

static void handle_signal(int signal)
{
  switch(signal){
    case SIGUSR1 : 
      if(log_level > KATCP_LEVEL_TRACE){
        log_level--;
        log_changed = 1;
      }
      break;

    case SIGUSR2 : 
      if(log_level < KATCP_LEVEL_OFF){
        log_level++;
        log_changed = 1;
      }
      break;

    case SIGHUP : 
      log_reload = 1;
      break;

    default :
      return;
  }

}

int main(int argc, char **argv)
{
#define BUFFER 64
  char buffer[BUFFER];
  char *level, *app, *server, *output;
  int run, fd, i, j, c, verbose, attempts, detach, result, truncate, flags;
  struct katcl_parse *p;
  struct katcl_line *ls, *lo;
  struct sigaction sa;
  time_t now;
  struct tm *local;

  i = j = 1;
  app = argv[0];

  verbose = 0;
  attempts = 2;
  detach = 0;
  truncate = 0;

  server = getenv("KATCP_SERVER");
  if(server == NULL){
    server = "localhost";
  }

  output = NULL;
  level = NULL;

  flags = 0; /* placate -Wall */

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return EX_OK;

        case 'v' : 
          verbose++;
          j++;
          break;

        case 'd' : 
          detach = 1;
          j++;
          break;

        case 'f' : 
          detach = 0;
          j++;
          break;

        case 't' : 
          truncate = 1;
          j++;
          break;

        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 'l' :
        case 'o' :
        case 'a' :
        case 's' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: usage: argument needs a parameter\n", app);
            return EX_USAGE;
          }

          switch(c){
            case 'l' :
              level = argv[i] + j;
              break;
            case 'o' :
              output = argv[i] + j;
              break;
            case 'a' : 
              attempts = atoi(argv[i] + j);
              break;
            case 's' : 
              server = argv[i] + j;
              break;
          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: usage: unknown option -%c\n", app, argv[i][j]);
          return EX_USAGE;
      }
    } else {
      if(output){
        fprintf(stderr, "%s: usage: unexpected extra argument %s (can only save to one file)\n", app, argv[i]);
        return EX_USAGE;
      } 
      output = argv[i];
      i++;
    }
  }

  if(detach){
    if(fork_parent() < 0){
      fprintf(stderr, "%s: unable to detach process\n", app);
      return EX_OSERR;
    }
  }

  sa.sa_handler = handle_signal;
#if 0
  sa.sa_flags = SA_RESTART;
#endif
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));

  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);
  sigaction(SIGUSR2, &sa, NULL);

  if(server == NULL){
    server = "localhost:7147";
  }

  if(level){
    log_changed = 1;
    log_level = log_to_code_katcl(level);
    if(log_level < 0){
      fprintf(stderr, "%s: usage: invalid initial log priority %s\n", app, level);
      return EX_USAGE;
    } 
  }

  if(output == NULL){
    if(detach == 1){
      fprintf(stderr, "%s: usage: need a filename as target\n", app);
      return EX_USAGE;
    }
    fd = STDOUT_FILENO;
  } else {
    flags = O_CREAT | O_WRONLY;
    if(truncate == 0){
      flags |= O_APPEND;
    }
    fd = open(output, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if(fd < 0){
      fprintf(stderr, "%s: unable to open file %s: %s\n", app, output, strerror(errno));
      return EX_OSERR;
    }
  }

  lo = create_katcl(fd);
  if(lo == NULL){
    fprintf(stderr, "%s: unable to allocate log state\n", app);
    return EX_OSERR;
  }


  /**********************/

  while((attempts-- > 0) && ((fd = net_connect(server, 0, 0)) < 0)){
    sleep(1);
  }

  if(attempts <= 0){
    sync_message_katcl(lo, KATCP_LEVEL_FATAL, NAME, "unable to connect to %s", server);
    return EX_UNAVAILABLE;
  }

  ls = create_katcl(fd);
  if(ls == NULL){
    sync_message_katcl(lo, KATCP_LEVEL_FATAL, NAME, "unable to allocate parser state");
    return EX_OSERR;
  }

  if(detach){
    fclose(stderr);
  }

  time(&now);
  local = localtime(&now);
  strftime(buffer, BUFFER - 1, "%Y-%m-%dT%H:%M:%S", local);

  sync_message_katcl(lo, KATCP_LEVEL_INFO, NAME, "monitor start for %s at %s", server, buffer);

  for(run = 1; run > 0;){

    if(log_reload > 0){
      if(output){
        fd = open(output, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if(fd >= 0){
          exchange_katcl(lo, fd);
        }
      }

      log_reload = 0;
    }
 
    /* WARNING: will only run after the next message - may have to interrupt syscall to get past this */
    if(log_changed > 0){

      level = log_to_string_katcl(log_level);
      if(level){

        p = create_parse_katcl();
        if(p){

#ifdef KATCP_STRICT_CONFORMANCE
          add_string_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?log-limit");
#else 
          add_string_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?log-level");
#endif
          add_string_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_LAST, level);

          append_parse_katcl(lo, p);

          /* dodgy refcount dealings: p is created with refcount = 0, so can only do write at end, otherwise may end up being deallocated */

          append_parse_katcl(ls, p);
          write_katcl(ls);

        }
      } else {
        sync_message_katcl(lo, KATCP_LEVEL_ERROR, NAME, "invalid log priority number %d", level);
      }

      log_changed = 0;
    }

    result = read_katcl(ls);
    if(result < 0){
      sync_message_katcl(lo, KATCP_LEVEL_FATAL, NAME, "read from network failed: %s", strerror(errno));
      return EX_OSERR;
    }

    if(result == 1){
      run = 0;
    }

    while(have_katcl(ls)){
      p = ready_katcl(ls);
      if(p){
        append_parse_katcl(lo, p);
      }
    }

    write_katcl(lo);
  }

  return EX_OK;
#undef BUFFER
}

