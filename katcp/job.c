/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#ifdef KATCP_SUBPROCESS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"

#define JOB_MAGIC 0x21525110

#if 0
#define JOB_MAY_REQUEST 0x01
#define JOB_MAY_WRITE   0x02
#define JOB_MAY_WORK    0x04
#define JOB_MAY_READ    0x08
#if 0
#define JOB_MAY_KILL    0x10
#define JOB_MAY_COLLECT 0x20
#endif
#define JOB_PRE_CONNECT 0x40
#endif

#define JOB_STATE_PRE      1
#define JOB_STATE_UP       2
#define JOB_STATE_POST     3
#define JOB_STATE_DRAIN    4
#define JOB_STATE_WAIT     5
#define JOB_STATE_DONE     6

/******************************************************************/

#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_job_katcp(struct katcp_job *j)
{
  if(j->j_magic != JOB_MAGIC){
    fprintf(stderr, "job at %p should have magic 0x%x, but has 0x%x instead\n", j, JOB_MAGIC, j->j_magic);
    abort();
  }
}
#else
#define sane_job_katcp(j)
#endif

/* manage the job queue notice logic *************************************************/

static int add_tail_job(struct katcp_dispatch *d, struct katcp_job *j, struct katcp_notice *n)
{
  /* WARNING: increments the reference count for the notice, on success */

  struct katcp_notice **tmp;
  unsigned int index;

  if(j->j_count >= j->j_size){

#ifdef DEBUG
    fprintf(stderr, "size=%d, count=%d - increasing queue\n", j->j_size, j->j_count);
#endif
#ifdef KATCP_CONSISTENCY_CHECKS
    if(j->j_size < j->j_count){
      fprintf(stderr, "add: warning: detected rapid size increase of queue, expect corruption\n");
      abort();
    }
#endif
    tmp = realloc(j->j_queue, sizeof(struct katcp_notice *) * (j->j_size + 1));

    if(tmp == NULL){
      return -1;
    }
    j->j_queue = tmp;

    if(j->j_head > 0){
      j->j_queue[j->j_size] = j->j_queue[0];
      if(j->j_head > 1){
        memmove(&(j->j_queue[0]), &(j->j_queue[1]), sizeof(struct katcp_notice *) * (j->j_head - 1));
      }
      j->j_queue[j->j_head - 1] = NULL;
    }

    j->j_size = j->j_size + 1;
  }

  index = (j->j_head + j->j_count) % j->j_size;
#if DEBUG > 1
  fprintf(stderr, "job add[%d]=%p\n", index, n);
#endif
  j->j_queue[index] = n;
  j->j_count++;

  hold_notice_katcp(d, n);

  return 0;
}

static struct katcp_notice *remove_index_job(struct katcp_dispatch *d, struct katcp_job *j, unsigned int index)
{
  unsigned int end;
  struct katcp_notice *n;

  /* WARNING does not reduce the reference count of notice */

  if(j->j_count <= 0){
#ifdef DEBUG
    fprintf(stderr, "remove: nothing to remove\n");
#endif
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(index >= j->j_size){
    fprintf(stderr, "index %u out of range %u\n", index, j->j_size);
    abort();
  }
  if(j->j_queue[index] == NULL){
    fprintf(stderr, "index %u (head=%u,count=%u) already null\n", index, j->j_head, j->j_count);
    abort();
  }
#endif

#if DEBUG > 1
  fprintf(stderr, "job del[%d]=%p\n", index, j->j_queue[index]);
#endif

  n = j->j_queue[index];
  j->j_queue[index] = NULL;

  if(index == j->j_head){
    /* hopefully the common, simple case: only one interested party */
    j->j_head = (j->j_head + 1) % j->j_size;
    j->j_count--;
    return n;
  }

  if((j->j_head + j->j_count) > j->j_size){ /* wrapping case */
    if(index >= j->j_head){ /* position before wrap around, move up head */
      if(index > j->j_head){
        memcpy(&(j->j_queue[j->j_head + 1]), &(j->j_queue[j->j_head]), (index - j->j_head) * sizeof(struct katcp_notice *));
      }
      j->j_queue[j->j_head] = NULL;
      j->j_head = (j->j_head + 1) % j->j_size;
      j->j_count--;
      return n; /* WARNING: done here */
    }
  } else { /* if no wrapping, we can not be before head */
    if(index < j->j_head){
      return NULL;
    }
  }

  /* now move back end by one, to overwrite position at index */
  end = j->j_head + j->j_count - 1; /* WARNING: relies on count+head never being zero, hence earlier test */
  if(index > end){
    return NULL;
  }
  if(index < end){
    memcpy(&(j->j_queue[index]), &(j->j_queue[index + 1]), (end - index) * sizeof(struct katcp_notice *));
  } /* else index is end, no copy needed  */

  j->j_queue[end] = NULL;
  j->j_count--;

  return n;
}

static struct katcp_notice *remove_head_job(struct katcp_dispatch *d, struct katcp_job *j)
{
  return remove_index_job(d, j, j->j_head);
}

/***********************************************************************************************************/

#if defined (DEBUG) || defined(UNIT_TEST_JOB)
void dump_queue_job_katcp(struct katcp_job *j, FILE *fp)
{
  unsigned int i, k;

  fprintf(fp, "job queue (%d):", j->j_size);
  for(i = 0; i < j->j_size; i++){
    if(j->j_queue[i]){
      fprintf(fp, " <%p>", j->j_queue[i]);
    } else {
      fprintf(fp, " [%d]", i);
    }
  }
  fprintf(fp, "\n");

  for(k = j->j_head, i = 0; i < j->j_count; i++, k = (k + 1) % j->j_size){
    if(j->j_queue[k] == NULL){
      fprintf(stderr, "job queue: error: null field at %d\n", k);
    }
  }

  while(i < j->j_size){

    if(j->j_queue[k] != NULL){
      fprintf(stderr, "job queue: error: used field at %d\n", k);
    }

    k = (k + 1) % j->j_size;
    i++;
  }
}
#endif

#if 0
int unlink_queue_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *target)
{
  struct katcp_job *j;
  unsigned int i, k;

  j = target;

  if(j->j_queue == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to dequeue notice from empty job");
    return -1;
  }

  for(i = j->j_head, k = 0; k < j->j_count; i = (i + 1) % j->j_size, k++){
#ifdef KATCP_CONSISTENCY_CHECKS
    if(j->j_queue[i] == NULL){
      fprintf(stderr, "unlink: found null entry in queue\n");
      abort();
    }
#endif
    if(j->j_queue[i] == n){
      remove_index_job(j, i);
      return 0;
    }
  }
  
#ifdef KATCP_CONSISTENCY_CHECKS
  fprintf(stderr, "unlink: notice not registered with job\n");
  abort();
#endif

  return -1;
}
#endif

/* deallocate job, does not unlink itself from anywhere **********/

static void delete_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  if(j == NULL){
    return;
  }

  if(j->j_halt || (j->j_count > 0)){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to delete job %p which is noticed", j);
  }

  if(j->j_line){
    destroy_katcl(j->j_line, 1);
    j->j_line = NULL;
  }

  if(j->j_queue){
    free(j->j_queue);
    j->j_queue = NULL;
  }

  if(j->j_url){
    destroy_kurl_katcp(j->j_url);
    j->j_url = NULL;
  }

  if(j->j_map){
    destroy_map_katcp(d, j->j_map, NULL);
    j->j_map = NULL;
  }

  j->j_halt = NULL;
  j->j_count = 0;

  j->j_state = JOB_STATE_DONE;

  j->j_recvr = 0;
  j->j_sendr = 0;

  free(j);
}

/* create, modify notices to point back at job ********************/

static int relay_cmd_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *inform;

