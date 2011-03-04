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
#include "katsensor.h"
#include "netc.h"

#define JOB_MAGIC 0x21525110

#define JOB_MAY_REQUEST 0x01
#define JOB_MAY_WRITE   0x02
#define JOB_MAY_WORK    0x04
#define JOB_MAY_READ    0x08
#define JOB_MAY_KILL    0x10
#define JOB_MAY_COLLECT 0x20
#define JOB_PRE_CONNECT 0x40

/******************************************************************/

#ifdef DEBUG
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
  struct katcp_notice **tmp;
  unsigned int index;

  if(j->j_count >= j->j_size){

#ifdef DEBUG
    fprintf(stderr, "size=%d, count=%d - increasing queue\n", j->j_size, j->j_count);
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

  return 0;
}

static struct katcp_notice *remove_index_job(struct katcp_dispatch *d, struct katcp_job *j, unsigned int index)
{
  unsigned int end;
  struct katcp_notice *n;

  if(j->j_count <= 0){
#ifdef DEBUG
    fprintf(stderr, "remove: nothing to remove\n");
#endif
    return NULL;
  }

#ifdef DEBUG
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

#ifdef DEBUG
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
#ifdef DEBUG
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
  
#ifdef DEBUG
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

  if(j->j_name){
    free(j->j_name);
    j->j_name = NULL;
  }

  if(j->j_map){
    destroy_map_katcp(d, j->j_map);
    j->j_map = NULL;
  }

  j->j_halt = NULL;
  j->j_count = 0;

  j->j_state = 0;

  free(j);
}

/* create, modify notices to point back at job ********************/

static int relay_log_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "should detach log handler");
    return 0;
  }

  log_relay_katcp(d, p);

  return 1;
}

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, char *name, pid_t pid, int fd, int async, struct katcp_notice *halt)
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
  j->j_name = NULL;

  j->j_pid = pid;
  j->j_halt = NULL;

  if(async){
    j->j_state = JOB_PRE_CONNECT;
  } else {
    j->j_state = JOB_MAY_REQUEST | JOB_MAY_WRITE | JOB_MAY_WORK | JOB_MAY_READ;
  }

  if(j->j_pid > 0){
    j->j_state |= JOB_MAY_KILL | JOB_MAY_COLLECT;
  }

  j->j_code = KATCP_RESULT_INVALID;
  j->j_line = NULL;

  j->j_queue = NULL;
  j->j_head = 0;
  j->j_count = 0;
  j->j_size = 0;

  j->j_map = NULL;

  j->j_name = strdup(name);
  if(j->j_name == NULL){
    delete_job_katcp(d, j);
    return NULL;
  }

  j->j_map = create_map_katcp();
  if(j->j_map == NULL){
    delete_job_katcp(d, j);
    return NULL;
  }

  dl = template_shared_katcp(d);
  if(dl){
    if(match_inform_job_katcp(dl, j, "#log", &relay_log_job_katcp, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register log relay for job %s", j->j_name ? j->j_name : "<anonymous>");
    }
  } 

  /* WARNING: do line clone last, so that fd isn't closed on failure */
  if(fd >= 0){
    j->j_line = create_katcl(fd);
    if(j->j_line == NULL){
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

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "terminating job %p (%s)", j, j->j_name ? j->j_name : "<anonymous>");

  j->j_state = 0;

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

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "ending job [%d]=%p (%s) immediately", i, j, j->j_name ? j->j_name : "<anonymous>");

    j->j_state = 0;
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

  if((j->j_state & JOB_MAY_REQUEST) == 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "not sending request, another one already in queue\n");
    return 0; /* still waiting for a reply */
  }

  if((j->j_state & JOB_MAY_WRITE) == 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "not sending request, finished writing\n");
    return 0; /* still waiting for a reply */
  }

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
      j->j_state &= ~(JOB_MAY_REQUEST);
      return 0;
    }

    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to append request message to job");

    /* WARNING: after turnaround, p never valid */
    px = turnaround_parse_katcl(p, KATCP_RESULT_FAIL);
  } else {
    px = NULL;
  }

  n = remove_head_job(d, j);
  if(n){
    update_notice_katcp(d, n, px, 1, 1);
  }

  /* TODO: try next item if this one fails */

  return -1;
}

