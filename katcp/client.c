#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "katpriv.h"
#include "katcl.h"
#include "katcp.h"
#include "netc.h"

int run_client_katcp(struct katcp_dispatch *d, char *host, int port)
{
  int fd, result;
  fd_set fsr, fsw;

  fd = net_connect(host, port, 0);
  if(fd < 0){
    return terminate_katcp(d, KATCP_EXIT_ABORT);
  }

  reset_katcp(d, fd);
  name_katcp(d, "%s:%d", host, port);

  while(exited_katcp(d) == KATCP_EXIT_NOTYET){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    FD_SET(fd, &fsr);
    if(flushing_katcp(d)){ /* only write data if we have some */
#ifdef DEBUG
      fprintf(stderr, "client: want to flush data\n");
#endif
      FD_SET(fd, &fsw);
    }

    result = select(fd + 1, &fsr, &fsw, NULL, NULL);
    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            return terminate_katcp(d, KATCP_EXIT_ABORT);
        }
        break;
#if 0
      case  0 :
        return 0;
#endif
    }

    if(FD_ISSET(fd, &fsr)){
      result = read_katcp(d);
      if(result){
        if(result < 0){
          return terminate_katcp(d, KATCP_EXIT_ABORT);
        } else {
          terminate_katcp(d, KATCP_EXIT_HALT);
        }
      }
    }

    result = dispatch_katcp(d);
    if(result < 0){
      return terminate_katcp(d, KATCP_EXIT_ABORT);
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcp(d);
      if(result < 0){
        return terminate_katcp(d, KATCP_EXIT_ABORT);
      }
    }
  }

  return 0;
}