#ifdef DEBUG
  fprintf(stderr, "job: running cmd relay with dispatch %p and notice %p\n", d, n);
#endif

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    return 0;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "about to relay cmd message %s ...", inform);
  
  if(!strcmp(inform, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "detaching command match in response to %s", inform);
    return 0;
  }

  if(append_parse_katcl(d->d_line, p) < 0){
    return -1;
  }

  return 1;
}

int acknowledge_parse_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p)
{
  struct katcp_job *j;

  j = find_containing_job_katcp(d, n->n_name);
  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "job that issued %s no longer available", KATCP_SET_REQUEST);
    return -1;
  }

#ifdef DEBUG 
  fprintf(stderr, "job: found job %p matching notice %s\n", j, n->n_name);
#endif

  sane_job_katcp(j);

  if(j->j_recvr <= 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "logic problem, answered a request to generate reply for requst %s", KATCP_SET_REQUEST);
    return -1; 
  }

  if(append_parse_katcl(j->j_line, p) < 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to send reply for %s", KATCP_SET_REQUEST);
    return -1;
  }

  if(j->j_recvr <= 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "logic problem, acknowledging request when none is outstanding");
  }

  j->j_recvr--;

#ifdef DEBUG
  fprintf(stderr, "job: acknowledged request, receive down to %d\n", j->j_recvr);
#endif

  return 0;
}

int acknowledge_request_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int code, char *fmt, ...)
{
  char *result;
  struct katcl_parse *p, *px;
  va_list args;

  result = code_to_name_katcm(code);
  if(result == NULL){
    result = KATCP_INVALID;
  }

  /* WARNING: we damage the notice. That is almost ok. Multiple interested parties to this notice would run the request multiple times, which would be worse */

  p = remove_parse_notice_katcp(d, n);
  if(p == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "really odd internal problem, failed to remove parse structure which we just had");
    return -1;
  }

  va_start(args, fmt);
  px = turnaround_extra_parse_katcl(p, KATCP_RESULT_OK, NULL, args);
  va_end(args);
  if(px == NULL){ 
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to generate reply for requst %s", KATCP_SET_REQUEST);
    return -1;
  }

  if(acknowledge_parse_job_katcp(d, n, px) < 0){
    destroy_parse_katcl(px);
    return -1;
  }

  destroy_parse_katcl(px);

  return 0;
}

static int get_request_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p, *px;
  char *cmd;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    return 0;
  }

  cmd = get_string_parse_katcl(p, 0);
  if(cmd == NULL){
    return 0;
  }
  
  if(!strcmp(cmd, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "detaching get command match in response to %s", cmd);
    return 0;
  }

  if(strcmp(cmd, KATCP_GET_REQUEST)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "received unexpected request %s, expected %s", cmd, KATCP_SET_REQUEST);
    return 0;
  }

  px = get_dbase_katcp(d, p);
  if(px == NULL){
    if(acknowledge_request_job_katcp(d, n, KATCP_RESULT_FAIL, NULL, NULL) < 0){
      return 0;
    } 
    /* a get failing isn't a reason to give up, it may just be that there is nothing to be found */
    return 1;
  }

  if(acknowledge_parse_job_katcp(d, n, px) < 0){
    destroy_parse_katcl(px);
    return 0;
  }

  return 1;
}

static int set_request_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *cmd;
#if DEBUG
  char *label, *value;
#endif

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    return 0;
  }

  cmd = get_string_parse_katcl(p, 0);
  if(cmd == NULL){
    return 0;
  }
  
  if(!strcmp(cmd, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "detaching set command match in response to %s", cmd);
    return 0;
  }

  if(strcmp(cmd, KATCP_SET_REQUEST)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "received unexpected request %s, expected %s", cmd, KATCP_SET_REQUEST);
    return 0;
  }

#if DEBUG
  label = get_string_parse_katcl(p, 1);
  value = get_string_parse_katcl(p, 2);
  if((label == NULL) || (value == NULL)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "insufficient parameters to %s", KATCP_SET_REQUEST);
    return 0;
  }

  fprintf(stderr, "job: fielding set request with label=%s, value=%s\n", label, value);

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "fielding request %s with label %s and value %s", cmd, label, value);
#endif

  if(set_dbase_katcp(d, p) < 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to honour set request");
  }

  if(acknowledge_request_job_katcp(d, n, KATCP_RESULT_OK, NULL, NULL) < 0){
    return 0;
  }

  return 1;
}

static int dict_request_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *cmd;

  p = get_parse_notice_katcp(d, n);
  if (p == NULL)
    return 0;

  cmd = get_string_parse_katcl(p, 0);
  if (cmd == NULL)
    return 0;

  if(!strcmp(cmd, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "detaching dict command match in response to %s", cmd);
    return 0;
  }

  if(strcmp(cmd, KATCP_DICT_REQUEST)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "received unexpected request %s, expected %s", cmd, KATCP_DICT_REQUEST);
    return 0;
  }

  if (dict_katcp(d, p) < 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to honour dict request");
  }

  if(acknowledge_request_job_katcp(d, n, KATCP_RESULT_OK, NULL, NULL) < 0){
    return 0;
  }

  return 1;
}

static int relay_log_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *inform;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    return 0;
  }
  
  if(!strcmp(inform, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "detaching log match in response to %s", inform);
    return 0;
  }

  log_relay_katcp(d, p);

  return 1;
}

