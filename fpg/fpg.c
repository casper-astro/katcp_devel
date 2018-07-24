#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#include <sysexits.h>

#include "katcl.h"
#include "katcp.h"
#include "katpriv.h"

#include "netc.h"
#include <avltree.h>

#define V6_FPGA_DEVICE_ID 0x004288093

#define KCPFPG_LABEL      "kcpfpg"

#define LAST_CMD          "?quit"
#define UPLOAD_CMD        "?uploadbin"

#define SHORT_TIMEOUT       5000
#define LONG_TIMEOUT_FACTOR 3 

#define BINFILE_FUDGE      4

#define BUFFER             8192
#define BINFILE_HEAD       132     /* require at least this much */

#define CONNECT_ATTEMPTS   6

struct ipr_state{
  int i_verbose;
  int i_fd;
  int i_ufd;

  struct katcl_line *i_line;
  struct katcl_line *i_input;
  struct katcl_line *i_print;

#if 0
  struct stat sb;
#endif

  char *i_label;

  char i_buffer[BUFFER];
  unsigned int i_used;
  unsigned int i_seen;

  unsigned int i_timeout;
};


static int dispatch_client(struct ipr_state *ipr, char *name, unsigned int timeout)
{
  fd_set fsr, fsw;
  struct timeval tv;
  struct katcl_parse *px;
  int result;
  char *ptr;
  int fd;
#if 0
  int count;
#endif

  fd = fileno_katcl(ipr->i_line);

  for(;;){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    if(name){ /* only look for data if we need it */
      FD_SET(fd, &fsr);
    }

    if(flushing_katcl(ipr->i_line)){ /* only write data if we have some */
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
            log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "select failed while waiting for remote: %s", strerror(errno));
            return -1;
        }
        break;
      case  0 :
        log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "remote timed out after %u ms", timeout);
        return -1;
    }

    if(FD_ISSET(fd, &fsw)){
      result = write_katcl(ipr->i_line);
      if(result < 0){
        fprintf(stderr, "dispatch: write failed: %s\n", strerror(error_katcl(ipr->i_line)));
        return -1;
      }
      if((result > 0) && (name == NULL)){ /* if we finished writing and don't expect a match then quit */
        return 0;
      }
    }

    result = read_katcl(ipr->i_line);
    if(result){
      log_message_katcl(ipr->i_print, KATCP_LEVEL_TRACE, ipr->i_label, "read result is %d", result);
      return -1;
    }

    while(have_katcl(ipr->i_line) > 0){
#if 0
      count = arg_count_katcl(ipr->i_line);
#endif
      ptr = arg_string_katcl(ipr->i_line, 0);

      if(ptr){ 
        if(ipr->i_verbose){
          if(ptr[0] == KATCP_INFORM){
            px = ready_katcl(ipr->i_line);
            if(px){
              append_parse_katcl(ipr->i_print, px);
            }
          }
        }
        if(name && (!strcmp(name, ptr))){
          return 0;
        }
      }
    }
  }
}

/****************************************************************************************/

void destroy_ipr(struct ipr_state *i)
{
  if(i == NULL){
    return;
  }

  if(i->i_print){

    sync_message_katcl(i->i_print, KATCP_LEVEL_DEBUG, i->i_label, "deallocating intepreter state variables");
    destroy_katcl(i->i_print, 1);
    i->i_print = NULL;
  }

  if(i->i_line){
    destroy_rpc_katcl(i->i_line);
    i->i_line = NULL;
  }

  if(i->i_input){
    destroy_katcl(i->i_input, 0);
    i->i_input = NULL;
  }

  if(i->i_fd > STDIN_FILENO){
    close(i->i_fd);
  }

#if 0
  if(i->i_mapped){
    munmap(i->i_mapped, i->sb.st_size);
    i->i_mapped = NULL;
  }
#endif

  if(i->i_ufd > 0){
    close(i->i_ufd);
    i->i_ufd = -1;
  }

  i->i_label = NULL;

  free(i);
}

