/* (c) 2012 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define DEBUG

#define KCPPAR_NAME "kcppar"

void usage(char *app)
{
  printf("usage: %s [flags] [-s server+ command args*]*\n", app);
  printf("-h                 this help\n");
  printf("-v                 increase verbosity\n");
  printf("-q                 run quietly\n");
  printf("-k                 emit katcp log messages\n");
  printf("-s server:port     specify server:port\n");
  printf("-t seconds         set timeout\n");
  printf("-r                 toggle printing of reply messages\n");
  printf("-i                 toggle printing of inform messages\n");
  printf("-m                 munge replies into log messages (requires -k)\n");
  printf("-n                 suppress version information emitted on connect\n");

  printf("return codes:\n");
  printf("0     command completed successfully\n");
  printf("1     command failed\n");
  printf("2     other errors\n");

  printf("environment variables:\n");
  printf("  KATCP_SERVER     default server (overridden by -s option)\n");

  printf("notes:\n");
  printf("  command and parameters have to be given as separate arguments\n");
}

#define BUFFER 1024
#define TIMEOUT   4

#define RX_SETUP 1 
#define RX_UP    2 
#define RX_DONE  0
#define RX_BAD   (-1)

struct remote{
  char *r_name;
  struct katcl_line *r_line;

  int r_state;

  unsigned int r_index;
  unsigned int r_count;

  struct katcl_parse **r_vector;
  char *r_match;
};

struct set{
  struct remote **s_vector;
  unsigned int s_count;
};

void destroy_remote(struct remote *rx)
{
  unsigned int i;

  if(rx == NULL){
    return;
  }

  if(rx->r_name){
    free(rx->r_name);
    rx->r_name = NULL;
  }

  if(rx->r_line){
    destroy_katcl(rx->r_line, 1);
    rx->r_line = NULL;
  }

  rx->r_index = 0;
  rx->r_state = RX_BAD;

  rx->r_match = NULL;

  if(rx->r_vector){
    for(i = 0; i < rx->r_count; i++){
      if(rx->r_vector[i]){
        destroy_parse_katcl(rx->r_vector[i]);
        rx->r_vector[i] = NULL;
      }
    }
    free(rx->r_vector);
    rx->r_vector = NULL;
  }
  rx->r_count = 0;

  free(rx);
}

struct remote *create_remote(char *name)
{
  struct remote *rs;

  rs = malloc(sizeof(struct remote));
  if(rs == NULL){
    return NULL;
  }

  rs->r_name = NULL;
  rs->r_line = NULL;

  rs->r_index = 0;
  rs->r_state = RX_BAD;

  rs->r_count = 0;
  rs->r_vector = NULL;
  rs->r_match = NULL;

  rs->r_name = strdup(name);
  if(rs->r_name == NULL){
    destroy_remote(rs);
    return NULL;
  }

  return rs;
}

int queue_remote(struct remote *rs, struct katcl_parse *px)
{
  struct katcl_parse **tmp;

  tmp = realloc(rs->r_vector, sizeof(struct katcl_parse *) * (rs->r_count + 1));
  if(tmp == NULL){
    return -1;
  }

  rs->r_vector = tmp;

  rs->r_vector[rs->r_count] = px;

  rs->r_count++;
  
  return 0;
}

/********************************************************************/

struct set *create_set()
{
  struct set *ss;

  ss = malloc(sizeof(struct set));
  if(ss == NULL){
    return NULL;
  }

  ss->s_count = 0; 
  ss->s_vector = NULL;

  return ss;
}

void destroy_set(struct set *ss)
{
  unsigned int i;

  if(ss == NULL){
    return;
  }

  for(i = 0; i < ss->s_count; i++){
    destroy_remote(ss->s_vector[i]);
    ss->s_vector[i] = NULL;
  }

  if(ss->s_vector){
    free(ss->s_vector);
    ss->s_vector = NULL;
  }

  free(ss);
}

struct remote *find_remote(struct set *ss, char *name)
{
  unsigned int i;
  struct remote *rs;

  for(i = 0; i < ss->s_count; i++){
    rs = ss->s_vector[i];
    if(!strcmp(rs->r_name, name)){
      return rs;
    }
  }

  return NULL;
}

struct remote *add_remote(struct set *ss, char *name)
{
  struct remote *rs;
  struct remote **tmp;

  rs = create_remote(name);
  if(rs == NULL){
    return NULL;
  }

  tmp = realloc(ss->s_vector, sizeof(struct remote *) * (ss->s_count + 1));
  if(tmp == NULL){
    destroy_remote(rs);
    return NULL;
  }

