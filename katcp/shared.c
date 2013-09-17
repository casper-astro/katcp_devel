/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/types.h>

#include <unistd.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"

#define SHARED_MAGIC 0x548a52ed

static volatile int child_signal_shared = 0;

static void handle_child_shared(int signal)
{
#ifdef DEBUG
  fprintf(stderr, "received child signal\n");
#endif
  child_signal_shared++;
}

#ifdef DEBUG
void sane_shared_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  if(d == NULL){
    fprintf(stderr, "shared sane: invalid handle\n");
    abort();
  }

  if(d->d_shared == NULL){
    fprintf(stderr, "shared sane: no shared component in handle\n");
    abort();
  }

  s = d->d_shared;

  if(s->s_magic != SHARED_MAGIC){
    fprintf(stderr, "shared sane: bad magic 0x%08x, expected 0x%x\n", s->s_magic, SHARED_MAGIC);
    abort();
  }

  if(s->s_mode >= s->s_size){
    fprintf(stderr, "shared sane: mode=%u over size=%u\n", s->s_mode, s->s_size);
    abort();
  }
  
  if(s->s_new >= s->s_size){
    fprintf(stderr, "shared sane: new mode=%u over size=%u\n", s->s_new, s->s_size);
    abort();
  }

  if(s->s_template == d){
    if(d->d_clone >= 0){
      fprintf(stderr, "shared sane: instance %p is template but also clone id %d\n", d, d->d_clone);
      abort();
    }
  } else {
    if((d->d_clone < 0) || (d->d_clone >= s->s_count)){
      fprintf(stderr, "shared sane: instance %p is not template but clone id %d out of range 0:%d\n", d, d->d_clone, s->s_count);
      abort();
    }
  }

  if(s->s_used > s->s_count){
    fprintf(stderr, "shared sane: used %d more than have %d\n", s->s_used, s->s_count);
    abort();
  }

}
#endif

int child_signal_shared_katcp(struct katcp_shared *s)
{
  if(child_signal_shared <= 0){
    return 0;
  }

  child_signal_shared = 0;

  return 1;
}

int init_signals_shared_katcp(struct katcp_shared *s)
{
  if(s->s_restore_signals){ 
    /* already set up, thanks */
    return 0;
  }

  /* block child processes, and remember old settings in mp */
  sigprocmask(SIG_SETMASK, NULL, &(s->s_mask_current));
  sigaddset(&(s->s_mask_current), SIGCHLD);

  sigprocmask(SIG_SETMASK, &(s->s_mask_current), &(s->s_mask_previous));

  /* mask current is now empty again, suitable for use in pselect */
  sigdelset(&(s->s_mask_current), SIGCHLD);

  s->s_action_current.sa_handler = handle_child_shared;
  s->s_action_current.sa_flags = 0;
  sigemptyset(&(s->s_action_current.sa_mask));
  sigaction(SIGCHLD, &(s->s_action_current), &(s->s_action_previous));

  s->s_restore_signals = 1;

  return 0;
}

int undo_signals_shared_katcp(struct katcp_shared *s)
{
  if(s->s_restore_signals == 0){
    return 0;
  }

  sigaction(SIGCHLD, &(s->s_action_previous), NULL);
  sigprocmask(SIG_BLOCK, &(s->s_mask_previous), NULL);

  s->s_restore_signals = 0;

  return 0;
}

int startup_shared_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  s = malloc(sizeof(struct katcp_shared));
  if(s == NULL){
    return -1;
  }

  s->s_magic = SHARED_MAGIC;
  s->s_default = KATCP_LEVEL_INFO;

  s->s_vector = NULL;

  s->s_size = 0;
#if 0
  s->s_modal = 0;
#endif

  s->s_prehook = NULL;
  s->s_posthook = NULL;

  s->s_commands = NULL;

  s->s_mode_sensor = NULL;
  s->s_mode = 0;
  s->s_flaky = 0;

  s->s_new = 0;
  s->s_options = NULL;
  s->s_transition = NULL;

  s->s_template = NULL;
  s->s_clients = NULL;

  s->s_count = 0;
  s->s_used = 0;

  s->s_lfd = (-1);

#if 0
  s->s_table = NULL;
  s->s_entries = 0;
#endif

  s->s_tasks = NULL;
  s->s_number = 0;

  s->s_queue = NULL;
  s->s_length = 0;

  s->s_extras = NULL;
  s->s_total = 0;

  s->s_notices = NULL;
  s->s_pending = 0;

  s->s_busy = 0;

  s->s_groups = NULL;
  s->s_fallback = NULL;
  s->s_this = NULL;
  s->s_members = 0;

  s->s_endpoints = NULL;

  s->s_build_state = NULL;
  s->s_build_items = 0;