static int hold_version_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *inform, *module, *version, *build, *copy;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    return 0;
  }
  
  if(!strcmp(inform, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "detaching in response to %s", inform);
    return 0;
  }

  module = get_string_parse_katcl(p, 1);
  version = get_string_parse_katcl(p, 2);
  build = get_string_parse_katcl(p, 3);

  if((module == NULL) || (version == NULL)){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "insufficent parameters in version inform");
    return 1;
  }
  
  copy = path_from_notice_katcp(n, module, 0);
  if(copy == NULL){
    return 1;
  }

  if(add_version_katcp(d, copy, 0, version, build) < 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to record version information for %s", copy);
  }

  free(copy);

  return 1;
}

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, struct katcp_url *name, pid_t pid, int fd, int async, struct katcp_notice *halt)
{
  struct katcp_job *j, **t;
  struct katcp_shared *s;
  struct katcp_dispatch *dl;

  s = d->d_shared;

  t = realloc(s->s_tasks, sizeof(struct katcp_job *) * (s->s_number + 1));
  if(t == NULL){
    return NULL;
  }
  s->s_tasks = t;

  j = malloc(sizeof(struct katcp_job));
  if(j == NULL){
    return NULL;
  }

  j->j_magic = JOB_MAGIC;
  j->j_url = NULL;

  j->j_pid = pid;
  j->j_halt = NULL;

#if 0
  if(async){
    j->j_state = JOB_PRE_CONNECT;
  } else {
    j->j_state = JOB_MAY_REQUEST | JOB_MAY_WRITE | JOB_MAY_WORK | JOB_MAY_READ;
  }
#endif

#if 0
  if(j->j_pid > 0){
    j->j_state |= JOB_MAY_KILL | JOB_MAY_COLLECT;
  }
#endif

  j->j_state = async ? JOB_STATE_PRE : JOB_STATE_UP;
  j->j_code = KATCP_RESULT_INVALID;

  j->j_sendr = 0;
  j->j_recvr = 0;

  j->j_line = NULL;

  j->j_queue = NULL;
  j->j_head = 0;
  j->j_count = 0;
  j->j_size = 0;

  j->j_map = NULL;

  j->j_url = name;
  if(j->j_url == NULL){
    delete_job_katcp(d, j);
    return NULL;
  }

  j->j_map = create_map_katcp();
  if(j->j_map == NULL){
    j->j_url = NULL;
    delete_job_katcp(d, j);
    return NULL;
  }

  dl = template_shared_katcp(d);
  if(dl){
    if(match_inform_job_katcp(dl, j, KATCP_DICT_REQUEST, &dict_request_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register dict command for job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }
    if(match_inform_job_katcp(dl, j, KATCP_GET_REQUEST, &get_request_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register %s command for job %s", KATCP_GET_REQUEST, j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }
    if(match_inform_job_katcp(dl, j, KATCP_SET_REQUEST, &set_request_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register %s command for job %s", KATCP_SET_REQUEST, j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }
    if(match_inform_job_katcp(dl, j, KATCP_LOG_INFORM, &relay_log_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register log relay for job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }

    /* WARNING: will slurp up any and every new-style version message. We might have to suppress some of that, possibly using special option to kcpcmd ... */

    if(match_inform_job_katcp(dl, j, KATCP_VERSION_CONNECT_INFORM, &hold_version_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register version recorder for job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }
    if(match_inform_job_katcp(dl, j, KATCP_VERSION_LIST_INFORM, &hold_version_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register version recorder for job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }

    if(match_inform_job_katcp(dl, j, KATCP_SENSOR_LIST_INFORM, &match_sensor_list_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to match sensor-list on job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }

    if(match_inform_job_katcp(dl, j, KATCP_SENSOR_STATUS_INFORM, &match_sensor_status_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to match sensor-status on job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    }


  } 

  /* WARNING: do line clone last, so that fd isn't closed on failure */
  if(fd >= 0){
    j->j_line = create_katcl(fd);
    if(j->j_line == NULL){
      j->j_url = NULL;
      delete_job_katcp(d, j);
      return NULL;
    }
  }

  /* after this point we are not permitted to fail :*) */

  if(halt){
    j->j_halt = halt;
    hold_notice_katcp(d, j->j_halt);
  }

  s->s_tasks[s->s_number] = j;
  s->s_number++;
  
  j->j_url->u_use++;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "created job %s", j->j_url->u_str);

  return j;
}

#if 0
struct katcp_job *via_notice_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  struct katcp_job *j;

  if(n->n_target == NULL){
    return NULL;
  }

  j = n->n_target;

  sane_job_katcp(j);

  return j;
}
#endif


#if 0

/* manually stop a task, trigger notice as if stopped on its own, hard makes it unsafe **/
int stop_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result;

  sane_job_katcp(j);

  result = 0;

  /* WARNING: maybe: only kill jobs which have a pid, wait for the child to close - that ensures that we receive all messages */

  if((j->j_state & JOB_MAY_KILL) && (j->j_pid > 0)){
    if(kill(j->j_pid, SIGTERM) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to kill process %u: %s", j->j_pid, strerror(errno));
      result = (-1);
    }
    j->j_state &= ~JOB_MAY_KILL;
  } else if(j->j_state & JOB_MAY_WRITE){
    j->j_state &= ~JOB_MAY_WRITE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "attempting to stop job which is already finished (state=0x%x)\n", j->j_state);
  }

  return result;
}
#endif

int zap_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  sane_job_katcp(j);

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "terminating job %p (%s)", j, j->j_url->u_str ? j->j_url->u_str : "<anonymous>");

  j->j_state = JOB_STATE_DRAIN;

  return 0;
}

int ended_jobs_katcp(struct katcp_dispatch *d)
{
  int i;
  struct katcp_job *j;
  struct katcp_shared *s;

  s = d->d_shared;

  for(i = 0; i < s->s_number; i++){
    j = s->s_tasks[i];

    sane_job_katcp(j);

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "ending job [%d]=%p (%s) immediately", i, j, j->j_url->u_str ? j->j_url->u_str : "<anonymous>");

    j->j_state = JOB_STATE_DONE;
  }

  return (s->s_number > 0) ? 0 : 1;
}

int issue_request_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  struct katcp_notice *n;
  struct katcl_parse *p, *px;

#ifdef DEBUG
  fprintf(stderr, "issue: checking if job needs to issue a request (count=%d)\n", j->j_count);
#endif

  if(j->j_count <= 0){
    return 0; /* nothing to do */
  }

  if(j->j_sendr > 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not sending request, another one already in queue");
    return 0; /* still waiting for a reply */
  }

  switch(j->j_state){
    case JOB_STATE_POST : 
    case JOB_STATE_DRAIN : 
    case JOB_STATE_DONE :
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "not sending request, job is terminating");
      break;
  }

#if 0
  if((j->j_state & JOB_MAY_WRITE) == 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "not sending request, finished writing");
    return 0; /* still waiting for a reply */
  }
#endif

  n = j->j_queue[j->j_head];
  if(n == NULL){
#ifdef DEBUG
    fprintf(stderr, "issue: logic problem: nothing in queue, but woken up\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "queue of %u elements is empty at %u", j->j_count, j->j_head);
    return -1;
  }

  p = get_parse_notice_katcp(d, n);
  if(p){
#ifdef DEBUG
    fprintf(stderr, "issue: got parse %p from notice %p (%s)\n", p, n, n->n_name);
#endif
    if(append_parse_katcl(j->j_line, p) >= 0){
      j->j_sendr++;
      return 0;
    }

    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to append request message to job");

    p = remove_parse_notice_katcp(d, n);
    if(p){
      px = turnaround_extra_parse_katcl(p, KATCP_RESULT_FAIL, "allocation");
      if(px){ 
        p = NULL;
        /* WARNING: turnaround invalidates p */
        if(set_parse_notice_katcp(d, n, px) < 0){
          destroy_parse_katcl(px);
        }
      } else {
        destroy_parse_katcl(p);
      }
    }
  }

  n = remove_head_job(d, j);
  if(n){
    trigger_notice_katcp(d, n);
    release_notice_katcp(d, n);
  }

  /* TODO: try next item if this one fails */

  return -1;
}

int match_inform_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, char *match, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_notice *n;
  struct katcp_trap *kt;

  sane_job_katcp(j);

  kt = find_map_katcp(j->j_map, match);
  if(kt == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "new inform match for %s", match);

    n = create_parse_notice_katcp(d, NULL, 0, NULL);
    if(n == NULL){
      return -1;
    }

    if(match_notice_job_katcp(d, j, match, n) < 0){
      return -1;
    }

  } else {
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "reusing inform match for %s", match);

    n = kt->t_notice;
  }

  if(add_notice_katcp(d, n, call, data)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to add to notice %s", n->n_name);
    return -1;
  }

  return 0;
}

int match_notice_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, char *match, struct katcp_notice *n)
{
  struct katcp_trap *kt;
  struct katcp_url *ku;
  char *ptr;
  int len;

  kt = find_map_katcp(j->j_map, match);
  if(kt){
    return -1;
  }

  /* WARNING: ties a notice to a job via a name. At some stage this could be a firmer link */
  ku = j->j_url;
  if(ku && ku->u_str){
    len = strlen(ku->u_str) + strlen(match) + 1;
    ptr = malloc(len);
    if(ptr == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate string of %d bytes", len);
      return -1;
    }

    snprintf(ptr, len, "%s%s", ku->u_str, match);
    ptr[len - 1] = '\0';
    rename_notice_katcp(d, n, ptr);

    free(ptr);

  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no proper name for job %p", j);
  }

  if(add_map_katcp(d, j->j_map, match, n) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to add match %s to job", match);
    return -1;
  }

  return 0;
}

