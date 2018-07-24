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

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define KCPCON_NAME "concurrent"

#define BUFFER 1024
#define TIMEOUT   4

#define RX_SETUP 1 
#define RX_UP    2 
#define RX_OK    0
#define RX_FAIL  (-1)
#define RX_BAD   (-2)

struct child{
  pid_t c_pid;
  struct katcl_line *c_line;
  int c_status;
};

struct state{
  struct child **s_vector;
  unsigned int s_count;
  unsigned int s_finished;

  struct katcl_line *s_up;
  int s_code;
};

/*********************************************************************/

static volatile int got_child_signal = 0;

static void handle_child(int signal)
{
#ifdef DEBUG
  fprintf(stderr, "received child signal\n");
#endif
  got_child_signal++;
}

/*********************************************************************/


void destroy_child(struct child *c)
{
  if(c == NULL){
    return;
  }

  if(c->c_pid > 0){
    kill(c->c_pid, SIGHUP);
    c->c_pid = 0;
  }

  if(c->c_line){
    destroy_katcl(c->c_line, 1);
    c->c_line = NULL;
  }

  c->c_status = (-1);

  free(c);
}

/*********************************************************************/

void destroy_state(struct state *s)
{
  unsigned int i;

  if(s == NULL){
    return;
  }

  if(s->s_up){
    destroy_katcl(s->s_up, 0);
    s->s_up = NULL;
  }

  if(s->s_vector){
    for(i = 0; i < s->s_count; i++){
      destroy_child(s->s_vector[i]);
      s->s_vector[i] = NULL;
    }
    free(s->s_vector);
    s->s_vector = NULL;
  }
  s->s_count = 0;
  s->s_finished = 0;
  s->s_code = 0;

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
  s->s_finished = 0;
  s->s_code = 0;

  s->s_up = create_katcl(fd);
  if(s->s_up == NULL){
    destroy_state(s);
    return NULL;
  }

  return s;
}

/*********************************************************************/

int launch_child(struct state *s, char **vector)
{
  int fds[2], fd;
  pid_t pid;
  struct child **tmp;
  struct child *c;
  struct katcl_line *k;
  unsigned int i;

  tmp = realloc(s->s_vector, sizeof(struct child *) * (s->s_count + 1));
  if(tmp == NULL){
    return -1;
  }

  s->s_vector = tmp;

  c = malloc(sizeof(struct child));
  if(c == NULL){
    return -1;
  }

  c->c_pid = 0;
  c->c_line = NULL;
  c->c_status = 0;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "unable to allocate socketpair: %s", strerror(errno));
    destroy_child(c);
    return -1;
  }

  pid = fork();
  if(pid < 0){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "unable to fork: %s", strerror(errno));
    close(fds[0]);
    close(fds[1]);
    destroy_child(c);
    return -1;
  }

  if(pid > 0){
    c->c_pid = pid;

    close(fds[0]);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    c->c_line = create_katcl(fds[1]);
    if(c->c_line == NULL){
      destroy_child(c);
    }

    s->s_vector[s->s_count] = c;
    s->s_count++;

    return 0;
  }

  /* WARNING: can not return from code below, have to exit() */

  close(fds[1]);

  fd = fds[0];

  k = create_katcl(fd);

  for(i = STDIN_FILENO; i <= STDERR_FILENO; i++){
    if(fd != i){
      if(dup2(fd, i) < 0){
        if(k){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPCON_NAME, "unable to duplicate fd %d to replace %d: %s", fd, i, strerror(errno));
        }
        exit(1);
      }
    }
  }

  execvp(vector[0], vector);

  if(k){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, KCPCON_NAME, "unable to run %s: %s", vector[0], strerror(errno));
  }

  exit(1);
  return -1;
}