#if 0
  s->s_version_subsystem = NULL;
  s->s_version_major = 0;
  s->s_version_minor = 0;
#endif

  s->s_versions = NULL;
  s->s_amount = 0;

  s->s_sensors = NULL;
  s->s_tally = 0;

  FD_ZERO(&(s->s_read));
  FD_ZERO(&(s->s_write));

  s->s_vector = malloc(sizeof(struct katcp_entry));
  if(s->s_vector == NULL){
    free(s);
    return -1;
  }

  s->s_vector[0].e_name = NULL;
  s->s_vector[0].e_prep = NULL;
  s->s_vector[0].e_enter = NULL;
  s->s_vector[0].e_leave = NULL;
  s->s_vector[0].e_state = NULL;
  s->s_vector[0].e_clear = NULL;

  s->s_vector[0].e_status = KATCP_STATUS_UNKNOWN;

#if 0
  s->s_vector[0].e_version = NULL;
  s->s_vector[0].e_major = 0;
  s->s_vector[0].e_minor = 1;
#endif

  s->s_size = 1;

  s->s_type = NULL;
  s->s_type_count = 0;

#ifdef DEBUG
  if(d->d_shared){
    fprintf(stderr, "startup shared: major logic failure: instance %p already has shared data %p\n", d, d->d_shared);
    abort();
  }
#endif

  /* have not touched the signal handlers yet */
  s->s_restore_signals = 0;

  s->s_template = d;
  d->d_shared = s;

#ifdef KATCP_EXPERIMENTAL
  if(init_flats_katcp(d, KATCP_FLAT_STACK) < 0){
    shutdown_shared_katcp(d);
    return -1;
  }
#endif

  return 0;
}

void shutdown_shared_katcp(struct katcp_dispatch *d)
{
  int i;
  struct katcp_shared *s;
  struct katcp_cmd *c;

  if(d == NULL){
    return;
  }

  s = d->d_shared;
  if(s == NULL){
#ifdef DEBUG
    fprintf(stderr, "shutdown: warning, no shared state for %p\n", d);
#endif
    return;
  }

  /* clear modes only once, when we clear the template */
  /* modes want callbacks, so we invoke them before operating on internals */
  if(d->d_clone < 0){
    for(i = 0; i < s->s_size; i++){
#if 0 /* probably not that wise, if we haven't entered it formally then better not lie */
      s->s_mode = i;
#endif
      if(s->s_vector[i].e_clear){
        (*(s->s_vector[i].e_clear))(d, i);
        s->s_vector[i].e_clear = NULL;
      }
      s->s_vector[i].e_state = NULL;
      if(s->s_vector[i].e_name){
        free(s->s_vector[i].e_name);
        s->s_vector[i].e_name = NULL;
      }
    }
  }

  if(d->d_clone >= 0){
#ifdef DEBUG
    fprintf(stderr, "shutdown: releasing clone %d/%d\n", d->d_clone, s->s_count);
    if((d->d_clone < 0) || (d->d_clone >= s->s_count)){
      fprintf(stderr, "shutdown: major logic problem: clone=%d, count=%d\n", d->d_clone, s->s_count);
      abort();
    }
    if(s->s_clients[d->d_clone] != d){
      fprintf(stderr, "shutdown: no client match for %p at %d\n", d, d->d_clone);
      abort();
    }
#endif
    s->s_clients[d->d_clone] = NULL;
    s->s_count--;
    if(d->d_clone < s->s_count){
      s->s_clients[d->d_clone] = s->s_clients[s->s_count];
      s->s_clients[d->d_clone]->d_clone = d->d_clone;
    }
    if(s->s_used > s->s_count){
      s->s_used = s->s_count;
    }
    /* if we are a clone, just de-register - don't destroy shared */
    return;
  }

#ifdef DEBUG
  if(s->s_template != d){
    fprintf(stderr, "shutdown: clone=%d, but %p not registered as template (%p instead)\n", d->d_clone, d, s->s_template);
    abort();
  }
#endif

  /* TODO: what about destroying jobs, need to happen before sensors ? */

  destroy_notices_katcp(d);

  /* WARNING: s_mode_sensor used to leak, hopefully fixed now */
  s->s_mode_sensor = NULL;
  destroy_sensors_katcp(d);

  destroy_versions_katcp(d);
  
  destroy_type_list_katcp(d);

  destroy_arbs_katcp(d);

  while(s->s_count > 0){
#ifdef DEBUG
    if(s->s_clients[0]->d_clone != 0){
      fprintf(stderr, "shutdown: major logic failure: first client is %d\n", s->s_clients[0]->d_clone);
    }
#endif
    shutdown_katcp(s->s_clients[0]);
  }

  /* at this point it is unsafe to call API functions on the shared structure */

#ifdef KATCP_EXPERIMENTAL
  /* TODO: destroy groups ... */
  destroy_flats_katcp(d);
  destroy_groups_katcp(d);
  release_endpoints_katcp(d);
#endif


  free(s->s_clients);
  s->s_clients = NULL;
  s->s_template = NULL;

  while(s->s_commands != NULL){
    c = s->s_commands;
    s->s_commands = c->c_next;
    c->c_next = NULL;
    shutdown_cmd_katcp(c);
  }

  s->s_mode = 0;
  s->s_flaky = 1;

  if(s->s_options){
    free(s->s_options);
    s->s_options = NULL;
  }

  if(s->s_vector){
    free(s->s_vector);
    s->s_vector = NULL;
  }
  s->s_size = 0;

#if 0
  if(s->s_table){
    free(s->s_table);
    s->s_table = NULL;
  }
  s->s_entries = 0;
#endif
  for(i = 0; i < s->s_number; i++){
    /* TODO, maybe have own function */
  }
  if(s->s_tasks){
    free(s->s_tasks);
    s->s_tasks = NULL;
  }
  s->s_number = 0;

  if(s->s_build_state){
    for(i = 0; i < s->s_build_items; i++){
      free(s->s_build_state[i]);
    }
    free(s->s_build_state);
    s->s_build_state = NULL;
  }

#if 0
  if(s->s_version_subsystem){
    free(s->s_version_subsystem);
    s->s_version_subsystem = NULL;
  }
#endif

#ifdef DEBUG
  if(s->s_length > 0){
    fprintf(stderr, "shutdown: probably bad form to rely on shutdown to clear schedule\n");
  }
#endif
  empty_timers_katcp(d);

  /* restore signal handlers if we messed with them */
  undo_signals_shared_katcp(s);
  
  free(s);
}