int submit_to_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, struct katcl_parse *p, char *name, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_notice *n;
  int result;

  sane_job_katcp(j);

  n = create_parse_notice_katcp(d, name, 0, p);
  if(n == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notice while submitting things to job %s", j->j_url->u_str);
    return -1;
  }

  if(call){
    if(add_notice_katcp(d, n, call, data)){
      return -1;
    }
  }

  result = notice_to_job_katcp(d, j, n);
  if(result < 0){

    p = remove_parse_notice_katcp(d, n);
    if(p){
      /* WARNING: PROBLEM: we need to set a parse for notice to trigger */
      destroy_parse_katcl(p);
#if 0
      px = turnaround_parse_katcl(p, KATCP_RESULT_FAIL);
      if(px){ 
        if(set_parse_notice_katcp(d, n, px) < 0){
          destroy_parse_katcl(px);
        }
      } else {
      }
#endif
    }

    trigger_notice_katcp(d, n);
  }

  return 0;
}

int notice_to_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, struct katcp_notice *n)
{
#if 0
  struct katcl_parse *p, *px;
#endif

  sane_job_katcp(j);

#ifdef DEBUG
  fprintf(stderr, "submitting notice %p(%s) to job %p(%s)\n", n, n->n_name, j, j->j_url->u_str);

  if(get_parse_notice_katcp(d, n) == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "notice submitted to job should have an associated parse structure");
  }
#endif

  if(add_tail_job(d, j, n)){
    /* on failure, caller is responsible for passed parameters */
    return -1;
  }

  issue_request_job_katcp(d, j); /* ignore return code, errors should come back as replies */

  return 0;
}

static int fail_request_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, struct katcl_parse *p)
{
  struct katcl_parse *px, *py;
  int result;

  /* WARNING: refcount + reuse logic quite scary */
  px = copy_parse_katcl(p);
  if(px == NULL){
    return -1;
  }

  py = turnaround_extra_parse_katcl(px, KATCP_RESULT_FAIL, "unimplemented");
  if(py == NULL){
    destroy_parse_katcl(px);
    return -1;
  } 

  /* turnaround invalidates px, but py has to be destroyed (append does a copy internally) */

  result = append_parse_katcl(j->j_line, py);
  destroy_parse_katcl(py);

  return result;
}

static int field_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result;
  char *cmd;
  struct katcp_notice *n;
  struct katcl_parse *p;
  struct katcp_trap *kt;
#ifdef DEBUG
  int i;
  char *tmp;
#endif

  for(;;){

    p = ready_katcl(j->j_line);
    if(p == NULL){
      result = parse_katcl(j->j_line);
      if(result <= 0){
#ifdef DEBUG
        fprintf(stderr, "job: nothing more to parse (result=%d)\n", result);
#endif
        return result;
      }
      p = ready_katcl(j->j_line);
      if(p == NULL){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "claim to have parsed data (result=%d) but no parse available", result);
        return -1;
      }
    }

    /* now we have a parse structure, it is our responsibility to call clear from here onwards (use have_katcl to clear via side-effect) */

    cmd = get_string_parse_katcl(p, 0);
    if(cmd == NULL){
      clear_katcl(j->j_line);
      return -1;
    }

#ifdef DEBUG
    fprintf(stderr, "job: alt:");
    for(i = 0; (tmp = get_string_parse_katcl(p, i)); i++){
      fprintf(stderr, " <%s>", tmp);
    }
    fprintf(stderr, "\n");
#endif

    switch(cmd[0]){

      case KATCP_REPLY   :

        if(j->j_sendr == 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "received spurious reply, no request was issued");
          clear_katcl(j->j_line);
          return -1;
        }

        n = remove_head_job(d, j);
        if(n == NULL){
          /* as long as there are references, notices should not go away */
          log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no outstanding notice despite waiting for a reply");
          clear_katcl(j->j_line);
          return -1;
        }

        set_parse_notice_katcp(d, n, p);
        trigger_notice_katcp(d, n);
        release_notice_katcp(d, n);

        j->j_sendr--;

        /* see if we can start the next round, if there is one */
        issue_request_job_katcp(d, j);
        break;

      case KATCP_REQUEST : 

#ifdef DEBUG
        fprintf(stderr, "job: saw %s request, current receive count is %d\n", cmd, j->j_recvr);
#endif

        if(j->j_recvr > 0){
          /* WARNING: keeps old parse around ... until recvr decremented */
          return 1; 
        }

        kt = find_map_katcp(j->j_map, cmd);
        if(kt){
#ifdef DEBUG
          fprintf(stderr, "job: found match for %s in map\n", cmd);
#endif
          n = kt->t_notice;
          add_parse_notice_katcp(d, n, p);

          j->j_recvr++;

          trigger_notice_katcp(d, n);
        } else {
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unable to handle request %s by job %s", cmd, j->j_url->u_str);
          fail_request_job_katcp(d, j, p);
        }

        break;

      case KATCP_INFORM  :

        kt = find_map_katcp(j->j_map, cmd);
        if(kt){
#ifdef DEBUG
          fprintf(stderr, "job: found match for %s in map\n", cmd);
#endif
          n = kt->t_notice;
          add_parse_notice_katcp(d, n, p);
          trigger_notice_katcp(d, n);
        } else {
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "ignoring inform %s of job %s", cmd, j->j_url->u_str);
        }

        if(!strcmp(cmd, KATCP_RETURN_JOB)){

#ifdef DEBUG
          fprintf(stderr, "job: saw return inform message\n");
#endif

          if(j->j_halt){
            log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "relaying return value");
            n = j->j_halt;
            j->j_halt = NULL;

#ifdef DEBUG
            fprintf(stderr, "job: waking notice %p\n", n);
#endif

            set_parse_notice_katcp(d, n, p);
            trigger_notice_katcp(d, n);
            release_notice_katcp(d, n);
          }

          j->j_state = JOB_STATE_DONE;
#if 0
          zap_job_katcp(d, j);
#endif

          /* WARNING: maybe not a bad thing to get stuck on a #return message */
          /* clear_katcl(j->j_line); */
          return 0;
        }

        break;
    }

    /* clear parse - get another one later */
    clear_katcl(j->j_line);
  }

}

#if 0
static int field_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result;
  char *cmd;
  struct katcp_notice *n;
  struct katcl_parse *p;
  struct katcp_trap *kt;
#ifdef DEBUG
  int i;
  char *tmp;
#endif

  while((result = have_katcl(j->j_line)) > 0){

    cmd = arg_string_katcl(j->j_line, 0);
    if(cmd == NULL){
      return -1;
    }

    p = ready_katcl(j->j_line);

    if(p == NULL){
      /* if we got this far, we should really have a ready data structure */
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to retrieve parsed job data");
      return -1;
    }

#ifdef DEBUG
    fprintf(stderr, "job: processing message starting with <%s %s ...>\n", cmd, arg_string_katcl(j->j_line, 1));

    fprintf(stderr, "job: alt:");
    for(i = 0; (tmp = get_string_parse_katcl(p, i)); i++){
      fprintf(stderr, " <%s>", tmp);
    }
    fprintf(stderr, "\n");
#endif

    switch(cmd[0]){

      case KATCP_REPLY   :

        if(j->j_sendr == 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "received spurious reply, no request was issued");
          return -1;
        }

        n = remove_head_job(d, j);
        if(n == NULL){
          /* as long as there are references, notices should not go away */
          log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no outstanding notice despite waiting for a reply");
          return -1;
        }

        set_parse_notice_katcp(d, n, p);
        trigger_notice_katcp(d, n);
        release_notice_katcp(d, n);

        j->j_sendr--;

        /* see if we can start the next round, if there is one */
        issue_request_job_katcp(d, j);
        break;

      case KATCP_REQUEST : 

        /* our logic is unable to service requests */
        kt = find_map_katcp(j->j_map, cmd);
        if(kt){
#ifdef DEBUG
          fprintf(stderr, "job: found match for %s in map\n", cmd);
#endif
          n = kt->t_notice;
          add_parse_notice_katcp(d, n, p);
          trigger_notice_katcp(d, n);
        } else {
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unable to handle request %s by job %s", cmd, j->j_url->u_str);
          fail_request_job_katcp(d, j, p);
        }

        break;

      case KATCP_INFORM  :

        kt = find_map_katcp(j->j_map, cmd);
        if(kt){
#ifdef DEBUG
          fprintf(stderr, "job: found match for %s in map\n", cmd);
#endif
          n = kt->t_notice;
          add_parse_notice_katcp(d, n, p);
          trigger_notice_katcp(d, n);
        } else {
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "ignoring inform %s of job %s", cmd, j->j_url->u_str);
        }

        if(!strcmp(cmd, KATCP_RETURN_JOB)){

#ifdef DEBUG
          fprintf(stderr, "job: saw return inform message\n");
#endif

          if(j->j_halt){
            log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "relaying return value");
            n = j->j_halt;
            j->j_halt = NULL;

            p = ready_katcl(j->j_line);
            if(p == NULL){
              /* if we got this far, we should really have a ready data structure */
              log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to retrieve parsed return data");
              return -1;
            }

#ifdef DEBUG
            fprintf(stderr, "job: waking notice %p\n", n);
#endif

            set_parse_notice_katcp(d, n, p);
            trigger_notice_katcp(d, n);
            release_notice_katcp(d, n);
          }

          /* terminating job, otherwise halt notice can not assume that job is gone */

          zap_job_katcp(d, j);
        }

        break;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "job: nothing more to field (result=%d)\n", result);