struct ipr_state *create_ipr(char *server, char *file, int verbose, char *label, unsigned int timeout)
{
  struct ipr_state *i;

  i = malloc(sizeof(struct ipr_state));
  if(i == NULL){
    return NULL;
  }

  i->i_verbose = verbose;	

  i->i_fd = -1;
  i->i_ufd = -1;

  i->i_line = NULL;
  i->i_input = NULL;
  i->i_print = NULL;

  i->i_label = label;

  /* i_buffer */
  i->i_used = 0;
  i->i_seen = 0;

  i->i_timeout = timeout;

  i->i_print = create_katcl(STDOUT_FILENO);
  if(i->i_print == NULL){
    fprintf(stderr, "unable to allocate state\n");
    destroy_ipr(i);
    return NULL;
  }

  log_message_katcl(i->i_print, KATCP_LEVEL_DEBUG, i->i_label, "initialising intepreter state variables");

  i->i_line = create_name_rpc_katcl(server);
  if(i->i_line == NULL){
    sync_message_katcl(i->i_print, KATCP_LEVEL_ERROR, i->i_label, "unable to create client connection to server %s: %s", server, strerror(errno));
    destroy_ipr(i);
    return NULL;
  }

  if((file == NULL) || (!strcmp(file, "-"))){
    i->i_fd = STDIN_FILENO;
  } else {
    i->i_fd = open(file, O_RDONLY);
    if(i->i_fd < 0){
      log_message_katcl(i->i_print, KATCP_LEVEL_ERROR, i->i_label, "unable to open file %s: %s", file, strerror(errno));
      destroy_ipr(i);
      return NULL;
    }
  }

  i->i_input = create_katcl(i->i_fd);
  if(i->i_input == NULL){
    destroy_ipr(i);
    return NULL;
  }

#if 0
  /* Memory mapping the file contents and parsing to the start of bin file */
  fstat(i->i_fd, &(i->sb));
  sync_message_katcl(i->i_print, KATCP_LEVEL_DEBUG, i->i_label, "Total size of file is: %lu", i->sb.st_size);

  /* offset must be a multiple of page size */
  i->i_mapped = mmap(NULL, i->sb.st_size, PROT_READ, MAP_PRIVATE, i->i_fd, 0);
  if(i->i_mapped == MAP_FAILED){
    sync_message_katcl(i->i_print, KATCP_LEVEL_ERROR, i->i_label, "Error in memory mapping file %s", file);
    i->i_mapped = NULL;
    destroy_ipr(i);
    return NULL;
  }
#endif

  return i;

}

/****************************************************************************************/

int search_marker(struct ipr_state *ipr)
{
  int rr;
  unsigned int i, j, test, limit, len;

  len = strlen(LAST_CMD);
  test = len + BINFILE_FUDGE;

  for(;;){
    rr = read(ipr->i_fd, ipr->i_buffer + ipr->i_used, BUFFER - ipr->i_used);
    if(rr <= 0){
      if(rr < 0){
        switch(errno){
          case EAGAIN :
          case EINTR  :
          break;
          default :
          log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "read of commands failed: %s", strerror(errno));
          return -1;
        }
      } else {
        log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "premature end of file before bitstream");
        return 1;
      }
    } else {
      ipr->i_used += rr;
    }

#ifdef DEBUG
    fprintf(stderr, "now have %u, checking less %u\n", ipr->i_used, test);
