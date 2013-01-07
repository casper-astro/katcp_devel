#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>
#include <netc.h>

#include "tcpborphserver3.h"
#include "loadbof.h"
#include "tg.h"

#define MTU               1024*64

#define UPLOAD_LABEL      "upload"

#define UPLOAD_TIMEOUT    30 
#define UPLOAD_PORT       7146


void destroy_port_data_tbs(struct katcp_dispatch *d, struct tbs_port_data *pd)
{
  if (pd == NULL){
    return;
  }

  if (pd->t_fd > 0){
    close(pd->t_fd);
    pd->t_fd = (-1);
  }

  pd->t_port = 0;


  free(pd);
}

struct tbs_port_data *create_port_data_tbs(struct katcp_dispatch *d, char *file, int port, int program, unsigned int expected, unsigned int timeout)
{
  struct tbs_port_data *pd;
  char name[40], *ptr;

  pd = malloc(sizeof(struct tbs_port_data));

  if(port <= 0){
    pd->t_port = UPLOAD_PORT;
  } else {
    pd->t_port = port;
  }

  if(timeout <= 0){
    pd->t_timeout = UPLOAD_TIMEOUT;
  } else {
    pd->t_timeout = timeout;
  }

  pd->t_program = program;
  pd->t_expected = expected;

  pd->t_fd = (-1);

  if(file == NULL){
    sprintf(name,"/dev/shm/ubf-%d",getpid());
    ptr = name;
    pd->t_program = 1;
  } else {
    ptr = file;
  }

  pd->t_fd = open(ptr, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
  if (pd->t_fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open file: %s", strerror(errno));
    destroy_port_data_tbs(NULL, pd);
    return NULL;
  }

  if(file == NULL){
    if (unlink(ptr) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to remove temporary file: %s", strerror(errno));
      destroy_port_data_tbs(NULL, pd);
      return NULL;
    }
  }

  return pd;
}

int upload_tbs(struct katcl_line *l, void *data)
{ 
  struct tbs_port_data *pd;
  int lfd, nfd, rr, wr, have;
  unsigned char buf[MTU];
  unsigned int count;

  pd = data;

  if (pd == NULL){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "no state supplied to subordinate logic");
    return -1;
  }

  lfd = net_listen(NULL, pd->t_port, 0);
  if (lfd < 0){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "unable to bind port %d: %s", pd->t_port, strerror(errno));
    return -1;
  }

  signal(SIGALRM, SIG_DFL);
  alarm(pd->t_timeout);

  nfd = accept(lfd, NULL, 0);
  close(lfd);

  if (nfd < 0){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "accept on port %d failed: %s", pd->t_port, strerror(errno));
    return -1;
  }
  
  count = 0;

  for (;;){
    rr = read(nfd, buf, MTU);
    if (rr == 0){
      break;
    } else if (rr < 0){
      sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "read failed while receiving bof file: %s", strerror(errno));
      close(nfd);
      return -1;
    }

    have = 0;
    do {
      wr = write(pd->t_fd, buf + have, rr - have);
      switch(wr){

        case -1:
          switch(errno){
            case EAGAIN:
            case EINTR:
              break;
            default:
              sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "saving of bof file failed: %s", strerror(errno));
              close(nfd);
              return -1;
          }
          break;

        case 0:
          sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "unexpected zero write");
          close(nfd);
          return -1;

        default:
          have += wr;
#if 0
          sync_message_katcl(l, KATCP_LEVEL_DEBUG, NULL, "%s: wrote %d bytes to parent", __func__, wr);
#endif
          break;
      }
    } while(have < rr);

    count += rr;

#if 0
    sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "uploaded %d bytes", pd->t_rsize);
#endif

    alarm(UPLOAD_TIMEOUT);
  }

  close(nfd);

  sync_message_katcl(l, KATCP_LEVEL_INFO, UPLOAD_LABEL, "received bof file data of %u bytes", count);

  if(pd->t_expected > 0){
    if(pd->t_expected != count){
      sync_message_katcl(l, KATCP_LEVEL_ERROR, UPLOAD_LABEL, "expected %u bytes, received %u", pd->t_expected, count);
      return -1;
    }
  }

  alarm(0);

  return 0;
}

int upload_resume_tbs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *ptr;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "got something from job via notice %p", n);

  p = get_parse_notice_katcp(d, n);
  if(p){
    ptr = get_string_parse_katcl(p, 0);
    if(ptr){
      if(!strcmp(ptr, KATCP_RETURN_JOB)){
        ptr = get_string_parse_katcl(p, 1);
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "expected to see a return inform, got %s instead", ptr);
        ptr = NULL;
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "empty wakeup message");
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no message available on wakeup");
    ptr = NULL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, ptr ? ptr : KATCP_FAIL);

  resume_katcp(d);

  return 0;
}

int upload_complete_tbs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct tbs_port_data *pd;
  struct katcl_parse *p;
  char *inform, *status;
  int fd;
  struct bof_state *bs;

#if 0
  int fd, offset, wr, sr;
