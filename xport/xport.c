/* (c) 2012 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define REGISTER_POWERSTATE  0x280

#define DEBUG

#define NAME "xport"

#define DEFAULT_PORT 10001

#define STATE_UNKNOWN   0
#define STATE_UP        1
#define STATE_DOWN      2
#define STATE_STARTING  3

struct roach{
  char *r_name;
  struct sockaddr_in r_sa;
  int r_state;
};

struct state{
  struct roach **s_vector;
  unsigned int s_count;
  unsigned int s_current;

  int s_fd;
  struct katcl_line *s_up;
};

/*********************************************************************/

void free_roach(struct roach *r);

/*********************************************************************/

void destroy_state(struct state *s)
{
  int i;

  if(s == NULL){
    return;
  }

  if(s->s_vector){
    for(i = 0; i < s->s_count; i++){
      free_roach(s->s_vector[i]);
      s->s_vector[i] = NULL;
    }
    free(s->s_vector);
  }

  if(s->s_fd >= 0){
    close(s->s_fd);
  }

  if(s->s_up){
    destroy_katcl(s->s_up, 0);
    s->s_up = NULL;
  }

  free(s);
}

struct state *create_state(int fd)
{
  struct state *s;

  s = malloc(sizeof(struct state));
  if(s == NULL){
    return NULL;
  }

  s->s_vector = NULL;
  s->s_count = 0;
  s->s_current = 0;

  s->s_up = create_katcl(fd);
  if(s->s_up == NULL){
    destroy_state(s);
    return NULL;
  }

  return s;
}

/*********************************************************************/

void free_roach(struct roach *r)
{
  if(r == NULL){
    return;
  }

  if(r->r_name){
    free(r->r_name);
    r->r_name = NULL;
  }

  free(r);
}

int add_roach(struct state *s, char *name)
{
  struct roach *r, **tmp;
  struct hostent *he;

  r = malloc(sizeof(struct roach));
  if(r == NULL){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "unable to allocate %d bytes", sizeof(struct roach));
    return -1;
  }

  r->r_state = STATE_UNKNOWN;
  r->r_name = strdup(name);

  if(r->r_name == NULL){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "unable to duplicate string %s", name);
    free_roach(r);
    return -1;
  }

  if(inet_aton(name, &(r->r_sa.sin_addr)) == 0){
    he = gethostbyname(name);
    if((he == NULL) || (he->h_addrtype != AF_INET)){
      sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "unable to resolve roach name %s", name);
      free_roach(r);
      return -1;
    } else {
      r->r_sa.sin_addr = *(struct in_addr *) he->h_addr;
    }
  }

  r->r_sa.sin_port = htons(DEFAULT_PORT);
  r->r_sa.sin_family = AF_INET;

  tmp = realloc(s->s_vector, sizeof(struct roach *) * (s->s_count + 1));
  if(tmp == NULL){
    return -1;
  }

  s->s_vector = tmp;

  s->s_vector[s->s_count] = r;
  s->s_count++;

  return 0;
}

int issue_read(struct state *s, struct roach *r, unsigned int address)
{
  char buffer[3];
  int wr;

  buffer[0] = 0x01;
  buffer[1] = address & 0xff;
  buffer[2] = (address >> 8) & 0xff;

  wr = sendto(s->s_fd, buffer, 3, MSG_DONTWAIT | MSG_NOSIGNAL, &(r->r_sa), sizeof(struct sockaddr_in));

  if(wr < 0){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "unable to issue read to roach %s", r->r_name, strerror(errno));
    return -1;
  }

  if(wr != 3){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "incomplete io to roach %s");
    return -1;
  }

  return 0;
}

int issue_write(struct state *s, struct roach *r, unsigned int address, unsigned int value)
{
  char buffer[5];
  int wr;

  buffer[0] = 0x01;
  buffer[1] = address & 0xff;
  buffer[2] = (address >> 8) & 0xff;
  buffer[3] = value & 0xff;
  buffer[4] = (value >> 8) & 0xff;

  wr = sendto(s->s_fd, buffer, 5, MSG_DONTWAIT | MSG_NOSIGNAL, &(r->r_sa), sizeof(struct sockaddr_in));

  if(wr < 0){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "unable to issue read to roach %s", r->r_name, strerror(errno));
    return -1;
  }

  if(wr != 5){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "incomplete io to roach %s");
    return -1;
  }

  return 0;
}