#endif


    if(ipr->i_used > test){
      limit = ipr->i_used - test;

      for(i = 0; i < limit; i++){
        if(ipr->i_buffer[i] == '?'){
          if(!strncmp(ipr->i_buffer + i, LAST_CMD, len)){
            if(i > 0){
              if(load_katcl(ipr->i_input, ipr->i_buffer, i) < 0){
                log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to load last %u command bytes", i);
                return -1;
              }
              ipr->i_seen += i;
            }

            sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "loaded %u bytes of commands", ipr->i_seen);

            i += len;

            for(j = 0; j < BINFILE_FUDGE; j++){
              switch(ipr->i_buffer[i]){
                case '\r' :
                case '\n' :
                  i++;
                  break;
                default :
                  j = BINFILE_FUDGE; /* terminate for loop */
                  break;
              }
            }

            if(i > ipr->i_used){
              log_message_katcl(ipr->i_print, KATCP_LEVEL_FATAL, ipr->i_label, "internal logic problem, ran over buffer");
              return -1;
            }

            memmove(ipr->i_buffer, ipr->i_buffer + i, ipr->i_used - i);
            ipr->i_used = ipr->i_used - i;

            ipr->i_seen = 0;

            return 0;
          }
        }
      }

      if(limit >= (BUFFER / 2)){
        if(load_katcl(ipr->i_input, ipr->i_buffer, BUFFER / 2) < 0){
          log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable a further %u command bytes", BUFFER / 2);
          return -1;
        }

        memmove(ipr->i_buffer, ipr->i_buffer + (BUFFER / 2), ipr->i_used - (BUFFER / 2));
        ipr->i_used = ipr->i_used - (BUFFER / 2);
        ipr->i_seen += BUFFER / 2;
      }
    }
  }

}

int check_bitstream(struct ipr_state *ipr)
{
  int rr;
  uint32_t id;

  while(ipr->i_used < BUFFER){
    rr = read(ipr->i_fd, ipr->i_buffer + ipr->i_used, BUFFER - ipr->i_used);
    if(rr <= 0){
      if(rr < 0){
        switch(errno){
          case EAGAIN :
          case EINTR  :
          log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "initial bitstream read failed: %s", strerror(errno));
          return -1;
        }
      } else {
        if(ipr->i_used < BINFILE_HEAD){
          log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "short bitstream end of file: %s", strerror(errno));
          return -1;
        } else {
          /* EXIT loop */
          break;
        }
      }
    } else {
      ipr->i_used += rr;
    }
  }

  /* FPGA device id calculation from bin file */
  id = (0x000000FF & ipr->i_buffer[128    ]) << 24
     | (0x000000FF & ipr->i_buffer[128 + 1]) << 16 
     | (0x000000FF & ipr->i_buffer[128 + 2]) <<  8  
     | (0x000000FF & ipr->i_buffer[128 + 3]) <<  0 ;

	/* Virtex6 device:XQ6VSX475T = 0x04288093 */
  if(id != V6_FPGA_DEVICE_ID){
    log_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "data does not seem to contain a Virtex 6 image");
    return -1;
  }

  log_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "Virtex 6 image detected");

  return 0;
}

int prepare_upload(struct ipr_state *ipr)
{
#if 0
  char *status;
#endif

  /* populate a request */
  if(append_string_katcl(ipr->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "?uploadbin")   < 0) {
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to populate upload request");
    return -1;
  }

  /* use above function to send upload request */
  if(dispatch_client(ipr, NULL, ipr->i_timeout * LONG_TIMEOUT_FACTOR) < 0) {
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to send upload request");
    return -1;
  }

#if 0
  status = arg_string_katcl(ipr->i_line, 1);
  if((status == NULL) || strcmp(status, KATCP_OK)){
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "uploadbin request failed");
    return -1;
  }

  /* clean up request for next call */
  have_katcl(ipr->i_line);
#endif

  return 0;
}

int waitfor_fpga(struct ipr_state *ipr)
{
  char *status;

  /* use above function to send upload request */
  if(dispatch_client(ipr, "!uploadbin", ipr->i_timeout * LONG_TIMEOUT_FACTOR) < 0) {
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "did not see uploadbin reply");

    return -1;
  }

  status = arg_string_katcl(ipr->i_line, 1);
  if(status == NULL){
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to retrieve uploadbin status field");
    return -1;
  }

  if(strcmp(status, KATCP_OK)){
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "upload failed");
    return -1;
  }

  log_message_katcl(ipr->i_print, KATCP_LEVEL_INFO, ipr->i_label, "fpga programmed");

  /* clean up request for next call */
  have_katcl(ipr->i_line);

  return 0;
}

