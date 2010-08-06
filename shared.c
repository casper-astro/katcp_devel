
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
#include "katpriv.h"
#include "katsensor.h"
#include "netc.h"

#define SHARED_MAGIC 0x548a52ed

static volatile int child_signal_shared = 0;

static void handle_child_shared(int signal)
{
#ifdef DEBUG
  fprintf(stderr, "received child signal");
#endif
  child_signal_shared++;
}

#ifdef DEBUG
static void sane_shared_katcp(struct katcp_dispatch *d)
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
#else
#define sane_shared_katcp(d)
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

  s->s_vector = NULL;

  s->s_size = 0;
  s->s_modal = 0;

  s->s_commands = NULL;
  s->s_mode = 0;

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

  s->s_notices = NULL;
  s->s_pending = 0;

  s->s_build_state = NULL;
  s->s_build_items = 0;

#if 0
  s->s_version_subsystem = NULL;
  s->s_version_major = 0;
  s->s_version_minor = 0;
#endif

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
  s->s_vector[0].e_enter = NULL;
  s->s_vector[0].e_leave = NULL;
  s->s_vector[0].e_state = NULL;
  s->s_vector[0].e_clear = NULL;

  s->s_vector[0].e_version = NULL;
  s->s_vector[0].e_major = 0;
  s->s_vector[0].e_minor = 1;

  s->s_size = 1;

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
      s->s_mode = i; /* be nice to the clear function, get current mode works */
      if(s->s_vector[i].e_clear){
        (*(s->s_vector[i].e_clear))(d);
      }
      s->s_vector[i].e_state = NULL;
      if(s->s_vector[i].e_name){
        free(s->s_vector[i].e_name);
        s->s_vector[i].e_name = NULL;
      }
      if(s->s_vector[i].e_version){
        free(s->s_vector[i].e_version);
        s->s_vector[i].e_version = NULL;
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

  destroy_notices_katcp(d);
  destroy_sensors_katcp(d);

  while(s->s_count > 0){
#ifdef DEBUG
    if(s->s_clients[0]->d_clone != 0){
      fprintf(stderr, "shutdown: major logic failure: first client is %d\n", s->s_clients[0]->d_clone);
    }
#endif
    shutdown_katcp(s->s_clients[0]);
  }

  /* at this point it is unsafe to call API functions on the shared structure */

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

#if 0
int watch_shared_katcp(struct katcp_dispatch *d, char *name, pid_t pid, void (*call)(struct katcp_dispatch *d, int status))
{
  return watch_type_shared_katcp(d, name, pid, 0, call);
}

int watch_type_shared_katcp(struct katcp_dispatch *d, char *name, pid_t pid, int type, void (*call)(struct katcp_dispatch *d, int status))
{
  struct katcp_process *pt;
  struct katcp_shared *s;
  int i;

  sane_shared_katcp(d);

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if((pid <= 0) && (name == NULL)){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "usage problem of watch shared");
    return -1;
  }

  for(i = 0; i < s->s_entries; i++){
    if(((pid > 0) && (s->s_table[i].p_pid == pid)) || (name && !strcmp(s->s_table[i].p_name, name))){
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "found match for process at %d", s->s_table[i].p_pid);
      if(call){ /* overwrite */
        s->s_table[i].p_call = call; 
      } else { /* delete */
        s->s_entries--;
        if(s->s_table[i].p_name){
          free(s->s_table[i].p_name);
        }
        if(i < s->s_entries){
          s->s_table[i].p_call  = s->s_table[s->s_entries].p_call;
          s->s_table[i].p_pid   = s->s_table[s->s_entries].p_pid;
          s->s_table[i].p_name  = s->s_table[s->s_entries].p_name;
          s->s_table[i].p_type  = s->s_table[s->s_entries].p_type;
          s->s_table[i].p_state = s->s_table[s->s_entries].p_state;
        }
      }
      return 0;
    } 
  }
  
  if(call == NULL){
#ifdef DEBUG
    fprintf(stderr, "watch: attemping to delete nonexistant entry\n");
#endif
    return -1;
  }

  /* new, insert */

  if(pid <= 0){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "need a valid process id to insert into watch");
    return -1;
  }

  pt = realloc(s->s_table, (s->s_entries + 1) * sizeof(struct katcp_process));
  if(pt == NULL){
    return -1;
  }

  s->s_table = pt;

  s->s_table[s->s_entries].p_pid = pid;
  s->s_table[s->s_entries].p_type = type;
  s->s_table[s->s_entries].p_call = call;
  s->s_table[s->s_entries].p_state = KATCP_PS_UP;

  if(name){
    s->s_table[s->s_entries].p_name = strdup(name);
    if(s->s_table[s->s_entries].p_name == NULL){
      return -1;
    }
  } else {
    s->s_table[s->s_entries].p_name = NULL;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "inserted new process watch at %d", s->s_entries);

  s->s_entries++;

  return 0;
}

