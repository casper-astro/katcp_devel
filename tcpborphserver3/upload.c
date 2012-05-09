
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

#define MTU               1048576
#define DSIZE             10485760 /*10MBytes*/
#define UPLOADCOMPLETE    "#upload-complete"
#define UPLOADFAIL        "#upload-fail"


struct tbs_port_data *create_port_data_tbs()
{
  struct tbs_port_data *pd;
  void *ptr;

  //pd = malloc(sizeof(struct tbs_port_data));
  ptr = mmap(NULL, sizeof(struct tbs_port_data) + DSIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
  if (ptr == MAP_FAILED)
    return NULL;
  
  pd = (struct tbs_port_data*) ptr;

  pd->t_port = 7146;

  pd->t_data = ptr + sizeof(struct tbs_port_data);

  pd->t_dsize = DSIZE;
  pd->t_rsize = 0;
  
  return pd;
}

void destroy_port_data_tbs(struct tbs_port_data *pd)
{
  if (pd){
    munmap(pd, pd->t_dsize + sizeof(struct tbs_port_data));
  }
}

int upload_tbs(struct katcl_line *l, void *data)
{ 
  struct tbs_port_data *pd;
  int run, lfd, nfd, rr, ndsize;

  if (data == NULL)
    return -1;

  pd = data;
  if (pd == NULL)
    return -1;

  lfd = net_listen(NULL, pd->t_port, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
  if (lfd < 0){
    
    return -1;
  }

  nfd = accept(lfd, NULL, 0);
  close(lfd);

  if (nfd < 0){
    
    return -1;
  }

  for (run = 1; run > 0; ){

    if (pd->t_rsize + MTU > pd->t_dsize) {
      /*resize data by DSIZE*/
      ndsize = pd->t_dsize + ((pd->t_dsize / DSIZE) + 1) * DSIZE;

      pd = mremap(pd, pd->t_dsize, ndsize + sizeof(struct tbs_port_data), 0);
      if (pd == MAP_FAILED){
        append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, UPLOADFAIL);
        while(write_katcl(l) == 0);
        close(nfd);
        return -1;
      }
      
      pd->t_dsize = ndsize;

      sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "%s: resized pd to %d", __func__, pd->t_dsize);
    }

    rr = read(nfd, pd->t_data, MTU);

    if (rr <= 0){
      run = rr;
      break;
    }

    pd->t_rsize += rr;
    
    sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "%s: received %d bytes total %d", __func__, rr, pd->t_rsize);

  }

  append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, UPLOADCOMPLETE);
  while(write_katcl(l) == 0);
  close(nfd);

  return 0;
}

int upload_complete_tbs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct tbs_port_data *pd;
  struct katcl_parse *p;
  char *inform;

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


#ifdef DEBUG
  fprintf(stderr, "%s: GOT A BOF data captured @(%p) data size allocated %d data received %d\n", __func__, pd->t_data, pd->t_dsize, pd->t_rsize);
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
  if (pd == NULL)
    return KATCP_RESULT_FAIL;

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
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(dl, url->u_str);
      if (j){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: found job for %s", __func__, url->u_str);
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }
      
      j = run_child_process_tbs(dl, url, &upload_tbs, pd, NULL);
      if (j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: run child process returned null for %s", __func__ , url->u_str);
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }
      
      if (match_inform_job_katcp(dl, j, "#upload-complete", &upload_complete_tbs, pd) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s: run child process returned null for %s", __func__, url->u_str);
        zap_job_katcp(dl,j);
        return KATCP_RESULT_FAIL;
      }

      break; 
  }
  
  extra_response_katcp(d, KATCP_RESULT_OK, "%d", pd->t_port);

  return KATCP_RESULT_OWN;
}