#endif
  return result;
}
#endif

/* stuff to be called from mainloop *******************************/

int wait_jobs_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  int status, code;
  pid_t pid;
  int i;

  s = d->d_shared;

  while((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0){

#ifdef DEBUG
   fprintf(stderr, "got child process %d with status 0x%x\n", pid, status);
#endif

    i = 0;
    while(i < s->s_number){
      j = s->s_tasks[i];

      if(j->j_pid == pid){
        if(WIFEXITED(status)){
          code = WEXITSTATUS(status);
          log_message_katcp(d, code ? KATCP_LEVEL_INFO : KATCP_LEVEL_DEBUG, NULL, "job %d completed with code %d", j->j_pid, code);
          j->j_code = code ? KATCP_RESULT_FAIL : KATCP_RESULT_OK;
        } else if(WIFSIGNALED(status)){
          code = WTERMSIG(status);
          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "process %d terminated by signal %d", j->j_pid, code);
          j->j_code = KATCP_RESULT_FAIL;
        } else {
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "process %d exited abnormally", j->j_pid);
          j->j_code = KATCP_RESULT_FAIL;
        }


#if 0
        j->j_state &= ~(JOB_MAY_KILL | JOB_MAY_COLLECT);
#endif

        j->j_state = JOB_STATE_DONE;
        j->j_pid = 0;
      }

      i++;
    }
  }

  return 0;
}

int load_jobs_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  int i, fd;

  s = d->d_shared;

  for(i = 0; i < s->s_number; i++){
    j = s->s_tasks[i];

    fd = fileno_katcl(j->j_line);
    if(fd >= 0){

      switch(j->j_state){
        case JOB_STATE_PRE   :  
          FD_SET(fd, &(s->s_write));
          break;
        case JOB_STATE_UP    :
          FD_SET(fd, &(s->s_read));
          /* FALL */
        case JOB_STATE_POST :  
        case JOB_STATE_DRAIN :  
          if(flushing_katcl(j->j_line)){
            FD_SET(fd, &(s->s_write));
          }
          break;
        /* case JOB_STATE_DONE : */
      }
      if(fd > s->s_max){ /* WARNING: what if no fd gets added ? */
        s->s_max = fd;
      }
    }

#if 0
    if(j->j_state & (JOB_MAY_READ | JOB_MAY_WRITE | JOB_PRE_CONNECT)){
      fd = fileno_katcl(j->j_line);
      if(fd >= 0){
        if(j->j_state & JOB_MAY_READ){
          FD_SET(fd, &(s->s_read));
        }
        if((j->j_state & JOB_MAY_WRITE) && flushing_katcl(j->j_line)){
          FD_SET(fd, &(s->s_write));
        }
        if(j->j_state & JOB_PRE_CONNECT){
          FD_SET(fd, &(s->s_write));
        }
      }
    }
#endif
  }

  return 0;
}

int run_jobs_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  struct katcp_job *j;
  struct katcl_parse *p, *px;
  int i, fd, result, code;
  unsigned int len;
  char *string;

  s = d->d_shared;

  i = 0;
  while(i < s->s_number){
    j = s->s_tasks[i];

#ifdef DEBUG
    fprintf(stderr, "job: checking job %d/%d in state 0x%x\n", i, s->s_number, j->j_state);
    sane_job_katcp(j);
#endif

    switch(j->j_state){
      case JOB_STATE_PRE :
      case JOB_STATE_UP :
      case JOB_STATE_POST :
      case JOB_STATE_DRAIN :
        fd = fileno_katcl(j->j_line);
        break;
      /* JOB_STATE_WAIT */
      /* JOB_STATE_DONE */
      default : 
        fd = (-1);
        break;
    }

    switch(j->j_state){ /* async connect completes */
      case JOB_STATE_PRE : 
        if(FD_ISSET(fd, &(s->s_write))){
          len = sizeof(int);
          result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);
          if(result == 0){
            switch(code){
              case 0 :
                j->j_state = JOB_STATE_UP;
                log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "async connect to %s succeeded", j->j_url->u_str);
                break;
              case EINPROGRESS : 
                log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw an in progress despite write set being ready on job %s", j->j_url->u_str);
                break;
              default : 
                log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to connect to %s: %s", j->j_url->u_str, strerror(code));
                j->j_state = JOB_STATE_DONE;
                break;
            }
          }
        }
        break;
    }

    switch(j->j_state){ /* read */
      case JOB_STATE_UP : 
        if(FD_ISSET(fd, &(s->s_read))){
          result = read_katcl(j->j_line);
#ifdef DEBUG
          fprintf(stderr, "job: read from job returns %d\n", result);
#endif
          if(result < 0){
            code = error_katcl(j->j_line);

            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to read from subordinate task: %s", code ? strerror(code) : "no error recorded");
            j->j_state = JOB_STATE_DONE;
            j->j_code = KATCP_RESULT_FAIL;
          }

          if(result > 0){ /* end of file, won't do further io */
            if(j->j_pid > 0){
              j->j_state = JOB_STATE_WAIT;
            } else {
              j->j_state = JOB_STATE_POST;
              j->j_code = KATCP_RESULT_OK;
            }
          }
        }
        break;
    }

    switch(j->j_state){ /* process */
      case JOB_STATE_UP : 
      case JOB_STATE_WAIT :  /* WARNING: we stall until waitpid() comes back */
      case JOB_STATE_POST : 

        result = field_job_katcp(d, j);
#ifdef DEBUG
        fprintf(stderr, "job: field job returns %d\n", result);
#endif
        if(result < 0){ /* error */
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to process messages from subordinate task");
          j->j_state = JOB_STATE_DONE;
          j->j_code = KATCP_RESULT_FAIL;
        } else {
          if(j->j_state == JOB_STATE_POST){ /* done processing */
            j->j_state = JOB_STATE_DRAIN;
          }
        }
        break;
      /* JOB_STATE_PRE  */
      /* JOB_STATE_DRAIN  */
      /* JOB_STATE_DONE */
    }

    switch(j->j_state){ /* write */
      case JOB_STATE_UP :
      case JOB_STATE_POST :
      case JOB_STATE_DRAIN :
        if(FD_ISSET(fd, &(s->s_write))){
          result = write_katcl(j->j_line);

          if(result < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to write messages to subordinate task");
            j->j_state = JOB_STATE_DONE;
            j->j_code = KATCP_RESULT_FAIL;
          }

          if(result > 0){
            if(j->j_state == JOB_STATE_DRAIN){
              j->j_state = JOB_STATE_DONE;
            }
          }
        }

        if(j->j_state == JOB_STATE_DRAIN){
          if(!flushing_katcl(j->j_line)){  /* done writing */
#ifdef DEBUG
            fprintf(stderr, "job: about to transition from drain to done\n");
#endif
            j->j_state = JOB_STATE_DONE;
          }
        }
      /* JOB_STATE_PRE */
      /* JOB_STATE_WAIT */
      /* JOB_STATE_POST */

      break;
    }