int setup_network(struct state *s)
{
  s->s_fd = socket(AF_INET, SOCK_DGRAM, 0);

  if(s->s_fd < 0){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, NAME, "unable to allocate a socket: %s", strerror(errno));

    return -1;
  }

  return 0;
}

/*******************************************************************************/

void usage(char *app)
{
  printf("usage: %s [flags] [command-string]*\n", app);
  printf("-h                 this help\n");
  printf("-v                 increase verbosity\n");
  printf("-q                 run quietly\n");
#if 0
  printf("-k                 emit katcp log messages\n");
  printf("-r                 toggle printing of reply messages\n");
  printf("-i                 toggle printing of inform messages\n");
#endif

  printf("return codes:\n");
  printf("0     command completed successfully\n");
  printf("1     command failed\n");
  printf("3     network problems\n");
  printf("2     usage problems\n");
  printf("4     internal errors\n");

  printf("notes:\n");
  printf("  command and parameters have to be given as single quoted strings\n");
}

#define BUFFER 64

int main(int argc, char **argv)
{
  struct state *ss;
  struct roach *r;
  struct sockaddr_in sa;
  fd_set fsr, fsw;
  char *cmd;
  int i, j, c, mfd, fd, verbose, result, status, len, run;
  sigset_t mask_current, mask_previous;
  struct sigaction action_current, action_previous;
  pid_t pid;
  char buffer[BUFFER];

  ss = create_state(STDOUT_FILENO);
  if(ss == NULL){
    return 4;
  }

#if 0
  char *app, *parm, *cmd, *copy, *ptr, *servers;
  int verbose, result, status, base, info, reply, timeout, pos, flags, show;
  int xmit, code;
  unsigned int len;
  
  info = 1;
  reply = 1;
  i = j = 1;
  app = argv[0];
  base = (-1);
  timeout = 5;
  pos = (-1);
  k = NULL;
  show = 1;
  parm = NULL;
#endif

  verbose = 1;
  i = j = 1;

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(NAME);
          return 0;

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          j++;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;

        default:
          sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unknown option -%c", argv[i][j]);
          return 2;
      }
    } else {

      if(add_roach(ss, argv[i]) < 0){
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to add roach %s", argv[i]);
        return 4;
      }
      i++;
    }
  }

  if(setup_network(ss) < 0){
    return 3;
  }

  for(run = 1; run;){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    mfd = ss->s_fd;

    FD_SET(ss->s_fd, &fsw);
    FD_SET(ss->s_fd, &fsr);

    if(flushing_katcl(ss->s_up)){
      fd = fileno_katcl(ss->s_up);
      FD_SET(fd, &fsw);
      if(fd > mfd){
        mfd = fd;
      }
    }

    result = select(mfd + 1, &fsr, &fsw, NULL, NULL);
    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "select failed: %s", strerror(errno));
            return 4;
        }
        break;
      case  0 :
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "requests timed out despite having no timeout");
        /* could terminate cleanly here, but ... */
        return 4;
    }

    
    fd = fileno_katcl(ss->s_up);
    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(ss->s_up);
    }

    if(FD_ISSET(ss->s_fd, &fsw)){
      r = ss->s_vector[ss->s_current];
      switch(r->r_state){
        case STATE_UNKNOWN :
          if(issue_read(ss, r, REGISTER_POWERSTATE) < 0){

          }
          break;
      }
    }

    if(FD_ISSET(ss->s_fd, &fsr)){
      len = sizeof(struct sockaddr_in);
      result = recvfrom(ss->s_fd, buffer, BUFFER, 0, &sa, &len);
      if(result < 0){
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "receive failed: %s", strerror(errno));
      }

      if(result == 0){
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "empty packet from");
      }

      switch(buffer[0]){
        default :
          log_message_katcl(ss->s_up, KATCP_LEVEL_TRACE, NAME, "got back message code 0x%x", buffer[0]);
          break;
      }

    }

    sleep(1);

    ss->s_current++;
    if(ss->s_current >= ss->s_count){
      ss->s_current = 0;
    }

  }


