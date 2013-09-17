/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

/* A commandline utility allowing one to send commands as arguments, 
 * where replies return "ok" strings, these are converted to exit
 * codes, in an attempt to make scripting a bit easier
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

#define KCPCMD_NAME "kcpcmd"

#define FMT_TEXT   0
#define FMT_AUTO   1
#define FMT_HEX    2
#define FMT_BIN    3

void usage(char *app)
{
  printf("usage: %s [options] command [args]\n", app);
  printf("-h                 this help\n");
  printf("-v                 increase verbosity\n");
  printf("-q                 run quietly\n");
  printf("-x                 print output in hex\n");
  printf("-a                 autodetect mode, only print nonprintable fields as hex\n");
  printf("-b                 print binary fields, including null characters\n");
  printf("-k                 emit katcp log messages\n");
  printf("-s server:port     specify server:port\n");
  printf("-t seconds         set timeout\n");
  printf("-p position        only print a particular argument number\n");
  printf("-l label           set katcp log message module label\n");
  printf("-r                 toggle printing of reply messages\n");
  printf("-i                 toggle printing of inform messages\n");
  printf("-m                 munge replies into log messages (requires -k)\n");
  printf("-n                 suppress version information emitted on connect\n");

  printf("return codes:\n");
  printf("0     command completed successfully\n");
  printf("1     command failed\n");
  printf("2     other errors\n");

  printf("environment variables:\n");
  printf("  KATCP_SERVER     default server (overridden by -s option)\n");

  printf("notes:\n");
  printf("  command and parameters have to be given as separate arguments\n");
  printf("  hex and auto mode will convert parameters starting with 0x from hex to binary\n");
}

#define BUFFER 1024
#define TIMEOUT   4
int print_arg(struct katcl_line *l, int index, int fmt)
{
  char *ptr, *tmp;
  unsigned int want, have, safe, i, dfmt;
  int c;

  if(arg_null_katcl(l, index) > 0){
    c = '\0';
    fputc(c, stdout);
    return 0;
  }

  switch(fmt){
    case FMT_TEXT :
      ptr = arg_string_katcl(l, index);
      if(ptr == NULL){
#ifdef DEBUG
        fprintf(stderr, "print: unable to acquire argument %d\n", index);
#endif
        return -1;
      }
      fputs(ptr, stdout);
      return 0;

    case FMT_HEX  :
    case FMT_BIN  :
    case FMT_AUTO :
      ptr = NULL;
      want = BUFFER;
      have = 0;
      safe = TIMEOUT;
      do{
        tmp = realloc(ptr, want);
        if(tmp == NULL){
          free(ptr);
#ifdef DEBUG
          fprintf(stderr, "print: unable to reallocate %p to %d\n", ptr, want);
#endif
          return -1;
        }
        have = want;
        ptr = tmp;
        want = arg_buffer_katcl(l, index, ptr, have);
        safe--;
        if(!safe){
#ifdef DEBUG
          fprintf(stderr, "print: safety threshold exceeded, wanted %d\n", want);
#endif
          free(ptr);
          return -1;
        }
      } while(want > have);

      if(want <= 0){
        free(ptr);
#ifdef DEBUG
        fprintf(stderr, "print: argument %d length is %d\n", index, want);
#endif
        return (want < 0) ? -1 : 0;
      }

      if(fmt == FMT_AUTO){
        dfmt = FMT_TEXT;
        for(i = 0; i < want; i++){
          if(!isprint(ptr[i])){
            dfmt = FMT_HEX;
          }
        }
      }  else {
        dfmt = fmt;
      }

      switch(dfmt){
        case FMT_BIN :
        case FMT_TEXT :
          fwrite(ptr, 1, want, stdout);
          break;
        case FMT_HEX :
          fprintf(stdout, "0x");
          for(i = 0; i < want; i++){
            fprintf(stdout, "%02x", ((unsigned char *)ptr)[i]);
          }
      }

      free(ptr);
      return 0;
    default : 
#ifdef DEBUG
      fprintf(stderr, "print: unknown format %d\n", fmt);
#endif
      return -1;
  }
}
#undef BUFFER

static int h2n(int v)
{
  switch(v){
    case '0' : return 0;
    case '1' : return 1;
    case '2' : return 2;
    case '3' : return 3;
    case '4' : return 4;
    case '5' : return 5;
    case '6' : return 6;
    case '7' : return 7;
    case '8' : return 8;
    case '9' : return 9;
    case 'A' :
    case 'a' : return 10;
    case 'B' :
    case 'b' : return 11;
    case 'C' :
    case 'c' : return 12;
    case 'D' :
    case 'd' : return 13;
    case 'E' :
    case 'e' : return 14;
    case 'F' :
    case 'f' : return 15;
    default  : 
               fprintf(stderr, "error: %c not a hex digit\n", v);
               return -1;
  }
}

int load_arg(struct katcl_line *l, char *arg, int fmt, int flags)
{
  unsigned char *tmp;
  int len, i, j, v, result;

  switch(fmt){
    case FMT_BIN  :
    case FMT_TEXT :
      return append_string_katcl(l, flags, arg);
    case FMT_AUTO : 
    case FMT_HEX  : 
      if(strncmp(arg, "0x", 2)){
        return append_string_katcl(l, flags, arg);
      } else {
        len = strlen(arg + 2);
        if(len % 2){
          fprintf(stderr, "warning: argument %s contains an odd number of hex characters, ignoring the trailing one\n", arg);
          len--;
        }

        len = len / 2;

        if(len == 0){
          /* NULL parameters are really bad form */
          return append_buffer_katcl(l, flags, NULL, 0);
        }

        tmp = malloc(len);
        if(tmp == NULL){
          fprintf(stderr, "warning: argument %s contains an odd number of hex characters, ignoring the trailing one\n", arg);
          return -1;
        }
  
        j = 2; 
        for(i = 0; i < len; i++){
          v = h2n(arg[j++]);
          if(v < 0){
            free(tmp);
            return -1;
          }
          tmp[i] = 0xf0 & (v << 4);
          v = h2n(arg[j++]);
          if(v < 0){
            free(tmp);
            return -1;
          }
          tmp[i] |= 0x0f & v;
        }

        result = append_buffer_katcl(l, flags, tmp, len);
        free(tmp);

        return result;
      }
      break;
    default :
      fprintf(stderr, "error: unhandled format %d\n", fmt);
      return -1;
  }
}

