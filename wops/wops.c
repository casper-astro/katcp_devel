
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include <arpa/inet.h>

#include <sys/time.h>
#include <sys/select.h>

#include <katcp.h>
#include <katcl.h>

#define WOPS_SKIP             1
#define WOPS_OK               0
#define WOPS_ERROR_LOGIC     -1
#define WOPS_ERROR_COMMS     -2
#define WOPS_ERROR_PERMANENT -3

#if 0
#define WOPS_ERROR_TIMEOUT   -3
#endif

struct wops_state
{
  int w_verbose;
  struct katcl_line *w_line;
  char *w_register;
  char *w_server;
  struct timeval w_when;
};

/*************************************************************************/

void usage(char *app)
{
  printf("usage: %s [-t timeout] [-s server] [-h] [-r] [-l] [-v] [-q] commands\n", app);
  printf("\n");
  printf("-h                this help\n");
  printf("-v                increase verbosity\n");
  printf("-q                operate quietly\n");
  printf("-r                restart on transient failures\n");
  printf("-l                loop through commands\n");
  printf("-s server:port    select the server to contact\n");
  printf("-t milliseconds   set a command timeout in ms\n");
  printf("\n");
  printf("commands:\n");
  printf("\n");
  printf("c:register=value  poll register until value (01X)\n");
  printf("w:register=value  write values (01FRPG) to register\n");
  printf("d:                delay until timeout\n");
  printf("t:milliseconds    specify a new timeout\n");
  printf("p:string          display a string\n");
  printf("return codes:\n");
  printf("\n");
  printf("0                 success\n");
  printf("1                 logic failure\n");
  printf("2                 communications failure\n");
  printf("3                 other permanent failures\n");
}

int extract_register_wops(struct wops_state *w, char *op)
{
  int i, len;
  char *ptr;

  for(i = 2; (op[i] != '\0') && (op[i] != '='); i++);

  if(op[i] == '\0'){
    return WOPS_ERROR_PERMANENT;
  }

  len = i - 2;
  ptr = realloc(w->w_register, len + 1);
  if(ptr == NULL){
    return WOPS_ERROR_PERMANENT;
  }

  w->w_register = ptr;

  memcpy(w->w_register, op + 2, len);
  w->w_register[len] = '\0';

  return i + 1;
}

/**************************************************************************/

void set_timeout_wops(struct wops_state *w, unsigned int timeout)
{
  struct timeval delta, now;

  gettimeofday(&now, NULL);
  delta.tv_sec = timeout / 1000;
  delta.tv_usec = (timeout % 1000) * 1000;

#ifdef DEBUG
  fprintf(stderr, "timeout: +%ums\n", timeout);
#endif

  add_time_katcp(&(w->w_when), &now, &delta);
}

int maintain_wops(struct wops_state *w)
{
  struct timeval now, delta;

  while(w->w_line == NULL){

#ifdef DEBUG
    fprintf(stderr, "maintain: attemping reconnect\n");
#endif
  
    w->w_line = create_name_rpc_katcl(w->w_server);

    if(w->w_line == NULL){

      gettimeofday(&now, NULL);

      if(cmp_time_katcp(&(w->w_when), &now) <= 0){
        return -1;
      }

      sub_time_katcp(&delta, &(w->w_when), &now);
      if(delta.tv_sec > 0){
        delta.tv_sec = 1;
        delta.tv_usec = 0;
      }

      select(0, NULL, NULL, NULL, &delta);
    }
  }

  return 0;
}