int match_inform_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, char *match, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_notice *n;
  struct katcp_trap *kt;
  struct katcl_parse *p;
  char *ptr;
  int len;

  sane_job_katcp(j);

  kt = find_map_katcp(j->j_map, match);
  if(kt == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "allocating new inform match for %s", match);

#if 1 /* unclear if this is really needed, could probably do with an empty parse  ... */
    p = create_parse_katcl(NULL);
    if(p == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate empty message");
      return -1;
    }

    if(add_plain_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST | KATCP_FLAG_LAST, match) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to assemble empty message");
      destroy_parse_katcl(p);
      return -1;
    }
#endif

    /* TODO: adjust when katcp:// scheme is sorted out */
    len = (j->j_name ? strlen(j->j_name) : 0) + strlen(match) + 1;

    ptr = malloc(len);
    if(ptr){
      snprintf(ptr, len, "%s%s", j->j_name, match);
    }

    n = create_parse_notice_katcp(d, ptr ? ptr : match, 0, p);
    if(ptr){
      free(ptr);
    }

    if(n == NULL){
      destroy_parse_katcl(p);
      return -1;
    }

    if(add_map_katcp(d, j->j_map, match, n) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to add match %s to job", match);
      return -1;
    }


  } else {
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "adding extra inform match for %s", match);
    n = kt->t_notice;
  }

  if(add_notice_katcp(d, n, call, data)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to add to notice %s", n->n_name);
    return -1;
  }

  return 0;
}

int submit_to_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, struct katcl_parse *p, char *name, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_notice *n;

  sane_job_katcp(j);

  n = create_parse_notice_katcp(d, name, 0, p);
  if(n == NULL){
    return -1;
  }

  if(call){
    if(add_notice_katcp(d, n, call, data)){
#if 0
      cancel_notice_katcp(d, n);
#endif
      return -1;
    }
  }

  return notice_to_job_katcp(d, j, n);
}

int notice_to_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, struct katcp_notice *n)
{
  sane_job_katcp(j);

#ifdef DEBUG
  fprintf(stderr, "submitting notice %p(%s) to job %p(%s)\n", n, n->n_name, j, j->j_name);

  if(get_parse_notice_katcp(d, n) == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "notice submitted to job should have an associated parse structure, and probably a callback too");
  }
#endif

  if(add_tail_job(d, j, n)){
#if 0
    /* WARNING: may not be needed */
    cancel_notice_katcp(d, n);
#endif
    return -1;
  }

  hold_notice_katcp(d, n);

  issue_request_job_katcp(d, j); /* ignore return code, errors should come back as replies */

  return 0;
}

static int field_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result;
  char *cmd;
  struct katcp_notice *n;
  struct katcl_parse *p;
  struct katcp_trap *kt;

  result = have_katcl(j->j_line);

  if(result <= 0){
#ifdef DEBUG
  fprintf(stderr, "job: nothing to field (result=%d)\n", result);
#endif
    return result;
  }

  cmd = arg_string_katcl(j->j_line, 0);
  if(cmd == NULL){
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "job: processing message starting with <%s ..>\n", cmd);
#endif

  switch(cmd[0]){

    case KATCP_REQUEST : 
      /* our logic is unable to service requests */
#if 0
      extra_response_katcl(j->j_line, KATCP_RESULT_FAIL, NULL);
#endif
      break;

    case KATCP_REPLY   :

      if(j->j_state & JOB_MAY_REQUEST){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "received spurious reply, no request was issued");
        return -1;
      }

      p = ready_katcl(j->j_line);
      if(p == NULL){
        /* if we got this far, we should really have a ready data structure */
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to retrieve parsed job data");
        return -1;
      }

      n = remove_head_job(d, j);
      if(n == NULL){
        /* as long as there are references, notices should not go away */
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no outstanding notice despite waiting for a reply");
        return -1;
      }

      update_notice_katcp(d, n, p, 1, 1);

      j->j_state |= JOB_MAY_REQUEST;

      /* see if we can start the next round, if there is one */
      issue_request_job_katcp(d, j);
      break;

    case KATCP_INFORM  :

      p = ready_katcl(j->j_line);
      if(p == NULL){
        /* if we got this far, we should really have a ready data structure */
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to retrieve parsed job data");
        return -1;
      }

      kt = find_map_katcp(j->j_map, cmd);
      if(kt){
        wake_notice_katcp(d, kt->t_notice, p);
      } else {
        log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "ignoring inform %s of job %s", cmd, j->j_name);
      }