/***********************************************************************/

static void release_clone(struct katcp_dispatch *d)
{
  struct katcp_dispatch *dt;
  struct katcp_shared *s;
  int i, it;
#if 0
  int fd;
#endif

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

#if 0
  fd = fileno_katcl(d->d_line);
#endif

  inform_client_connections_katcp(d, KATCP_CLIENT_DISCONNECT); /* will not send to itself */

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

int link_shared_katcp(struct katcp_dispatch *d, struct katcp_dispatch *cd)
{
  struct katcp_shared *s;
  struct katcp_dispatch **dt;

  s = cd->d_shared;

#ifdef DEBUG
  if(d->d_shared){
    fprintf(stderr, "clone: logic error: clone %p already has shared state\n", d);
    abort();
  }
  if(d->d_clone >= 0){
    fprintf(stderr, "clone: clone already has id %d\n", d->d_clone);
    abort();
  }
  if(s == NULL){
    fprintf(stderr, "clone: logic error: template %p has no shared state\n", cd);
    abort();
  }
  if(s->s_template != cd){
    fprintf(stderr, "clone: logic problem: not cloning from template\n");
    abort();
  }
#endif

  dt = realloc(s->s_clients, sizeof(struct katcp_dispatch) * (s->s_count + 1));
  if(dt == NULL){
    shutdown_katcp(d);
    return -1;
  }
  s->s_clients = dt;
  s->s_clients[s->s_count] = d;

  d->d_clone = s->s_count;
  s->s_count++;

  d->d_shared = s;

  return 0;
}


/***********************************************************************/

int load_shared_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;
  int i, result, fd, status;

  sane_shared_katcp(d);
  s = d->d_shared;

#ifdef DEBUG
  if(s->s_template != d){
    fprintf(stderr, "load shared: not invoked with template %p != %p\n", d, s->s_template);
    abort();
  }
#endif

  result = exiting_katcp(d) ? (-1) : 0;

  /* WARNING: all clients contiguous */
  for(i = 0; i < s->s_used; i++){
    dx = s->s_clients[i];
    fd = fileno_katcl(dx->d_line);
#ifdef DEBUG
    if(fd < 0){
      fprintf(stderr, "load shared[%d]=%p: bad fd\n", i, dx);
      abort();
    }
#endif

    status = exited_katcp(dx);
#ifdef DEBUG
    fprintf(stderr, "load shared[%d]: status is %d, fd=%d\n", i, status, fd);
#endif
    switch(status){
      case KATCP_EXIT_NOTYET : /* still running */
        /* load up read fd */
        FD_SET(fd, &(s->s_read));
        if(fd > s->s_max){
          s->s_max = fd;
        }
        break;

      case KATCP_EXIT_QUIT : /* only this connection is shutting down */
#if 0
        on_disconnect_katcp(dx, NULL);
#endif
        break;

      default : /* global shutdown */
#ifdef DEBUG
        fprintf(stderr, "load shared[%d]: termination initiated\n", i);
#endif

        result = (-1); /* pre shutdown mode */
        terminate_katcp(d, status); /* have the shutdown code go to template */

#if 0
        on_disconnect_katcp(dx, NULL);
#endif

        break;
    }

    if(flushing_katcp(dx)){ /* if flushing ensure that we get called if space becomes available later */
#ifdef DEBUG
      fprintf(stderr, "load shared[%d]: want to flush data\n", i);
#endif
      FD_SET(fd, &(s->s_write));
      if(fd > s->s_max){
        s->s_max = fd;
      }
    }
  }

  return result;
}

