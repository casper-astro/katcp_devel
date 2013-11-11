/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sysexits.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>


#include "katpriv.h"
#include "katcl.h"
#include "katcp.h"
#include "netc.h"

#define KATCP_INIT_WAIT    3600
#define KATCP_HALT_WAIT       1
#define KATCP_BRIEF_WAIT  10000   /* 10 ms */

int inform_client_connections_katcp(struct katcp_dispatch *d, char *type)
{
  int i;
  struct katcp_shared *s;
  struct katcp_dispatch *dx;

  s = d->d_shared;

#ifdef DEBUG
  if(s == NULL){
    fprintf(stderr, "inform: no shared state\n");
    abort();
  }
#endif

  for(i = 0; i < s->s_used; i++){
    dx = s->s_clients[i];

    if(dx != d){
#ifdef DEBUG
      fprintf(stderr, "multi[%d]: informing %p of new connection\n", i, d);
#endif

      send_katcp(dx, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, type, KATCP_FLAG_LAST | KATCP_FLAG_STRING, d->d_name);
    }
  }

  return 0;
}

static int client_list_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;
  unsigned int detail;
  unsigned long count;
  char *ptr, *level;

  s = d->d_shared;
  count = 0;
  detail = 0;

  if(argc > 1){
    ptr = arg_string_katcp(d, 1);
    if(ptr && !strcmp(ptr, "detailed")){
      detail = 1;
    }
  }

  if(s != NULL){
    while(count < s->s_used){
      dx = s->s_clients[count];

      prepend_inform_katcp(d);

      if(detail){
        append_string_katcp(d, KATCP_FLAG_STRING, dx->d_name);

        level = log_to_string_katcl(dx->d_level);
        append_string_katcp(d, KATCP_FLAG_STRING, level ? level : "unknown");

        append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, dx->d_pause ? "paused" : "parsing");

      } else {
        append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, dx->d_name);
      }
      count++;
    }
  }

#if 0
  if(count > 0){
    send_katcp(d, 
      KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!client-list", 
                         KATCP_FLAG_STRING, KATCP_OK,
      KATCP_FLAG_LAST  | KATCP_FLAG_ULONG, (unsigned long) count);
  } else {
    send_katcp(d, 
      KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!client-list", 
                         KATCP_FLAG_STRING, KATCP_FAIL,
      KATCP_FLAG_LAST  | KATCP_FLAG_STRING, "internal state inaccessible");
  }
#endif

  prepend_reply_katcp(d);
  if(count > 0){
    append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
    append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, count);
  } else {
    append_string_katcp(d, KATCP_FLAG_STRING, KATCP_FAIL);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "internal state inaccessible");
  }
  
  return KATCP_RESULT_OWN;
}

static int pipe_from_file_katcp(struct katcp_dispatch *dl, char *file)
{
#define S_READ 1
#define S_PARSE 2
#define S_DONE 3
  int fds[2], fd;
  pid_t pid;
  struct katcl_line *pl, *fl;
  int done;
  int state, rsvp;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    return -1;
  }

  pid = fork();
  if(pid < 0){
    close(fds[0]);
    close(fds[1]);
    return -1;
  }

  if(pid > 0){
    close(fds[0]);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return fds[1];
  }

  /* now in child */

#if 0 /* risky, might clobber stuff we still need */
  shutdown_katcp(dl);
#endif

  close(fds[1]);

  fd = open(file, O_RDONLY);
  if(fd < 0){
    exit(EX_UNAVAILABLE);
  }

  pl = create_katcl(fds[0]);
  fl = create_katcl(fd);

  if((pl == NULL) || (fl == NULL)){
    exit(EX_UNAVAILABLE);
  }

  for(done = 0; done == 0;){

    done = read_katcl(fl);
    if(done < 0){
      exit(EX_UNAVAILABLE);
    }
    
    while(have_katcl(fl)){
      if(arg_request_katcl(fl)){
#ifdef DEBUG
        fprintf(stderr,"init: found request: %s\n",arg_string_katcl(fl,0));
#endif
        rsvp = relay_katcl(fl, pl);

        if(rsvp < 0){
#ifdef DEBUG
          fprintf(stderr,"init: relay_katcl: Unable to relay\n");
#endif
          exit(EX_UNAVAILABLE);
        }
        else if(!write_katcl(pl)){
          exit(EX_UNAVAILABLE);
        }
        
        for (state = S_READ; state != S_DONE; ){
          switch (state){
            case S_READ:
                rsvp = read_katcl(pl);
                if (rsvp == 0)
                  state = S_PARSE;
              break;
            case S_PARSE:
                
                if (have_katcl(pl)){
                  if(arg_reply_katcl(pl)){
#ifdef DEBUG
                    fprintf(stderr,"Found REPLY: %s\n",arg_string_katcl(pl,0));
#endif
                    state = S_DONE;
                  }
                  else if (arg_inform_katcl(pl)){
                    state = S_PARSE;
                  }
                }
                else {
                  state = S_READ;
                }
              break;
          }
        }
      }
    }

  }

  exit(EX_OK);
  return -1;