int program_bin(struct ipr_state *ipr, char *server, int port)
{
  int attempts, run;
  int rr, wr;

  for(attempts = 0; attempts < CONNECT_ATTEMPTS; attempts++){
    ipr->i_ufd = net_connect(server, port, ipr->i_verbose ? (NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS) : 0);
    if(ipr->i_ufd < 0){
      log_message_katcl(ipr->i_print, KATCP_LEVEL_INFO, ipr->i_label, "retrying connect to port %d", port);
      usleep(40000*(attempts+1)*(attempts+2));
    } else {
      attempts = CONNECT_ATTEMPTS;
    }
  }

  if(ipr->i_ufd < 0){
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to connect to port %d: %s", port, strerror(errno));
    return -1;
  }

  for(run = 1; run != 0;){

    if((ipr->i_used < BUFFER / 2) && (run > 0)){
      rr = read(ipr->i_fd, ipr->i_buffer + ipr->i_used, BUFFER - ipr->i_used);
      if(rr <= 0){
        if(rr < 0){
          switch(errno){
            case EAGAIN :
            case EINTR  :
              break;
            default :
              log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "read of bitstream failed: %s", strerror(errno));
              return -1;
          }
        } else {
          run = (-1); /* almost exit */
        }
      } else {
        ipr->i_used += rr;
      }
    }

    if(ipr->i_used > 0){
      wr = write(ipr->i_ufd, ipr->i_buffer, ipr->i_used);
      if(wr < 0){
        switch(errno){
          case EAGAIN :
          case EINTR  :
            break;
          default :
            log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "upload of bitstream failed after %u bytes: %s", ipr->i_seen, strerror(errno));
            return -1;
        }
      } else {
        if(wr < ipr->i_used){
          memmove(ipr->i_buffer, ipr->i_buffer + wr, ipr->i_used - wr);
          ipr->i_used = ipr->i_used - wr;
        } else {
          ipr->i_used = 0;
        }
        ipr->i_seen += wr;
      }
    } else {
      if(run < 0){
        run = 0;
      }
    }
  }

  close(ipr->i_ufd);
  ipr->i_ufd = (-1);

  log_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "send %u bytes of bitstream", ipr->i_seen);

  if(waitfor_fpga(ipr) < 0) {
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "await reply failed", __func__);
    return -1;
  }

  return 0;
}

int finalise_upload(struct ipr_state *ipr)
{
  char *status;

  /* populate a request */
  if(append_string_katcl(ipr->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "?finalise") < 0) {
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to populate finalise request");
    return -1;
  }

  /* use above function to send upload request */
  if(dispatch_client(ipr, "!finalise", ipr->i_timeout * LONG_TIMEOUT_FACTOR) < 0) {
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to send finalise request");

    return -1;
  }

  status = arg_string_katcl(ipr->i_line, 1);
  if((status == NULL) || strcmp(status, KATCP_OK)){
    log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "finalise request failed");
    return -1;
  }

  /* clean up request for next call */
  have_katcl(ipr->i_line);

  return 0;
}

void usage(char *name)
{
  printf("usage: %s [-n] [-s server] [-l label] [-q] [-v] [-h] file.fpg [server]\n", name);
  printf("-s server   connect to the given server\n");
  printf("-q          run quietly\n");
  printf("-v          increase verbosity\n");
  printf("-h          this help\n");
  printf("-n          do not program, just run register/meta definitions\n");

}