int reap_children_shared_katcp(struct katcp_dispatch *d, pid_t pid, int force)
{
  int count, pending, status, i, found, retries, result, attempts;
  struct timespec period;
  pid_t got;
  struct katcp_shared *s;
  void (*call)(struct katcp_dispatch *d, int status);

  s = d->d_shared;

  count = 0;
  found = 0;
  result = 0;
  pending = 0;
  retries = 0;

  attempts = force ? KATCP_WAITPID_CHECKS : 1;

  if(force){
    for(i = 0; i < s->s_entries; i++){
      if(s->s_table[i].p_state == KATCP_PS_TERM){
        pending++;
      }
    }
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "%d terminated processes need to be collected", pending);
  }

  do {

    if(retries > 0){

      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "collected %d of %d tasks so far", count, pending);

      period.tv_sec = 0;
      period.tv_nsec = KATCP_WAITPID_POLL;
      pselect(0, NULL, NULL, NULL, &period, &(s->s_mask_current));
    }

    retries++;

    while((got = waitpid(WAIT_ANY, &status, WNOHANG)) > 0){
      if((pid > 0) && (pid == got)){
        found = pid;
      }
      i = 0;
      while(i < s->s_entries){
        if(s->s_table[i].p_pid == got){

          if(s->s_table[i].p_state == KATCP_PS_UP){
            log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "reaping running entry %d of %d with callback", i, s->s_entries);
            call = s->s_table[i].p_call;
          } else {
            log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "reaping terminated entry %d of %d", i, s->s_entries);
            call = NULL;
          }

          s->s_entries--;

          if(s->s_table[i].p_name){
            free(s->s_table[i].p_name);
            s->s_table[i].p_name = NULL;
          }

          if(i < s->s_entries){
            s->s_table[i].p_call  = s->s_table[s->s_entries].p_call;
            s->s_table[i].p_pid   = s->s_table[s->s_entries].p_pid;
            s->s_table[i].p_name  = s->s_table[s->s_entries].p_name;
            s->s_table[i].p_type  = s->s_table[s->s_entries].p_type;
            s->s_table[i].p_state = s->s_table[s->s_entries].p_state;
          }

          if(call){
            (*call)(d, status);
          }

          count++;
        } else {
          i++;
        }
      }
    }
    /* WARNING: intricate exit condition: found is zero if no match and pid zero if not looking for a pariticular one */
  } while((retries < attempts) && ((found != pid) || (count < pending)));

  result = count;

  if(force){
    if(pid != found){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to reap process with id %u in table of %d entries", pid, s->s_entries);
      result = (-1);
    } 

    if(count < pending){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "only terminated %d processes, %d still not terminated", count, pending - count);
      result = (-1);
    }
  }

  return result;
}

/* returns the number of non-terminated processes killed (0 or 1) or -1 on failure */

static int signal_child_katcp(struct katcp_dispatch *d, struct katcp_process *p, int signal)
{
  int result;

  if(p->p_state != KATCP_PS_UP){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attemping to terminate process %u which is already terminating", p->p_pid);
    result = 0;
  } else {
    result = 1;
  }

  if(kill(p->p_pid, SIGTERM) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to kill process %u: %s", p->p_pid, strerror(errno));

    return -1;
  }

  p->p_state = KATCP_PS_TERM;
  return result;
}

int end_type_shared_katcp(struct katcp_dispatch *d, int type, int force)
{
  int i, result, value;
  struct katcp_shared *s;

  result = 0;

  if(d == NULL){
    return -1;
  }

  if(type == 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "terminating all subprocesses");
  } else {
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "terminating subprocesses matching type %d", type);
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  for(i = 0; i < s->s_entries; i++){
    if((type == 0) || (s->s_table[i].p_type == type)){
      value = signal_child_katcp(d, &(s->s_table[i]), SIGTERM);
      if(value < 0){
        result = value;
      } else {
        result += value;
      }
    }
  }

  value = reap_children_shared_katcp(d, 0, force);
  if(value < 0){
    result = value;
  }

  return result;
}
#endif

