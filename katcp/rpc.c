
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>

#include "katcl.h"
#include "katpriv.h"
#include "netc.h"

struct katcl_line *create_name_katcl(char *name)
{
  int fd;
  struct katcl_line *l;

  if(name){
    fd = net_connect(name, 0, 1);
    if(fd < 0){
#if 0
      fprintf(stderr, "connect: unable to contact %s: %s\n", name, strerror(errno));
#endif
      return NULL;
    }
  } else {
    fd = STDIN_FILENO;
  }

  l = create_katcl(fd);
  if(l == NULL){
    if(name){
      close(fd);
    }
    return NULL;
  }

  return l;
}

int finished_request_katcl(struct katcl_line *l, struct timeval *until)
{
  fd_set fsr, fsw;
  struct timeval tv, now;
  int result;
  int fd;

  fd = fileno_katcl(l);

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  for(;;){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    FD_SET(fd, &fsr);

    if(flushing_katcl(l)){ /* only write data if we have some */
      FD_SET(fd, &fsw);
    }

    if(until){
      gettimeofday(&now, NULL);
      if(cmp_time_katcp(&now, until) < 0){
        sub_time_katcp(&tv, until, &now);
      } else {
        tv.tv_sec = 0;
        tv.tv_usec = 1;
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
        fprintf(stderr, "dispatch: no io activity within %lu.%lds\n", tv.tv_sec, tv.tv_usec);
        return -1;
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(l);
      if(result < 0){
      	fprintf(stderr, "dispatch: write failed: %s\n", strerror(error_katcl(l)));
      	return -1;
      }
    }

    if(FD_ISSET(fd, &fsr)){
      if(read_katcl(l) < 0){
      	fprintf(stderr, "dispatch: read failed: %s\n", strerror(error_katcl(l)));
      	return -1;
      }
    }

    if(have_katcl(l) > 0){
      if(arg_reply_katcl(l)){
        return 1;
      }

      return 0;
    }
  }
}

#if 0
static int dispatch_client(struct katcl_line *l, char *msgname, int verbose, unsigned int timeout)
{
  fd_set fsr, fsw;
  struct timeval tv;
  int result;
  char *ptr, *match, *prev;
  int prefix;
  int i,j;
  int fd;

  fd = fileno_katcl(l);

  if(msgname){
    switch(msgname[0]){
      case '!' :
      case '?' :
        prefix = strlen(msgname + 1);
        match = msgname + 1;
        break;
      default :
        prefix = strlen(msgname);
        match = msgname;
        break;
    }
  } else {
    prefix = 0;
    match = NULL;
  }

  for(;;){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    if(match){ /* only look for data if we need it */
      FD_SET(fd, &fsr);
    }

    if(flushing_katcl(l)){ /* only write data if we have some */
      FD_SET(fd, &fsw);
    }

    tv.tv_sec  = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    result = select(fd + 1, &fsr, &fsw, NULL, &tv);
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
        if(verbose){
          fprintf(stderr, "dispatch: no io activity within %u ms\n", timeout);
        }
        return -1;
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(l);
      if(result < 0){
      	fprintf(stderr, "dispatch: write failed: %s\n", strerror(error_katcl(l)));
      	return -1;
      }
      if((result > 0) && (match == NULL)){ /* if we finished writing and don't expect a match then quit */
      	return 0;
      }
    }

    if(FD_ISSET(fd, &fsr)){
      if(read_katcl(l) < 0){
      	fprintf(stderr, "dispatch: read failed: %s\n", strerror(error_katcl(l)));
      	return -1;
      }
    }

    while(have_katcl(l) > 0){

      if (verbose) {
        i = arg_count_katcl(l);
        prev = arg_string_katcl(l,0);
        for( j = 0; j < i; j++ ) {
          ptr = arg_string_katcl(l,j);
          if(ptr && prev){
            /*prevent writing out binary data from read*/
            if( j == 2 && !strcmp("read", match) && !strcmp(prev, KATCP_OK) ){
              /*TODO do this properly*/
              fprintf(stdout," <binary data>");
            } else {
              if(j != 0) {
                fprintf(stdout, " ");
              } 
              fprintf(stdout,"%s", ptr);
              prev = ptr;  
            }
          }
        }
        fprintf(stdout,"\n");
      }

      ptr = arg_string_katcl(l, 0);
      if(ptr){
      	switch(ptr[0]){
          case KATCP_INFORM : 
            break;
          case KATCP_REPLY : 
            if(match){
              if(strncmp(match, ptr + 1, prefix) || ((ptr[prefix + 1] != '\0') && (ptr[prefix + 1] != ' '))){
      	        fprintf(stderr, "dispatch: warning, encountered reply <%s> does not match <%s>\n", ptr, match);
              } else {
              	ptr = arg_string_katcl(l, 1);
              	if(ptr && !strcmp(ptr, KATCP_OK)){
              	  return 0;
                } else {
                  return -1;
                }
              }
            }
            break;
          case KATCP_REQUEST : 
      	    fprintf(stderr, "dispatch: warning, encountered an unanswerable request <%s>\n", ptr);
            break;
          default :
            fprintf(stderr, "dispatch: read malformed message <%s>\n", ptr);
            break;
        }
      }
    }
  }
}
#endif

