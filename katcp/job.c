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

/* functions given to notice to unlink itself from job ************/

#if 0
int unlink_halt_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *target)
{
  struct katcp_job *j;

  j = target;

  j->j_halt = NULL;
  
  /* TODO: maybe initiate destruction of job */

  return 0;
}
#endif


/* manage the job queue notice logic *************************************************/

static int add_tail_job(struct katcp_job *j, struct katcp_notice *n)
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

  n->n_use++;

  return 0;
}

static struct katcp_notice *remove_index_job(struct katcp_job *j, unsigned int index)
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
    n->n_use--;
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
      n->n_use--;
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

  n->n_use--;
  return n;
}

static struct katcp_notice *remove_head_job(struct katcp_job *j)
{
  return remove_index_job(j, j->j_head);
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

  j->j_halt = NULL;
  j->j_count = 0;

  j->j_state = 0;

  free(j);
}

/* create, modify notices to point back at job ********************/

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, char *name, pid_t pid, int fd, struct katcp_notice *halt)
{
  struct katcp_job *j, **t;
  struct katcp_shared *s;

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
  j->j_halt = halt;

  j->j_state = JOB_MAY_REQUEST | JOB_MAY_WRITE | JOB_MAY_WORK | JOB_MAY_READ;
  if(j->j_pid > 0){
    j->j_state |= JOB_MAY_KILL | JOB_MAY_COLLECT;
  }

  j->j_code = KATCP_RESULT_INVALID;
  j->j_line = NULL;

  j->j_queue = NULL;
  j->j_head = 0;
  j->j_count = 0;
  j->j_size = 0;

  /* WARNING: slightly ugly resource release logic on failure, but avoids accidentally closing fd on failure */
  j->j_name = strdup(name);
  if(j->j_name == NULL){
    free(j);
    return NULL;
  }

  if(fd >= 0){
    j->j_line = create_katcl(fd);
    if(j->j_line == NULL){
      if(j->j_name){
        free(j->j_name);
        j->j_name = NULL;
      }
      free(j);
      return NULL;
    }
  }

  if(halt){
    halt->n_use++;
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

/* manually stop a task, trigger notice as if stopped on its own, hard makes it unsafe **/

int stop_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, int hard)
{
  int result;

  sane_job_katcp(j);

  result = 0;

  /* WARNING: maybe: only kill jobs which have a pid, wait for the child to close - that ensures that we receive all messages */

  if((j->j_state & JOB_MAY_KILL) && (j->j_pid > 0)){
    if(kill(j->j_pid, hard ? SIGKILL : SIGTERM) < 0){
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

int issue_request_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  struct katcp_notice *n;

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
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "queue of %u elements is empty at %u", j->j_count, j->j_head);
    return -1;
  }

  if(append_parse_katcl(j->j_line, n->n_parse, 0) < 0){
    /* TODO: somehow wake notice informing it of the failure */
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to append request message to job");
    return -1;
  }

  j->j_state &= ~(JOB_MAY_REQUEST);

  return 0;
}

int submit_to_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, struct katcl_parse *p, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n))
{
  struct katcp_notice *n;

  sane_job_katcp(j);

  n = create_parse_notice_katcp(d, NULL, 0, p);
  if(n == NULL){
    return -1;
  }

  if(add_tail_job(j, n)){
#if 0
    /* WARNING: may not be needed */
    cancel_notice_katcp(d, n);
#endif
    return -1;
  }

  if(add_notice_katcp(d, n, call)){
#if 0
    cancel_notice_katcp(d, n);
#endif
    return -1;
  }

  issue_request_job_katcp(d, j); /* ignore return code, errors should come back as replies */

  return 0;
}