#ifdef DEBUG
    fprintf(stderr, "job: state after run is 0x%x\n", j->j_state);
#endif

    if(j->j_state == JOB_STATE_DONE){  /* just plain done */

      n = remove_head_job(d, j);
      while(n != NULL){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "failing pending notice for job %s", j->j_url->u_str);

        p = remove_parse_notice_katcp(d, n);
        if(p){
          px = turnaround_extra_parse_katcl(p, KATCP_RESULT_FAIL, "disconnected");
          if(px){
            /* WARNING: turnaround invalidates p */
            p = NULL;
            if(set_parse_notice_katcp(d, n, px) < 0){
              destroy_parse_katcl(px);
            }
          } else {
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to generate failed response on disconnect");
            destroy_parse_katcl(p);
          }
        }

        trigger_notice_katcp(d, n);
        release_notice_katcp(d, n);

        n = remove_head_job(d, j);
      }

      p = create_referenced_parse_katcl();
      if(p){
        add_plain_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, KATCP_RETURN_JOB);
        string = code_to_name_katcm(j->j_code);
        add_plain_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_LAST, string ? string : KATCP_FAIL);
      }

      if(j->j_halt){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "waking halt notice");
        n = j->j_halt;
        j->j_halt = NULL;

        
        set_parse_notice_katcp(d, n, p);
        trigger_notice_katcp(d, n);
        release_notice_katcp(d, n);

      }

      destroy_map_katcp(d, j->j_map, p);
      j->j_map = NULL;

      if(p){
        /* both the halt notice and all the maps copy the parse, so destroy the reference here */
        destroy_parse_katcl(p);
      }

      delete_job_katcp(d, j);

      s->s_number--;
      if(i < s->s_number){
        s->s_tasks[i] = s->s_tasks[s->s_number];
      }
    } else {
      i++;
    }

  }

  return 0;
}

/* functions offered to users *************************************/

struct katcp_job *find_job_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  unsigned int i;

  s = d->d_shared;

  for(i = 0; i < s->s_number; i++){
    j = s->s_tasks[i];
    sane_job_katcp(j);
    if(!strcmp(name, j->j_url->u_str)){
      return j;
    }
  }

  return NULL;
}

struct katcp_job *find_containing_job_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  unsigned int i;

  s = d->d_shared;

  for(i = 0; i < s->s_number; i++){
    j = s->s_tasks[i];
    sane_job_katcp(j);
    if(containing_kurl_katcp(j->j_url, name) > 0){
      return j;
    }
  }

  return NULL;
}

struct katcp_job *process_name_create_job_katcp(struct katcp_dispatch *d, char *cmd, char **argv, struct katcp_notice *halt, struct katcp_notice *relay)
{
  struct katcp_url *u;
  struct katcp_job *j;

  u = create_exec_kurl_katcp(cmd);
  if(u){
    j = process_relay_create_job_katcp(d, u, argv, halt, relay);
    if(j){
      return j;
    }
    destroy_kurl_katcp(u);
  }

  return NULL;
}

#if 0
struct katcp_job *process_create_job_katcp(struct katcp_dispatch *d, struct katcp_url *file, char **argv, struct katcp_notice *halt)
{
  return process_relay_create_job_katcp(d, file, argv, halt, NULL);
}
#endif

struct katcp_job *process_relay_create_job_katcp(struct katcp_dispatch *d, struct katcp_url *file, char **argv, struct katcp_notice *halt, struct katcp_notice *relay)
{
  int fds[2];
  pid_t pid;
  char *ptr;
  int copies, len;
  struct katcl_line *xl;
  struct katcp_job *j;
  char *client;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    return NULL;
  }

  /* WARNING: if we every destroy logic in child after fork, client content needs to be duplicated */
  if(d && (d->d_name[0] != '\0')){
    client = d->d_name;
  } else {
    client = "unknown";
  }

  pid = fork();
  if(pid < 0){
    close(fds[0]);
    close(fds[1]);
    return NULL;
  }

  if(pid > 0){
    close(fds[0]);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    j = create_job_katcp(d, file, pid, fds[1], 0, halt);
    if(j == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to allocate job logic so terminating child process");
      kill(pid, SIGTERM);
      close(fds[1]);
#if 0
      /* convention: on sucess we assume responsibility for all pointers we are given, on failure we are not responsibly for anything. A failure should be equivalnet to the call never happening */
      destroy_kurl_katcp(file);
#endif
      return NULL;
    }

    /* construct a #inform from the command given, register it triggering given relay */
    if(relay && file->u_cmd){
      len = strlen(file->u_cmd) + 2;
      ptr = malloc(len);
      if(ptr){
        snprintf(ptr, len, "%c%s", KATCP_INFORM, file->u_cmd);
        ptr[len - 1] = '\0';

        if(match_notice_job_katcp(d, j, ptr, relay) < 0){
          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register command relay for job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
        }
        free(ptr);
      }
    }

    return j;
  }

  /* WARNING: now in child, do not call return, use exit */

  setenv("KATCP_CLIENT", client, 1);

  xl = create_katcl(fds[0]);

  close(fds[1]);

  copies = 0;
  if(fds[0] != STDOUT_FILENO){
    if(dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard output for child process %u (%s)", getpid(), strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(fds[0] != STDIN_FILENO){
    if(dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard input for child process %u (%s)", getpid(), strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(copies >= 2){
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  }

  execvp(file->u_cmd, argv);
  sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to run command %s (%s)", file->u_cmd, strerror(errno)); 

  destroy_katcl(xl, 0);

  exit(EX_OSERR);
  return NULL;
}

struct katcp_job *network_name_connect_job_katcp(struct katcp_dispatch *d, char *host, int port, struct katcp_notice *halt)
{
  struct katcp_url *u;
  struct katcp_job *j;

  u = create_kurl_katcp("katcp", host, port, "/");
  if(u){
    j = network_connect_job_katcp(d, u, halt);
    if(j){
      return j;
    }
    destroy_kurl_katcp(u);
  }

  return NULL;
}

struct katcp_job *network_connect_job_katcp(struct katcp_dispatch *d, struct katcp_url *url, struct katcp_notice *halt)
{
  struct katcp_job *j;
  int fd;

  fd = net_connect(url->u_host, url->u_port, NETC_ASYNC | NETC_TCP_KEEP_ALIVE);

  if (fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to connect to %s:%d: %s", url->u_host, url->u_port, strerror(errno));
    return NULL;
  }
  
  /* WARNING: j->j_url->str is can not be taken as a unique key if we connect to the same host more than once */
  /* this host is the search string for job and notice */
  j = create_job_katcp(d, url, 0, fd, 1, halt);

  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"unable to allocate job logic so closing connection");
    close(fd);
  }
  
  return j;
}

/***********************************************************************************/

#define KATCP_JOB_UNLOCK   0x1
#define KATCP_JOB_LOCAL    0x2
#define KATCP_JOB_GLOBAL   0x3

#define KATCP_GLOBAL_NOTICE "any-job-exit"
#define KATCP_LOCAL_NOTICE  "job-%s-exit"

