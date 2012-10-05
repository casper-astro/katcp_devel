/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include <sys/types.h>

#include "katcl.h"
#include "katpriv.h"
#include "katcp.h"
#include "netc.h"

struct katcl_line *create_name_rpc_katcl(char *name)
{
  return create_extended_rpc_katcl(name, 0);
}

struct katcl_line *create_extended_rpc_katcl(char *name, int flags)
{
  int fd;
  struct katcl_line *l;

  if(name){
    fd = net_connect(name, 0, flags);
    if(fd < 0){
#if 0
      fprintf(stderr, "connect: unable to contact %s: %s\n", name, strerror(errno));
#endif
      return NULL;
    }
  } else {
    fd = dup(STDIN_FILENO);
    if(fd < 0){
      return NULL;
    }
  }

  l = create_katcl(fd);
  if(l == NULL){
    close(fd);
    return NULL;
  }

  return l;
}

void destroy_rpc_katcl(struct katcl_line *l)
{
  /* can close things regardless, we dup STDIN in the create case */
  destroy_katcl(l, 1);
}

int await_reply_rpc_katcl(struct katcl_line *l, unsigned int timeout)
{
  int result; 
  struct timeval now, until, delta;
  char *ptr;

  delta.tv_sec = timeout / 1000;
  delta.tv_usec = (timeout % 1000) * 1000;

  gettimeofday(&now, NULL);
  add_time_katcp(&until, &now, &delta);

  while((result = complete_rpc_katcl(l, 0, &until)) == 0);

  if(result < 0){
    /* TODO: end connection, request/replies potentially out of sync */
    return -1;
  }

  ptr = arg_string_katcl(l, 1);
  if(ptr == NULL){
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
    return 1;
  }

  return 0;
}

int complete_rpc_katcl(struct katcl_line *l, unsigned int flags, struct timeval *until)
{
  fd_set fsr, fsw;
  struct timeval tv, now;
  int result;
  int fd;

  fd = fileno_katcl(l);

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  for(;;){

    if(have_katcl(l) > 0){
      if(arg_reply_katcl(l)){
        return 1;
      }

      return 0;
    }

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    FD_SET(fd, &fsr);

    if(flushing_katcl(l)){ /* only write data if we have some */
      FD_SET(fd, &fsw);
    }

    if(until){
      gettimeofday(&now, NULL);
#ifdef DEBUG
      fprintf(stderr, "now %lu.%lds, until %lu.%lds\n", now.tv_sec, now.tv_usec, until->tv_sec, until->tv_usec);
#endif
      if(cmp_time_katcp(&now, until) < 0){
        sub_time_katcp(&tv, until, &now);
      } else {
        tv.tv_sec = 0;
        tv.tv_usec = 12;
      }
      result = select(fd + 1, &fsr, &fsw, NULL, &tv);
    } else {
      result = select(fd + 1, &fsr, &fsw, NULL, NULL); /* indefinitely */
    }

    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            return -1;
        }
        break;
      case  0 :
#ifdef FEEDBACK
        fprintf(stderr, "dispatch: no io activity within %lu.%lds\n", tv.tv_sec, tv.tv_usec);
#endif
        return -1;
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(l);
      if(result < 0){
#ifdef FEEDBACK
      	fprintf(stderr, "dispatch: write failed: %s\n", strerror(error_katcl(l)));
#endif
      	return -1;
      }
    }

    if(FD_ISSET(fd, &fsr)){
      result = read_katcl(l);
      if(result){
#ifdef FEEDBACK
      	fprintf(stderr, "dispatch: read failed: %s\n", (result < 0) ? strerror(error_katcl(l)) : "connection terminated");
#endif
      	return -1;
      }
    }

  }
}

int send_rpc_katcl(struct katcl_line *l, unsigned int timeout, ...)
{
  int result;
  va_list args;
#if 0
  struct timeval until, delta, now;
  char *ptr;
#endif

  va_start(args, timeout);
  result = vsend_katcl(l, args);
  va_end(args);

  if(result < 0){
#ifdef DEBUG
    fprintf(stderr, "issue: vsend failed\n");
#endif
    return -1;
  }

  return await_reply_rpc_katcl(l, timeout);

#if 0
  delta.tv_sec = timeout / 1000;
  delta.tv_usec = (timeout - delta.tv_sec * 1000) * 1000;
  gettimeofday(&now, NULL);
  add_time_katcp(&until, &now, &delta);

#ifdef DEBUG
  fprintf(stderr, "now is %lu.%lds, finish at %lu.%lds\n", now.tv_sec, now.tv_usec, until.tv_sec, until.tv_usec);
#endif

  while((result = complete_rpc_katcl(l, 0, &until)) == 0);

  if(result < 0){
    /* TODO: end connection, request/replies potentially out of sync */
    return -1;
  }

  ptr = arg_string_katcl(l, 1);
  if(ptr == NULL){
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
    return 1;
  }

  return 0;
#endif
}

#ifdef UNIT_TEST_RPC

int main()
{
  struct katcl_line *l;
  int result;

  l = create_name_rpc_katcl(NULL);
  if(l == NULL){
    fprintf(stderr, "unable to create line\n");
    return 1;
  }

  result = send_rpc_katcl(l, 10000, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?help", KATCP_FLAG_STRING | KATCP_FLAG_LAST, "fred");

  if(result < 0){
    fprintf(stderr, "request failed\n");
    return 1;
  }

  printf("request %s\n", result ? "failed" : "ok");

  destroy_rpc_katcl(l);

  return 0;
}
#endif

