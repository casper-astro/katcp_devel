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

int unlink_halt_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *payload)
{
  struct katcp_job *j;

  j = payload;

  j->j_halt = NULL;
  
  /* TODO: maybe initiate destruction of job */

  return 0;
}

static int remove_index_job(struct katcp_job *j, unsigned int index)
{
  unsigned int end;
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

  if(j->j_count == 0){
    return -1;
  }

  j->j_queue[index] = NULL;

  if(index == j->j_head){
    /* hopefully the common, simple case: only one interested party */
    j->j_head = (j->j_head + 1) % j->j_size;
    return 0;
  }

  if((j->j_head + j->j_count) > j->j_size){ /* wrapping case */
    if(index >= j->j_head){ /* position before wrap around, move up head */
      if(index > j->j_head){
        memcpy(&(j->j_queue[j->j_head + 1]), &(j->j_queue[j->j_head]), (index - j->j_head) * sizeof(struct katcp_notice *));
      }
      j->j_queue[j->j_head] = NULL;
      j->j_head = (j->j_head + 1) % j->j_size;
      j->j_count--;
      return 0; /* WARNING: done here */
    }
  } else { /* if no wrapping, we can not be before head */
    if(index < j->j_head){
      return -1;
    }
  }

  /* now move back end by one, to overwrite position at index */
  end = j->j_head + j->j_count - 1; /* WARNING: relies on count+head never being zero, hence earlier test */
  if(index > end){
    return -1;
  }
  if(index < end){
    memcpy(&(j->j_queue[index]), &(j->j_queue[index + 1]), (end - index) * sizeof(struct katcp_notice *));
  } /* else index is end, no copy needed  */

  j->j_queue[end] = NULL;
  j->j_count--;

  return 0;
}

int unlink_queue_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *payload)
{
  struct katcp_job *j;
  unsigned int i, k;

  j = payload;

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

/* deallocate job, does not unlink itself from anywhere **********/

static void delete_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  if(j == NULL){
    return;
  }

  if(j->j_halt || (j->j_count > 0)){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to delete job %p which is noticed", j);
  }

  if(j->j_pid > 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to delete job %p with child %u", j, j->j_pid);
  }

  if(j->j_line){
    destroy_katcl(j->j_line, 1);
    j->j_line = NULL;
  }

  if(j->j_queue){
    free(j->j_queue);
    j->j_queue = NULL;
  }

  j->j_halt = NULL;

  free(j);
}

/* create, modify notices to point back at job ********************/

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, pid_t pid, int fd, struct katcp_notice *halt)
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
  j->j_pid = pid;
#if 0
  j->j_fd = fd;
#endif
  j->j_halt = halt;
  j->j_ended = 0;
  j->j_status = (-1);
  j->j_line = NULL;

  j->j_queue = NULL;
  j->j_head = 0;
  j->j_count = 0;

  if(fd >= 0){
    j->j_line = create_katcl(fd);
    if(j->j_line == NULL){
      free(j);
      return NULL;
    }
  }

  if(halt){
    halt->n_payload = j;
    halt->n_release = &unlink_halt_job_katcp;
  }

  s->s_tasks[s->s_number] = j;
  s->s_number++;

  return j;
}

struct katcp_job *via_notice_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  struct katcp_job *j;

  if(n->n_payload == NULL){
    return NULL;
  }

  j = n->n_payload;

  sane_job_katcp(j);

  return j;
}

/* manually stop a task, trigger notice as if stopped on its own **/

int stop_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result;

  result = 0;

  if(j->j_ended){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "job %p already ended", j);
    result = 1;
  }

  if(j->j_pid > 0){
    if(kill(j->j_pid, SIGTERM) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to kill process %u: %s", j->j_pid, strerror(errno));
      result = (-1);
    }
    j->j_status = (-1);
  }

  exchange_katcl(j->j_line, -1);

  return result;
}

static int field_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  int result, code;
  char *cmd, *module, *message, *priority;

  result = have_katcl(j->j_line);

  if(result <= 0){
    return result;
  }

  cmd = arg_string_katcl(j->j_line, 0);
  if(cmd == NULL){
    return -1;
  }

  switch(cmd[0]){

    case KATCP_REQUEST : 
      /* our logic is unable to service requests */
      extra_response_katcl(j->j_line, KATCP_RESULT_FAIL, NULL);
      break;

    case KATCP_REPLY   :
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

  return 0;
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

    i = 0;
    while(i < s->s_number){
      j = s->s_tasks[i];

      if(j->j_pid == pid){

        j->j_pid = 0;
        j->j_status = status;

        /* WARNING: risks flushing data before it has been processed */
        exchange_katcl(j->j_line, -1);

        if(j->j_ended == 0){
          if(j->j_halt){
            wake_notice_katcp(d, j->j_halt);
          }
          j->j_ended = 1;
        }

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
      FD_SET(fd, &(s->s_read));
      if(flushing_katcl(j->j_line)){
        FD_SET(fd, &(s->s_write));
      }
      if(fd > s->s_max){
        s->s_max = fd;
      }
    }
  }

  return 0;
}

int run_jobs_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  int i, count, fd;

  s = d->d_shared;

#ifdef DEBUG
  fprintf(stderr, "job: checking %d jobs\n", s->s_number);
#endif

  count = 0;
  i = 0;
  while(i < s->s_number){
    j = s->s_tasks[i];


    if(j->j_ended && (j->j_halt == NULL) && (j->j_count == 0)){ /* job ended and nobody interested in the status */
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "removing unnoticed and ended job %p", j);

      delete_job_katcp(d, j);

      s->s_number--;
      if(i < s->s_number){
        s->s_tasks[i] = s->s_tasks[s->s_number];
      }

    } else {
      fd = fileno_katcl(j->j_line);
      if((fd >= 0) && FD_ISSET(fd, &(s->s_read))){
        if(read_katcl(j->j_line)){
          /* WARNING: what about unprocessed data ? */
          exchange_katcl(j->j_line, -1);
          fd = (-1);
        }
      }

      if((fd >= 0) && (field_job_katcp(d, j) < 0)){
        exchange_katcl(j->j_line, -1);
        fd = (-1);
      }

      if((fd >= 0) && FD_ISSET(fd, &(s->s_write))){
        if(write_katcl(j->j_line) < 0){
          exchange_katcl(j->j_line, -1);
          fd = (-1);
        }
      }
      i++;
    }
  }

  return 0;
}

/* functions offered to users *************************************/

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

    j = create_job_katcp(d, pid, fds[1], halt);
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

struct katcp_job *wrapper_process_create_job_katcp(struct katcp_dispatch *d, char *file, char **argv, struct katcp_notice *halt)
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

    j = create_job_katcp(d, pid, fds[1], halt);
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

  //execvp(file, argv);
  execpy_do(file, argv);
  sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to run command %s (%s)", file, strerror(errno)); 

  destroy_katcl(xl, 0);

  exit(EX_OSERR);
  return NULL;
}

/* debug/diagnositic access to job logic **************************/

int job_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  struct katcp_notice *n;
  char *name, *watch, *cmd;
  char *vector[2];
  int i;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return KATCP_RESULT_FAIL;
  } else {
    if(!strcmp(name, "list")){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d jobs", s->s_number);
      for(i = 0; i < s->s_number; i++){
        j = s->s_tasks[i];
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "job %p is %s", j, j->j_ended ? "ended" : "up");
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
    } else {
      return KATCP_RESULT_FAIL;
    }
  }
}