int subprocess_resume_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  int count, i;

  p = get_parse_notice_katcp(d, n);
  if(p){
    count = get_count_parse_katcl(p);
  } else {
    count = 0;
  }

  prepend_reply_katcp(d);

  if(count < 2){
    append_string_katcp(d, KATCP_FLAG_LAST, KATCP_FAIL);
  } else {
    for(i = 1; i < (count - 1); i++){
      append_parameter_katcp(d, KATCP_FLAG_BUFFER, p, i);
    }
    append_parameter_katcp(d, KATCP_FLAG_LAST, p, i);
  }

  resume_katcp(d);

  return 0;
}

int subprocess_start_job_katcp(struct katcp_dispatch *d, int argc, int options)
{
  char **vector;
  struct katcp_job *j;
  struct katcp_notice *n, *nr;
  char *ptr, *cmd;
  int i, len;
  struct katcp_url *u;

  cmd = arg_string_katcp(d, 0);
  if((cmd == NULL) || (cmd[0] != KATCP_REQUEST)) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire name");
    return KATCP_RESULT_FAIL;
  }

  u = create_exec_kurl_katcp(cmd + 1);
  if (u == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse name uri from %s", cmd);
    return KATCP_RESULT_FAIL;
  }

  switch(options){
    case KATCP_JOB_UNLOCK  : /* run all instances concurrently */
      n = create_notice_katcp(d, NULL, 0);
      break;

    case KATCP_JOB_LOCAL  : /* only run one instance of this command */

      len = strlen(KATCP_LOCAL_NOTICE) + strlen(u->u_cmd);
      ptr = malloc(len + 1);
      if(ptr == NULL){
        destroy_kurl_katcp(u);
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len + 1);
        return KATCP_RESULT_FAIL;
      }

      snprintf(ptr, len, KATCP_LOCAL_NOTICE, u->u_cmd);
      ptr[len] = '\0';

      n = find_used_notice_katcp(d, ptr);
      if(n){
        destroy_kurl_katcp(u);
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "still waiting for %s", ptr);
        free(ptr);
        return KATCP_RESULT_FAIL;
      }

      n = create_notice_katcp(d, ptr, 0);
      free(ptr);

      break;

    case KATCP_JOB_GLOBAL  : /* only run one instance of any command */
      n = find_notice_katcp(d, KATCP_GLOBAL_NOTICE);
      if(n){
        destroy_kurl_katcp(u);
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "still waiting for %s", KATCP_GLOBAL_NOTICE);
        return KATCP_RESULT_FAIL;
      }
      n = create_notice_katcp(d, KATCP_GLOBAL_NOTICE, 0);
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bad option to subprocess");
      return KATCP_RESULT_FAIL;
  }

  if(n == NULL){
    destroy_kurl_katcp(u);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice");
    return KATCP_RESULT_FAIL;
  }

  vector = malloc(sizeof(char *) * (argc + 1));
  if(vector == NULL){
    destroy_kurl_katcp(u);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", sizeof(char *) * (argc + 1));
    return KATCP_RESULT_FAIL;
  }

  vector[0] = u->u_cmd;
  for(i = 1; i < argc; i++){
    /* WARNING: won't deal with arguments containing \0, but then again, the command-line doesn't do either */
    vector[i] = arg_string_katcp(d, i);
  }

#if 0
  nr = create_notice_katcp(d, NULL, 0);
  if(nr){
    if(add_notice_katcp(d, nr, &relay_cmd_job_katcp, NULL)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to add to notice %s", n->n_name);
    }
  }
#endif

  nr = register_notice_katcp(d, NULL, 0, &relay_cmd_job_katcp, NULL);

  vector[i] = NULL;
  j = process_relay_create_job_katcp(d, u, vector, n, nr);
  free(vector);

  if(j == NULL){
    destroy_kurl_katcp(u);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create job for command %s", cmd);
    return KATCP_RESULT_FAIL;
  }

  if(add_notice_katcp(d, n, &subprocess_resume_job_katcp, NULL)){
#if 0
    /* once proces_create has succeeded, it is its responsibility to clean up and destroy kurl - so rather end the job */
    destroy_kurl_katcp(u);
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to watch notice");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_PAUSE;
}

int unlocked_subprocess_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return subprocess_start_job_katcp(d, argc, KATCP_JOB_UNLOCK);
}

#if 0
int register_unlocked_subprocess_katcp(struct katcp_dispatch *d, char *match, char *help, int flags, int mode)
{
  /* this could be construed as excessive layering */
  return register_flag_mode_katcp(d, match, help, &unlocked_subprocess_cmd_katcp, flags, mode);
}
#endif

int register_subprocess_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *help, *mode;
  int fail, code;

  if(argc < 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return KATCP_RESULT_FAIL;
  }

  fail = 0;

  name = arg_string_katcp(d, 1);
  help = arg_string_katcp(d, 2);
  if((name == NULL) || (help == NULL)){
    fail = 1;
  }

  if(argc < 4){
    mode = NULL;
  } else {
    mode = arg_string_katcp(d, 3);
    if(mode == NULL){
      fail = 1;
    }
  }

  if(fail){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire parameters");
    return KATCP_RESULT_FAIL;
  }

  code = query_mode_code_katcp(d, mode);
  if(code < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown mode %s", mode);
    return KATCP_RESULT_FAIL;
  }

  if(register_flag_mode_katcp(d, name, help, &unlocked_subprocess_cmd_katcp, 0, code) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register handler for command %s", name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/* debug/diagnositic access to job logic **************************/

int resume_job(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{ 
  struct katcl_parse *p;
  char *ptr;
#if 0
  int i;
#endif

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "got something from job via notice %p", n);

  p = get_parse_notice_katcp(d, n);
  if(p){
    ptr = get_string_parse_katcl(p, 1);
#ifdef DEBUG
    fprintf(stderr, "resume: parameter %d is %s\n", 1, ptr);
#endif
  } else {
    ptr = NULL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, ptr ? ptr : KATCP_FAIL);

  resume_katcp(d);
  return 0;
}

int match_job(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "got match from job via notice %p", n);

  p = get_parse_notice_katcp(d, n);
  if(p){
    append_parse_katcp(d, p);
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to retrieve matched data");
  }

  return 0;
}

int job_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  struct katcp_notice *n;
#if 0
  struct katcp_notice *nr;