int main(int argc, char **argv)
{
  struct katcl_parse *px;
  char *server, *label, *file;
  char *request, *status;

  int verbose, fail, program;
  int i, j, c;

  int timeout = SHORT_TIMEOUT;
  int port = 7146;

  struct ipr_state *ipr;


  if(isatty(STDOUT_FILENO)){
    verbose = 1;
  } else {
    verbose = 0;
  }

  server = getenv("KATCP_SERVER");
  if(server == NULL){
    server = "localhost";
  }

  label = getenv("KATCP_LABEL");
  if(label == NULL){
    label = KCPFPG_LABEL;
  }

  file = NULL;
  fail = 1;

  program = 1;

  i = j = 1;
  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(argv[0]);
          return 0;

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'n' : 
          program = 0;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 'l' :
        case 's' :
        case 't' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: argument needs a parameter\n", argv[0]);
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
          fprintf(stderr, "%s: unknown option -%c\n", argv[0], argv[i][j]);
          return 2;
      }
    } else {

      if(file == NULL){
        file = argv[i];
      } else {
        /* should check if we are clobbering something */
        server = argv[i];
      }
      i++;
    }
  }

  /* Initialise the intepreter state */
  ipr = create_ipr(server, file, verbose, label, timeout); 
  if(ipr == NULL){
    fprintf(stderr, "%s: unable to allocate intepreter state\n", argv[0]);
    return 2;
  }

  if(search_marker(ipr) < 0){
    sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to scan fpg file", LAST_CMD);
    destroy_ipr(ipr);
    return 2;
  }

  if(ipr->i_verbose){
    sync_message_katcl(ipr->i_print, KATCP_LEVEL_TRACE, ipr->i_label, "using %ums and %ums for short and long timeouts respectively", ipr->i_timeout, ipr->i_timeout * LONG_TIMEOUT_FACTOR);
  }

  if(program){
    /* send UPLOAD command */
    if(prepare_upload(ipr) < 0){
      sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "upload prepare failed");
      destroy_ipr(ipr);
      return 4;
    }

    sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "upload request sent");

    /* Program FPGA */
    if(program_bin(ipr, server, port) < 0){
      sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to program fpga");
      destroy_ipr(ipr);
      return 4;
    }
  } else {
    sync_message_katcl(ipr->i_print, KATCP_LEVEL_INFO, ipr->i_label, "fpga programming inhibited, only defining registers");
  }

  while(have_katcl(ipr->i_input) > 0){

    request = arg_string_katcl(ipr->i_input, 0);

    fail = 1;

    if(request){

      if(!strcmp(request, UPLOAD_CMD)){
        log_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "skipping %s as handled previously", UPLOAD_CMD);
        fail = 0;
      } else if(request[0] != KATCP_REQUEST){
        log_message_katcl(ipr->i_print, KATCP_LEVEL_TRACE, ipr->i_label, "not sending %s as not a request", request);
        fail = 0;
      } else {

        px = ready_katcl(ipr->i_input);
        if(px){
          append_parse_katcl(ipr->i_line, px);

          if(await_reply_rpc_katcl(ipr->i_line, ipr->i_timeout) < 0){
            log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "timed out while sending %s request", request);
          } else{
            status = arg_string_katcl(ipr->i_line, 1);
            if(status == NULL){
              log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to retrieve status for %s request", request);
            } else {
              if(strcmp(status, KATCP_OK) != 0){
                log_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "request %s failed with status %s", request, status);
              } else {
                /* ok */
                fail = 0;
              }
            }
          }
        }
      }
    }

    if(fail){
      sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to program fpg file");
      destroy_ipr(ipr);
      return 4;
    }
  }

  /* Shanly, what were you thinking ? */
#if 0
  while(1 && timeout < 6){
    if(*request == '\r' || *request == '\n' || *request == '\0'){
      printf("Time to get out of loop\n");
      ptr++;
      break;
    }
    request++;
    timeout++;
  }
#endif

  if(program){
    if(finalise_upload(ipr) < 0){
      sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to send finalise");
      destroy_ipr(ipr);
      return 4;
    }
    sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "finalise command sent successfully");
  }

  destroy_ipr(ipr);

  return 0;
}
