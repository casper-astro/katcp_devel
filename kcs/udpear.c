#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/utsname.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <katcl.h>
#include <katcp.h>
#include <katpriv.h>

#include "kcs.h"

#define MTU 1500

/*call with &someint*/
int udp_ear_kcs(struct katcl_line *l, void *data)
{
  struct katcl_parse *p;

  struct sockaddr_in ear;
  int mfd, run, fd, lfd, rb, rtn;
  int *lport;
  unsigned char buffer[MTU];
  fd_set ins;
  fd_set outs;

  if (data == NULL)
    return -1;
  
  lport = data;
  if (lport  <= 0)
    return -1;

#ifdef DEBUG
  fprintf(stderr,"udp ear: about to try port: %d\n",*lport);
#endif

  ear.sin_family = AF_INET;
  ear.sin_port = htons(*lport);
  ear.sin_addr.s_addr = htonl(INADDR_ANY);

  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0){
#ifdef DEBUG
    fprintf(stderr,"udp ear: socket error: %s\n",strerror(errno));
#endif
    sync_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: socket error: %s", strerror(errno));
    return -1;
  }
  if (bind(fd, (struct sockaddr *) &ear, sizeof(struct sockaddr_in)) < 0) {
#ifdef DEBUG
    fprintf(stderr,"udp ear: bind error: %s\n",strerror(errno));
#endif
    sync_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: bind error: %s", strerror(errno));
    return -1;
  }

  lfd = fileno_katcl(l);
  mfd = ((lfd > fd) ? lfd : fd) + 1;

#ifdef DEBUG
  fprintf(stderr,"udp ear: about to run with socket on fd: %d\n",fd);
#endif

  FD_ZERO(&outs);
  for (run = 1; run > 0;) {

    FD_ZERO(&ins);
//    FD_ZERO(&outs);
    FD_SET(lfd, &ins);
    FD_SET(fd, &ins);
    if (flushing_katcl(l))
      FD_SET(lfd, &outs);

    rtn = select(mfd, &ins, &outs, NULL, NULL);
    //rtn = select(mfd, &ins, NULL, NULL, NULL);
    if (rtn < 0){
      switch (errno){
        case EAGAIN :
        case EINTR  :
          break;
        default :
          run =0;
          break;
      }
    } else {
#ifdef DEBUG
      fprintf(stderr,"udp ear: got select %d\n",rtn);
#endif
      if (FD_ISSET(fd,&ins)){
        bzero(buffer, MTU);
        rb = recv(fd, buffer, MTU, 0); 
        if (rb <= 0){
          log_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: recv error: %s", strerror(errno));
          run = 0;
          break;
        }
#ifdef DEBUG
        fprintf(stderr,"udp ear: fd %d read: %d bytes %s\n", fd, rb, buffer);
#endif
        //log_message_katcl(l, KATCP_LEVEL_INFO, NULL, "udp ear: recv %d bytes: %s", rb, buffer);
        p = create_referenced_parse_katcl();
        if (p) {
          add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#roach");
          add_string_parse_katcl(p,                    KATCP_FLAG_STRING, "add");
          add_string_parse_katcl(p,                    KATCP_FLAG_STRING, buffer);
          add_string_parse_katcl(p, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, "spare");
          if (append_parse_katcl(l, p) < 0){
            log_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: unable to append parse to line recv %d bytes: %s", rb, buffer);
          }
          destroy_parse_katcl(p);
        } else {
          log_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: unable to create parse structure log insted recv %d bytes: %s", rb, buffer);
        }
      }
      if (FD_ISSET(lfd,&ins)){
        rb = read_katcl(l);
        if (rb) {
#ifdef DEBUG
          fprintf(stderr,"udp ear: fd %d read EOF\n",lfd);
#endif
          fflush(stderr);
          run = 0;
          break;
        }
       /* while(have_katcl(l)){
          TODO: if this module is to speak katcp from parent job impl here
        }*/
      }
      if (FD_ISSET(lfd, &outs)){
#ifdef DEBUG
        fprintf(stderr,"udp ear: need to write katcl %d\n",rtn);
#endif
        write_katcl(l);
        FD_CLR(lfd, &outs);
      }
    }
  }

  return 0;
}

int handle_roach_via_udp_ear_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  int i, argc;

#ifdef DEBUG
  fprintf(stderr, "udpear: in handle_roach_via_udp_ear");
  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "udpear: in handle_roach_via_udp_ear");
#endif
  
  p = get_parse_notice_katcp(d, n);
  if (p) {
    argc = get_count_parse_katcl(p);
    for (i=0; i<argc; i++){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "udpear[%d]: %s", i, get_string_parse_katcl(p, i));
    }
  }
  
  return 1; 
}
  
int udpear_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_dispatch *dl;
  struct utsname utn;
  struct katcp_job *j;
  struct katcp_url *url;
  char *rtn;
  int port;

  dl = template_shared_katcp(d);

  if (uname(&utn) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "udpear: unable to uname");
    return KATCP_RESULT_FAIL;
  }
  
  switch (argc){
    case 1:
      port = 7147;
    case 2:
      rtn = arg_string_katcp(d,1);
      if (rtn != NULL)
        port = atoi(rtn);
      url = create_kurl_katcp("katcp",utn.nodename,port,"?udpear");
      if (url == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "udpear: could not create kurl");
        return KATCP_RESULT_FAIL;
      }

      j = find_job_katcp(dl,url->u_str);
      if (j){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "udpear: found job for %s",url->u_str);
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }
      
      j = run_child_process_kcs(dl, url, &udp_ear_kcs, &port, NULL);
      if (j == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "udpear: run child process returned null for %s",url->u_str);
        destroy_kurl_katcp(url);
        return KATCP_RESULT_FAIL;
      }
      
      if (match_inform_job_katcp(dl, j, "#roach", &handle_roach_via_udp_ear_kcs, NULL) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "udpear: run child process returned null for %s",url->u_str);
        zap_job_katcp(dl,j);
        return KATCP_RESULT_FAIL;
      }

      break; 
  }
  return KATCP_RESULT_OK;
}

