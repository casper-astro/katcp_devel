/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>

#include <sys/select.h>
#include <sys/time.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"

#define SENSOR_NAME_UNKNOWN   0
#define SENSOR_NAME_NOMINAL   1
#define SENSOR_NAME_WARN      2
#define SENSOR_NAME_ERROR     3
#define SENSOR_NAME_FAILURE   4
#define SENSOR_NAMES_COUNT    5

#define NAME "sq"


#if KATCP_PROTOCOL_MAJOR_VERSION >= 5   
#if KATCP_STATA_COUNT != 7
#error incorret number of status fields
#endif
char *sensor_status_names[KATCP_STATA_COUNT] = { "unknown", "nominal", "warn", "error", "failure", "unreachable", "inactive"};
#else
#if KATCP_STATA_COUNT != 5
#error incorret number of status fields
#endif
char *sensor_status_names[KATCP_STATA_COUNT] = { "unknown", "nominal", "warn", "error", "failure" };
#endif

volatile int up_running;

void handle_alarm(int signal)
{
  up_running = (-1);
}

struct katcl_line *initiate_connection(char *server, int verbose)
{
  struct katcl_line *l;
  int fd, flags;

  flags = 0;
  if(verbose > 0){
    flags = NETC_VERBOSE_ERRORS;
    if(verbose > 1){
      flags = NETC_VERBOSE_STATS;
    }
  } 

  fd = net_connect(server, 0, flags);
  if(fd < 0){
    if(verbose > 0){
      fprintf(stderr, "%s: unable to initiate connection to %s\n", NAME, server);
    }
    return NULL;
  }

  l = create_katcl(fd);
  if(l == NULL){
    if(verbose > 0){
      fprintf(stderr, "%s: unable to allocate state\n", NAME);
    }
    return NULL;
  }

  return l;
}

int issue_request(struct katcl_line *l, char *sensor, int verbose)
{
  /* TODO: could check returns */
  append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?sensor-sampling");
  append_string_katcl(l, KATCP_FLAG_STRING, sensor);
  append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "event");

  return 0;
}

int await_result(struct katcl_line *l, char *sensor, unsigned short *success, int verbose)
{
  char *status, *match, *ptr;
  fd_set fsr, fsw;
  int i, fd, count, result;

  fd = fileno_katcl(l);

  FD_ZERO(&fsr);
  FD_ZERO(&fsw);

  FD_SET(fd, &fsr);

  if(flushing_katcl(l)){ /* only write data if we have some */
    FD_SET(fd, &fsw);
  }

  result = select(fd + 1, &fsr, &fsw, NULL, NULL);
  switch(result){
    case -1 :
      switch(errno){
        case EAGAIN :
        case EINTR  :
          return 0;
        default  :
          return -1;
      }
      break;
    case 0 :
      return 0;
  }

  if(FD_ISSET(fd, &fsw)){
    result = write_katcl(l);
    if(result < 0){
      fprintf(stderr, "%s: write failed: %s\n", NAME, strerror(error_katcl(l)));
      return -1;
    }
  }

  if(FD_ISSET(fd, &fsr)){
    result = read_katcl(l);
    if(result){
      fprintf(stderr, "%s: read failed: %s\n", NAME, (result < 0) ? strerror(error_katcl(l)) : "connection terminated");
      return -1;
    }
  }

  while(have_katcl(l) > 0){
    ptr = arg_string_katcl(l, 0);
    if(ptr){
      switch(ptr[0]){
        case KATCP_INFORM : 
          if(!strcmp(ptr, "#sensor-status")){
            count = arg_count_katcl(l);
            if(count >= 5){
              match = arg_string_katcl(l, 3);
              status = arg_string_katcl(l, 4);
              if(match && status){
                if(verbose){
                  fprintf(stderr, "%s: sensor status is %s for %s\n", NAME, status, match);
                }
                for(i = 0; i < SENSOR_NAMES_COUNT; i++){
                  if(success[i] && !strcmp(sensor_status_names[i], status)){
                    if(verbose){
                      fprintf(stderr, "%s: sensor %s matches desired status %s\n", NAME, match, sensor_status_names[i]);
                    }
                    return 1;
                  }
                }
              }
            }
          }
          break;
        case KATCP_REPLY : 
          if(!strcmp(ptr, "!sensor-sampling")){
            ptr = arg_string_katcl(l, 1);
            if((ptr == NULL) || strcmp(ptr, KATCP_OK)){
              fprintf(stderr, "%s: unable to monitor sensor %s\n", NAME, sensor);
              return -1;
            }
          } else {
            if(verbose){
              fprintf(stderr, "%s: response %s is unexpected", NAME, ptr);
            }
          }
          break;
        case KATCP_REQUEST : 
          if(verbose > 0){
            fprintf(stderr, "%s: warning, encountered an unanswerable request <%s>\n", NAME, ptr);
          }
          break;
        default :
          fprintf(stderr, "%s: read malformed message <%s>\n", NAME, ptr);
          break;
      }
    }
  }

  return 0;
}

