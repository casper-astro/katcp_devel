/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>


int main(int argc, char *argv[]){
  
  int fd,rtn, socerr,socerrlen;
  struct sockaddr_in sa;
  fd_set wset;
  struct timeval tv;

  tv.tv_sec  = 0;
  tv.tv_usec = 1;
  socerrlen = sizeof(int);
  
  sa.sin_port   = htons(80);
  sa.sin_family = AF_INET;
  if (inet_aton("157.166.226.26",&(sa.sin_addr)) == 0){
    fprintf(stderr,"cannot connect to host\n");
    return -1;
  }

  fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK,0);
  
  FD_ZERO(&wset);
  FD_SET(fd,&wset);

  rtn = connect(fd,(struct sockaddr *)&sa,sizeof(struct sockaddr_in));
  if (rtn == 0){
    fprintf(stderr,"connect returned imidiately\n");
  } else if (rtn < 0){
    fprintf(stderr,"there was a connect error: %s\n",strerror(errno));
    if (errno == EINPROGRESS){
      for (;;){
        rtn = select(fd+1,NULL,&wset,NULL,&tv);
        if (rtn == 0){
          fprintf(stderr,".");
          FD_ZERO(&wset);
          FD_SET(fd,&wset);
        } else if (rtn < 0){
          fprintf(stderr,"select error: %s\n",strerror(errno));
          return 0;
        } else if (rtn > 0) {
          fprintf(stderr,"socket in write set is writable\n");
          rtn = getsockopt(fd, SOL_SOCKET, SO_ERROR, &socerr, &socerrlen);
          fprintf(stderr,"getsockopt rtn: %d\n",rtn);
          if (socerr > 0){
            fprintf(stderr,"error connecting: %s\n",strerror(socerr));
            return -1;
          } else {
            fprintf(stderr,"CONNECTED %s\n",strerror(socerr));
            break;
          }
        }
      }
    }
  }
  
  close(fd);
  fprintf(stderr,"Exiting program\n");
  return 0;
}