static int field_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result, code;
  char *cmd, *module, *message, *priority;
  struct katcp_notice *n;
  struct katcl_parse *p;

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

      n = remove_head_job(j);
      if(n == NULL){
        /* as long as there are references, notices should not go away */
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no outstanding notice despite waiting for a reply");
        return -1;
      }

      wake_notice_katcp(d, n, p);

      j->j_state |= JOB_MAY_REQUEST;

      /* see if we can start the next round, if there is one */
      issue_request_job_katcp(d, j);
      break;

    case KATCP_INFORM  :

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
  int status;
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
           j->j_code = WEXITSTATUS(status) ? KATCP_RESULT_FAIL : KATCP_RESULT_OK;
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

    if(j->j_state & (JOB_MAY_READ | JOB_MAY_WRITE)){
      fd = fileno_katcl(j->j_line);
      if(fd >= 0){
        if(j->j_state & JOB_MAY_READ){
          FD_SET(fd, &(s->s_read));
        }
        if((j->j_state & JOB_MAY_WRITE) && flushing_katcl(j->j_line)){
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
  struct katcp_msg *m;
  struct katcp_job *j;
  struct katcl_parse *pq, *pp; /* reQuest, rePly */
  int i, count, fd, result;
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

    if(j->j_state & (JOB_MAY_READ | JOB_MAY_WRITE)){
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

#ifdef DEBUG
    fprintf(stderr, "job: state after run is 0x%x\n", j->j_state);
#endif

    if(j->j_state == 0){ 

      n = remove_head_job(j);
      while(n != NULL){
#if 0
        pq = parse_notice_katcp(d, n);
        pp = NULL;
        if(m){
          p = create_parse_katcl();
          if(p){
            /* TODO */
            log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "need to provide status");
          }
        }
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "waking notices still queued");
#endif
        wake_notice_katcp(d, n, NULL);
        n = remove_head_job(j);
      }

      if(j->j_halt){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "waking halt notice");
        n = j->j_halt;
        j->j_halt = NULL;

        pp = create_parse_katcl();
        if(pp){
          add_plain_parse_katcl(pp, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, KATCP_INFORM_JOB);
          add_plain_parse_katcl(pp, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "stop");
        }

        n->n_use--;
        wake_notice_katcp(d, n, pp);
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

    j = create_job_katcp(d, file, pid, fds[1], halt);
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

  fd = net_connect(host, port, 0);

  if (fd < 0){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Unable to connect to ROACH: %s",host);
    return NULL;
  }
  
  /* WARNING: j->j_name is can not be taken as a unique key if we connect to the same host more than once */
  j = create_job_katcp(d, host, 0, fd, halt);

  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"unable to allocate job logic so closing connection");
    close(fd);
  }
  
  return j;
}

/* debug/diagnositic access to job logic **************************/

int resume_job(struct katcp_dispatch *d, struct katcp_notice *n)
{ 
  struct katcl_parse *p;
  char *ptr;
#if 0
  int i;
#endif

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "got something from job via notice %p", n);

  p = parse_notice_katcp(d, n);
  if(p){
    ptr = get_string_parse_katcl(p, 1);
#ifdef DEBUG
    fprintf(stderr, "resume: parameter %d is %s\n", 1, ptr);
#endif
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "got something from job via notice %p", n);
  } else {
    ptr = NULL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, ptr ? ptr : KATCP_FAIL);

  resume_katcp(d);
  return 0;
}

int job_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  struct katcp_notice *n;
  struct katcl_parse *p;
  char *name, *watch, *cmd, *host, *label;
  char *vector[2];
  int i, port, count;

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
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d jobs", s->s_number);
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

      if(submit_to_job_katcp(d, j, p, &resume_job) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to submit message to job");
        destroy_parse_katcl(p);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_PAUSE;
#if 0
    } else if(!strcmp(name, "quote")){

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
      if(count < 3){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a command to send");
        return KATCP_RESULT_FAIL;
      }

      m = create_msg_katcl(NULL);
      if(m == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create message");
        return KATCP_RESULT_FAIL;
      }

      for(i = 2; i < count; i++){
        /* should have a nicer way of transferring arg from one to another */
      }

      return KATCP_RESULT_PAUSE;
#endif
    } else {
      return KATCP_RESULT_FAIL;
    }
  }
}

#ifdef UNIT_TEST

#include <unistd.h>

#define FUDGE 10000

int main()
{
  struct katcp_job *j;
  struct katcp_dispatch *d;
  int i, k, r;

  srand(getpid());
  fprintf(stderr, "test: seed is %d\n", getpid());

  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "unable to create dispatch\n");
    return 1;
  }

  j = create_job_katcp(d, 0, -1, NULL);
  if(j == NULL){
    fprintf(stderr, "unable to create job\n");
    return 1;
  }

  for(i = 0; i >= 0; i++){
    r = rand() % FUDGE;
    if(r == 0){
      for(k = 0; k < FUDGE; k++){
        remove_head_job(j);
      }
    } else if((r % 3) == 0){
      remove_head_job(j);
    } else {
      add_tail_job(j, (struct katcp_notice *) i);
    }
    dump_queue_job_katcp(j, stderr);
  }

  return 0;
}
#endif