int read_word_wops(struct wops_state *w, char *name, uint32_t *value)
{
  int result[4], status, i;
  int expect[4] = { 6, 0, 2, 2 };
  char *ptr;
  uint32_t tmp;

  if(maintain_wops(w) < 0){
    return -1;
  }

  result[0] = append_string_katcl(w->w_line,                           KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?read");
  result[1] = append_string_katcl(w->w_line,                           KATCP_FLAG_STRING, name);
  result[2] = append_unsigned_long_katcl(w->w_line,                    KATCP_FLAG_ULONG,  0);
  result[3] = append_unsigned_long_katcl(w->w_line,  KATCP_FLAG_LAST | KATCP_FLAG_ULONG,  4);

  expect[1] = strlen(name) + 1;
  for(i = 0; i < 4; i++){
    if(result[i] != expect[i]){
#ifdef DEBUG
      fprintf(stderr, "read: append[%d]=%d != %d\n", i, result[i], expect[i]);
#endif
      return -1;
    }
  }

  while((status = complete_rpc_katcl(w->w_line, 0, &(w->w_when))) == 0);
#ifdef DEBUG
  fprintf(stderr, "read: status is %d\n", status);
#endif
  if(status < 0){
    if(w->w_line){
      destroy_rpc_katcl(w->w_line);
      w->w_line = NULL;
    }
    return -1;
  }

  ptr = arg_string_katcl(w->w_line, 1);
  if(ptr == NULL){
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
    return 1;
  }

  status = arg_buffer_katcl(w->w_line, 2, &tmp, 4);
  if(status != 4){
    return -1;
  }

  *value = ntohl(tmp);

  return 0;
}

int write_word_wops(struct wops_state *w, char *name, uint32_t value)
{
  int result[4], status, i;
  int expect[4] = { 7, 0, 2, 5 };
  char *ptr;
  uint32_t tmp;

  if(maintain_wops(w) < 0){
    return -1;
  }

  tmp = htonl(value);

  result[0] = append_string_katcl(w->w_line,       KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?write");
  result[1] = append_string_katcl(w->w_line,                          KATCP_FLAG_STRING, name);
  result[2] = append_unsigned_long_katcl(w->w_line,                   KATCP_FLAG_ULONG,  0);
  result[3] = append_buffer_katcl(w->w_line,        KATCP_FLAG_LAST | KATCP_FLAG_BUFFER, &tmp, 4);

  expect[1] = strlen(name) + 1;
  for(i = 0; i < 4; i++){
    if(result[i] != expect[i]){
#ifdef DEBUG
      fprintf(stderr, "write: result[%d]=%d != %d\n", i, result[i], expect[i]);
#endif
      return -1;
    }
  }

  while((status = complete_rpc_katcl(w->w_line, 0, &(w->w_when))) == 0);
  if(status < 0){
    if(w->w_line){
      destroy_rpc_katcl(w->w_line);
      w->w_line = NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "write: complete call failed\n");
#endif
    return -1;
  }

  ptr = arg_string_katcl(w->w_line, 1);
  if(ptr == NULL){
#ifdef DEBUG
    fprintf(stderr, "write: unable to acquire first parameter\n");
#endif
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
#ifdef DEBUG
    fprintf(stderr, "write: problematic return code %s\n", ptr);
#endif
    return 1;
  }

  return 0;
}

/****************************************************************************/

int perform_check_wops(struct wops_state *w, char *op)
{
  int j, base, result;
  uint32_t check, mask, got;

  base = extract_register_wops(w, op);
  if(base <= 0){
    return base;
  }

  mask = 0xffffffff;
  check = 0;

  for(j = 0; (j < 32) && (op[base + j] != '\0'); j++){
    check = check << 1;
    mask = mask << 1;
    switch(op[base + j]){
      case 'x' :
      case 'X' :
        mask |= 1;
        break;
      case '1' :
        check |= 1;
        break;
      case '0' : 
        break;
      default :
        return WOPS_ERROR_PERMANENT;
    }
  }

  mask = ~mask;

#ifdef DEBUG
  fprintf(stderr, "check: name=%s, mask=%08x, check=%08x\n", w->w_register, mask, check);
#endif

  for(;;){

    result = read_word_wops(w, w->w_register, &got);
    if(result < 0){
#ifdef DEBUG
      fprintf(stderr, "check: read returns failure=%d\n", result);
#endif
      return WOPS_ERROR_COMMS;
    }


#if 0
    result = send_rpc_katcl(w->w_line, 10000, 
      KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?read", 
                         KATCP_FLAG_STRING, w->w_register, 
                         KATCP_FLAG_STRING, "0",
      KATCP_FLAG_LAST  | KATCP_FLAG_STRING, "4");

    if(result < 0){
      return WOPS_ERROR_COMMS;
    }

    if(arg_buffer_katcl(w->w_line, 2, &got, 4) != 4){
      return WOPS_ERROR_COMMS;
    }
#endif

#ifdef DEBUG
    fprintf(stderr, "check: got %08x\n", got);
#endif

    if((got & mask) == check){
#ifdef DEBUG
      fprintf(stderr, "check: tested succeeded (masked got = 0x%0x)\n", got & mask);
#endif
      return WOPS_OK;
    }

    /* TODO: something more clever with this */
    sleep(1);

  }

  return 0;
}

int perform_write_wops(struct wops_state *w, char *op)
{
  int base, status;
  uint32_t vector[3];
  int i, j, count;

  if(w->w_line == NULL){
    return WOPS_ERROR_COMMS;
  }

  base = extract_register_wops(w, op);
  if(base <= 0){
    return base;
  }

  count = 1;
  for(i = 0; i < 3; i++){
    vector[i] = 0;
  }

  for(j = 0; (j < 32) && (op[base + j] != '\0'); j++){
    for(i = 0; i < 3; i++){
      vector[i] = vector[i] << 1;
    }
    switch(op[base + j]){
      case '0' :
        break;
      case '1' :
        for(i = 0; i < 3; i++){
          vector[i] = vector[i] | 1;
        }
        break;
      case 'F' : 
      case 'f' : 
        if(count < 2) count = 2;
        vector[0] = vector[0] | 1;
        break;
      case 'R' : 
      case 'r' : 
        if(count < 2) count = 2;
        vector[1] = vector[1] | 1;
        vector[2] = vector[2] | 1;
        break;
      case 'P' : 
      case 'p' : 
        count = 3;
        vector[1] = vector[1] | 1;
        break;
      case 'G' : 
      case 'g' : 
        count = 3;
        vector[0] = vector[0] | 1;
        vector[2] = vector[2] | 1;
        break;
      default :
        return WOPS_ERROR_PERMANENT;
    }
  }

  for(i = 0; i < count; i++){
#ifdef DEBUG
    fprintf(stderr, "write: [%d]=0x%08x\n", i, vector[i]);
#endif
    status = write_word_wops(w, w->w_register, vector[i]);
    if(status < 0){
      return status;
    }
  }

  return status;
}

/**********************************************************************/

int perform_wops(struct wops_state *w, char *op)
{
  int delay;
  struct timeval delta, now;

  if((op == NULL) || (op[0] == '\0')){
    return WOPS_ERROR_PERMANENT;
  }

  if(op[0] == '-'){
    return WOPS_SKIP;
  }

  if(op[1] != ':'){
    return WOPS_ERROR_PERMANENT;
  }

  switch(op[0]){
    case 'p' : 
      puts(op + 2);
      return WOPS_OK;

    case 't' : /* set the timeout for commands */

      delay = atoi(op + 2);
      if(delay <= 0){
        return WOPS_ERROR_PERMANENT;
      }

#ifdef DEBUG
      fprintf(stderr, "t: setting new timeout in %ums\n", delay);
#endif

      set_timeout_wops(w, delay);

      return WOPS_OK;
    case 'd' : /* delay until timeout */

      gettimeofday(&now, NULL);

      if(cmp_time_katcp(&(w->w_when), &now) > 0){
        sub_time_katcp(&delta, &(w->w_when), &now);
#ifdef DEBUG
        fprintf(stderr, "d: waiting for %lu.%0lds\n", delta.tv_sec, delta.tv_usec);
#endif
        select(0, NULL, NULL, NULL, &delta);
#ifdef DEBUG
      } else {
        fprintf(stderr, "d: timeout already happened\n");
#endif
      }

      return WOPS_OK;
    case 'c' : /* check a value */
      return perform_check_wops(w, op);
    case 'w' : /* write a value */
      return perform_write_wops(w, op);
    default :
#ifdef DEBUG
      fprintf(stderr, "unknown operation %c\n", op[0]);
#endif
      break;
  }

  return WOPS_ERROR_PERMANENT;
}

/***************************************************/

void destroy_wops(struct wops_state *w)
{
  if(w == NULL){
    return;
  }

  if(w->w_line){
    destroy_rpc_katcl(w->w_line);
    w->w_line = NULL;
  }

  if(w->w_register){
    free(w->w_register);
    w->w_register = NULL;
  }

  if(w->w_server){
    free(w->w_server);
    w->w_server = NULL;
  }

  free(w);
}

#if 0
int reset_wops(struct wops_state *w)
{
  if(w->w_line){
    destroy_rpc_katcl(w->w_line);
    w->w_line = NULL;
  }

  w->w_line = create_name_rpc_katcl(w->w_server);

  return w->w_line ? 0 : (-1);
}
#endif

struct wops_state *create_wops(char *server, int verbose, unsigned int timeout)
{
  struct wops_state *w;

  w = malloc(sizeof(struct wops_state));
  if(w == NULL){
    return NULL;
  }

  w->w_verbose = verbose;
  w->w_register = NULL;
  w->w_server = NULL;
  w->w_line = NULL;

  w->w_server = strdup(server);
  if(w->w_server == NULL){
    destroy_wops(w);
    return NULL;
  }

  w->w_line = create_name_rpc_katcl(server);

  set_timeout_wops(w, timeout);

  return w;
}

/***************************************************/

int main(int argc, char **argv)
{
  int i, j, c, status;
  char *app, *server;
  int verbose, loop, base, result, retry;
#if 0
  struct katcl_line *k, *l;
#endif
  struct wops_state *w;
  unsigned int timeout;

  verbose = 1;
  i = j = 1;
  app = "wops";
  loop = 0;
  timeout = 10000;
  retry = 0;

  server = getenv("KATCP_SERVER");
  if(server == NULL){
    server = "localhost";
  }

  base = argc;

  while (i < argc) {
#ifdef DEBUG
    fprintf(stderr, "main: considering argument [%d]=%s\n", i, argv[i]);
#endif
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

        case 'r' : 
          retry = 1;
          j++;
          break;

        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 'l' : 
          loop = 1;
          j++;
          break;

        case 't' :
        case 's' :

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
            case 's' :
              server = argv[i] + j;
              break;
            case 't' :
              timeout = atoi(argv[i] + j);
#ifdef DEBUG
              fprintf(stderr, "%s: new timeout is %u\n", app, timeout);
#endif
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
#ifdef DEBUG
      fprintf(stderr, "%s: first arg at %d, max %d\n", app, base, argc);
#endif
    }
  }


#ifdef DEBUG
  fprintf(stderr, "%s: parsed command line, base=%d argc=%d\n", app, base, argc);
#endif

  status = 0;

  w = create_wops(server, verbose, timeout);
  if(w == NULL){
    fprintf(stderr, "%s: unable to allocate word operation state\n", app);
    return 2;
  }

  do{

    for(i = base; i < argc; i++){
      if(verbose > 2){
        fprintf(stderr, "%s: performing operation [%d]=%s\n", app, i, argv[i]);
      }
      result = perform_wops(w, argv[i]);
      if(verbose > 2){
        fprintf(stderr, "%s: operation returns %d\n", app, result);
      }
      switch(result){
        case WOPS_SKIP :
          break;
        case WOPS_OK :
          status = 0;
          break;
        case WOPS_ERROR_PERMANENT :
        case WOPS_ERROR_LOGIC     :
          i = argc;
          status = result * (-1);
          loop = 0;
          break;
        case WOPS_ERROR_COMMS : 
          if(retry == 0){
            loop = 0;
            status = result * (-1);
          }
          i = argc;
          break;
      }
    }
  } while(loop);

  if(w){
    destroy_wops(w);
  }

  return status;
}