#undef S_READ
#undef S_PARSE
#undef S_DONE
}

void add_client_server_katcp(struct katcp_dispatch *dl, int fd, char *label)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;

  s = dl->d_shared;

#ifdef DEBUG
  if(s->s_used >= s->s_count){
    fprintf(stderr, "add client: need a free slot to add a client\n");
    abort();
  }
#endif

  log_message_katcp(dl, KATCP_LEVEL_INFO, NULL, "new client connection %s", label);

  fcntl(fd, F_SETFD, FD_CLOEXEC);

  dx = s->s_clients[s->s_used];
  s->s_used++;

  reset_katcp(dx, fd);
  name_katcp(dx, "%s", label);

  inform_client_connections_katcp(dx, KATCP_CLIENT_CONNECT); /* will not send to itself */

  print_versions_katcp(dx, KATCP_PRINT_VERSION_CONNECT);
#if 0
  on_connect_katcp(dx);
#endif
}

void perforate_client_server_katcp(struct katcp_dispatch *dl)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;

  s = dl->d_shared;

  if(s->s_used >= s->s_count){

    dx = s->s_clients[(unsigned int)rand() % s->s_count];

    basic_inform_katcp(dx, KATCP_CLIENT_DISCONNECT, "overloaded");

    terminate_katcp(dx, KATCP_EXIT_QUIT);
#if 0
    on_disconnect_katcp(dx, "displaced by new client connection");
#endif
  }
}

static int update_flags_katcp(struct katcp_dispatch *d, int argc, int flags)
{
  char *match;
  int i;

  if(d->d_shared == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(argc < 2){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  for(i = 1; i < argc; i++){
    match = arg_string_katcp(d, i);
    if(match == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire parameter %d", i);
      return KATCP_RESULT_FAIL;
    }

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "about to update command %s", match);

    if(update_command_katcp(d, match, flags) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to hide or expose command %s", match);
      return KATCP_RESULT_FAIL;
    }
  }

  return KATCP_RESULT_OK;
}

int expose_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return update_flags_katcp(d, argc, 0);
}

int hide_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return update_flags_katcp(d, argc, KATCP_CMD_HIDDEN);
}