#if 0
int end_pid_shared_katcp(struct katcp_dispatch *d, pid_t pid, int force)
{
  int i, result, value;
  struct katcp_shared *s;

  result = 0;

  if(d == NULL){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if(pid <= 0){
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "terminating process %u", pid);

  for(i = 0; i < s->s_entries; i++){
    if(s->s_table[i].p_pid == pid){

      value = signal_child_katcp(d, &(s->s_table[i]), SIGTERM);
      if(value < 0){
        result = value;
      } else {
        result += value;
      }
    }
  }

  value = reap_children_shared_katcp(d, 0, force);
  if(value < 0){
    result = value;
  }

  return result;
}
#endif

#if 0
int end_name_shared_katcp(struct katcp_dispatch *d, char *name, int force)
{
  int i, result, value;
  struct katcp_shared *s;

  result = 0;

  if(d == NULL){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if(name == NULL){
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "attempting to terminate child process %s", name);

  for(i = 0; i < s->s_entries; i++){
    if(!strcmp(s->s_table[i].p_name, name)){
      value = signal_child_katcp(d, &(s->s_table[i]), SIGTERM);
      if(value < 0){
        result = value;
      } else {
        result += value;
      }
    }
  }

  value = reap_children_shared_katcp(d, 0, force);
  if(value < 0){
    result = value;
  }

  return result;
}
#endif

/*******************************************************************/

int listen_shared_katcp(struct katcp_dispatch *d, int count, char *host, int port)
{
  int i;
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

/***********************************************************************/

int mode_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *actual, *flags;

  sane_shared_katcp(d);

  name = NULL;

  if(d->d_shared->s_modal == 0){
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

    if(enter_name_mode_katcp(d, name, flags) < 0){
      return KATCP_RESULT_FAIL;
    }
  }

  actual = query_mode_name_katcp(d);
  if(actual == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no name available for current mode");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "current mode now %s", actual);

  if(name && strcmp(actual, name)){
    extra_response_katcp(d, KATCP_RESULT_FAIL, actual);
  } else {
    extra_response_katcp(d, KATCP_RESULT_OK, actual);
  }

  return KATCP_RESULT_OWN;
}

/***********************************************************************/

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
    s->s_vector[i].e_enter = NULL;
    s->s_vector[i].e_leave = NULL;
    s->s_vector[i].e_state = NULL;
    s->s_vector[i].e_clear = NULL;

    s->s_vector[i].e_version = NULL;
    s->s_vector[i].e_major = 0;
    s->s_vector[i].e_minor = 1;
  }

  s->s_size = mode + 1;

  return 0;
}

int mode_version_katcp(struct katcp_dispatch *d, int mode, char *subsystem, int major, int minor)
{
  struct katcp_shared *s;
  struct katcp_entry *e;
  char *copy;

  sane_shared_katcp(d);
  s = d->d_shared;

  if(expand_modes_katcp(d, mode) < 0){
    return -1;
  }

  e = &(s->s_vector[mode]);

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

  return 0;
}

int store_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state)
{
  return store_full_mode_katcp(d, mode, NULL, NULL, NULL, state, NULL);
}

int store_clear_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state, void (*clear)(struct katcp_dispatch *d))
{
  return store_full_mode_katcp(d, mode, NULL, NULL, NULL, state, clear);
}

int store_full_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, int (*enter)(struct katcp_dispatch *d, char *flags, unsigned int from), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d))
{
  struct katcp_shared *s;
  char *copy;
  int result;

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
    (*(s->s_vector[mode].e_clear))(d);
    result = 1;
  }
  if(s->s_vector[mode].e_name != NULL){
    free(s->s_vector[mode].e_name);
    s->s_vector[mode].e_name = NULL;
    result = 1;
  }

  s->s_vector[mode].e_name  = copy;
  s->s_vector[mode].e_enter = enter;
  s->s_vector[mode].e_leave = leave;
  s->s_vector[mode].e_state = state;
  s->s_vector[mode].e_clear = clear;

  if(name){
    if(s->s_modal == 0){
      if(register_katcp(d, "?mode", "mode change command (?mode [new-mode])", &mode_cmd_katcp)){
        return -1;
      }
      s->s_modal = 1;
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

  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown mode %s", name);
  return -1;
}

int enter_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *flags)
{
  struct katcp_shared *s;
  unsigned int to, from;

  sane_shared_katcp(d);

  s = d->d_shared;

  if(mode >= s->s_size){
    /* TODO: report errors */
    return -1;
  }

  from = s->s_mode;
  if(from == mode){
    /* TODO: report no change */
    return s->s_mode;
  }
  to = mode;

  if(s->s_vector[from].e_leave){
    (*(s->s_vector[from].e_leave))(d, to);
  }

  s->s_mode = to;

  if(s->s_vector[to].e_enter){
    s->s_mode = (*(s->s_vector[to].e_enter))(d, flags, from);
  }

  if(s->s_mode >= s->s_size){
    s->s_mode = 0;
  }

  if(from != s->s_mode){ /* mode has changed,  report it */
    if(s->s_vector[s->s_mode].e_name){ /* but only if it has a name */
      broadcast_inform_katcp(d, "#mode", s->s_vector[s->s_mode].e_name);
    }
  }

  return s->s_mode;
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