int string_launch_child(struct state *s, char *string)
{
  char **vector, **tmp;
  char *t;
  unsigned int i, v, j, d, ws;
  int run;

  vector = NULL;
  j = 0; 
  i = 0;
  ws = 1;
  v = 0;

  for(run = 1; run > 0;){
    switch(string[i]){
      case '\0'  :
        run = 0;
        /* fall */
      case ' '   :
      case '\t'  :
        if(ws == 0){
          ws = 1;
          tmp = realloc(vector, sizeof(char *) * (v + 2));
          if(tmp == NULL){
            run = (-1);
          } else {
            vector = tmp;
            d = i - j;
            t = malloc(sizeof(char) * (d + 1));
            if(t == NULL){
              run = (-1);
            } else {
              strncpy(t, string + j, d);
              t[d] = '\0';
              vector[v] = t;
              v++;
              vector[v] = NULL;
            }
          }
        }
        i++;
        break;
      default :
        if(ws){
          j = i;
          ws = 0;
        }
        i++;
      break;
    }
  }

  if(vector == NULL){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "no command found in string <%s>", string);
    return -1;
  }


  if(run < 0){
    sync_message_katcl(s->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "allocation error while decomposing %s", string);
  } else {
#ifdef DEBUG
  fprintf(stderr, "have vector <%s ...> of %u entries\n", vector[0], v);
#endif
    run = launch_child(s, vector);
  }

  if(vector){
    for(i = 0; i < v; i++){
      if(vector[i]){
        free(vector[i]);
        vector[i] = NULL;
      }
    }
    free(vector);
  }

  return run;
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
  printf("2     usage problems\n");
  printf("4     internal errors\n");

  printf("notes:\n");
  printf("  command and parameters have to be given as single quoted strings\n");
}

int main(int argc, char **argv)
{
  struct state *ss;
  struct child *cx;
  fd_set fsr, fsw;
  char *cmd;
  int i, j, c, mfd, fd, verbose, result, status;
  sigset_t mask_current, mask_previous;
  struct sigaction action_current, action_previous;
  pid_t pid;

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

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;

        default:
          sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "unknown option -%c", argv[i][j]);
          return 2;
      }
    } else {
#ifdef DEBUG
      fprintf(stderr, "about to start <%s>\n", argv[i]);
#endif
      if(string_launch_child(ss, argv[i]) < 0){
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, KCPCON_NAME, "unable to start <%s>", argv[i]);
        return 4;
      }
      i++;
    }
  }

  sigprocmask(SIG_SETMASK, NULL, &mask_current);
  sigaddset(&mask_current, SIGCHLD);

  action_current.sa_handler = &handle_child;
  action_current.sa_flags = SA_NOCLDSTOP;
  sigfillset(&(action_current.sa_mask));
  sigaction(SIGCHLD, &action_current, &action_previous);

  sigprocmask(SIG_SETMASK, &mask_current, &mask_previous);

  sigemptyset(&mask_current);
#if 0
  sigaddset(&mask_current, SIGTERM);
#endif

  for(ss->s_finished = 0; ss->s_finished < ss->s_count;){


    mfd = 0;

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    if(flushing_katcl(ss->s_up)){
      mfd = fileno_katcl(ss->s_up);
      FD_SET(mfd, &fsw);
    }

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
              if(cx->c_pid > 0){
                log_message_katcl(ss->s_up, KATCP_LEVEL_DEBUG, KCPCON_NAME, "subordinate job %u ended", cx->c_pid);
              }
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
              log_message_katcl(ss->s_up, KATCP_LEVEL_DEBUG, KCPCON_NAME, "subordinate job[%u] %u exited with code %d", i, cx->c_pid, result);
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

  /* force drain */
  while(write_katcl(ss->s_up) == 0);

  result = ss->s_code;

  destroy_state(ss);

  /* WARNING: pointlessly fussy */
  sigaction(SIGCHLD, &action_previous, NULL);
  sigprocmask(SIG_BLOCK, &mask_previous, NULL);

  return result;
}