int main(int argc, char **argv)
{
  char *app, *server, *match, *parm, *tmp, *cmd, *extra, *label;
  int i, j, c, fd;
  int verbose, result, status, base, run, info, reply, display, max, prefix, timeout, fmt, pos, flags, munge, show;
  struct katcl_line *l, *k;
  fd_set fsr, fsw;
  struct timeval tv;

  label = KCPCMD_NAME;
  
  server = getenv("KATCP_SERVER");
  if(server == NULL){
    server = "localhost";
  }
  
  info = 1;
  reply = 1;
  verbose = 1;
  i = j = 1;
  app = argv[0];
  base = (-1);
  timeout = 5;
  fmt = FMT_TEXT;
  pos = (-1);
  k = NULL;
  munge = 0;
  show = 1;
  parm = NULL;

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
          info = 0;
          reply = 0;
          j++;
          break;
        case 'i' : 
          info = 1 - info;
          j++;
          break;
        case 'r' : 
          reply = 1 - reply;
          j++;
          break;
        case 'n' : 
          show = 0;
          j++;
          break;
        case 'k' : 
          k = create_katcl(STDOUT_FILENO);
          if(k == NULL){
            fprintf(stderr, "%s: unable to create katcp message logic\n", app);
            return 2;
          }
          j++;
          break;
        case 'a' :
          fmt = FMT_AUTO;
          j++;
          break;
        case 'x' :
          fmt = FMT_HEX;
          j++;
          break;
        case 'b' :
          fmt = FMT_BIN;
          j++;
          break;
        case 'm' :
          munge = 1;
          j++;
          break;

        case 'l' :
        case 's' :
        case 't' :
        case 'p' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: argument needs a parameter\n", app);
            return 2;
          }

          switch(c){
            case 'l' :
              label = argv[i] + j;
              break;
            case 's' :
              server = argv[i] + j;
              break;
            case 't' :
              timeout = atoi(argv[i] + j);
              break;
            case 'p' :
              pos = atoi(argv[i] + j);
              if(pos < 0){
                fprintf(stderr, "%s: position needs to be nonnegative, not %d\n", app, pos);
                return 2;
              }
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
          fprintf(stderr, "%s: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      base = i;
      i = argc;
    }
  }

  if(munge){
    if(k){
      reply = 1;
    }
  }

  if(base < 0){
    if(k){
      sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "no command given");
    }
    fprintf(stderr, "%s: need a command to send (use -h for help)\n", app);
    return 2;
  }

  status = 1;

  flags = 0;
  if(verbose > 0){
    flags = NETC_VERBOSE_ERRORS;
    if(verbose > 1){
      flags = NETC_VERBOSE_STATS;
    }
  }

  fd = net_connect(server, 0, flags);
  if(fd < 0){
    if(k){
      sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to connect to %s", server);
    }
    return 2;
  }

  l = create_katcl(fd);
  if(l == NULL){
    if(k){
      sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to create katcp parser");
    }
    fprintf(stderr, "%s: unable to create katcp parser\n", app);
    return 2;
  }

  i = base;
  match = NULL;
  flags = ((i + 1) < argc) ? KATCP_FLAG_FIRST : (KATCP_FLAG_FIRST | KATCP_FLAG_LAST);
  switch(argv[i][0]){
    case KATCP_REQUEST :
      match = argv[i] + 1;
      /* FALL */
    case KATCP_INFORM :
    case KATCP_REPLY  :
      append_string_katcl(l, flags, argv[i]);
    break;
    default :
      match = argv[i];
      append_args_katcl(l, flags, "%c%s", KATCP_REQUEST, argv[i]);
    break;
  }
  i++;
  while(i < argc){
    tmp = argv[i];
    i++;
    flags = (i < argc) ? 0 : KATCP_FLAG_LAST;
    if(load_arg(l, tmp, fmt, flags) < 0){
      if(k){
        sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to load argument %d", i);
      }
      fprintf(stderr, "%s: unable to load argument %d\n", app, i);
      return 2;
    }
  }

  if(match){
    for(prefix = 0; (match[prefix] != '\0') && (match[prefix] != ' '); prefix++);
#ifdef DEBUG
    fprintf(stderr, "debug: checking prefix %d of %s\n", prefix, match);
#endif
  } else {
    prefix = 0; /* pacify -Wall, prefix only used if match is set */
  }

  /* WARNING: logic a bit intricate */

  for(run = 1; run;){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    if(match){ /* only look for data if we need it */
      FD_SET(fd, &fsr);
    }

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
            if(k){
              sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "select failed: %s", strerror(errno));
            }
            return 2;
        }
        break;
      case  0 :
        if(k){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "request %s timed out after %d seconds", argv[base], timeout);
        } 
        if(verbose){
          fprintf(stderr, "%s: no io activity within %d seconds\n", app, timeout);
        }
        /* could terminate cleanly here, but ... */
        return 2;
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(l);
      if(result < 0){
        if(k){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "write failed: %s", strerror(errno));
        } 
        fprintf(stderr, "%s: write failed: %s\n", app, strerror(error_katcl(l)));
      	return 2;
      }
      if((result > 0) && (match == NULL)){ /* if we finished writing and don't expect a match then quit */
      	run = 0;
      }
    }

    if(FD_ISSET(fd, &fsr)){
      result = read_katcl(l);
      if(result){
        if(k){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "read failed: %s", (result < 0) ? strerror(error_katcl(l)) : "connection terminated");

        } 
        fprintf(stderr, "%s: read failed: %s\n", app, (result < 0) ? strerror(error_katcl(l)) : "connection terminated");
      	return 2;
      }
    }

    while(have_katcl(l) > 0){
      cmd = arg_string_katcl(l, 0);
      if(cmd){
        display = 0;
      	switch(cmd[0]){
          case KATCP_INFORM : 
            display = info;
            if(show == 0){
              if(!strcmp(KATCP_VERSION_CONNECT_INFORM, cmd)){
                display = 0;
              }
              if(!strcmp(KATCP_VERSION_INFORM, cmd)){
                display = 0;
              }
              if(!strcmp(KATCP_BUILD_STATE_INFORM, cmd)){
                display = 0;
              }
            }

            break;
          case KATCP_REPLY : 
            display = reply;
            parm = arg_string_katcl(l, 1);
            if(match){
              if(strncmp(match, cmd + 1, prefix) || ((cmd[prefix + 1] != '\0') && (cmd[prefix + 1] != ' '))){
                if(k){
                  sync_message_katcl(k, KATCP_LEVEL_WARN, label, "encountered unexpected reply %s", cmd);
                } 
                fprintf(stderr, "%s: warning: encountered unexpected reply <%s>\n", app, cmd);
              } else {
              	if(parm && !strcmp(parm, KATCP_OK)){
              	  status = 0;
                }
              	run = 0;
              }
            }
            break;
          case KATCP_REQUEST : 
            if(k){
              sync_message_katcl(k, KATCP_LEVEL_WARN, label, "encountered unanswerable request %s", cmd);
            } 
            fprintf(stderr, "%s: warning: encountered an unanswerable request <%s>\n", app, cmd);
            break;
          default :
            if(k){
              sync_message_katcl(k, KATCP_LEVEL_WARN, label, "read malformed message %s", cmd);
            } 
            fprintf(stderr, "%s: read malformed message <%s>\n", app, cmd);
            break;
        }
        if(display){
#ifdef DEBUG
          fprintf(stderr, "need to display\n");
#endif
          if(k){
            if(munge && parm && (cmd[0] == KATCP_REPLY)){
              if(!strcmp(parm, KATCP_OK)){
                sync_message_katcl(k, KATCP_LEVEL_DEBUG, cmd + 1, KATCP_OK);
              } else {
                extra = arg_string_katcl(l, 2);
                sync_message_katcl(k, KATCP_LEVEL_WARN, cmd + 1, "%s (%s)", parm, extra ? extra : "no extra information");
              } 
            } else {
              relay_katcl(l, k);
            }
          } else {
            max = arg_count_katcl(l);
            if(pos < 0){
              for(i = 0; i < max; i++){
                if(print_arg(l, i, fmt) < 0){
                  fprintf(stderr, "%s: failed to print argument %d\n", app, i);
                  return 2;
                }
                fputc(((i + 1) == max) ? '\n' : ' ' , stdout);
              }
            } else {
              if(pos < max){
                i = pos;
                if(print_arg(l, i, fmt) < 0){
                  fprintf(stderr, "%s: failed to print argument %d\n", app, i);
                  return 2;
                }
              }
            }
          }
        }
      }
    }
  }

  destroy_katcl(l, 1);
  if(k){
    while(write_katcl(k) == 0);

    destroy_katcl(k, 0);
  }

  return status;
}