int run_shared_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;
  int i, fd, result;

  sane_shared_katcp(d);
  s = d->d_shared;

  /* WARNING: all clients contiguous */
  for(i = 0; i < s->s_used; i++){
    dx = s->s_clients[i];
    fd = fileno_katcl(dx->d_line);

#ifdef DEBUG
    fprintf(stderr, "run shared[%d/%d]: %p, fd=%d\n", i, s->s_used, dx, fd);
#endif

    if(FD_ISSET(fd, &(s->s_write))){
      if(write_katcp(dx) < 0){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "write to %s failed: %s", dx->d_name, strerror(error_katcl(dx->d_line)));
        release_clone(dx);
        continue; /* WARNING: after release_clone, dx will be invalid, forcing a continue */
      }
    }

    /* don't read or process if we are flushing on exit */
    if(exited_katcp(dx)){
      if(!flushing_katcp(dx)){
        release_clone(dx);
      }
      continue;
    }

    if(FD_ISSET(fd, &(s->s_read))){
      if((result = read_katcp(dx))){
        if(result > 0){
          log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "received end of file from %s", dx->d_name);
        } else {
          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "read failed from %s: %s", dx->d_name, strerror(error_katcl(dx->d_line)));
        }
        release_clone(dx);
        continue;
      }
    }

    if(dispatch_katcp(dx) < 0){
      release_clone(dx);
      continue;
    }
  }

  return 0;
}

int ended_shared_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_dispatch *dx;
  int i, status;

  sane_shared_katcp(d);
  s = d->d_shared;

#ifdef DEBUG
  if(s->s_template != d){
    fprintf(stderr, "ended shared: not invoked with template %p != %p\n", d, s->s_template);
    abort();
  }
#endif

  status = exited_katcp(d);
  for(i = 0; i < s->s_used; i++){
    dx = s->s_clients[i];
    if(!exited_katcp(dx)){
#ifdef DEBUG
      fprintf(stderr, "ended[%d]: unsolicted disconnect with code %d\n", i, status);
#endif

      basic_inform_katcp(dx, KATCP_DISCONNECT_INFORM, (status == KATCP_EXIT_RESTART) ? "restart" : "shutdown");

      terminate_katcp(dx, status); /* have the shutdown code go others */
#if 0
      on_disconnect_katcp(dx, NULL);
#endif
    }
  }

  return (s->s_used > 0) ? 0 : 1;
}


/*******************************************************************/

int allocate_clients_shared_katcp(struct katcp_dispatch *d, unsigned int count)
{
  int i;
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if(s->s_clients){
#ifdef DEBUG
    fprintf(stderr, "listen: unwilling to overwrite %d existing clients\n", s->s_count);
    abort();
#endif
    return -1;
  }

  for(i = 0; i < count; i++){
    if(clone_katcp(d) == NULL){
      if(i == 0){
        close(s->s_lfd);
        s->s_lfd = (-1);
        return -1;
      } else {
#ifdef DEBUG
        fprintf(stderr, "listen: wanted to create %d instances, only could do %d\n", count, i);
#endif
        return i;
      }
    }
  }

#ifdef DEBUG
  fprintf(stderr, "listen: created %d requested instances\n", count);
#endif

  return count;
}