#if 0
      if(!strcmp(cmd, "#log")){

        /* TODO: fix this section */
        /* WARNING: ditches timestamp from log message */
        /* WARNING: is disgustingly inefficient */

        priority = arg_string_katcl(j->j_line, 1);
        module = arg_string_katcl(j->j_line, 3);
        message = arg_string_katcl(j->j_line, 4);

        code = (-1);
        if(priority){
          code = log_to_code(priority);
        }

        if((message == NULL) || (module == NULL) || (code < 0)){
          return -1;
        }

        log_message_katcp(d, code, module, "%s", message);
      } else 
#endif
      
      if(!strcmp(cmd, KATCP_RETURN_JOB)){

#ifdef DEBUG
        fprintf(stderr, "job: saw return inform message\n");
#endif

        /* WARNING: may have to terminate job to maintain consistency, otherwise halt notices may no longer assume that job is gone */
#if 0
        j->j_state &= ~(JOB_MAY_READ | JOB_MAY_WRITE | JOB_MAY_WORK); /* if job returns, clearly doesn't want to give us more */
        exchange_katcl(j->j_line, -1); /* but close fd's so that termination happens  just to be save */

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

          update_notice_katcp(d, n, p, 1, 1);
        }

        /* terminating job, otherwise halt notice can not assume that job is gone */

        zap_job_katcp(d, j);
      }

    break;
  }

  return 1;
}

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

      if((j->j_pid == pid) && (j->j_state & JOB_MAY_COLLECT)){
        if(WIFEXITED(status)){
          code = WEXITSTATUS(status);
          log_message_katcp(d, code ? KATCP_LEVEL_INFO : KATCP_LEVEL_DEBUG, NULL, "process %d exited with code %d", j->j_pid, code);
          j->j_code = code ? KATCP_RESULT_FAIL : KATCP_RESULT_OK;
        } else if(WIFSIGNALED(status)){
          code = WTERMSIG(status);
          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "process %d terminated by signal %d", j->j_pid, code);
          j->j_code = KATCP_RESULT_FAIL;
        } else {
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "process %d exited abnormally", j->j_pid, code);
          j->j_code = KATCP_RESULT_FAIL;
        }
        j->j_state &= ~(JOB_MAY_KILL | JOB_MAY_COLLECT);
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
        if(fd > s->s_max){
          s->s_max = fd;
        }
      }
    }
  }

  return 0;
}