int forget_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *match;

  if(d->d_shared == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(argc < 2){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  match = arg_string_katcp(d, 1);
  if(match == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a command to forget");
    return KATCP_RESULT_FAIL;
  }

  if(deregister_command_katcp(d, match) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to deregister command");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int chdir_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *dir;

  if(d->d_shared == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(argc < 2){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  dir = arg_string_katcp(d, 1);
  if(dir == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrive directory argument");
    return KATCP_RESULT_FAIL;
  }

  if(chdir(dir) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to change to %s %s", dir, strerror(errno));
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int setenv_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *label, *value;

  if(d->d_shared == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(argc < 2){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  label = arg_string_katcp(d, 1);
  if(argc == 2){
    if(unsetenv(label) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to clear variable %s : %s", label, strerror(errno));
      return KATCP_RESULT_FAIL;
    }
    return KATCP_RESULT_OK;
  }

  value = arg_string_katcp(d, 2);
  if(value == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire value for variable %s", label);
    return KATCP_RESULT_FAIL;
  }

  if(setenv(label, value, 1) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to set variable %s : %s", label, strerror(errno));
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int system_info_cmd_katcp(struct katcp_dispatch *d, int argc)
{
#define BUFFER 64
  char buffer[BUFFER];
  struct tm *when;
  time_t now, delta;
  struct katcp_shared *s;
  unsigned long hours;
  unsigned int minutes, seconds;

  s = d->d_shared;

  when = localtime(&(s->s_start));
  if(when == NULL){
    return KATCP_RESULT_FAIL;
  }

  time(&now);

  delta = now - s->s_start;

  hours   = (delta / 3600);
  minutes = (delta / 60)     - (hours * 60);
  seconds = (delta)          - (((hours * 60) + minutes) * 60);

  strftime(buffer, BUFFER - 1, "%Y-%m-%dT%H:%M:%S", when);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "server started at %s localtime which is T%lu:%02u:%02u ago", buffer, hours, minutes, seconds);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d active client %s", s->s_used, (s->s_used == 1) ? "connection" : "connections");
#ifdef KATCP_SUBPROCESS
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d subordinate %s", s->s_number, (s->s_number == 1) ? "job" : "jobs");
#endif

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d %s", s->s_tally, (s->s_tally == 1) ? "sensor" : "sensors");

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d %s", s->s_size, (s->s_size == 1) ? "mode" : "modes");
  if(s->s_size > 1){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d (%s) is current %s mode", s->s_mode,  s->s_vector[s->s_mode].e_name ? s->s_vector[s->s_mode].e_name : "UNKNOWN", s->s_flaky ? "failed" : "ok");
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d %s", s->s_pending, (s->s_pending == 1) ? "notice" : "notices");

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d %s scheduled", s->s_length, (s->s_length == 1) ? "timer" : "timers");

  return KATCP_RESULT_OK;
#undef BUFFER
}

#if 0
int uptime_cmd_katcp(struct katcp_dispatch *d, int argc)
{
#define BUFFER 64
  char buffer[BUFFER];
  struct tm *when;
  time_t now, delta;
  struct katcp_shared *s;
  unsigned long hours;
  unsigned int minutes, seconds;

  s = d->d_shared;

  when = localtime(&(s->s_start));
  if(when == NULL){
    return KATCP_RESULT_FAIL;
  }

  time(&now);

  delta = now - s->s_start;

  hours   = (delta / 3600);
  minutes = (delta / 60)     - (hours * 60);
  seconds = (delta)          - (minutes * 60);

  strftime(buffer, BUFFER - 1, "%Y-%m-%dT%H:%M:%S", when);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "server started at %s localtime which is T%lu:%02u:%02u ago", buffer, hours, minutes, seconds);

  return KATCP_RESULT_OK;
#undef BUFFER
}
#endif

int prepare_core_loop_katcp(struct katcp_dispatch *dl)
{
  struct katcp_shared *s;

#ifdef DEBUG
  fprintf(stderr, "running prepare core loop\n");
#endif

  dl->d_exit = KATCP_EXIT_ABORT; /* assume the worst */

  s = dl->d_shared;

  /* double check this logic */
  if(s == NULL){
#ifdef DEBUG
    fprintf(stderr, "no shared state in core loop preparation\n");
#endif
    return -1;
  }

  add_code_version_katcp(dl);
  add_kernel_version_katcp(dl);

  /* extra commands, not really part of the standard */
  register_flag_mode_katcp(dl, "?setenv",  "sets/clears an enviroment variable (?setenv [label [value]]", &setenv_cmd_katcp, KATCP_CMD_HIDDEN, 0);
  register_flag_mode_katcp(dl, "?chdir",   "change directory (?chdir directory)", &chdir_cmd_katcp, KATCP_CMD_HIDDEN, 0);

  register_flag_mode_katcp(dl, "?forget",  "deregister a command (?forget command)", &forget_cmd_katcp, KATCP_CMD_HIDDEN, 0);
  register_flag_mode_katcp(dl, "?hide",    "hide a command (?hide command ...)", &hide_cmd_katcp, KATCP_CMD_HIDDEN, 0);
  register_flag_mode_katcp(dl, "?expose",  "unhide a command (?expose command ...)", &expose_cmd_katcp, KATCP_CMD_HIDDEN, 0);

  register_flag_mode_katcp(dl, "?dispatch","dispatch operations (?dispatch [list])", &dispatch_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?notice",  "notice operations (?notice [list|watch|wake])", &notice_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?define",  "runtime definitions (?define [mode name]", &define_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?arb",     "arbitrary callback manipulation (?arb [list]", &arb_cmd_katcp, 0, 0);

#ifdef KATCP_SUBPROCESS
  register_flag_mode_katcp(dl, "?job",     "job operations (?job [list|process notice-name exec://executable-file|network notice-name katcp://net-host:remote-port|watchdog job-name|match job-name inform-message|stop job-name])", &job_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?process", "register a process command (?process executable help-string [mode]", &register_subprocess_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?sensor",  "sensor operations (?sensor [list|create|relay job-name])", &sensor_cmd_katcp, 0, 0);
#else
  register_flag_mode_katcp(dl, "?sensor",  "sensor operations (?sensor [list|create])", &sensor_cmd_katcp, 0, 0);
#endif
  register_flag_mode_katcp(dl, "?version", "version operations (?sensor [add module version [mode]|remove module])", &version_cmd_katcp, 0, 0);

  register_flag_mode_katcp(dl, "?system-info",  "report server information (?system-info)", &system_info_cmd_katcp, 0, 0);

#ifdef KATCP_EXPERIMENTAL
  register_flag_mode_katcp(dl, "?listen-duplex", "accept new duplex connections on given interface (?listen-duplex [interface:]port)", &listen_duplex_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?list-duplex",  "report duplex information (?list-duplex)", &list_duplex_cmd_katcp, 0, 0);
#endif

  time(&(s->s_start));

  /* used to randomly select a client to displace when a new connection arrives and table is full */
  srand(getpid());

  return 0;
}

int run_core_loop_katcp(struct katcp_dispatch *dl)
{
#define LABEL_BUFFER 32
  int run, nfd, result, suspend;
  unsigned int len;
  struct sockaddr_in sa;
  struct timespec delta;
  struct katcp_shared *s;
  char label[LABEL_BUFFER];
  long opts;

  s = dl->d_shared;

  /* setup child signal routines, in case they haven't yet */
  init_signals_shared_katcp(s);

  run = 1;

  while(run){
    FD_ZERO(&(s->s_read));
    FD_ZERO(&(s->s_write));

#if 0
    gettimeofday(&now, NULL);
    future.tv_sec = now.tv_sec + KATCP_INIT_WAIT;
    future.tv_usec = now.tv_usec;
#endif

    s->s_max = (-1);

    suspend = run_timers_katcp(dl, &delta);

    if(run > 0){ /* only bother with new connections if not stopping */
      if(s->s_lfd >= 0){
        FD_SET(s->s_lfd, &(s->s_read));
        if(s->s_lfd > s->s_max){
          s->s_max = s->s_lfd;
        }
      } else {
        if(s->s_used <= 0){ /* if we are not listening, and we have run out of clients, shut down too */
          run = (-1);
        }
      }

    }

#ifdef KATCP_SUBPROCESS
    load_jobs_katcp(dl);
#endif
    load_arb_katcp(dl);

    if(load_shared_katcp(dl) < 0){ /* want to shut down */
      run = (-1);
    }

#ifdef KATCP_EXPERIMENTAL
    /* WARNING: new code */
    if(load_flat_katcp(dl) < 0){
      run = (-1);
    }
#endif


    if(run < 0){

      run = 0; /* assume we have stopped, revert to stopping if the below still need to do work */

      if(ended_shared_katcp(dl) <= 0){
        run = (-1);
      }

#ifdef KATCP_SUBPROCESS
      if(ended_jobs_katcp(dl) <= 0){
        run = (-1);
      }
#endif

      delta.tv_sec = KATCP_HALT_WAIT;
      delta.tv_nsec = 0;

      suspend = 0;
      FD_ZERO(&(s->s_read));
    }
    
    if(s->s_busy > 0){
      suspend = 0;
      delta.tv_sec = 0;
      delta.tv_nsec = KATCP_BRIEF_WAIT;
    }

#ifdef DEBUG
    if(suspend){
      fprintf(stderr, "multi: selecting indefinitely\n");
    } else {
      fprintf(stderr, "multi: selecting for %lu.%lu\n", delta.tv_sec, delta.tv_nsec);
    }
#endif

    /* delta now timespec, not timeval */
    result = pselect(s->s_max + 1, &(s->s_read), &(s->s_write), NULL, suspend ? NULL : &delta, &(s->s_mask_current));
#ifdef DEBUG
    fprintf(stderr, "multi: select=%d, used=%d\n", result, s->s_used);
#endif

    s->s_busy = 0;

    if(result < 0){

      FD_ZERO(&(s->s_read));
      FD_ZERO(&(s->s_write));

      switch(errno){
        case EAGAIN :
        case EINTR  :
/*        case ERESTARTSYS : */
          result = 0;
          break;
        default  :
#ifdef DEBUG
          fprintf(stderr, "select failed: %s\n", strerror(errno));
#endif
          run = 0; 
          break;
      }
    }

    if(child_signal_shared_katcp(s)){
#ifdef DEBUG
      fprintf(stderr, "multi: saw child signal\n");
#endif
#ifdef KATCP_SUBPROCESS
      wait_jobs_katcp(dl);
#endif
    }

#ifdef KATCP_EXPERIMENTAL
    /* WARNING: new logic */
    run_flat_katcp(dl);
#endif

#ifdef KATCP_EXPERIMENTAL 
    run_endpoints_katcp(dl);
#endif

    run_shared_katcp(dl);
#ifdef KATCP_SUBPROCESS
    run_jobs_katcp(dl);
#endif
    run_notices_katcp(dl);
    run_arb_katcp(dl);

    if(FD_ISSET(s->s_lfd, &(s->s_read))){
      if(s->s_used < s->s_count){

        len = sizeof(struct sockaddr_in);
        nfd = accept(s->s_lfd, (struct sockaddr *) &sa, &len);

        if(nfd >= 0){
          snprintf(label, LABEL_BUFFER, "%s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
          label[LABEL_BUFFER - 1] = '\0';

          add_client_server_katcp(dl, nfd, label);
        }

#if 1
        opts = fcntl(nfd, F_GETFL, NULL);
        if(opts >= 0){
          opts = fcntl(nfd, F_SETFL, opts | O_NONBLOCK);
        }
#endif

      } else {
        /* WARNING: will run a busy loop, terminating one entry each cycle until space becomes available. We expect an exit to happen quickly, otherwise this could empty out all clients (though there is a backoff, since we pick a slot randomly) */
        perforate_client_server_katcp(dl);
      }
    }

  }

#ifdef DEBUG
  fprintf(stderr, "core loop: finished: global run=%d\n", run);
#endif

  /* clean up, but also done in destroy shared */
  undo_signals_shared_katcp(s);

  return (exited_katcp(dl) == KATCP_EXIT_ABORT) ? (-1) : 0;
#undef LABEL_BUFFER
}

int run_pipe_server_katcp(struct katcp_dispatch *dl, char *file, int pfd)
{
  int fd;
  unsigned int need;

  if(pfd < 0){
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  /* in case we are given a script file, we need two slots */
  need = file ? 2 : 1;
  if(allocate_clients_shared_katcp(dl, need) < need){
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  if(prepare_core_loop_katcp(dl) < 0){
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }
  
  if(file){
    fd = pipe_from_file_katcp(dl, file);
    if(fd < 0){
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
    add_client_server_katcp(dl, fd, file);
  }

  if(clone_katcp(dl)){
    add_client_server_katcp(dl, pfd, "-");
  } else {
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  return run_core_loop_katcp(dl);
}

int run_config_server_katcp(struct katcp_dispatch *dl, char *file, int count, char *host, int port)
{
  int fd, result;

  if(count <= 0){
#ifdef DEBUG
    fprintf(stderr, "server: need a natural number of clients, not %d\n", count);
#endif
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  if(allocate_clients_shared_katcp(dl, count) <= 0){
#ifdef DEBUG
    fprintf(stderr, "allocation of %d clients failed\n", count);
#endif
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  if(prepare_core_loop_katcp(dl) < 0){
#ifdef DEBUG
    fprintf(stderr, "server: need a natural number of clients, not %d\n", count);
#endif
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  if(count > 1){
#ifdef DEBUG
    fprintf(stderr, "multi: more than one client, registering client list\n");
#endif
    register_katcp(dl, "?client-list", "displays client list (?client-list)", &client_list_cmd_katcp);
  }

  if(file){
    fd = pipe_from_file_katcp(dl, file);
    if(fd < 0){
#ifdef DEBUG
    fprintf(stderr, "creation of pipe from file failed\n");
#endif
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
    add_client_server_katcp(dl, fd, file);
  }

  result = listen_shared_katcp(dl, host, port);
  if(result < 0){
#ifdef DEBUG
    fprintf(stderr, "multi: unable to initiate server listen\n");
#endif
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  return run_core_loop_katcp(dl);
}

int load_from_file_katcp(struct katcp_dispatch *d, char *file)
{
  int fd;

  if(file){
    fd = pipe_from_file_katcp(d, file);
    if(fd < 0){
#ifdef DEBUG
      fprintf(stderr, "creation of pipe from file failed\n");
#endif
      return -1;
    }
    add_client_server_katcp(d, fd, file);
    return 0;
  }
  
  return -1;
}

