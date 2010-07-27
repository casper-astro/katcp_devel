#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

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

static void release_clone(struct katcp_dispatch *d)
{
  struct katcp_dispatch *dt;
  struct katcp_shared *s;
  int fd, i, it;

  s = d->d_shared;
  if(s == NULL){
#ifdef DEBUG
    fprintf(stderr, "major logic failure: no shared state\n");
    abort();
#endif
    return;
  }

#ifdef DEBUG
  if((d->d_clone < 0) || (d->d_clone >= s->s_used)){
    fprintf(stderr, "major logic failure: clone value %d out of range\n", d->d_clone);
    abort();
  }
  if(s->s_clients[d->d_clone] != d){
    fprintf(stderr, "major logic failure: clone %p not at location %d\n", d, d->d_clone);
    abort();
  }
#endif

  fd = fileno_katcl(d->d_line);

  reset_katcp(d, -1);

#ifdef DEBUG
  fprintf(stderr, "release: released %d/%d\n", d->d_clone, s->s_used);
#endif

  s->s_used--;

  if(d->d_clone < s->s_used){

    i = d->d_clone;
    it = s->s_used;

    dt = s->s_clients[it];
#ifdef DEBUG
    if(d != s->s_clients[i]){
      fprintf(stderr, "release: logic problem: %p not at %d\n", d, i);
      abort();
    }
#endif

    /* exchange entries to keep used ones contiguous, and to make it safe to use release clone while iterating in ascending order over client table */
    s->s_clients[i] = dt;
    s->s_clients[it] = d;

    dt->d_clone = i;
    d->d_clone = it;
  }
}

static int inform_client_connect_katcp(struct katcp_dispatch *d)
{
  int i;
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef DEBUG
  if(s == NULL){
    fprintf(stderr, "inform: no shared state\n");
    abort();
  }
#endif

  for(i = 0; i < s->s_used; i++){
    d = s->s_clients[i];

#ifdef DEBUG
    fprintf(stderr, "multi[%d]: informing %p of new connection\n", i, d);
#endif

    send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#client-connected", KATCP_FLAG_LAST | KATCP_FLAG_STRING, d->d_name);
  }

  return 0;
}

static int client_list_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;
  unsigned int count;

  s = d->d_shared;
  count = 0;

  if(s != NULL){
    while(count < s->s_used){
      dx = s->s_clients[count];
      send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#client-list", KATCP_FLAG_LAST | KATCP_FLAG_STRING, dx->d_name);
      count++;
    }
  }

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
  
  return KATCP_RESULT_OWN;
}