int run_jobs_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  struct katcp_job *j;
  struct katcl_parse *p, *px;
  int i, count, fd, result, code;
  unsigned int len;
  char *string;

  s = d->d_shared;

  count = 0;
  i = 0;
  while(i < s->s_number){
    j = s->s_tasks[i];

#ifdef DEBUG
    fprintf(stderr, "job: checking job %d/%d in state 0x%x\n", i, s->s_number, j->j_state);
    sane_job_katcp(j);
#endif

#if 0
    if(j->j_state > 1){
      } /* else some notice still interested in this job */
    } else {
#endif

    if(j->j_state & (JOB_MAY_READ | JOB_MAY_WRITE | JOB_PRE_CONNECT)){
      fd = fileno_katcl(j->j_line);
    } else {
      fd = (-1);
    } 

    if((j->j_state & JOB_MAY_READ) && FD_ISSET(fd, &(s->s_read))){
      result = read_katcl(j->j_line);
#ifdef DEBUG
      fprintf(stderr, "job: read from job returns %d\n", result);
#endif
      if(result < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to read from subordinate task");
        j->j_state = 0;
        j->j_code = KATCP_RESULT_FAIL;
      }

      if(result > 0){ /* end of file, won't do further io */
        j->j_state &= ~(JOB_MAY_REQUEST | JOB_MAY_WRITE | JOB_MAY_READ);
        j->j_code = KATCP_RESULT_OK;
      }
    }

    if(j->j_state & JOB_MAY_WORK){
      result = field_job_katcp(d, j);
#ifdef DEBUG
      fprintf(stderr, "job: field job returns %d\n", result);
#endif
      if(result < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to process messages from subordinate task");
        j->j_state = 0;
        j->j_code = KATCP_RESULT_FAIL;
      }

      if(result == 0){ /* nothing in buffer */
        if((j->j_state & (JOB_MAY_READ | JOB_MAY_WRITE)) == 0){ /* and no more io */
          j->j_state &= ~JOB_MAY_WORK; /* implies we are done with processing things */
        }
      }
    }

    if((j->j_state & JOB_MAY_WRITE) && FD_ISSET(fd, &(s->s_write))){
      if(write_katcl(j->j_line) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to write messages to subordinate task");
        j->j_state = 0;
        j->j_code = KATCP_RESULT_FAIL;
      }
    }

    if((j->j_state & JOB_PRE_CONNECT) && FD_ISSET(fd, &(s->s_write))){
      len = sizeof(int);
      result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);
      if(result == 0){
        switch(code){
          case 0 :
            j->j_state = (JOB_MAY_REQUEST | JOB_MAY_WRITE | JOB_MAY_WORK | JOB_MAY_READ) | ((JOB_MAY_KILL | JOB_MAY_COLLECT) & j->j_state);
            log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "async connect to %s succeeded", j->j_name);
            break;
          case EINPROGRESS : 
            log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw an in progress despite write set being ready on job %s", j->j_name);
            break;
          default : 
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to connect to %s: %s", j->j_name, strerror(code));
            break;
        }
      }
    }

#ifdef DEBUG
    fprintf(stderr, "job: state after run is 0x%x\n", j->j_state);
#endif

    if(j->j_state == 0){ 

      n = remove_head_job(d, j);
      while(n != NULL){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "failing pending notice for job %s", j->j_name);

        p = get_parse_notice_katcp(d, n);
        if(p){
          px = turnaround_parse_katcl(p, KATCP_RESULT_FAIL);
          /* p = NULL; */
          if(px == NULL){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to generate failed response on disconnect");
          }
        } else {
          px = NULL;
        }

        update_notice_katcp(d, n, px, 1, 1);
        n = remove_head_job(d, j);
      }

      if(j->j_halt){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "waking halt notice");
        n = j->j_halt;
        j->j_halt = NULL;

        p = create_parse_katcl();
        if(p){
          add_plain_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, KATCP_RETURN_JOB);
          string = code_to_name_katcm(j->j_code);
          add_plain_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_LAST, string ? string : KATCP_FAIL);
        }

        update_notice_katcp(d, n, p, 1, 1);

        /* wake makes a copy, so get rid of this instance */
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
    if(!strcmp(name, j->j_name)){
      return j;
    }
  }

  return NULL;
}

struct katcp_job *process_create_job_katcp(struct katcp_dispatch *d, char *file, char **argv, struct katcp_notice *halt)
{
  int fds[2];
  pid_t pid;
  int copies;
  struct katcl_line *xl;
  struct katcp_job *j;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    return NULL;
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
    }

    return j;
  }

  /* WARNING: now in child, do not call return, use exit */

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

  execvp(file, argv);
  sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to run command %s (%s)", file, strerror(errno)); 

  destroy_katcl(xl, 0);

  exit(EX_OSERR);
  return NULL;
}

struct katcp_job *network_connect_job_katcp(struct katcp_dispatch *d, char *host, int port, struct katcp_notice *halt)
{
  struct katcp_job *j;
  int fd;

  fd = net_connect(host, port, NETC_ASYNC);

  if (fd < 0){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Unable to connect to: %s:%d",host,port);
    return NULL;
  }
  
  /* WARNING: j->j_name is can not be taken as a unique key if we connect to the same host more than once */
  /* this host is the search string for job and notice */
  j = create_job_katcp(d, host, 0, fd, 1, halt);

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
  char *ptr;
  struct katcl_parse *p;
  unsigned int count, i;

  p = get_parse_notice_katcp(d, n);
  if(p){
    ptr = get_string_parse_katcl(p, 1);
  } else {
    ptr = NULL;
  }