int listen_shared_katcp(struct katcp_dispatch *d, char *host, int port)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  s->s_lfd = net_listen(host, port, 0);
  if(s->s_lfd < 0){
    return -1;
  }

  fcntl(s->s_lfd, F_SETFD, FD_CLOEXEC);

  return 0;
}

struct katcp_dispatch *template_shared_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;
  if(s == NULL){
#ifdef DEBUG
    fprintf(stderr, "template: major logic problem, template is NULL\n");
    sleep(1);
#endif
    return NULL;
  }

  return s->s_template;
}

/***********************************************************************/


int mode_resume_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *actual, *code;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "resuming after mode change");

  p = get_parse_notice_katcp(d, n);
  if(p){
    code = get_string_parse_katcl(p, 1);
  } else {
    code = NULL;
  }

  actual = query_mode_name_katcp(d);
  if(actual == NULL){
    return KATCP_RESULT_FAIL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, code ? code : KATCP_RESULT_OK);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, actual);

  resume_katcp(d);

  return 0;
}

int mode_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *actual, *flags, *scheduled;
  struct katcp_shared *s;
  int result;

  sane_shared_katcp(d);

  s = d->d_shared;
  name = NULL;

  if(s->s_mode_sensor == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem, mode command invoked without modes registered");
    return KATCP_RESULT_FAIL;
  }

  if(argc > 1){

    if(argc > 2){
      flags = arg_string_katcp(d, 2);
    } else {
      flags = NULL;
    }

    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return KATCP_RESULT_FAIL;
    }

    result = enter_name_mode_katcp(d, name, flags);

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "mode change request to %s yields code %d", name, result);

    if(result < 0){
      return KATCP_RESULT_FAIL;
    }

    if(result > 0){

      if(s->s_transition == NULL){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "logic problem - no transition notice available");
        return KATCP_RESULT_FAIL;
      }

      if(add_notice_katcp(d, s->s_transition, &mode_resume_katcp, NULL) < 0){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to register post mode resumption callback");
        return KATCP_RESULT_FAIL;
      } else {
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "about to enter mode %s", name);
        return KATCP_RESULT_PAUSE;
      }
    }
  }

  actual = query_mode_name_katcp(d);
  if(actual == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no name available for current mode");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "current mode now %s", actual);
  if(s->s_new != s->s_mode){
    scheduled = s->s_vector[s->s_new].e_name;
    if(scheduled){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "transition planned to mode %s", scheduled);
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "transition planned to enter mode number %d", s->s_new);
    }
  }

  if(name && strcmp(actual, name)){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, actual);
  } 

  return extra_response_katcp(d, KATCP_RESULT_OK, actual);
}

/***********************************************************************/

static unsigned int count_modes_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;

  return s->s_size;
}

static int expand_modes_katcp(struct katcp_dispatch *d, unsigned int mode)
{
  struct katcp_shared *s;
  struct katcp_entry *e;
  int i;

  sane_shared_katcp(d);

  s = d->d_shared;

  if(mode < s->s_size){ /* already allocated */
    return 0;
  }

  e = realloc(s->s_vector, sizeof(struct katcp_entry) * (mode + 1));
  if(e == NULL){
    return -1;
  }
  s->s_vector = e;

  for(i = s->s_size; i <= mode; i++){
    s->s_vector[i].e_name = NULL;
    s->s_vector[i].e_prep = NULL;
    s->s_vector[i].e_enter = NULL;
    s->s_vector[i].e_leave = NULL;
    s->s_vector[i].e_state = NULL;
    s->s_vector[i].e_clear = NULL;

    s->s_vector[i].e_status = KATCP_STATUS_UNKNOWN;

#if 0
    s->s_vector[i].e_version = NULL;
    s->s_vector[i].e_major = 0;
    s->s_vector[i].e_minor = 1;
#endif
  }

  s->s_size = mode + 1;

  return 0;
}

int mode_version_katcp(struct katcp_dispatch *d, int mode, char *subsystem, int major, int minor)
{
#define BUFFER 20
  struct katcp_shared *s;
#if 0
  struct katcp_entry *e;
#endif
  char buffer[BUFFER];

  sane_shared_katcp(d);
  s = d->d_shared;

  if(expand_modes_katcp(d, mode) < 0){
    return -1;
  }

#if 0
  e = &(s->s_vector[mode]);
#endif

  if(subsystem){
    snprintf(buffer, BUFFER, "%d.%d", major, minor);
    buffer[BUFFER - 1] = '\0';

    add_version_katcp(d, subsystem, mode, buffer, NULL);
  }

#if 0
  if(e->e_version){
    free(e->e_version);
    e->e_version = NULL;
  }

  if(subsystem){
    copy = strdup(subsystem);
    if(copy == NULL){
      return -1;
    }
    e->e_version = copy;
  }

  if(major > 0){
    e->e_major = major;
  }
  if(minor > 0){
    e->e_minor = minor;
  }
#endif

  return 0;
#undef BUFFER
}

