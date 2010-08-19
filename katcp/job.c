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

int unlink_halt_job_katcp(struct katcp_dispatch *d, void *payload)
{
  struct katcp_job *j;

  j = payload;

  j->j_halt = NULL;
  
  /* TODO: maybe initiate destruction of job */

  return 0;
}

int unlink_data_job_katcp(struct katcp_dispatch *d, void *payload)
{
  struct katcp_job *j;

  j = payload;

  j->j_data = NULL;
  
  /* TODO: maybe initiate destruction of job */

  return 0;
}

/* deallocate notice, do not unlink itself from anywhere **********/

static void delete_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  if(j == NULL){
    return;
  }

  if(j->j_data || j->j_halt){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to delete job %p which is noticed", j);
  }

  if(j->j_pid > 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "attempting to delete job %p with child %u", j, j->j_pid);
  }

  if(j->j_fd > 0){
    close(j->j_fd);
    j->j_fd = (-1);
  }

  free(j);
}

/* create, modify notices to point back at job ********************/

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, pid_t pid, int fd, struct katcp_notice *halt, struct katcp_notice *data)
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
  j->j_fd = fd;
  j->j_halt = halt;
  j->j_data = data;
  j->j_ended = 0;
  j->j_status = (-1);

  if(halt){
    halt->n_payload = j;
    halt->n_release = &unlink_halt_job_katcp;
  }

  if(data){
    data->n_payload = j;
    data->n_release = &unlink_data_job_katcp;
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

  if(j->j_fd >= 0){
    close(j->j_fd);
    j->j_fd = (-1);
  }

  return result;
}

/* wait on child exit status **************************************/

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

        if(j->j_fd >= 0){
          close(j->j_fd);
          j->j_fd = (-1);
        }

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
  int i;

  s = d->d_shared;

  for(i = 0; i < s->s_number; i++){
    j = s->s_tasks[i];
    if(j->j_fd >= 0){

#if 0
      /* TODO: load up the file descriptors */
      FD_SET(j->j_fd, &(s->s_read));
      if(j->j_fd > s->s_max){
        s->s_max = j->j_fd;
      }
#endif
    }
  }

  return 0;
}

int run_jobs_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_job *j;
  int i, count;

  s = d->d_shared;

#ifdef DEBUG
  fprintf(stderr, "job: checking %d jobs\n", s->s_number);
#endif

  count = 0;
  i = 0;
  while(i < s->s_number){
    j = s->s_tasks[i];


    if(j->j_ended && (j->j_halt == NULL) && (j->j_data == NULL)){ /* job ended and nobody interested in the status */
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "removing unnoticed and ended job %p", j);

      delete_job_katcp(d, j);

      s->s_number--;
      if(i < s->s_number){
        s->s_tasks[i] = s->s_tasks[s->s_number];
      }

    } else {
      if(j->j_fd >= 0){
        /* TODO: check read fd, write fd, etc */
        if(FD_ISSET(j->j_fd, &(s->s_read))){
        }
      }
      i++;
    }
  }

  return 0;
}

struct katcp_job *process_create_job_katcp(struct katcp_dispatch *d, char *file, char **argv, struct katcp_notice *halt, struct katcp_notice *data)
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

    j = create_job_katcp(d, pid, fds[1], halt, data);
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

      j = process_create_job_katcp(d, cmd, vector, n, NULL);

      if(j == NULL){
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;
    } else {
      return KATCP_RESULT_FAIL;
    }
  }
}