  ss->s_vector = tmp;

  ss->s_vector[ss->s_count] = rs;
  ss->s_count++;

  return rs;
}

struct remote *acquire_remote(struct set *ss, char *name)
{
  struct remote *rs;

  rs = find_remote(ss, name);
  if(rs){
    return rs;
  }

  return add_remote(ss, name);
}

int load_parse_set(struct set *ss, char *list, struct katcl_parse *px)
{
  unsigned int i, j, insert;
  int state;
  char *ptr, *base;
  struct remote *rs;
  int result;

  base = strdup(list);
  if(base == NULL){
    return -1;
  }

  result = 0;
  state = 0;
  j = 0;
  i = 0;
  insert = 0;

  while(state >= 0){
    switch(base[i]){
      case '\0' :
        if(state == 1){
          insert = 1;
        }
        state = (-1);
        break;

      case '\t' :
      case ' ' :
        base[i] = '\0';
        if(state == 1){
          insert = 1;
        }
        state = 0;
        break;

      case ',' :
        base[i] = '\0';
        if(state == 1){
          insert = 1;
        } /* could be fussy here */
        state = 0;
        break;

      default :
        if(state == 0){
          j = i;
        }
        state = 1;
        break;
    }
    i++;

    if(insert){
      ptr = base + j;

#ifdef DEBUG
     fprintf(stderr, "adding to server %s\n", ptr);
#endif

      rs = acquire_remote(ss, ptr);
      if(rs == NULL){
#ifdef DEBUG
        fprintf(stderr, "unable to look up server %s\n", ptr);
#endif
        result = (-1);
      } else {
        if(queue_remote(rs, px) < 0){
#ifdef DEBUG
          fprintf(stderr, "unable to add message to queue of server %s\n", ptr);
#endif
          result = (-1);
        }
      }
      insert = 0;
    }

  }

  free(base);

  return result;
}

int activate_remotes(struct set *ss, struct katcl_line *k)
{
  unsigned int i;
  struct remote *rx;
  int fd;

#ifdef DEBUG
  fprintf(stderr, "launching %u clients\n", ss->s_count);
#endif

  for(i = 0; i < ss->s_count; i++){
    rx = ss->s_vector[i];
#ifdef DEBUG
    fprintf(stderr, "attempting to start connect to %s (%u requests)\n", rx->r_name, rx->r_count);
#endif
    fd = net_connect(rx->r_name, 0, NETC_ASYNC);
    if(fd < 0){
      if(k){
        /* TODO */
      }
      return -1;
    }

    if(rx->r_line){
#ifdef DEBUG
      fprintf(stderr, "logic failure: line already initialised\n");
#endif
      return -1;
    }

    rx->r_line = create_katcl(fd);
    if(rx->r_line == NULL){
      if(k){
        /* TODO */
      }
#ifdef DEBUG
      fprintf(stderr, "setup failure: unable to create line for %s\n", rx->r_name);
#endif
      close(fd);
      return -1;
    }

    rx->r_state = RX_SETUP;
    rx->r_index = 0;
  }

  return 0;
}

int next_request(struct remote *rx)
{
  char *ptr;

  rx->r_match = NULL;

  if(rx->r_index >= rx->r_count){
    return 1;
  }

  ptr = get_string_parse_katcl(rx->r_vector[rx->r_index], 0);
  if(ptr == NULL){
    return -1;
  }
  if(ptr[0] != KATCP_REQUEST){
    return -1;
  }

  if(append_parse_katcl(rx->r_line, rx->r_vector[rx->r_index]) < 0){
    return -1;
  }

  rx->r_match = ptr + 1;
  rx->r_index++;

  return 0;
}

int update_state(struct remote *rx, int state)
{
  if(rx->r_state == state){
    return 0;
  }

  rx->r_state = state;

  switch(state){
    case RX_DONE : 
    case RX_BAD : 
      return 1;
    default :
      return 0;
  }
}