int store_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state)
{
  return store_sensor_mode_katcp(d, mode, NULL, NULL, NULL, NULL, state, NULL, KATCP_STATUS_UNKNOWN);
}

int store_clear_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode))
{
  return store_sensor_mode_katcp(d, mode, NULL, NULL, NULL, NULL, state, clear, KATCP_STATUS_UNKNOWN);
}

int store_full_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, int (*enter)(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode))
{
  return store_sensor_mode_katcp(d, mode, name, NULL, enter, leave, state, clear, KATCP_STATUS_UNKNOWN);
}

int store_prepared_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, struct katcp_notice *(*prepare)(struct katcp_dispatch *d, char *flags, unsigned int from, unsigned int to), int (*enter)(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode))
{
  return store_sensor_mode_katcp(d, mode, name, prepare, enter, leave, state, clear, KATCP_STATUS_UNKNOWN);
}

int store_sensor_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, struct katcp_notice *(*prepare)(struct katcp_dispatch *d, char *flags, unsigned int from, unsigned int to), int (*enter)(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode), unsigned int status)
{
  struct katcp_shared *s;
  char *copy, *vector[1];
  int result, skip;

  sane_shared_katcp(d);
  s = d->d_shared;

  if(expand_modes_katcp(d, mode) < 0){
    return -1;
  }

  if(name){
    copy = strdup(name);
    if(copy == NULL){
      return -1;
    }
  } else {
    copy = NULL;
  }

  result = 0;

  if(s->s_vector[mode].e_clear){
    (*(s->s_vector[mode].e_clear))(d, mode);
    result = 1;
  }

  if(s->s_vector[mode].e_name != NULL){
    free(s->s_vector[mode].e_name);
    s->s_vector[mode].e_name = NULL;
    result = 1;
  }

  s->s_vector[mode].e_name  = copy;
  s->s_vector[mode].e_prep  = prepare;
  s->s_vector[mode].e_enter = enter;
  s->s_vector[mode].e_leave = leave;
  s->s_vector[mode].e_state = state;
  s->s_vector[mode].e_clear = clear;

  s->s_vector[mode].e_status = status;
  
  if(name){

    skip = 0;
    if(s->s_mode_sensor == NULL){

      if(register_katcp(d, "?mode", "mode change command (?mode [new-mode])", &mode_cmd_katcp)){
        result = (-1);
      }

      if(mode > 0){
        vector[0] = "mode0";
      } else {
        vector[0] = name;
        skip = 1;
      }

      if(register_discrete_sensor_katcp(d, 0, "mode", "current mode", "none", NULL, NULL, NULL, vector, 1) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create mode sensor");
      } else {
        s->s_mode_sensor = find_sensor_katcp(d, "mode");
        if(s->s_mode_sensor == NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate recently newly created mode sensor");
          skip = 1;
          result = (-1);
        }
      }
    }  

    if(skip == 0){
      if(expand_sensor_discrete_katcp(d, s->s_mode_sensor, mode, name) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to add mode %d (%s) to mode sensor", mode, name);
        result = (-1);
      }
    }
  }

  return result;
}

int is_mode_katcp(struct katcp_dispatch *d, unsigned int mode)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;

  return (s->s_mode == mode) ? 1 : 0;
}

void *get_mode_katcp(struct katcp_dispatch *d, unsigned int mode)
{
  sane_shared_katcp(d);

  if(mode >= d->d_shared->s_size){
    return NULL;
  }

  return d->d_shared->s_vector[mode].e_state;
}

void *need_current_mode_katcp(struct katcp_dispatch *d, unsigned int mode)
{
  sane_shared_katcp(d);

  if(mode >= d->d_shared->s_size){
    return NULL;
  }

  if(d->d_shared->s_mode != mode){
    return NULL;
  }

  return d->d_shared->s_vector[mode].e_state;
}

void *get_current_mode_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;

  return get_mode_katcp(d, s->s_mode);
}

void *get_new_mode_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;

  return get_mode_katcp(d, s->s_new);
}

