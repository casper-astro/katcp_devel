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
#define UPLOADCOMPLETE    "#upload-complete"
#define UPLOADFAIL        "#upload-fail"
#define UPLOAD_TIMEOUT    30 

void destroy_port_data_tbs(struct tbs_port_data *pd)
{
  if (pd){

    if (pd->t_fd > 0)
      close(pd->t_fd);

#if 0
    if (pd->t_data != NULL || pd->t_data != MAP_FAILED)
      munmap(pd->t_data, pd->t_rsize);
#endif

    munmap(pd, sizeof(struct tbs_port_data));
  }
}

struct tbs_port_data *create_port_data_tbs()
{
  struct tbs_port_data *pd;
  char name[40];
  void *ptr;

  ptr = mmap(NULL, sizeof(struct tbs_port_data), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED)
    return NULL;
  
  pd = (struct tbs_port_data*) ptr;

  pd->t_port = 7146;
  
  sprintf(name,"/dev/shm/ubf-%d",getpid());

  pd->t_fd = open(name, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
  if (pd->t_fd < 0){
#ifdef DEBUG
    fprintf(stderr, "%s: open error: %s\n", __func__, strerror(errno));
#endif
    destroy_port_data_tbs(pd);
    return NULL;
  }

  if (unlink(name) < 0){
#ifdef DEBUG
    fprintf(stderr, "%s: unlink error: %s\n", __func__, strerror(errno));
#endif
    destroy_port_data_tbs(pd);
    return NULL;
  }

  pd->t_data = NULL;
  pd->t_rsize = 0;
  
  return pd;
}

int upload_tbs(struct katcl_line *l, void *data)
{ 
  struct tbs_port_data *pd;
  int run, lfd, nfd, rr, wr, have;
  unsigned char buf[MTU];

  if (data == NULL)
    return -1;

  pd = data;
  if (pd == NULL)
    return -1;

  lfd = net_listen(NULL, pd->t_port, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
  if (lfd < 0){
    return -1;
  }

  signal(SIGALRM, SIG_DFL);
  alarm(UPLOAD_TIMEOUT);

  nfd = accept(lfd, NULL, 0);
  close(lfd);

  if (nfd < 0){
    return -1;
  }
  
  for (run = 1; run > 0; ){

    rr = read(nfd, buf, MTU);

    if (rr == 0){
      run = rr;
      break;
    } else if (rr < 0){
      sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "%s: read failed while receiving bof file: %s", __func__, strerror(errno));
      append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, UPLOADFAIL);
      while(write_katcl(l) == 0);
      close(nfd);
      return -1;
    }

    have = 0;
    do {
      wr = write(pd->t_fd, buf+have, rr-have);
      switch(wr){
        case -1:
          switch(errno){
            case EAGAIN:
            case EINTR:
              break;
            default:
              sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "%s: WRITE FAILED %s", __func__, strerror(errno));
              append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, UPLOADFAIL);
              while(write_katcl(l) == 0);
              close(nfd);
              return-1;
          }
        case 0:
          sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "%s: WRITE FAILED %s", __func__, strerror(errno));
          append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, UPLOADFAIL);
          while(write_katcl(l) == 0);
          close(nfd);
          return-1;
        default:
          have += wr;
          sync_message_katcl(l, KATCP_LEVEL_DEBUG, NULL, "%s: wrote %d bytes to parent", __func__, wr);
          break;
      }
    } while(have < rr);

    pd->t_rsize += rr;

    sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "uploaded %d bytes", pd->t_rsize);
    alarm(UPLOAD_TIMEOUT);
  }
  
  append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, UPLOADCOMPLETE);
  while(write_katcl(l) == 0);
  close(nfd);

  alarm(0);

  return 0;
}

int upload_complete_tbs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct tbs_port_data *pd;
  struct katcl_parse *p;
  char *inform;

  struct bof_state *bs;

#if 0
  int fd, offset, wr, sr;
#endif

  pd = data;
  if (pd == NULL)
    return -1;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    destroy_port_data_tbs(pd);
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    destroy_port_data_tbs(pd);
    return 0;
  }

#ifdef DEBUG
  fprintf(stderr, "%s: got inform %s\n", __func__, inform);
#endif
  
  if(strcmp(inform, UPLOADCOMPLETE) != 0){
    destroy_port_data_tbs(pd);
    return 0;
  }
  
#if 0
#ifdef DEBUG
  fprintf(stderr, "%s: about to map fd [%d] %d bytes\n", __func__, pd->t_fd, pd->t_rsize);
#endif
  pd->t_data = mmap(pd->t_data, pd->t_rsize, PROT_READ, MAP_FILE | MAP_SHARED, pd->t_fd, 0);
  if (pd->t_data == MAP_FAILED){
    destroy_port_data_tbs(pd);
    return -1;
  }
#endif

#ifdef DEBUG
  fprintf(stderr, "%s: GOT A BOF data mapped @(%p) data received %d\n", __func__, pd->t_data, pd->t_rsize);
#endif
  
#if 1

  if (lseek(pd->t_fd, 0, SEEK_SET) < 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable lseek begining of file");
    destroy_port_data_tbs(pd);
    return KATCP_RESULT_FAIL;
  }

/*==========================================*/
 
  if(stop_fpga_tbs(d) < 0){
    destroy_port_data_tbs(pd);
    return KATCP_RESULT_FAIL;
  }

  bs = open_bof_fd(d, dup(pd->t_fd));
  if(bs == NULL){
    destroy_port_data_tbs(pd);
    return KATCP_RESULT_FAIL;
  }

  if(start_fpga_tbs(d, bs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to program uploaded bof file");
    close_bof(d, bs);
    destroy_port_data_tbs(pd);
    return KATCP_RESULT_FAIL;
  }

  close_bof(d, bs);

/*==========================================*/
#endif

#if 0
  fd = creat("upload-output", S_IRUSR | S_IWUSR);
  if (fd < 0){
    destroy_port_data_tbs(pd);
    return 0;
  }

  offset = 0;
  wr = 0;
  do {
    offset += wr;

    if((offset + MTU) > pd->t_rsize){
      if(offset > pd->t_rsize){
        sr = 0;
      } else {
        sr = pd->t_rsize - offset;
      }
    } else {
      sr = MTU;
    }

  } while((sr > 0) && (wr = write(fd, pd->t_data+offset, sr)) > 0);
  
  close(fd);
#endif

  destroy_port_data_tbs(pd);
  
  return 0;
}


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
        destroy_port_data_tbs(pd);
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(dl, url->u_str);
      if (j){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: found job for %s", __func__, url->u_str);
        destroy_kurl_katcp(url);
        destroy_port_data_tbs(pd);
        return KATCP_RESULT_FAIL;
      }

      j = run_child_process_tbs(dl, url, &upload_tbs, pd, NULL);
      if (j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: run child process returned null for %s", __func__ , url->u_str);
        destroy_kurl_katcp(url);
        destroy_port_data_tbs(pd);
        return KATCP_RESULT_FAIL;
      }
      
      if (match_inform_job_katcp(dl, j, UPLOADCOMPLETE, &upload_complete_tbs, pd) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: match inform for job %s failed", __func__, url->u_str);
        zap_job_katcp(dl,j);
        destroy_port_data_tbs(pd);
        return KATCP_RESULT_FAIL;
      }
      break; 
  }
  
  extra_response_katcp(d, KATCP_RESULT_OK, "%d", pd->t_port);

  return KATCP_RESULT_OWN;
}
