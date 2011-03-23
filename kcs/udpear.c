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

#include <arpa/inet.h>
#include <netinet/in.h>

#include <katcl.h>
#include <katcp.h>

#define MTU 1500

/*call with &someint*/
int udp_ear_kcs(struct katcl_line *l, void *data)
{
  struct sockaddr_in ear;
  int mfd, run, fd, lfd, rb, rtn;
  int *lport;
  char buffer[MTU];
  fd_set ins;
  fd_set outs;

  if (data == NULL)
    return -1;
  
  lport = data;
  if (lport  <= 0)
    return -1;

  ear.sin_family = AF_INET;
  ear.sin_port = *lport;
  ear.sin_addr.s_addr = htonl(INADDR_ANY);

  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: socket error: %s", strerror(errno));
    return -1;
  }
  if (bind(fd, (struct sockaddr *) &ear, sizeof(struct sockaddr_in)) < 0) {
    sync_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: bind error: %s", strerror(errno));
    return -1;
  }

  lfd = fileno_katcl(l);
  mfd = ((lfd > fd) ? lfd : fd) + 1;

  for (run = 1; run > 0;) {

    FD_ZERO(&ins);
    FD_ZERO(&outs);
    FD_SET(lfd, &ins);
    FD_SET(fd, &ins);
    if (flushing_katcl(l))
      FD_SET(lfd, &outs);

    rtn = select(mfd, &ins, &outs, NULL, NULL);
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
      if (FD_ISSET(lfd, &outs)){
        write_katcl(l);
      }
      if (FD_ISSET(fd,&ins)){
        rb = recv(fd, buffer, MTU, 0); 
        if (rb <= 0){
          log_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: recv error: %s", strerror(errno));
          run = 0;
          break;
        }
        log_message_katcl(l, KATCP_LEVEL_INFO, NULL, "udp ear: recv %d bytes: %s", rb, buffer);
      }
      if (FD_ISSET(lfd,&ins)){
        rb = read_katcl(l);
        if (rb) {
          run = 0;
          break;
        }
       /* while(have_katcl(l)){
          TODO: if this module is to speak katcp from parent job impl here
        }*/
      }
    }
  }

  return 0;
}

int udpear_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_job *j;
  struct katcp_url *url;
  switch (argc){
    case 2:
   //   url = create
   //   j = find_job_katcp(d,

        
  }
  return KATCP_RESULT_FAIL;
}

