/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

/* A commandline utility which retrieves data using the bulkread command
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"

void usage(char *app)
{
  printf("usage: %s [options] borph-register\n", app);
  printf("-h                 this help\n");
  printf("-v                 increase verbosity\n");
  printf("-q                 run quietly\n");
  printf("-f save-file       file to save the data to (default: stdout)\n");
  printf("-o byte-count      offset into register (default: 0)\n");
  printf("-b byte-count      number of bytes to retrieve\n");
  printf("-s server:port     address and port of roach\n");
  printf("-t seconds         set timeout\n");

  printf("return codes:\n");
  printf("0     transfer completed successfully\n");
  printf("1     remote errors\n");
  printf("2     other errors\n");

  printf("environment variables:\n");
  printf("  KATCP_SERVER     default server (overridden by -s option)\n");

  printf("notes:\n");
  printf("  retrieving data which is not word aligned may have unpredictable results\n");
}

#define TIMEOUT   4

int main(int argc, char **argv)
{
  char *app, *server, *ptr;
  int i, j, c, fd, ffd;
  int verbose, result, timeout, flags;
  struct katcl_line *l;
  fd_set fsr, fsw;
  struct timeval tv;

  unsigned int bytes;
  unsigned int offset;
  char *borph;
  char *file;

  unsigned char *buffer, *tmp;
  unsigned int bufsize, bufgot;
  int bufwant;
  int wr;

  unsigned long bulkcode;
  
  server = getenv("KATCP_SERVER");
  if(server == NULL){
    server = "localhost";
  }

  verbose = 1;
  i = j = 1;
  app = argv[0];
  timeout = 5;

  bytes = 0;
  offset = 0;

  file = NULL;
  borph = NULL;
  
  buffer = NULL;
  bufsize = 0;
  bufgot = 0;

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return 0;

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 's' :
        case 't' :
        case 'p' :

        case 'b' :
        case 'o' :
        case 'f' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: usage: argument needs a parameter\n", app);
            return 2;
          }

          switch(c){
            case 'f' :
              file = argv[i] + j;
              break;
            case 's' :
              server = argv[i] + j;
              break;
            case 'o' :
              offset = atoi(argv[i] + j);
              break;
            case 'b' :
              bytes = atoi(argv[i] + j);
              break;
            case 't' :
              timeout = atoi(argv[i] + j);
              break;
          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: usage: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      if(borph != NULL){
        fprintf(stderr, "%s: usage: unexpected extra argument %s (can only read one register)\n", app, argv[i]);
        return 2;
      }
      borph = argv[i];
      i++;
    }
  }

  if(borph == NULL){
    fprintf(stderr, "%s: usage: need a borph register to read (use -h for help)\n", app);
    return 2;
  }

  flags = 0;
  if(verbose > 0){
    flags = NETC_VERBOSE_ERRORS;
    if(verbose > 1){
      flags = NETC_VERBOSE_STATS;
    }
  }

  fd = net_connect(server, 0, flags);
  if(fd < 0){
    return 2;
  }

  l = create_katcl(fd);
  if(l == NULL){
    fprintf(stderr, "%s: error: unable to allocate state\n", app);
    return 2;
  }

  if(file == NULL){
    ffd = STDOUT_FILENO;
  } else {
    ffd = open(file, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if(ffd < 0){
      fprintf(stderr, "%s: error: unable to open file %s: %s\n", app, file, strerror(errno));
      return 2;
    }
  }

  append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?bulkread");
  
  flags = (((offset == 0) && (bytes == 0)) ? KATCP_FLAG_LAST : 0) | KATCP_FLAG_STRING;
  append_string_katcl(l, flags, borph);

  flags = ((bytes == 0) ? KATCP_FLAG_LAST : 0) | KATCP_FLAG_ULONG;
  append_unsigned_long_katcl(l, flags, offset);

  flags = KATCP_FLAG_LAST | KATCP_FLAG_ULONG;
  append_unsigned_long_katcl(l, flags, bytes);

  /* WARNING: not only is the logic a bit intricate, it doesn't clean up properly either (close fds, etc) */

  for(;;){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    FD_SET(fd, &fsr);

    if(flushing_katcl(l)){ /* only write data if we have some */
      FD_SET(fd, &fsw);
    }

    tv.tv_sec  = timeout;
    tv.tv_usec = 0;

    result = select(fd + 1, &fsr, &fsw, NULL, &tv);
    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            return 2;
        }
        break;
      case  0 :
        if(verbose){
          fprintf(stderr, "%s: timeout: no io activity within %d seconds\n", app, timeout);
        }
        return 2;
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(l);
      if(result < 0){
      	fprintf(stderr, "%s: error: write failed: %s\n", app, strerror(error_katcl(l)));
      	return 2;
      }
    }

    if(FD_ISSET(fd, &fsr)){
      result = read_katcl(l);
      if(result){
      	fprintf(stderr, "%s: error: read failed: %s\n", app, (result < 0) ? strerror(error_katcl(l)) : "connection terminated");
      	return 2;
      }
    }

    while(have_katcl(l) > 0){
      ptr = arg_string_katcl(l, 0);
      if(ptr){
      	switch(ptr[0]){
          case KATCP_INFORM : 
            if(!strcmp("#bulkread", ptr)){
              bufwant = arg_buffer_katcl(l, 1, buffer, bufsize);
              if(bufsize < bufwant){
                tmp = realloc(buffer, bufwant);
                if(tmp == NULL){
                  fprintf(stderr, "%s: error: unable to allocate %d bytes\n", app, bufwant);
                  return 2;
                }
                buffer = tmp;
                bufwant = arg_buffer_katcl(l, 1, buffer, bufsize);
              }
              if(bufwant <= 0){
                fprintf(stderr, "%s: error: bulkread incomplete at position %d\n", app, bufgot);
                return 1;
              } else {
                wr = write(ffd, buffer, bufwant);
                if(wr < bufwant){
                  fprintf(stderr, "%s: error: unable to save data at position %d\n", app, bufgot);
                  return 2;
                }
                bufgot += bufwant;
              }
            } else if(!strcmp("#log", ptr)){
              fprintf(stderr, "%s: %s: %s\n", app, arg_string_katcl(l, 1), arg_string_katcl(l, 4));
            }
            break;
          case KATCP_REPLY : 
#ifdef DEBUG
            fprintf(stderr, "got reply <%s>\n", ptr);
#endif
            if(!strcmp("!bulkread", ptr)){
              ptr = arg_string_katcl(l, 1);
              if(ptr != NULL){
                if(!strcmp("ok", ptr)){
                  bulkcode = arg_unsigned_long_katcl(l, 2);
                  if(bulkcode != bufgot){
                    fprintf(stderr, "%s: data loss: transmitted %lu but captured %d\n", app, bulkcode, bufgot);
                    return 1;
                  }
                  if((bytes != 0) && (bytes != bufgot)){
                    fprintf(stderr, "%s: data loss: requested %d but captured %d\n", app, bytes, bufgot);
                    return 1;
                  }
                  if(ffd != STDOUT_FILENO){
                    close(ffd);
                    ffd = (-1);
                  }
                  return 0;
                } else {
                  fprintf(stderr, "%s: bulkread %s (not ok)\n", app, ptr);
                  return 1;
                }
              } else {
                fprintf(stderr, "%s: error: unable to acquire bulkread status code\n", app);
                return 2;
              }
            } else {
              fprintf(stderr, "%s: warning: unexpected reply %s\n", app, ptr);
            }
            break;
          case KATCP_REQUEST : 
      	    fprintf(stderr, "%s: warning: encountered an unanswerable request <%s>\n", app, ptr);
            break;
          default :
            fprintf(stderr, "%s: warning: read malformed message <%s>\n", app, ptr);
            break;
        }
      }
    }
  }

#if 0
  destroy_katcl(l, 1);
#endif

}