#endif

  pd = data;
  if (pd == NULL)
    return -1;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    destroy_port_data_tbs(d, pd);
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    destroy_port_data_tbs(d, pd);
    return 0;
  }

#ifdef DEBUG
  fprintf(stderr, "%s: got inform %s\n", __func__, inform);
#endif
  
  if(strcmp(inform, KATCP_RETURN_JOB) != 0){
    destroy_port_data_tbs(d, pd);
    return 0;
  }

  status = get_string_parse_katcl(p, 1);
  if(status == NULL){
    destroy_port_data_tbs(d, pd);
    return 0;
  }

  if(strcmp(status, KATCP_OK) != 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "encountered %s on upload", status);
    destroy_port_data_tbs(d, pd);
    return 0;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "transfer completed");
  
  if(pd->t_program){

    if (lseek(pd->t_fd, 0, SEEK_SET) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable lseek begining of file");
      destroy_port_data_tbs(d, pd);
      return 0;
    }
   
    if(stop_fpga_tbs(d) < 0){
      destroy_port_data_tbs(d, pd);
      return 0;
    }

    fd = dup(pd->t_fd);
    if(fd < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable dup file descriptor: %s", strerror(errno));
      destroy_port_data_tbs(d, pd);
      return 0;
    }

    bs = open_bof_fd(d, fd);
    if(bs == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to initialise programming logic");
      destroy_port_data_tbs(d, pd);
      return 0;
    }

    if(start_fpga_tbs(d, bs) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to program uploaded bof file");
      close_bof(d, bs);
      destroy_port_data_tbs(d, pd);
      return 0;
    }

    close_bof(d, bs);
  }

  destroy_port_data_tbs(d, pd);
  
  return 0;
}

#if 0
int upload_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_dispatch *dl;
  struct katcp_job *j;
  struct katcp_url *url;
  struct tbs_port_data *pd;
  char *rtn;

  dl = template_shared_katcp(d);

  pd = create_port_data_tbs();
  if (pd == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: couldn't create port data", __func__);
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr, "%s: with %d args\n", __func__, argc);
#endif
 
  switch (argc){

    case 1:
      pd->t_port = 7146;

    case 2:
      rtn = arg_string_katcp(d, 1);

      if (rtn != NULL)
        pd->t_port = atoi(rtn);

      url = create_exec_kurl_katcp("upload");
      if (url == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: could not create kurl", __func__);
        destroy_port_data_tbs(NULL, pd);
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(dl, url->u_str);
      if (j){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: found job for %s", __func__, url->u_str);
        destroy_kurl_katcp(url);
        destroy_port_data_tbs(NULL, pd);
        return KATCP_RESULT_FAIL;
      }

      j = run_child_process_tbs(dl, url, &upload_tbs, pd, NULL);
      if (j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: run child process returned null for %s", __func__ , url->u_str);
        destroy_kurl_katcp(url);
        destroy_port_data_tbs(NULL, pd);
        return KATCP_RESULT_FAIL;
      }
      
      if (match_inform_job_katcp(dl, j, UPLOAD_COMPLETE, &upload_complete_tbs, pd) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: match inform for job %s failed", __func__, url->u_str);
        zap_job_katcp(dl,j);
        destroy_port_data_tbs(NULL, pd);
        return KATCP_RESULT_FAIL;
      }
      break; 
  }
  
  extra_response_katcp(d, KATCP_RESULT_OK, "%d", pd->t_port);

  return KATCP_RESULT_OWN;
}
#endif

