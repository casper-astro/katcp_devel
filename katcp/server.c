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

#define KATCP_INIT_WAIT 3600
#define KATCP_POLL_WAIT    5
#define KATCP_HALT_WAIT    1

static int inform_client_connect_katcp(struct katcp_dispatch *d)
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

      send_katcp(dx, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#client-connected", KATCP_FLAG_LAST | KATCP_FLAG_STRING, dx->d_name);
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

        level = log_to_string(dx->d_level);
        append_string_katcp(d, KATCP_FLAG_STRING, level ? level : "unknown");

        level = log_to_string(dx->d_level);
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
          fprintf(stderr,"init: relay_katcl: Unable to relay\n");
          exit(EX_UNAVAILABLE);
        }
        else if(!write_katcl(pl)){
          exit(EX_UNAVAILABLE);
        }
        
        for (state = S_READ; state != S_DONE; ){
#ifdef DEBUG
          //fprintf(stderr,"STATE: %d\n",state);
#endif
          switch (state){
            case S_READ:
#ifdef DEBUG
                //ifprintf(stderr,"ABOUT TO READ pl\n");
#endif
                rsvp = read_katcl(pl);
                if (rsvp == 0)
                  state = S_PARSE;
              break;
            case S_PARSE:
                
                if (have_katcl(pl)){
                  if(arg_reply_katcl(pl)){
                    fprintf(stderr,"Found REPLY: %s\n",arg_string_katcl(pl,0));
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

#if 0 
    /* this logic to be replaced with something which actually parses
     * requests, and only sends out one request at a time, waiting for replies to
     * arrive */
    rr = read(fd, buffer, READ_BUFFER);
    if(rr <= 0){
      if(rr < 0){
        switch(errno){
          case EAGAIN :
          case EINTR  :
            break;
          default :
            exit(EX_OSERR);
            break;
        }
      } else {
        exit(EX_OK);
      }
    } else {
      hw = 0;
      do{
        wr = write(fds[0], buffer + hw, rr - hw);
        if(wr < 0){
          switch(errno){
            case EAGAIN :
            case EINTR  :
              break;
            default :
              exit(EX_OSERR);
              break;
          }
        } else {
          hw += wr;
        }
      } while(hw < rr);
    }
#endif
  }

  exit(EX_OK);
  return -1;
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

  fcntl(fd, F_SETFD, FD_CLOEXEC);

  dx = s->s_clients[s->s_used];
  s->s_used++;

  reset_katcp(dx, fd);
  name_katcp(dx, "%s", label);

  inform_client_connect_katcp(dx); /* do before creation to avoid seeing own creation message */
  on_connect_katcp(dx);
}

void perforate_client_server_katcp(struct katcp_dispatch *dl)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;

  s = dl->d_shared;

  if(s->s_used >= s->s_count){

    dx = s->s_clients[(unsigned int)rand() % s->s_count];

    terminate_katcp(dx, KATCP_EXIT_QUIT);
    on_disconnect_katcp(dx, "displaced by new client connection");
  }
}

int run_config_server_katcp(struct katcp_dispatch *dl, char *file, int count, char *host, int port)
{
#define LABEL_BUFFER 32
  int run, nfd, fd, result, suspend;
  unsigned int len;
  struct sockaddr_in sa;
  struct timespec delta;
  struct katcp_shared *s;
  char label[LABEL_BUFFER];
#if 0
  fd_set fsr, fsw;
#endif

  /* used to randomly select a client to displace when a new connection arrives and table is full */
  srand(getpid());

  if(count <= 0){
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  if(count > 1){
#ifdef DEBUG
    fprintf(stderr, "multi: more than one client, registering client list\n");
#endif
    register_katcp(dl, "?client-list", "displays client list (?client-list)", &client_list_cmd_katcp);
  }

  dl->d_exit = KATCP_EXIT_ABORT; /* assume the worst */

  result = listen_shared_katcp(dl, count, host, port);
  if(result <= 0){
#ifdef DEBUG
    fprintf(stderr, "multi: unable to initiate server listen\n");
#endif
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

  s = dl->d_shared;
  if(s == NULL){
#ifdef DEBUG
    fprintf(stderr, "multi: no shared state\n");
    abort();
#endif
    return terminate_katcp(dl, KATCP_EXIT_ABORT);
  }

#if 0
  register_flag_mode_katcp(dl, "?notice",  "notice operations (?notice [list|create|watch|wake])", &notice_cmd_katcp, KATCP_CMD_HIDDEN, 0);
  register_flag_mode_katcp(dl, "?job",     "job operations (?job [list|process notice-name executable-file])", &job_cmd_katcp, KATCP_CMD_HIDDEN, 0);
#else
  register_flag_mode_katcp(dl, "?notice",  "notice operations (?notice [list|watch|wake])", &notice_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?job",     "job operations (?job [list|process notice-name executable-file|network notice-name net-host remote-port|watchdog job-name])", &job_cmd_katcp, 0, 0);
  register_flag_mode_katcp(dl, "?process", "register a process command (?process executable help-string [mode]", &register_subprocess_cmd_katcp, 0, 0);
#endif

  register_katcp(dl, "?sensor-list",       "lists available sensors (?sensor-list [sensor])", &sensor_list_cmd_katcp);
  if(s->s_tally > 0){
    register_katcp(dl, "?sensor-sampling", "configure sensor (?sensor-sampling sensor [strategy [parameter]])", &sensor_sampling_cmd_katcp);
    register_katcp(dl, "?sensor-value",    "query a sensor (?sensor-value sensor)", &sensor_value_cmd_katcp);
    register_katcp(dl, "?sensor-dump",     "dump sensor tree (?sensor-dump)", &sensor_dump_cmd_katcp);
  }

  /* setup child signal routines, in case they haven't yet */
  init_signals_shared_katcp(s);

  if(file){
    fd = pipe_from_file_katcp(dl, file);
    if(fd < 0){
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
    add_client_server_katcp(dl, fd, file);
  }

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
      FD_SET(s->s_lfd, &(s->s_read));
      if(s->s_lfd > s->s_max){
        s->s_max = s->s_lfd;
      }
    }

    load_jobs_katcp(dl);

    if(load_shared_katcp(dl) < 0){ /* want to shut down */
      run = (-1);
    }

    if(run < 0){

      run = 0; /* assume we have stopped, revert to stopping if the below still need to do work */

      if(ended_shared_katcp(dl) <= 0){
        run = (-1);
      }
      if(ended_jobs_katcp(dl) <= 0){
        run = (-1);
      }

      delta.tv_sec = KATCP_HALT_WAIT;
      delta.tv_nsec = 0;

      suspend = 0;
      FD_ZERO(&(s->s_read));
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
      wait_jobs_katcp(dl);
    }

    run_shared_katcp(dl);
    run_jobs_katcp(dl);
    run_notices_katcp(dl);

    if(FD_ISSET(s->s_lfd, &(s->s_read))){
      if(s->s_used < s->s_count){

        len = sizeof(struct sockaddr_in);
        nfd = accept(s->s_lfd, (struct sockaddr *) &sa, &len);

        if(nfd >= 0){
          snprintf(label, LABEL_BUFFER, "%s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
          label[LABEL_BUFFER - 1] = '\0';

          add_client_server_katcp(dl, nfd, label);
        }

      } else {
        /* WARNING: will run a busy loop, terminating one entry each cycle until space becomes available. We expect an exit to happen quickly, otherwise this could empty out all clients (though there is a backoff, since we pick a slot randomly) */
        perforate_client_server_katcp(dl);
      }
    }

  }

#ifdef DEBUG
  fprintf(stderr, "multi: finished: global run=%d\n", run);
#endif

  /* clean up, but also done in destroy shared */
  undo_signals_shared_katcp(s);

  return (exited_katcp(dl) == KATCP_EXIT_ABORT) ? (-1) : 0;
#undef LABEL_BUFFER
}

int run_multi_server_katcp(struct katcp_dispatch *dl, int count, char *host, int port)
{
  return run_config_server_katcp(dl, NULL, count, host, port);
}

