#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "netc.h"

int net_connect(char *name, int port, int verbose)
{
  /* WARNING: this function may call resolvers, and blocks for those */
  /* WARNING: uses ipv4 API */

  int p, len, fd;
  char *ptr, *host;
  struct hostent *he;
  struct sockaddr_in sa;

  p = 23;

  ptr = strchr(name, ':');
  if(ptr){
    p = atoi(ptr + 1);
  }

  if(port){
    p = port;
  }

  if(p == 0){
    if(verbose) fprintf(stderr, "connect: unable to acquire a port number\n");
    return -2;
  }

  host = strdup(name);
  if(host == NULL){
    if(verbose) fprintf(stderr, "connect: unable to duplicate string\n");
    return -1;
  }

  ptr = strchr(host, ':');
  if(ptr){
    ptr[0] = '\0';
  }

  if(inet_aton(host, &(sa.sin_addr)) == 0){
    he = gethostbyname(host);
    if((he == NULL) || (he->h_addrtype != AF_INET)){
      if(verbose) fprintf(stderr, "connect: unable to map %s to ipv4 address\n", host);
      free(host);
      return -1;
    }

    sa.sin_addr = *(struct in_addr *) he->h_addr;
  }

  sa.sin_port = htons(p);
  sa.sin_family = AF_INET;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0){
    if(verbose) fprintf(stderr, "connect: unable to allocate socket: %s\n", strerror(errno));
    return -1;
  }
   
  if(verbose > 1){
    ptr = inet_ntoa(sa.sin_addr);
    if(verbose) fprintf(stderr, "connect: connecting to %s:%u\n", ptr, p);
  }

  len = sizeof(struct sockaddr_in);

  if(connect(fd, (struct sockaddr *)(&sa), len)){
    close(fd);
    if(verbose){
      ptr = inet_ntoa(sa.sin_addr);
      fprintf(stderr, "connect: connect to %s:%u failed: %s\n", ptr, p, strerror(errno));
    }
    return -1;
  }

  if(verbose > 1){
    if(verbose) fprintf(stderr, "connect: established connection\n");
  }

  return fd;
}

int net_listen(char *name, int port, int verbose)
{
  int p, len, fd;
  char *ptr, *host;
  struct hostent *he;
  struct sockaddr_in sa;
  int value;

  host = NULL;
  p = 0;

  if(name){
    host = strdup(name);
    if(host == NULL){
      if(verbose) fprintf(stderr, "listen: unable to duplicate string\n");
      return -1;
    }

    ptr = strchr(host, ':');
    if(ptr){
      ptr[0] = '\0';
      p = atoi(ptr + 1);
    } else {
      p = atoi(name);
      free(host);
      host = NULL;
    }
  }

  if(port){
    p = port;
  }

  if(p == 0){
    p = 23;
  }

  if(host){
    if(inet_aton(host, &(sa.sin_addr)) == 0){
      he = gethostbyname(host);
      if((he == NULL) || (he->h_addrtype != AF_INET)){
        if(verbose) fprintf(stderr, "listen: unable to map %s to ipv4 address\n", host);
        free(host);
        return -1;
      }
      sa.sin_addr = *(struct in_addr *) he->h_addr;
    }
    free(host);
  } else {
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
  }

  sa.sin_port = htons(p);
  sa.sin_family = AF_INET;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0){
    if(verbose) fprintf(stderr, "listen: unable to allocate socket: %s\n", strerror(errno));
    return -1;
  }

  /* slightly risky behaviour in order to gain some convenience */
  value = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
   
  if(verbose > 1){
    ptr = inet_ntoa(sa.sin_addr);
    if(verbose) fprintf(stderr, "listen: about to bind %u\n", p);
  }

  len = sizeof(struct sockaddr_in);
  if(bind(fd, (struct sockaddr *)(&sa), len)){
    close(fd);
    if(verbose) fprintf(stderr, "listen: bind to %u failed: %s\n", p, strerror(errno));
    return -1;
  }

  if(listen(fd, 3)){
    close(fd);
    if(verbose) fprintf(stderr, "listen: unable to listen on port %u: %s\n", p, strerror(errno));
    return -1;
  }

  if(verbose > 1){
    if(verbose) fprintf(stderr, "listen: ready for connections\n");
  }

  return fd;
}

#ifdef UNIT_TEST_NETC

int main(int argc, char **argv)
{
  int fd;

  fprintf(stderr, "netc.c test\n");

  if(argc < 2){
    fprintf(stderr, "usage: %s host:port\n", argv[0]);
    return 1;
  }

  fd = net_connect(argv[1], (argc > 2) ? atoi(argv[2]) : 0, 1);
  if(fd < 0){
    fprintf(stderr, "%s: failed\n", argv[0]);
    return 1;
  }

  fprintf(stderr, "%s: ok\n", argv[0]);
  close(fd);

  return 0;
}

#endif