int uploadbof_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_dispatch *dl;
  struct katcp_job *j;
  struct katcp_url *url;
  struct tbs_port_data *pd;
  unsigned int port, timeout, expected;
  struct tbs_raw *tr;
  struct katcp_notice *nx;
  char *name, *buffer;
  int len, result;

  dl = template_shared_katcp(d);
  if(dl == NULL){
    return KATCP_RESULT_FAIL;
  }

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return KATCP_RESULT_FAIL;
  }
 
  expected = 0;
  timeout = 0;
  port = UPLOAD_PORT;

  if(argc < 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a port and file name to save data");
    return KATCP_RESULT_INVALID;
  }

  port = arg_unsigned_long_katcp(d, 1);
  if(port <= 1024){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unwilling to bind reserved port %d", port);
    return KATCP_RESULT_INVALID;
  }
  if(port > 0xffff){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "port %d too large to be valid", port);
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 2);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire first parameter");
    return KATCP_RESULT_FAIL;
  }

  if(argc > 3){
    expected = arg_unsigned_long_katcp(d, 3);
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "expected length is %u bytes", expected);
    if(argc > 4){
      timeout = arg_unsigned_long_katcp(d, 4);
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "user requested a timeout of %us", timeout);
    }
  }


  if(strchr(name, '/') || (name[0] == '.')){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "refusing to upload file containing path information");
    return KATCP_RESULT_FAIL;
  }

  len = strlen(name) + strlen(tr->r_bof_dir) + 1;
  buffer= malloc(len + 1);
  if(buffer == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len + 1);
    extra_response_katcp(d, KATCP_RESULT_FAIL, "allocation");
    return KATCP_RESULT_OWN;
  }

  result = snprintf(buffer, len + 1, "%s/%s", tr->r_bof_dir, name);
  if(result != len){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "major logic failure: expected %d from snprintf, got %d", len, result);
    extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    free(buffer);
    return KATCP_RESULT_OWN;
  }

  nx = find_notice_katcp(d, buffer);
  if(nx){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "another upload to %s already seems in progress, halting this attempt", name);
    free(buffer);
    return KATCP_RESULT_FAIL;
  }

  nx = create_notice_katcp(d, buffer, 0);
  if(nx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notification logic to trigger when upload completes");
    free(buffer);
    return KATCP_RESULT_FAIL;
  }

  pd = create_port_data_tbs(d, buffer, port, 0, expected, timeout);
  free(buffer);

  if (pd == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not initialise upload routines");
    return KATCP_RESULT_FAIL;
  }

  /* added in the global space dl, so that it completes even if client goes away */
  if(add_notice_katcp(dl, nx, &upload_complete_tbs, pd) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register callback for upload completion");
    return KATCP_RESULT_FAIL;
  }

  /* add to local connection d, to resume it */
  if(add_notice_katcp(d, nx, &upload_resume_tbs, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register callback to resume command");
    return KATCP_RESULT_FAIL;
  }

  url = create_exec_kurl_katcp("upload");
  if (url == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: could not create kurl", __func__);
    destroy_port_data_tbs(NULL, pd);
    return KATCP_RESULT_FAIL;
  }

  j = find_job_katcp(dl, url->u_str);
  if (j){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "found job for %s", url->u_str);
    destroy_kurl_katcp(url);
    destroy_port_data_tbs(NULL, pd);
    return KATCP_RESULT_FAIL;
  }

  j = run_child_process_tbs(dl, url, &upload_tbs, pd, nx);
  if (j == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to run child process");
    destroy_kurl_katcp(url);
    destroy_port_data_tbs(NULL, pd);
    return KATCP_RESULT_FAIL;
  }
      
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "awaiting transfer on port %d", pd->t_port);

  return KATCP_RESULT_PAUSE;
}

int upload_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_dispatch *dl;
  struct katcp_job *j;
  struct katcp_url *url;
  struct tbs_port_data *pd;
  unsigned int port, timeout, expected;
  struct tbs_raw *tr;
  struct katcp_notice *nx;

  dl = template_shared_katcp(d);
  if(dl == NULL){
    return KATCP_RESULT_FAIL;
  }

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return KATCP_RESULT_FAIL;
  }
 
  expected = 0;
  timeout = 0;
  port = UPLOAD_PORT;

  if(argc > 1){
    port = arg_unsigned_long_katcp(d, 1);
    if(port <= 1024){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unwilling to bind reserved port %d", port);
      return KATCP_RESULT_INVALID;
    }
    if(port > 0xffff){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "port %d too large to be valid", port);
      return KATCP_RESULT_INVALID;
    }
  }

  if(argc > 2){
    expected = arg_unsigned_long_katcp(d, 2);
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "expected length is %u bytes", expected);
    if(argc > 3){
      timeout = arg_unsigned_long_katcp(d, 3);
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "user requested a timeout of %us", timeout);
    }
  }

  nx = find_notice_katcp(d, TBS_FPGA_CONFIG);
  if(nx){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "another upload already seems in progress, halting this attempt");
    return KATCP_RESULT_FAIL;
  }

  nx = create_notice_katcp(d, TBS_FPGA_CONFIG, 0);
  if(nx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notification logic to trigger when upload completes");
    return KATCP_RESULT_FAIL;
  }

  pd = create_port_data_tbs(d, NULL, port, 1, expected, timeout);

  if (pd == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: couldn't create port data", __func__);
    return KATCP_RESULT_FAIL;
  }

  /* added in the global space dl, so that it completes even if client goes away */
  if(add_notice_katcp(dl, nx, &upload_complete_tbs, pd) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register callback for upload completion");
    return KATCP_RESULT_FAIL;
  }

  url = create_exec_kurl_katcp("upload");
  if (url == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: could not create kurl", __func__);
    destroy_port_data_tbs(NULL, pd);
    return KATCP_RESULT_FAIL;
  }

  j = find_job_katcp(dl, url->u_str);
  if (j){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "found job for %s", url->u_str);
    destroy_kurl_katcp(url);
    destroy_port_data_tbs(NULL, pd);
    return KATCP_RESULT_FAIL;
  }

  j = run_child_process_tbs(dl, url, &upload_tbs, pd, nx);
  if (j == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to run child process");
    destroy_kurl_katcp(url);
    destroy_port_data_tbs(NULL, pd);
    return KATCP_RESULT_FAIL;
  }
      
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "awaiting transfer on port %d", pd->t_port);

  return KATCP_RESULT_OK;
}