int main(int argc, char **argv)
{
  struct set *ss;
  struct remote *rx;
  struct katcl_parse *px;
  struct katcl_line *k;
  struct timeval tv;
  fd_set fsr, fsw;

  char *app, *parm, *cmd, *copy, *ptr, *servers;
  int i, j, c, fd, mfd, fin;
  int verbose, result, status, base, info, reply, timeout, pos, flags, show;
  int xmit, code;
  unsigned int len;
  
  servers = getenv("KATCP_SERVER");
  if(servers == NULL){
    servers = "localhost:7147";
  }
  
  info = 1;
  reply = 1;
  verbose = 1;
  i = j = 1;
  app = argv[0];
  base = (-1);
  timeout = 5;
  pos = (-1);
  k = NULL;
  show = 1;
  parm = NULL;

  ss = create_set();
  if(ss == NULL){
    fprintf(stderr, "%s: unable to set up command set\n", app);
    return 2;
  }

  xmit = (-1);

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return 0;

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          info = 0;
          reply = 0;
          j++;
          break;

        case 'i' : 
          info = 1 - info;
          j++;
          break;
        case 'r' : 
          reply = 1 - reply;
          j++;
          break;

        case 'n' : 
          show = 0;
          j++;
          break;

        case 'x' : 
          xmit = 0;
          j++;
          break;

        case 'k' : 
          k = create_katcl(STDOUT_FILENO);
          if(k == NULL){
            fprintf(stderr, "%s: unable to create katcp message logic\n", app);
            return 2;
          }
          j++;
          break;

        case 's' :
        case 't' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: argument needs a parameter\n", app);
            return 2;
          }

          switch(c){
            case 's' :
              servers = argv[i] + j;
              break;
            case 't' :
              timeout = atoi(argv[i] + j);
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
          fprintf(stderr, "%s: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      if(xmit < 0){
        /* WARNING: this could make error detection worse */
        xmit = 0;
      }

      if(xmit == 0){
        px = create_referenced_parse_katcl();
        if(px == NULL){
          fprintf(stderr, "%s: unable to create parse instance\n", app);
          return 2;
        }

        switch(argv[i][0]){
          case KATCP_REQUEST : 
          case KATCP_REPLY   :
          case KATCP_INFORM  :
            ptr = argv[i];
            break;
          default :
            copy = malloc(strlen(argv[i]) + 1);
            if(copy == NULL){
              fprintf(stderr, "%s: unable to allocate temporary storage\n", app);
              return 2;
            }
            copy[0] = KATCP_REQUEST;
            strcpy(copy + 1, argv[i]);
            ptr = copy;
            break;
        }
        flags = KATCP_FLAG_FIRST;
      } else {
        ptr = argv[i];
        flags = 0;
      }

      i++;
      if((i >= argc) || (argv[i][0] == '-')){
        flags |= KATCP_FLAG_LAST;
      }

      if(add_string_parse_katcl(px, flags, ptr) < 0){
        fprintf(stderr, "%s: unable to add parameter %s\n", app, ptr);
        return 2;
      }

      if(flags & KATCP_FLAG_LAST){
#ifdef DEBUG
        fprintf(stderr, "par: loading command for servers %s\n", servers);
#endif
        if(load_parse_set(ss, servers, px) < 0){
          fprintf(stderr, "%s: unable to load command into server set %s\n", app, servers);
          return 2;
        }
      }

      if(copy){
        free(copy);
        copy = NULL;
      }

      xmit++;
    }
  }

  if(activate_remotes(ss, k) < 0){
    return 2;
  }

  for(fin = 0; fin < ss->s_count;){

    mfd = 0;
    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    for(i = 0; i < ss->s_count; i++){
      rx = ss->s_vector[i];
      if(rx->r_line){
        fd = fileno_katcl(rx->r_line);
        if(fd > mfd){
          mfd = fd;
        }
      } else {
        fd = (-1); /* WARNING: live dangerously */
      }

      switch(rx->r_state){
        case RX_SETUP :
          FD_SET(fd, &fsw);
          break;
        case RX_UP :
          if(flushing_katcl(rx->r_line)){ /* only write data if we have some */
            FD_SET(fd, &fsw);
          }
          FD_SET(fd, &fsr);
          break;
          /* case RX_DONE : */
          /* case RX_BAD  : */
        default :
          break;
      }
    }

    tv.tv_sec  = timeout;
    tv.tv_usec = 0;

    result = select(mfd + 1, &fsr, &fsw, NULL, &tv);
    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            if(k){
              sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "select failed: %s", strerror(errno));
            }
            return 2;
        }
        break;
      case  0 :
        if(k){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "requests timed out after %d seconds", argv[base], timeout);
        } 
        if(verbose){
          fprintf(stderr, "%s: no io activity within %d seconds\n", app, timeout);
        }
        /* could terminate cleanly here, but ... */
        return 2;
    }

    for(i = 0; i < ss->s_count; i++){
      rx = ss->s_vector[i];
      if(rx->r_line){
        fd = fileno_katcl(rx->r_line);
      } else {
        fd = (-1); /* WARNING: live dangerously */
      }

      switch(rx->r_state){
        case RX_SETUP :
          if(FD_ISSET(fd, &fsw)){
            len = sizeof(int);
            result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);
            if(result == 0){
              switch(code){
                case 0 :
                  if(k){
                    log_message_katcl(k, KATCP_LEVEL_DEBUG, NULL, "async connect to %s succeeded", rx->r_name);
                  }
                  if(next_request(rx) < 0){
                    sync_message_katcl(k, KATCP_LEVEL_ERROR, NULL, "failed to load request for destination %s", rx->r_name);
                    fin += update_state(rx, RX_BAD);
                  } else {
                    fin += update_state(rx, RX_UP);
                  }
                  break;
                case EINPROGRESS :
                  if(k){ 
                    log_message_katcl(k, KATCP_LEVEL_WARN, NULL, "saw an in progress despite write set being ready on job %s", rx->r_name);
                  }
                  break;
                default :
                  if(k){
                    sync_message_katcl(k, KATCP_LEVEL_ERROR, NULL, "unable to connect to %s: %s", rx->r_name, strerror(code));
                  }
                  fin += update_state(rx, RX_BAD);
                  break;
              }
            }
          }
          break;
        case RX_UP :

          if(FD_ISSET(fd, &fsw)){ /* flushing things */
            result = write_katcl(rx->r_line);
            if(result < 0){
              if(k){
                sync_message_katcl(k, KATCP_LEVEL_ERROR, NULL, "unable to write to %s", rx->r_name);
              }
              fin += update_state(rx, RX_BAD);
            }
          }

          if(FD_ISSET(fd, &fsr)){ /* get things */
            result = read_katcl(rx->r_line);
            if(result){
              if(k){
                sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "read failed: %s", (result < 0) ? strerror(error_katcl(rx->r_line)) : "connection terminated");
              } 
            }
          }

          while(have_katcl(rx->r_line) > 0){ /* compute */


            cmd = arg_string_katcl(rx->r_line, 0);
            if(cmd){
#ifdef DEBUG
              fprintf(stderr, "reading message <%s ...>\n", cmd);
#endif
              switch(cmd[0]){
                case KATCP_INFORM : 
#if 0
                  if(!strcmp(KATCP_VERSION_CONNECT_INFORM, cmd)){
                    display = 0;
                  }
                  if(!strcmp(KATCP_VERSION_INFORM, cmd)){
                    display = 0;
                  }
                  if(!strcmp(KATCP_BUILD_STATE_INFORM, cmd)){
                    display = 0;
                  }
#endif
                  break;
                case KATCP_REPLY : 

                  switch(cmd[1]){
                    case ' '  :
                    case '\n' : 
                    case '\r' :
                    case '\t' :
                    case '\\' :
                    case '\0' :
                      if(k){
                        sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "unreasonable response message");
                      } 
                      fin += update_state(rx, RX_BAD);
                      break;
                    default : 
                      ptr = cmd + 1;
                      if(strcmp(ptr, rx->r_match)){
                        if(k){
                          sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "did not issue request matching response %s", ptr);
                        } 
                        fin += update_state(rx, RX_BAD);
                      } else {
                        parm = arg_string_katcl(rx->r_line, 1);
                        if(parm && !strcmp(parm, KATCP_OK)){
                          if(k){
                            log_message_katcl(k, KATCP_LEVEL_TRACE, KCPPAR_NAME, "request %s to %s returned ok", ptr, rx->r_name);
                          } 
                          result = next_request(rx);
                          if(result){
                            if(result < 0){
                              if(k){
                                sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "unable to queue request %s to %s", ptr, rx->r_name);
                              } 
                              fin += update_state(rx, RX_BAD);
                            } else {
                              fin += update_state(rx, RX_DONE);
                            } 
                          }
                        } else {
                          if(k){
                            sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPPAR_NAME, "request %s to %s failed", cmd, rx->r_name);
                          } 
                          fin += update_state(rx, RX_BAD);
                        }
                      }
                      break;
                  }
                  break;
                case KATCP_REQUEST : 
                  if(k){
                    sync_message_katcl(k, KATCP_LEVEL_WARN, KCPPAR_NAME, "encountered unanswerable request %s", cmd);
                  } 
                  fin += update_state(rx, RX_BAD);
                  break;
                default :
                  if(k){
                    sync_message_katcl(k, KATCP_LEVEL_WARN, KCPPAR_NAME, "read malformed message %s", cmd);
                  } 
                  break;
              }

            }
          }
          break;

        /* case RX_DONE : */
        /* case RX_BAD : */
        default :
          break;
      }
    }
  }

  destroy_set(ss);

  if(k){
    while(write_katcl(k) == 0);
    destroy_katcl(k, 0);
  }

  return status;
}