int query_mode_code_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  unsigned int i;

  sane_shared_katcp(d);

  s = d->d_shared;

  if(name == NULL){
    return s->s_mode;
  }

  for(i = 0; i < s->s_size; i++){
    if(s->s_vector[i].e_name && !(strcmp(s->s_vector[i].e_name, name))){
      return i;
    }
  }

  return -1;
}

int enter_name_mode_katcp(struct katcp_dispatch *d, char *name, char *flags)
{
  struct katcp_shared *s;
  unsigned int i;

  sane_shared_katcp(d);

  s = d->d_shared;

  for(i = 0; i < s->s_size; i++){
    if(s->s_vector[i].e_name && !(strcmp(s->s_vector[i].e_name, name))){
      return enter_mode_katcp(d, i, flags);
    }
  }
  
  /* flaky not set, current mode not compromised */
  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown mode %s", name);

  return -1;
}

int complete_mode_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_shared *s;
  struct katcp_acquire *a;
  int result;

  sane_shared_katcp(d);

  s = d->d_shared;

#if 0
  /* WARNING: now could happen to recover from a flaky current mode */
  if(s->s_new == s->s_mode){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to change to mode already changed to");
    return 0;
  }
#endif

  if(s->s_vector[s->s_new].e_leave){
    (*(s->s_vector[s->s_new].e_leave))(d, s->s_new);
  }

  if(s->s_vector[s->s_new].e_enter){
    result = (*(s->s_vector[s->s_new].e_enter))(d, n, s->s_options, s->s_new);

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "enter function for mode %u returns %d", s->s_new, result);

    if(result == 0){
      s->s_flaky = 0;
      s->s_mode = s->s_new;
    } else {
      s->s_flaky = 1;
      s->s_new = s->s_mode; /* remain in old mode */
    }
  } else {
    s->s_mode = s->s_new;
    s->s_flaky = 0;
    result = 0;
  }

  s->s_transition = NULL;
  if(s->s_options){
    free(s->s_options);
    s->s_options = NULL;
  }

  if(result == 0){
    if(s->s_vector[s->s_mode].e_name){ /* broadcast mode change */
      broadcast_inform_katcp(d, "#mode", s->s_vector[s->s_mode].e_name);
      setenv("KATCP_MODE", s->s_vector[s->s_mode].e_name, 1);
    } else {
      unsetenv("KATCP_MODE");
    }
    if(s->s_mode_sensor){
      set_status_sensor_katcp(s->s_mode_sensor, s->s_vector[s->s_mode].e_status);
      a = s->s_mode_sensor->s_acquire;
      if(a){
        set_discrete_acquire_katcp(d, a, s->s_mode);
      }
    }

  }

  return result;
}

int enter_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *flags)
{
  /* WARNING: now returns > 0 if mode change is scheduled, not immediate */

  struct katcp_shared *s;
  struct katcp_dispatch *dl;
  char *fallback;

  sane_shared_katcp(d);

  s = d->d_shared;

  if(s->s_transition){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "already transitioning to a new mode");
    return -1;
  }

  if(mode >= s->s_size){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown mode at index %u", mode);
    return -1;
  }

  if(mode == s->s_mode){
    if(s->s_flaky){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "attempting to re-initialise current mode");
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "already in requested mode");
    }
    return 0;
  }

  fallback = s->s_options;
  s->s_options = NULL;

  if(flags){
    s->s_options = strdup(flags);
    if(s->s_options == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate mode options %s", flags);
      s->s_options = fallback;
      return -1;
    }
  }

  s->s_new = mode;

  if(fallback){
    free(fallback);
    fallback = NULL;
  }

  if(s->s_vector[s->s_new].e_prep == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting immediate mode transition to %u", s->s_new);
    return complete_mode_katcp(d, NULL, NULL);
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting deferred mode transition to %u from %u", s->s_new, s->s_mode);

  s->s_transition = (*(s->s_vector[s->s_new].e_prep))(d, s->s_options, s->s_mode, s->s_new);
  if(s->s_transition == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to prepare for transition new mode");
    s->s_new = s->s_mode;
    s->s_flaky = 1;
    return -1;
  }

  dl = template_shared_katcp(d);
  if(dl == NULL){
    dl = d;
  }

  if(add_notice_katcp(dl, s->s_transition, &complete_mode_katcp, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to register mode change completion handler");
    s->s_flaky = 1;
    return -1;
  }

  return 1;
}

int query_mode_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;

  return s->s_mode;
}

char *query_mode_name_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  sane_shared_katcp(d);

  s = d->d_shared;

  return s->s_vector[s->s_mode].e_name;
}

