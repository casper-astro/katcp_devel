#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

#define   MTU     9000
#define   PORT    10011    

int main(int argc, char *argv[])
{
  struct sockaddr_in sar, peer;
  unsigned char buffer[MTU];
  int total, fd, run, rb, i, rtn;
  socklen_t len;
  fd_set ins;

  sar.sin_family = AF_INET;
  sar.sin_port = htons(PORT);
  sar.sin_addr.s_addr = htonl(INADDR_ANY);

  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0){
    fprintf(stderr, "udp: socket err: %s\n", strerror(errno));
    return -1;
  }

  if (bind(fd, (struct sockaddr *) &sar, sizeof(struct sockaddr_in)) < 0) {
    fprintf(stderr, "udp: bind err: %s\n", strerror(errno));
    return -1;
  }

  len = sizeof(struct sockaddr_in);
  total = 0;

  for (run = 1; run > 0; ){
    FD_ZERO(&ins);
    FD_SET(fd, &ins);

    rtn = select(fd + 1, &ins, NULL, NULL, NULL);
    if (rtn < 0){
      run = 0;
    } else {
      
      if (FD_ISSET(fd, &ins)){
        bzero(buffer, MTU);

        rb = recvfrom(fd, buffer, MTU, 0, (struct sockaddr *) &peer, &len);
        if (rb == 0 /*|| rb == 24*/){ /*this is bad since recv might read 24bytes in a random case and then exit*/
          fprintf(stderr, "udp: read EOF shutdown\n");
          run = 0;
        } else if (rb < 0){
          fprintf(stderr, "upd: error %s\n", strerror(errno));
        } else {
          
          fprintf(stderr, "udp: got data %d bytes\n", rb);
          for (i=0; i<rb; i++){
            fprintf(stderr, "%X ", buffer[i]);
          }
          fprintf(stderr,"\n");
        }
        total += rb;
        fprintf(stderr, "Current received total: %d bytes\n", total);
      }
    }
  }

  return 0;
}
