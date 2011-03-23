#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <katcl.h>

#define MTU 1500

/*call with &someint*/
int udp_ear_kcs(struct katcl_line *l, void *data)
{
  struct sockaddr_in ear;
  struct sigaction sag;
  int run, lport, fd, rb;
  char buffer[MTU];

  if (data == NULL)
    return -1;
  
  lport = *data;
  if (lport  <= 0)
    return -1;

  ear.sin_family = AF_INET;
  ear.sin_port = lport;
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
 
  for (run = 1; run > 0;) {
    
    rb = recv(fd, buffer, MTU, 0); 
    if (rb <= 0){
      sync_message_katcl(l, KATCP_LEVEL_ERROR, NULL, "udp ear: recv error: %s", strerror(errno));
      run = 0;
      break;
    }
    
    sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "udp ear: recv %d bytes: %s", rb, buffer);
    
  }


  return 0;
}