void usage(char *app){
  int i;

  printf("%s: sensor query - wait for a sensor to acquire a given status\n", NAME);
  printf("usage: %s [-h] [-v] [-q] [-s server:port] [-t timeout] [-w status] sensor-name\n", app);
  printf("-h              this help\n");
  printf("-v              increase verbosity\n");
  printf("-q              quiet\n");
  printf("-s server:port  connect to the server address on the given port\n");
  printf("-w status       set the sensor status to wait for. This option can be given\n");
  printf("                multiple times. Status values are:\n");

  printf("               ");

  for(i = 0; i < SENSOR_NAMES_COUNT; i++){
    printf(" %s", sensor_status_names[i]);
  }
  printf(" (default is %s)\n", sensor_status_names[SENSOR_NAME_NOMINAL]);

  printf("-t timeout      timeout in seconds (wait indefinitely by default)\n");

}

int main(int argc, char **argv)
{
  char *server, *sensor;
  int i, j, k, c;
  int verbose, result, timeout, code;
  struct katcl_line *l;
  unsigned short success[SENSOR_NAMES_COUNT];
  struct sigaction san, sao;

  server = getenv("KATCP_SERVER");
  if(server == NULL){
    server = "localhost";
  }
  
  verbose = 1;
  timeout = 0;
  sensor = NULL;

  for(i = 0; i < SENSOR_NAMES_COUNT; i++){
    success[i] = 0;
  }

  i = j = 1;
  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch(c){

        case 'h' :
          usage(argv[0]);
          return 0;

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 's' :
        case 't' :
        case 'w' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: argument needs a parameter\n", NAME);
            return 2;
          }

          switch(c){
            case 's' :
              server = argv[i] + j;
              break;
            case 't' :
              timeout = atoi(argv[i] + j);
              break;
            case 'w' : 
              for(k = 0; k < SENSOR_NAMES_COUNT; k++){
                if(!strcmp(sensor_status_names[k], argv[i] + j)){
                  success[k] = 1;
                }
              }
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
          fprintf(stderr, "%s: unknown option -%c\n", NAME, argv[i][j]);
          return 2;
      }
    } else {
      if(sensor){
        fprintf(stderr, "%s: extra parameter %s\n", NAME, argv[i]);
        return 2;
      }
      sensor = argv[i];
      i++;
    }
  }

  if(sensor == NULL){
    fprintf(stderr, "%s: need a sensor to monitor\n", NAME);
    return 2;
  }

  if(verbose){
    fprintf(stderr, "%s: waiting for sensor %s to acquire status", NAME, sensor);
  }

  i = j = 0;
  while(i < SENSOR_NAMES_COUNT){
    if(success[i]){
      if(verbose){
        fprintf(stderr, "%s%s", (j > 0) ? " or " : " ", sensor_status_names[i]);
      }
      j++;
    }
    i++;
  }
  
  if(j == 0){
    success[SENSOR_NAME_NOMINAL] = 1;
    if(verbose){
      fprintf(stderr, " %s\n", sensor_status_names[SENSOR_NAME_NOMINAL]);
    }
  } else {
    if(verbose){
      fprintf(stderr, "\n");
    }
  }

  up_running = 1;

  if(timeout > 0){
    san.sa_handler = &handle_alarm;
    san.sa_flags = 0;
    sigemptyset(&(san.sa_mask));

    sigaction(SIGALRM, &san, &sao);
    alarm(timeout);
  }

  l = NULL;
  code = 1;

  while(up_running > 0){

    if(l){
      sleep(1);
      destroy_katcl(l, 1);
    }

    l = initiate_connection(server, verbose);
    if(l == NULL){
      sleep(1);
      continue;
    }

    if(issue_request(l, sensor, verbose) < 0){
      continue;
    }

    while((result = await_result(l, sensor, success, verbose)) == 0);

    if(result > 0){
      up_running = 0;
      code = 0;
    }
  }

  if(l){
    destroy_katcl(l, 1);
  }

#if 0
  alarm(0);
  sigaction(SIGALRM, &sao, NULL);
#endif
      
  return code;
}