#define GENERIC_MODE_MAGIC 0x3213a303

/***************************************************************************/

#ifdef KATCP_SUBPROCESS
void destroy_dynamic_mode_katcp(struct katcp_dynamic_mode *dm);

struct katcp_dynamic_mode *create_dynamic_mode_katcp(char *cmd)
{
  struct katcp_dynamic_mode *dm;

  dm = malloc(sizeof(struct katcp_dynamic_mode));
  if(dm == NULL){
    return NULL;
  }

  dm->d_magic = GENERIC_MODE_MAGIC;
  dm->d_cmd = strdup(cmd);

  if(dm->d_cmd == NULL){
    destroy_dynamic_mode_katcp(dm);
    return NULL;
  }

  return dm;
}

void destroy_dynamic_mode_katcp(struct katcp_dynamic_mode *dm)
{
  if(dm == NULL){
    return;
  }

  if(dm->d_cmd){
    free(dm->d_cmd);
    dm->d_cmd = NULL;
  }

  free(dm);
}

void clear_dynamic_mode_katcp(struct katcp_dispatch *d, unsigned int mode)
{
  struct katcp_dynamic_mode *dm;

  dm = get_mode_katcp(d, mode);

  if(dm == NULL){
    return;
  }

  destroy_dynamic_mode_katcp(dm);
}

int enter_dynamic_mode_katcp(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to)
{
  struct katcl_parse *p;
  char *code;

  if(n == NULL){
    return 0;
  }

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    return -1;
  }

  code = get_string_parse_katcl(p, 1);
  if(code == NULL){
    return -1;
  }

  if(strcmp(code, KATCP_OK)){
    return -1;
  }

  return 0;
}

struct katcp_notice *prepare_dynamic_mode_katcp(struct katcp_dispatch *d, char *flags, unsigned int from, unsigned int to)
{
  struct katcp_dynamic_mode *dm;
  struct katcp_notice *halt;
  struct katcp_job *j;
  char *vector[2];

  dm = get_mode_katcp(d, to);
  if((dm == NULL) || (dm->d_magic != GENERIC_MODE_MAGIC)){
    return NULL;
  }

  if(dm->d_cmd == NULL){
    return NULL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "preparing transition from mode %u to %u using command %s", from, to, dm->d_cmd);

  halt = create_notice_katcp(d, NULL, 0);
  if(halt == NULL){
    return NULL;
  }

  vector[0] = dm->d_cmd;
  vector[1] = NULL;

  j = process_name_create_job_katcp(d, dm->d_cmd, vector, halt, NULL);
  if(j == NULL){
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "dynamic: created job %p with name %s\n", j, j->j_url->u_str);
#endif

  return halt;
}
#endif

int define_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
#ifdef KATCP_SUBPROCESS
  struct katcp_dynamic_mode *dm;
#endif
  char *name, *label, *script, *tmp;
  unsigned int m, status;
  int result;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(!strcmp(name, "mode")){

    label = arg_string_katcp(d, 2);
    script = arg_string_katcp(d, 3);

    tmp = arg_string_katcp(d, 4);
    if(tmp){
      status = status_code_sensor_katcl(tmp);
      if(status < 0){
        status = KATCP_STATUS_UNKNOWN;
      }
    } else {
      status = KATCP_STATUS_UNKNOWN;
    }


    if(label == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a mode label");
      return KATCP_RESULT_FAIL;
    }

    if(query_mode_code_katcp(d, label) >= 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "mode %s already defined", label);
      return KATCP_RESULT_FAIL;
    }

    m = count_modes_katcp(d);

#ifdef KATCP_SUBPROCESS
    if(script){
      dm = create_dynamic_mode_katcp(script);
      if(dm == NULL){
        return KATCP_RESULT_FAIL;
      }

      result = store_sensor_mode_katcp(d, m, label, &prepare_dynamic_mode_katcp, &enter_dynamic_mode_katcp, NULL, dm, &clear_dynamic_mode_katcp, status);

      if(result < 0){
        destroy_dynamic_mode_katcp(dm);
      }
    } else {
#endif
      result = store_sensor_mode_katcp(d, m, label, NULL, NULL, NULL, NULL, NULL, status);
#ifdef KATCP_SUBPROCESS
    }
#endif

    if(result < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate mode %s", label);
      return KATCP_RESULT_FAIL;
    }

    return KATCP_RESULT_OK;
  } else { /* TODO maybe more definitions here */
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_FAIL;
}