  count = get_count_parse_katcl(p);

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
  struct katcp_notice *n;
  char *name, *ptr;
  int i, len;

  name = arg_string_katcp(d, 0);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire name");
    return KATCP_RESULT_FAIL;
  }
  name++;

  switch(options){
    case KATCP_JOB_UNLOCK  : /* run all instances concurrently */
      n = create_notice_katcp(d, NULL, 0);
      break;

    case KATCP_JOB_LOCAL  : /* only run one instance of this command */

      len = strlen(KATCP_LOCAL_NOTICE) + strlen(name);
      ptr = malloc(len + 1);
      if(ptr == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len + 1);
        return KATCP_RESULT_FAIL;
      }

      snprintf(ptr, len, KATCP_LOCAL_NOTICE, name);
      ptr[len] = '\0';

      n = find_used_notice_katcp(d, ptr);
      if(n){
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
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "still waiting for %s", KATCP_GLOBAL_NOTICE);
        return KATCP_RESULT_FAIL;
      }
      n = create_notice_katcp(d, KATCP_GLOBAL_NOTICE, 0);
      break;
  }

  if(n == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice");
    return KATCP_RESULT_FAIL;
  }

  vector = malloc(sizeof(char *) * (argc + 1));
  if(vector == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", sizeof(char *) * (argc + 1));
    return KATCP_RESULT_FAIL;
  }

  vector[0] = name;
  for(i = 1; i < argc; i++){
    /* WARNING: won't deal with arguments containing \0, but then again, the command-line doesn't do either */
    vector[i] = arg_string_katcp(d, i);
  }

  vector[i] = NULL;
  j = process_create_job_katcp(d, name, vector, n);
  free(vector);

  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create job for command %s", name);
    return KATCP_RESULT_FAIL;
  }

  if(add_notice_katcp(d, n, &subprocess_resume_job_katcp, NULL)){
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
  struct katcl_parse *p;
  char *name, *watch, *cmd, *host, *label, *buffer, *tmp;
  char *vector[2];
  int i, port, count, len, flags, special;

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
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "job on %s (%p) with %d notices in queue, %s, %s, %s, %s, %s and %s", 
        j->j_name, j, j->j_count,
        (j->j_state & JOB_MAY_REQUEST) ? "can issue requests" : "has a request pending", 
        (j->j_state & JOB_MAY_WRITE) ? "can write data" : "has finished writing", 
        (j->j_state & JOB_MAY_READ) ? "can read" : "has stopped reading", 
        (j->j_state & JOB_MAY_WORK) ? "can process data" : "has no more data", 
        (j->j_state & JOB_MAY_KILL) ? "may be signalled" : "may not be signalled", 
        (j->j_state & JOB_MAY_COLLECT) ? "has an outstanding status code" : "has no status to collect");
        log_map_katcp(d, j->j_name, j->j_map);
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

    } else if(!strcmp(name, "process")){
      watch = arg_string_katcp(d, 2);
      cmd = arg_string_katcp(d, 3);

      if((cmd == NULL) || (watch == NULL)){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "insufficient parameters for launch");
        return KATCP_RESULT_FAIL;
      }

      n = create_notice_katcp(d, watch, 0);
      if(n == NULL){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice called %s", watch);
        return KATCP_RESULT_FAIL;
      }

      vector[0] = cmd;
      vector[1] = NULL;

      j = process_create_job_katcp(d, cmd, vector, n);

      if(j == NULL){
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "network")){ 
      watch = arg_string_katcp(d, 2);
      host = arg_string_katcp(d, 3);
      port = arg_unsigned_long_katcp(d, 4);

      if ((host == NULL) || (watch == NULL) || (port == 0)){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "insufficient parameters for launch");
        return KATCP_RESULT_FAIL;
      }

      n = create_notice_katcp(d, watch, 0);
      if (n == NULL){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice called %s", watch);
        return KATCP_RESULT_FAIL;
      }

      j = network_connect_job_katcp(d, host, port, n); 
      
      if (j == NULL){
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
  int i, k, r;

  srand(getpid());
  fprintf(stderr, "test: seed is %d\n", getpid());

  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "unable to create dispatch\n");
    return 1;
  }

  j = create_job_katcp(d, "test", 0, -1, 0, NULL);
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
