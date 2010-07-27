/* example client to read and write registers, using borph_read and 
 * borph_write - edit main to do what you want
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <sys/select.h>
#include <sys/time.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"

/* supporting logic starts here */

static int dispatch_client(struct katcl_line *l, char *msgname, int verbose, unsigned int timeout)
{
  fd_set fsr, fsw;
  struct timeval tv;
  int result;
  char *ptr, *match;
  int prefix;
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
      ptr = arg_string_katcl(l, 0);
      if(ptr){
#ifdef DEBUG
        fprintf(stderr, "dispatch: got back %s\n", ptr);
#endif
      	switch(ptr[0]){
          case KATCP_INFORM : 
            break;
          case KATCP_REPLY : 
            if(match){
              if(strncmp(match, ptr + 1, prefix) || ((ptr[prefix + 1] != '\0') && (ptr[prefix + 1] != ' '))){
      	        fprintf(stderr, "dispatch: warning, encountered reply <%s> not match <%s>\n", ptr, match);
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

/* function which sends a value: write register offset value */

int borph_write(struct katcl_line *l, char *regname, void *buffer, int len, unsigned int timeout)
{
  /* populate a request */
  if(append_string_katcl(l, KATCP_FLAG_FIRST, "?write")   < 0) return -1;
  if(append_string_katcl(l, 0, regname)                   < 0) return -1;
  if(append_unsigned_long_katcl(l, 0, 0)                  < 0) return -1;
  if(append_buffer_katcl(l, KATCP_FLAG_LAST, buffer, len) < 0) return -1;

  /* use above function to send request */
  if(dispatch_client(l, "!write", 1, timeout)             < 0) return -1;

  /* clean up request for next call */
  have_katcl(l);

  return 0;
}

/* function which retrieves a value: ?read register offset length */

int borph_read(struct katcl_line *l, char *regname, void *buffer, int len, unsigned int timeout)
{
  int count, got;

  if(append_string_katcl(l, KATCP_FLAG_FIRST, "?read")    < 0) return -1;
  if(append_string_katcl(l, 0, regname)                   < 0) return -1;
  if(append_unsigned_long_katcl(l, 0, 0)                  < 0) return -1;
  if(append_unsigned_long_katcl(l, KATCP_FLAG_LAST, len)  < 0) return -1;

  if(dispatch_client(l, "!read", 1, timeout)              < 0) return -1;

  count = arg_count_katcl(l);
  if(count < 2){
    fprintf(stderr, "read: insufficient arguments in reply\n");
    return -1;
  }

  got = arg_buffer_katcl(l, 2, buffer, len);

  if(got < len){
    fprintf(stderr, "read: partial data, wanted %d, got %d\n", len, got);
    return -1;
  }

  have_katcl(l);

  return len;
}

int main(int argc, char **argv)
{
  char *server;
  int fd;
  struct katcl_line *l;
  unsigned char data[4];

  if(argc <= 0){
    server = getenv("KATCP_SERVER");
  } else {
    server = argv[1];
  }

  if(server == NULL){
    fprintf(stderr, "need a server as first argument or in the KATCP_SERVER variable\n");
    return 2;
  }

  fd = net_connect(server, 0, 1);
  if(fd < 0){
    fprintf(stderr, "unable to connect to %s\n", server);
    return 2;
  }

  l = create_katcl(fd);
  if(l == NULL){
    fprintf(stderr, "unable to allocate state\n");
    return 2;
  }

  if(borph_read(l, "junk", data, 4, 50000) < 0){
    fprintf(stderr, "unable to read register\n");
    return 2;
  }

  /* careful - intel integers little endian */
  data[0] = 0xde;
  data[1] = 0xad;
  data[2] = 0xbe;
  data[3] = 0xef;

  if(borph_write(l, "junk", data, 4, 50000) < 0){
    fprintf(stderr, "unable to write to register\n");
    return 2;
  }

  destroy_katcl(l, 1);

  return 0;
}