int run_multi_server_katcp(struct katcp_dispatch *dl, int count, char *host, int port)
{
  int run, i, nfd, fd, max, result, suspend, status;
  unsigned int len;
  struct sockaddr_in sa;
  struct timespec delta;
  struct katcp_dispatch *d;
  struct katcp_shared *s;
  fd_set fsr, fsw;

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

  register_katcp(dl, "?sensor-list",       "lists available sensors (?sensor-list [sensor])", &sensor_list_cmd_katcp);
  if(s->s_tally > 0){
    register_katcp(dl, "?sensor-sampling", "configure sensor (?sensor-sampling sensor [strategy [parameter]])", &sensor_sampling_cmd_katcp);
    register_katcp(dl, "?sensor-value",    "query a sensor (?sensor-value sensor)", &sensor_value_cmd_katcp);
    register_katcp(dl, "?sensor-dump",     "dump sensor tree (?sensor-dump)", &sensor_dump_cmd_katcp);
  }

  /* setup child signal routines, in case they haven't yet */
  init_signals_shared_katcp(s);

  run = 1;

  while(run){
    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

#if 0
    gettimeofday(&now, NULL);
    future.tv_sec = now.tv_sec + KATCP_INIT_WAIT;
    future.tv_usec = now.tv_usec;
#endif

    max = (-1);

    suspend = run_timers_katcp(dl, &delta);

    if(run > 0){ /* only bother with new connections if not stopping */
      FD_SET(s->s_lfd, &fsr);
      if(s->s_lfd > max){
        max = s->s_lfd;
      }
    }

    /* WARNING: all clients contiguous */
    for(i = 0; i < s->s_used; i++){
      d = s->s_clients[i];
      fd = fileno_katcl(d->d_line);
#ifdef DEBUG
      if(fd < 0){
        fprintf(stderr, "multi[%d]: bad fd\n", i);
        abort();
      }
#endif

      status = exited_katcp(d);
#ifdef DEBUG
      fprintf(stderr, "multi[%d]: preselect: status is %d, fd=%d\n", i, status, fd);
#endif
      switch(status){
        case KATCP_EXIT_NOTYET : /* still running */
          FD_SET(fd, &fsr);
          if(fd > max){
            max = fd;
          }
          break;
        case KATCP_EXIT_QUIT : /* only this connection is shutting down */
          
          on_disconnect_katcp(d, NULL);
          break;
        default : /* global shutdown */
#ifdef DEBUG
          fprintf(stderr, "multi[%d]: termination initiated\n", i);
#endif

          run = (-1); /* pre shutdown mode */
          terminate_katcp(dl, status); /* have the shutdown code go to template */

          on_disconnect_katcp(d, NULL);

          break;
      }

      if(flushing_katcp(d)){ /* if flushing ensure that we get called if space becomes available later */
#ifdef DEBUG
        fprintf(stderr, "multi[%d]: want to flush data\n", i);
#endif
        FD_SET(fd, &fsw);
        if(fd > max){
          max = fd;
        }
      }
    }

    if(run < 0){
#ifdef DEBUG
      fprintf(stderr, "multi: busy shutting down\n");
#endif

      status = exited_katcp(dl);
      for(i = 0; i < s->s_used; i++){
        d = s->s_clients[i];
        if(!exited_katcp(d)){
#ifdef DEBUG
          fprintf(stderr, "multi[%d]: unsolicted disconnect with code %d\n", i, status);
#endif
          terminate_katcp(d, status); /* have the shutdown code go others */
          on_disconnect_katcp(d, NULL);
        }
      }

      if(s->s_used <= 0){
        run = 0;
      }

      delta.tv_sec = KATCP_HALT_WAIT;
      delta.tv_nsec = 0;

      suspend = 0;
      FD_ZERO(&fsr);
    }

#ifdef DEBUG
    if(suspend){
      fprintf(stderr, "multi: selecting indefinitely\n");
    } else {
      fprintf(stderr, "multi: selecting for %lu.%lu\n", delta.tv_sec, delta.tv_nsec);
    }
#endif

    /* delta now timespec, not timeval */
    result = pselect(max + 1, &fsr, &fsw, NULL, suspend ? NULL : &delta, &(s->s_mask_current));
#ifdef DEBUG
    fprintf(stderr, "multi: select=%d, used=%d\n", result, s->s_used);
#endif

    if(result < 0){
      switch(errno){
        case EAGAIN :
        case EINTR  :
          result = 0;
          break;
        default  :
#ifdef DEBUG
          fprintf(stderr, "select failed: %s\n", strerror(errno));
#endif
          run = 0; 
          FD_ZERO(&fsr);
          FD_ZERO(&fsw);
          break;
      }
    }

    if(saw_child_shared_katcp(s)){
      reap_children_shared_katcp(dl, 0, 0);
    }

    if(result <= 0){ /* don't look at connections if no activity */
      continue;
    }

    /* WARNING: all clients contiguous */
    for(i = 0; i < s->s_used; i++){
      d = s->s_clients[i];
      fd = fileno_katcl(d->d_line);

#ifdef DEBUG
      fprintf(stderr, "multi[%d/%d]: postselect: location=%p, fd=%d\n", i, s->s_used, d, fd);
#endif

      if(FD_ISSET(fd, &fsw)){
        if(write_katcp(d) < 0){
          release_clone(d);
          continue; /* WARNING: after release_clone, d will be invalid, forcing a continue */
        }
      }

      if(exited_katcp(d)){
        if(!flushing_katcp(d)){
          release_clone(d);
        }
        continue;
      }

      /* don't read or process if we are flushing on exit */

      if(FD_ISSET(fd, &fsr)){
        if(read_katcp(d)){
          release_clone(d);
          continue;
        }
      }

      if(dispatch_katcp(d) < 0){
        release_clone(d);
        continue;
      }
    }

    if(FD_ISSET(s->s_lfd, &fsr)){
      if(s->s_used < s->s_count){

        len = sizeof(struct sockaddr_in);
        nfd = accept(s->s_lfd, (struct sockaddr *) &sa, &len);

        if(nfd >= 0){
          fcntl(nfd, F_SETFD, FD_CLOEXEC);

          inform_client_connect_katcp(dl); /* do before creation to avoid seeing own creation message */

          d = s->s_clients[s->s_used];
          s->s_used++;
          reset_katcp(d, nfd);
          name_katcp(d, "%s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
          on_connect_katcp(d);
        }

      } else {
        d = s->s_clients[(unsigned int)rand() % s->s_count];
        terminate_katcp(d, KATCP_EXIT_QUIT);
        on_disconnect_katcp(d, "displaced by %s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
        /* WARNING: will run a busy loop, terminating one entry each cycle until space becomes available. We expect an exit to happen quickly, otherwise this could empty out all clients */
      }
    }
  }

#ifdef DEBUG
  fprintf(stderr, "multi: finished: global run=%d\n", run);
#endif

  /* clean up, but also done in destroy shared */
  undo_signals_shared_katcp(s);

  return (exited_katcp(dl) == KATCP_EXIT_ABORT) ? (-1) : 0;
}
