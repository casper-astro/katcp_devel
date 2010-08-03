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

#define JOB_MAGIC 0x21525110

int unlink_halt_job_katcp(struct katcp_dispatch *d, void *payload)
{
  struct katcp_job *j;

  j = payload;

  j->j_halt = NULL;
  
  /* TODO: maybe initiate destruction of job */

  return 0;
}

int unlink_halt_job_katcp(struct katcp_dispatch *d, void *payload)
{
  struct katcp_job *j;

  j = payload;

  j->j_data = NULL;
  
  /* TODO: maybe initiate destruction of job */

  return 0;
}

void delete_job_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  if(j == NULL){
    return;
  }

  if(j->j_data || j->j_have){
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

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, pid_t pid, int fd, struct katcp_notice *halt, struct katcp_notice *data)
{
  struct katcp_job *j, **t;
  struct katcp_shared *s;

  s = d->d_shared;

  t = realloc(s->s_tasks, sizeof(struct katcp_job *) * (s->s_number + 1));
  if(t == NULL){
    return NULL;
  }

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
      result = (-1)
    }
  }

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

#if 0
  /* TODO: destroy job, unlink from notice */
#endif

  return result;
}

int collect_jobs_katcp(struct katcp_dispatch *d)
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

#if 0
        /* TODO: destroy job, unlink from notice */
        s->s_number--;
        if(i < s->s_number){
          s->s_tasks[i] = s->s_tasks[s->s_number];
        }
#endif

      } else {
        i++;
      }
    }
  }

  return 0;
}