#if 0
  sigemptyset(&mask_current);
#if 0
  sigaddset(&mask_current, SIGTERM);
#endif

  for(ss->s_finished = 0; ss->s_finished < ss->s_count;){



    for(i = 0; i < ss->s_count; i++){
      cx = ss->s_vector[i];
      if(cx->c_line){
        fd = fileno_katcl(cx->c_line);
        if(fd > mfd){
          mfd = fd;
        }
        FD_SET(fd, &fsr);
      } 
    }

    result = pselect(mfd + 1, &fsr, &fsw, NULL, NULL, &mask_current);
#ifdef DEBUG
    fprintf(stderr, "select returns %d\n", result);
#endif
    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "select failed: %s", strerror(errno));
            return 4;
        }
        break;
      case  0 :
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "requests timed out despite having no timeout");
        /* could terminate cleanly here, but ... */
        return 4;
    }


    fd = fileno_katcl(ss->s_up);
    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(ss->s_up);
    }

    for(i = 0; i < ss->s_count; i++){
      cx = ss->s_vector[i];
      if(cx->c_line){
        fd = fileno_katcl(cx->c_line);

        if(FD_ISSET(fd, &fsr)){ /* get things */
          result = read_katcl(cx->c_line);
          if(result){
            if(result < 0){
              sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "read failed: %s", strerror(error_katcl(cx->c_line)));
            } else {
              log_message_katcl(ss->s_up, KATCP_LEVEL_INFO, KCPCON_NAME, "subordinate job %u ended", cx->c_pid);
            }
            destroy_katcl(cx->c_line, 1);
            cx->c_line = NULL;
            ss->s_finished++;
            continue; /* WARNING */
          }
        }

        while(have_katcl(cx->c_line) > 0){ /* compute */
          cmd = arg_string_katcl(cx->c_line, 0);
          if(cmd){
#ifdef DEBUG
            fprintf(stderr, "reading message <%s ...>\n", cmd);
#endif
            switch(cmd[0]){
              case KATCP_INFORM : 
#if 0
                if(!strcmp(KATCP_VERSION_CONNECT_INFORM, cmd)){
                }
                if(!strcmp(KATCP_VERSION_INFORM, cmd)){
                }
                if(!strcmp(KATCP_BUILD_STATE_INFORM, cmd)){
                }
#endif
                relay_katcl(cx->c_line, ss->s_up);
                break;
            }
          }
        }
      }
    }

    /* the position of this logic is rather intricate */
    if(got_child_signal){
      got_child_signal = 0;
      while((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0){
        for(i = 0; i < ss->s_count; i++){
          cx = ss->s_vector[i];
          if(cx->c_pid == pid){

            if (WIFEXITED(status)) {
              result = WEXITSTATUS(status);
              log_message_katcl(ss->s_up, KATCP_LEVEL_INFO, KCPCON_NAME, "subordinate job[%u] %u exited with code %d", i, cx->c_pid, result);
              cx->c_status = (result > 4) ? 4 : result;
            } else if (WIFSIGNALED(status)) {
              result = WTERMSIG(status);
              log_message_katcl(ss->s_up, KATCP_LEVEL_WARN, KCPCON_NAME, "subordinate job[%u] %u killed by signal %d", i, cx->c_pid, result);
              cx->c_status = 4;
            } else {
              log_message_katcl(ss->s_up, KATCP_LEVEL_WARN, KCPCON_NAME, "subordinate job[%u] %u return unexpected status %d", i, cx->c_pid, status);
              cx->c_status = 4;
            }

            if(cx->c_status > ss->s_code){
              ss->s_code = cx->c_status;
            }

            cx->c_pid = (-1);
          }
        }
      }
    }
  }


  /* WARNING: pointlessly fussy */
  sigaction(SIGCHLD, &action_previous, NULL);
  sigprocmask(SIG_BLOCK, &mask_previous, NULL);

#endif

  /* force drain */
  while(write_katcl(ss->s_up) == 0);
  destroy_state(ss);

  return result;
}