#endif
  struct katcl_parse *p;
  char *name, *watch, *cmd, *host, *label, *buffer, *tmp;
  char **vector;
  int i, k, count, len, flags, special;
  struct katcp_url *url;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return KATCP_RESULT_FAIL;
  } else {
    if(!strcmp(name, "list")){
      for(i = 0; i < s->s_number; i++){
        j = s->s_tasks[i];
#if 0
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "job on %s (%p) with %d notices in queue, %s, %s, %s, %s, %s and %s", 
        j->j_url->u_str, j, j->j_count,
        (j->j_state & JOB_MAY_REQUEST) ? "can issue requests" : "has a request pending", 
        (j->j_state & JOB_MAY_WRITE) ? "can write data" : "has finished writing", 
        (j->j_state & JOB_MAY_READ) ? "can read" : "has stopped reading", 
        (j->j_state & JOB_MAY_WORK) ? "can process data" : "has no more data", 
        (j->j_state & JOB_MAY_KILL) ? "may be signalled" : "may not be signalled", 
        (j->j_state & JOB_MAY_COLLECT) ? "has an outstanding status code" : "has no status to collect");

        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "job on %s (%p) with %d notices in queue, %s, %s, %s, %s, and %s", 
        j->j_url->u_str, j, j->j_count,
        (j->j_state & JOB_MAY_REQUEST) ? "can issue requests" : "has a request pending", 
        (j->j_state & JOB_MAY_WRITE) ? "can write data" : "has finished writing", 
        (j->j_state & JOB_MAY_READ) ? "can read" : "has stopped reading", 
        (j->j_state & JOB_MAY_WORK) ? "can process data" : "has no more data", 
        (j->j_pid == 0) ? "is not a subprocess" : "is a subprocess");
#endif

        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "job on %s (%p) with %d notices in queue, %d sent requests, %d received requests, pid %d and state %d", 
        j->j_url->u_str, j, j->j_count,
        j->j_sendr, j->j_recvr, j->j_pid, j->j_state);
        log_map_katcp(d, j->j_url->u_str, j->j_map);
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d jobs", s->s_number);

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "match")){

      label = arg_string_katcp(d, 2);
      watch = arg_string_katcp(d, 3);

      if((label == NULL) || (watch == NULL)){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a job label and match");
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(d, label);
      if(j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find job labelled %s", label);
        return KATCP_RESULT_FAIL;
      }

      if(match_inform_job_katcp(d, j, watch, &match_job, NULL) < 0){
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "stop")){

      label = arg_string_katcp(d, 2);

      if(label == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a job label");
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(d, label);
      if(j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find job labelled %s", label);
        return KATCP_RESULT_FAIL;
      }

      zap_job_katcp(d, j);

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "process")){
      watch = arg_string_katcp(d, 2);
      cmd = arg_string_katcp(d, 3);

      if((cmd == NULL) || (watch == NULL)) {
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "insufficient parameters for launch");
        return KATCP_RESULT_FAIL;
      }
      url = create_kurl_from_string_katcp(cmd);
      if (url == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse name uri try exec:///path/to/file");
        return KATCP_RESULT_FAIL;
      }

      if(url->u_cmd == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "url %s has no executable component", cmd);
        return KATCP_RESULT_FAIL;
      }

      n = create_notice_katcp(d, watch, 0);
      if(n == NULL){
        destroy_kurl_katcp(url);
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notice called %s", watch);
        return KATCP_RESULT_FAIL;
      }

      if(argc < 4){
        destroy_kurl_katcp(url);
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%d parameters is too little", argc);
        return KATCP_RESULT_FAIL;
      }

      vector = malloc(sizeof(char *) * argc - 2);
      if(vector == NULL){
        destroy_kurl_katcp(url);
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate vector of %d elements", argc - 2);
        return KATCP_RESULT_FAIL;
      }

      vector[0] = url->u_cmd;
      k = 1;

      for(i = 4; i < argc; i++){
        vector[k] = arg_string_katcp(d, i);
        log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "parameter %d is %s", k, vector[k]);
        k++;
      }
      vector[k] = NULL;

#if 0
      nr = register_notice_katcp(d, NULL, 0, &relay_cmd_job_katcp, NULL);
      j = process_relay_create_job_katcp(d, url, vector, n, nr);
#else
      j = process_relay_create_job_katcp(d, url, vector, n, NULL);
#endif

      free(vector);

      if(j == NULL){
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }

#if 0 /* now sensors are always matched */
      job_match_sensor_katcp(d, j);
#endif

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "network")){ 
      watch = arg_string_katcp(d, 2);
      host = arg_string_katcp(d, 3);

      if ((watch == NULL) || (host == NULL)){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "insufficient parameters for launch");
        return KATCP_RESULT_FAIL;
      }
      url = create_kurl_from_string_katcp(host);
      if (url == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse name uri try katcp://host:port");
        return KATCP_RESULT_FAIL;
      }

      n = create_notice_katcp(d, watch, 0);
      if (n == NULL){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice called %s", watch);
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }

      //j = network_connect_job_katcp(d, host, port, n); 
      j = network_connect_job_katcp(d, url, n); 
      
      if (j == NULL){
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "watchdog")){

      label = arg_string_katcp(d, 2);
      if(label == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a job label to test");
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(d, label);
      if(j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find job labelled %s", label);
        return KATCP_RESULT_FAIL;
      }

      p = create_parse_katcl();
      if(p == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create message");
        return KATCP_RESULT_FAIL;
      }

      if(add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?watchdog") < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to assemble message");
        destroy_parse_katcl(p);
        return KATCP_RESULT_FAIL;
      }

#ifdef DEBUG
      fprintf(stderr, "job: submitting parse %p to job %p\n", p, j);
#endif

      if(submit_to_job_katcp(d, j, p, NULL, &resume_job, NULL) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to submit message to job");
        destroy_parse_katcl(p);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_PAUSE;

    } else if(!strcmp(name, "relay")){

      label = arg_string_katcp(d, 2);
      if(label == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a job label to test");
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(d, label);
      if(j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find job labelled %s", label);
        return KATCP_RESULT_FAIL;
      }

      count = arg_count_katcp(d);
      if(count < 4){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a command to send");
        return KATCP_RESULT_FAIL;
      }

      p = create_parse_katcl();
      if(p == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create message");
        return KATCP_RESULT_FAIL;
      }
      
      flags = KATCP_FLAG_FIRST;
      buffer = NULL;
      i = 3;
      special = 1;

      while(i < count){
        len = arg_buffer_katcp(d, i, NULL, 0);
        if(len < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bad length %d for argument %d", len, i);
          if(buffer){
            free(buffer);
          }
          destroy_parse_katcl(p);
          return KATCP_RESULT_FAIL;
        }
        tmp = realloc(buffer, len + special);
        if(tmp == NULL){
          if(buffer){
            free(buffer);
          }
          destroy_parse_katcl(p);
          return KATCP_RESULT_FAIL;
        }
        buffer = tmp;
        arg_buffer_katcp(d, i, buffer + special, len);
        if(special){
          if(buffer[special] != KATCP_REQUEST){
            buffer[0] = KATCP_REQUEST;
            special = 0;
            len++;
          }
        }

        i++;
        if(i == count){
          flags |= KATCP_FLAG_LAST;
        }

        if(add_buffer_parse_katcl(p, flags | KATCP_FLAG_BUFFER, buffer + special, len) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to assemble message");
          free(buffer);
          destroy_parse_katcl(p);
          return KATCP_RESULT_FAIL;
        }

        flags = 0;
        special = 0;
      }

      if(buffer){
        free(buffer);
      }

      if(submit_to_job_katcp(d, j, p, NULL, &resume_job, NULL) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to submit message to job");
        destroy_parse_katcl(p);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_PAUSE;

    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown job request %s", name);
      return KATCP_RESULT_FAIL;
    }
  }
}

#ifdef UNIT_TEST_JOB

#include <unistd.h>

#define TEST_LOOPS  10000

#define EMPTY_CHANCE  200
#define REMOVE_CHANCE   3

int main()
{
  struct katcp_job *j;
  struct katcp_dispatch *d;
  struct katcp_notice *n;
  struct katcp_url *ku;
  int i, k, r;

  srand(getpid());
  fprintf(stderr, "test: seed is %d\n", getpid());

  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "unable to create dispatch\n");
    return 1;
  }

  ku = create_kurl_from_string_katcp("katcp://example.com:7147/");
  if(ku == NULL){
    fprintf(stderr, "unable to create url\n");
    return 1;
  }

  j = create_job_katcp(d, ku, 0, -1, 0, NULL);
  if(j == NULL){
    fprintf(stderr, "unable to create job\n");
    return 1;
  }

  n = create_notice_katcp(d, "test", 0);
  if(n == NULL){
    fprintf(stderr, "unable to create notice\n");
    return 1;
  }

  for(i = 0; i < TEST_LOOPS; i++){
    r = rand() % EMPTY_CHANCE;
    if(r == 0){
      for(k = 0; k < i; k++){
        remove_head_job(NULL, j);
      }
    } else if((r % REMOVE_CHANCE) == 0){
      remove_head_job(NULL, j);
    } else {
      add_tail_job(NULL, j, n);
    }
    dump_queue_job_katcp(j, stderr);
  }

  return 0;
}
#endif

#endif
