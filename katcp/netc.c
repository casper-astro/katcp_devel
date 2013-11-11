/* GPLed code taken from shore:lib/net-connect.c */

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
#include <netinet/tcp.h>

#include "netc.h"

int net_connect(char *name, int port, int flags)
{
  /* WARNING: this function may call resolvers, and blocks for those */
  /* WARNING: uses ipv4 API */

  int p, len, fd, se, option;
  char *ptr, *host;
  struct hostent *he;
  struct sockaddr_in sa;
#ifndef SOCK_NONBLOCK
  long opts;
#endif

  p = NETC_DEFAULT_PORT;

  ptr = strchr(name, ':');
  if(ptr){
    p = atoi(ptr + 1);
  }

  if(port){
    p = port;
  }

  if(p == 0){
    if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "connect: unable to acquire a port number\n");
    errno = EINVAL;
    return -2;
  }

  if((name[0] == '\0') || (name[0] == ':')){
#ifdef INADDR_LOOPBACK
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#else
    if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "connect: no destination address given\n");
    errno = EINVAL;
    return -2;
#endif
  } else {
    host = strdup(name);
    if(host == NULL){
      if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "connect: unable to duplicate string\n");
      errno = ENOMEM;
      return -1;
    }

    ptr = strchr(host, ':');
    if(ptr){
      ptr[0] = '\0';
    }

    if(inet_aton(host, &(sa.sin_addr)) == 0){
      he = gethostbyname(host);
      if((he == NULL) || (he->h_addrtype != AF_INET)){
        if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "connect: unable to map %s to ipv4 address\n", host);
        free(host);
        errno = EINVAL;
        return -1;
      }

      sa.sin_addr = *(struct in_addr *) he->h_addr;
    }

    free(host);
  }

  sa.sin_port = htons(p);
  sa.sin_family = AF_INET;

  if(flags & NETC_ASYNC){
#ifdef SOCK_NONBLOCK
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
#else
    fd = socket(AF_INET, SOCK_STREAM, 0);
    opts = fcntl(fd, F_GETFL, NULL);
    if(opts >= 0){
      opts = fcntl(fd, F_SETFL, opts | O_NONBLOCK);
    }
#endif
  } else {
    fd = socket(AF_INET, SOCK_STREAM, 0);
  }
  if(fd < 0){
    if(flags & NETC_VERBOSE_ERRORS){
      se = errno;
      fprintf(stderr, "connect: unable to allocate socket: %s\n", strerror(errno));
      errno = se;
    }
    return -1;
  }
   
  if(flags & NETC_VERBOSE_STATS){
    ptr = inet_ntoa(sa.sin_addr);
    fprintf(stderr, "connect: connecting to %s:%u\n", ptr, p);
  }
  
  if(flags & NETC_TCP_KEEP_ALIVE){
    option = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(option)) < 0){
      fprintf(stderr,"connect: cannot set keepalive socket option\n");
      return -1;
    }
#ifdef TCP_KEEPIDLE
    option = 10;
    if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &option, sizeof(option)) < 0){
      fprintf(stderr,"connect: cannot set keepalive socket option\n");
      return -1;
    }
#endif
#ifdef TCP_KEEPINTVL
    option = 10;
    if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &option, sizeof(option)) < 0){
      fprintf(stderr,"connect: cannot set keepalive socket option\n");
      return -1;
    }
#endif
#ifdef TCP_KEEPCNT
    option = 3;
    if (setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &option, sizeof(option)) < 0){
      fprintf(stderr,"connect: cannot set keepalive socket option\n");
      return -1;
    }
#endif
  }

  len = sizeof(struct sockaddr_in);

  if(connect(fd, (struct sockaddr *)(&sa), len)){
    if(flags & NETC_ASYNC){
      if(errno == EINPROGRESS){
        return fd;
      }
    }
    se = errno;
    close(fd);
    if(flags & NETC_VERBOSE_ERRORS){
      ptr = inet_ntoa(sa.sin_addr);
      fprintf(stderr, "connect: connect to %s:%u failed: %s\n", ptr, p, strerror(errno));
    }
    errno = se;
    return -1;
  }

  if(flags & NETC_VERBOSE_STATS){
    fprintf(stderr, "connect: established connection\n");
  }

  return fd;
}

int net_listen(char *name, int port, int flags)
{
  int p, len, fd, se;
  char *ptr, *host;
  struct hostent *he;
  struct sockaddr_in sa;
  int value;

  host = NULL;
  p = 0;

  if(name){
    host = strdup(name);
    if(host == NULL){
      if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "listen: unable to duplicate string\n");
      errno = ENOMEM;
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
    p = NETC_DEFAULT_PORT;
  }

  if(host){
    if(inet_aton(host, &(sa.sin_addr)) == 0){
      he = gethostbyname(host);
      if((he == NULL) || (he->h_addrtype != AF_INET)){
        if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "listen: unable to map %s to ipv4 address\n", host);
        free(host);
        errno = EINVAL;
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
    if(flags & NETC_VERBOSE_ERRORS){
      se = errno;
      fprintf(stderr, "listen: unable to allocate socket: %s\n", strerror(errno));
      errno = se;
    }
    return -1;
  }

  /* slightly risky behaviour in order to gain some convenience */
  value = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
   
#ifndef MSG_NOSIGNAL
#ifdef SO_NOSIGPIPE
  value = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));
#endif
#endif
   
  if(flags & NETC_VERBOSE_STATS){
    fprintf(stderr, "listen: about to bind %u\n", p);
  }

  len = sizeof(struct sockaddr_in);
  if(bind(fd, (struct sockaddr *)(&sa), len)){
    se = errno;
    close(fd);
    if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "listen: bind to %u failed: %s\n", p, strerror(errno));
    errno = se;
    return -1;
  }

  if(listen(fd, 3)){
    se = errno;
    close(fd);
    if(flags & NETC_VERBOSE_ERRORS) fprintf(stderr, "listen: unable to listen on port %u: %s\n", p, strerror(errno));
    errno = se;
    return -1;
  }

  if(flags & NETC_VERBOSE_STATS){
    fprintf(stderr, "listen: ready for connections\n");
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

  fd = net_connect(argv[1], (argc > 2) ? atoi(argv[2]) : 0, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
  if(fd < 0){
    fprintf(stderr, "%s: failed\n", argv[0]);
    return 1;
  }

  fprintf(stderr, "%s: ok\n", argv[0]);
  close(fd);

  return 0;
}

#endif
