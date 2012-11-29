/* (c) 2012 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define DEBUG

#define ITEM_STAY              0x0
#define ITEM_OK                0x1
#define ITEM_FAIL              0x2
#define ITEM_ALT               0x3

#define REGISTER_POWERSTATE  0x280
#define REGISTER_POWERUP     0x281
#define REGISTER_POWERDOWN   0x282

#define POWER_OFF                0
#define POWER_STARTING           1
#define POWER_ON                 2
#define POWER_NA                 3  

#define NAME                "xport"

#define DEFAULT_PORT         10001
#define DEFAULT_WAIT            10

#define ADD_READ               0x1
#define ADD_WRITE              0x2

#define BUFFER 64

struct item;

struct state{
  char *s_name;
  struct sockaddr_in s_sa;

  fd_set s_fsr, s_fsw;
  int s_max;

  int s_power;
  int s_address;
  int s_value;

  unsigned char s_buffer[BUFFER];
  unsigned int s_have;
  unsigned int s_done;

  int s_fd;

  struct katcl_line *s_up;

  struct item *s_table;
  unsigned int s_size;
  unsigned int s_index;

  unsigned int s_transition;
  unsigned int s_limit;

  struct timeval s_single;
  struct timeval s_total;
};

struct item{
  int (*i_call)(struct state *ss, int tag);
  unsigned int i_ok;
  unsigned int i_fail;
  unsigned int i_alt;

  int i_tag;
};

/*********************************************************************/
/* setup routines ****************************************************/

void destroy_state(struct state *ss)
{
  if(ss == NULL){
    return;
  }

  if(ss->s_name){
    free(ss->s_name);
  }

  ss->s_have = 0;
  ss->s_done = 0;

  if(ss->s_fd >= 0){
    close(ss->s_fd);
  }

  if(ss->s_up){
    destroy_katcl(ss->s_up, 0);
    ss->s_up = NULL;
  }

  free(ss);
}

struct state *create_state(int fd)
{
  struct state *ss;

  ss = malloc(sizeof(struct state));
  if(ss == NULL){
    return NULL;
  }

  ss->s_name = NULL;
  /* s_sa, fd sets */

  ss->s_max = (-1);

  ss->s_power = 0;
  ss->s_address = (-1);
  ss->s_value = (-1);

  /* buffer */
  ss->s_have = 0;
  ss->s_done = 0;

  ss->s_fd = (-1);

  ss->s_up = NULL;

  ss->s_table = NULL;
  ss->s_size = 0;
  ss->s_index = 0;

  ss->s_transition = ITEM_STAY;

  ss->s_up = create_katcl(fd);
  if(ss->s_up == NULL){
    destroy_state(ss);
    return NULL;
  }

  return ss;
}

int add_roach(struct state *ss, char *name)
{
  struct hostent *he;

  ss->s_power = POWER_NA;
  ss->s_name = strdup(name);

  if(ss->s_name == NULL){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to duplicate string %s", name);
    return -1;
  }

  if(inet_aton(name, &(ss->s_sa.sin_addr)) == 0){
    he = gethostbyname(name);
    if((he == NULL) || (he->h_addrtype != AF_INET)){
      sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to resolve roach name %s", name);
      return -1;
    } else {
      ss->s_sa.sin_addr = *(struct in_addr *) he->h_addr;
    }
  }

  ss->s_sa.sin_port = htons(DEFAULT_PORT);
  ss->s_sa.sin_family = AF_INET;

  return 0;
}

void load_table(struct state *ss, struct item *table, unsigned int size)
{
  ss->s_table = table;
  ss->s_size = size;
  ss->s_index = 0;
}

/*********************************************************************/
/* support logic *****************************************************/

int issue_ping(struct state *ss)
{
  char buffer[1];
  int wr;

  buffer[0] = 0x08;

  wr = send(ss->s_fd, buffer, 1, MSG_DONTWAIT | MSG_NOSIGNAL);

  if(wr < 0){
    switch(errno){
      case EAGAIN :
      case EINTR : 
        return 1;
      default :
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to issue read to roach %s", ss->s_name, strerror(errno));
        return -1;
    }
  }

  if(wr != 1){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "incomplete ping to roach %s");
    return -1;
  }

  return 0;
}

int issue_read(struct state *ss, unsigned int address)
{
  char buffer[3];
  int wr;

  buffer[0] = 0x01;
  buffer[1] = address & 0xff;
  buffer[2] = (address >> 8) & 0xff;

  wr = send(ss->s_fd, buffer, 3, MSG_DONTWAIT | MSG_NOSIGNAL);

  if(wr < 0){
    switch(errno){
      case EAGAIN :
      case EINTR : 
        return 1;
      default :
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to issue read to roach %s", ss->s_name, strerror(errno));
        return -1;
    }
  }

  if(wr != 3){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "incomplete io to roach %s");
    return -1;
  }

  ss->s_address = address;
  ss->s_value = (-1);

  return 0;
}

int issue_write(struct state *ss, unsigned int address, unsigned int value)
{
  char buffer[5];
  int wr;

  buffer[0] = 0x01;
  buffer[1] = address & 0xff;
  buffer[2] = (address >> 8) & 0xff;
  buffer[3] = value & 0xff;
  buffer[4] = (value >> 8) & 0xff;

  wr = send(ss->s_fd, buffer, 5, MSG_DONTWAIT | MSG_NOSIGNAL);

  if(wr < 0){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to issue read to roach %s", ss->s_name, strerror(errno));
    return -1;
  }

  if(wr != 5){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "incomplete io to roach %s");
    return -1;
  }

  ss->s_address = address;
  ss->s_value = value;

  return 0;
}

static void init_fd(struct state *ss)
{
  int fd;

  FD_ZERO(&(ss->s_fsr));
  FD_ZERO(&(ss->s_fsw));

  fd = fileno_katcl(ss->s_up);
  ss->s_max = fd;

  FD_SET(fd, &(ss->s_fsr));
  if(flushing_katcl(ss->s_up)){
    FD_SET(fd, &(ss->s_fsw));
  }
}

void add_fd(struct state *ss, int fd, unsigned int flag)
{
  if(ss->s_max < 0){
    init_fd(ss);
  }

  if(flag & ADD_READ){
    FD_SET(fd, &(ss->s_fsr));
  }

  if(flag & ADD_WRITE){
    FD_SET(fd, &(ss->s_fsw));
  }

  if(ss->s_max < fd){
    ss->s_max = fd;
  }
}

void trim_buffer(struct state *ss)
{
  if(ss->s_done == 0){
    return;
  }

  memmove(ss->s_buffer, ss->s_buffer + ss->s_done, BUFFER - ss->s_done);
  ss->s_have -= ss->s_done;
  ss->s_done = 0;
}

int load_buffer(struct state *ss, int force)
{
  int result;

  if(force == 0){ 
    if(!(FD_ISSET(ss->s_fd, &(ss->s_fsr)))){
      return ss->s_have;
    }
  }

  trim_buffer(ss);

  result = recv(ss->s_fd, ss->s_buffer + ss->s_have, BUFFER - ss->s_have, 0);
  if(result < 0){
    switch(errno){
      case EAGAIN :
      case EINTR  :
        return ss->s_have;
      default :
        log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "read from %s failed: %s", ss->s_name, strerror(errno));
        close(ss->s_fd);
        ss->s_fd = (-1);
        return -1;
    }
  } 

  ss->s_have += result;

  return ss->s_have;
}

int find_ping_reply(struct state *ss)
{
  int discard;

  discard = 0;

#ifdef DEBUG
  fprintf(stderr, "looking for reply: done=%u, have=%u\n", ss->s_done, ss->s_have);
#endif

  while(ss->s_done < ss->s_have){
    if(ss->s_buffer[ss->s_done] == 0x8){
      trim_buffer(ss);
      if(ss->s_have >= 3){
        ss->s_done = 1;
        return 0;
      } else {
        return 1;
      }
    } else {
      discard--;
    }
    ss->s_done++;
  }

  return (discard == 0) ? 1 : discard;
}

int find_read_reply(struct state *ss)
{
  int discard;
  discard = 0;

#ifdef DEBUG
  fprintf(stderr, "looking for reply: done=%u, have=%u\n", ss->s_done, ss->s_have);
#endif

  while(ss->s_done < ss->s_have){
    if(ss->s_buffer[ss->s_done] == 0x1){
      trim_buffer(ss);
      if(ss->s_have >= 3){
        ss->s_value = (ss->s_buffer[1]) + (ss->s_buffer[2] * 256);
        ss->s_done = 3;
        return 0;
      } else {
        return 1;
      }
    } else {
      discard--;
    }
    ss->s_done++;
  }

  return (discard == 0) ? 1 : discard;
}

void clear_io_state(struct state *ss)
{
  ss->s_address = (-1);
  ss->s_value = (-1);
}

int set_timeout(struct state *ss, unsigned int s, unsigned int us, unsigned int transition)
{
  struct timeval delta, now;

  if(ss->s_transition != ITEM_STAY){
    return 1;
  }

  gettimeofday(&now, NULL);

  delta.tv_sec = s;
  delta.tv_usec = us;

  add_time_katcp(&(ss->s_single), &now, &delta);

  ss->s_transition = transition;

  return 0;
}

/*********************************************************************/
/* state stuff *******************************************************/

int setup_network_item(struct state *ss, int tag)
{
  int result;
#ifndef SOCK_NONBLOCK
  int flags;
#endif

  if(ss->s_fd >= 0){
    return ITEM_ALT;
  }

#ifdef SOCK_NONBLOCK
  ss->s_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
#else
  ss->s_fd = socket(AF_INET, SOCK_STREAM, 0);
  flags = fcntl(fd, F_GETFL, NULL);
  if(flags >= 0){
    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
#endif

  if(ss->s_fd < 0){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to allocate a socket: %s", strerror(errno));

    return ITEM_FAIL;
  }

  result = connect(ss->s_fd, (struct sockaddr *)(&(ss->s_sa)), sizeof(struct sockaddr_in));
  if(result < 0){
    if(errno != EINPROGRESS){
      sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to connect to %s: %s", ss->s_name, strerror(errno));
      close(ss->s_fd);
      ss->s_fd = (-1);
      return ITEM_FAIL;
    }
  }

#if 0
  add_fd(ss, ss->s_fd, ADD_WRITE);
#endif

  return ITEM_OK;
}

int complete_network_item(struct state *ss, int tag)
{
  unsigned int len;
  int code, result;

  if(ss->s_fd < 0){
    return ITEM_FAIL;
  }
  
#if 0
  if(!FD_ISSET(ss->s_fd, &(ss->s_fsw))){
    add_fd(ss, ss->s_fd, ADD_WRITE);
    return ITEM_STAY;
  }
#endif

  len = sizeof(int);
  result = getsockopt(ss->s_fd, SOL_SOCKET, SO_ERROR, &code, &len);

  if(result != 0){
    return ITEM_FAIL;
  }

  switch(code){
    case 0 :
      log_message_katcl(ss->s_up, KATCP_LEVEL_DEBUG, NAME, "async connect to %s succeeded", ss->s_name);
      return ITEM_OK;

    case EINPROGRESS : 
      log_message_katcl(ss->s_up, KATCP_LEVEL_DEBUG, NAME, "still waiting for async connect to complete");
      set_timeout(ss, 4, 0, ITEM_FAIL);
      add_fd(ss, ss->s_fd, ADD_WRITE);
      return ITEM_STAY;

    default :
      log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "connect to %s failed: %s", ss->s_name, strerror(code));
      return ITEM_FAIL;
  }
}

int request_ping_item(struct state *ss, int tag)
{
  int result;

  if(ss->s_fd < 0){
    return ITEM_FAIL;
  }

  result = issue_ping(ss);
  if(result < 0){
    log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "write to %s failed: %s", ss->s_name, strerror(errno));
    return ITEM_FAIL;
  }

  if(result > 0){
    add_fd(ss, ss->s_fd, ADD_WRITE);
    return ITEM_STAY;
  }

#if 0
  set_timeout(ss, 3, 0, ITEM_FAIL);
  add_fd(ss, ss->s_fd, ADD_READ);
#endif

  return ITEM_OK;
}

int decode_ping_item(struct state *ss, int tag)
{
  int result;

  result = load_buffer(ss, 0);
  if(result < 0){
    return ITEM_FAIL;
  }

  set_timeout(ss, 3, 0, ITEM_FAIL);

  if(result == 0){ /* no bytes available */
    add_fd(ss, ss->s_fd, ADD_READ);
    return ITEM_STAY;
  }

  result = find_ping_reply(ss);
  if(result != 0){
    if(result < 0){
      log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "discarding %d bytes ahead of ping", result * (-1));
    }
    add_fd(ss, ss->s_fd, ADD_READ);
    return ITEM_STAY;
  }

  return ITEM_OK;
}

int reset_item(struct state *ss, int tag)
{
  if(ss->s_fd >= 0){
    close(ss->s_fd);
    ss->s_fd = (-1);
  }

  ss->s_have = 0;
  ss->s_done = 0;

  ss->s_power = POWER_NA;

  return ITEM_OK;
}

int request_read_item(struct state *ss, int tag)
{
  int result;

  /* WARNING: what about clearning out garbage at the start ? */

  if(ss->s_fd < 0){
    return ITEM_FAIL;
  }

  result = issue_read(ss, tag);
  if(result < 0){
    log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "write to %s failed: %s", ss->s_name, strerror(errno));
    return ITEM_FAIL;
  }

  if(result > 0){
    add_fd(ss, ss->s_fd, ADD_WRITE);
    return ITEM_STAY;
  }

  add_fd(ss, ss->s_fd, ADD_READ);
  return ITEM_OK;
}

int decode_powerstatus_item(struct state *ss, int tag)
{
  int result, value;

  result = load_buffer(ss, 0);
  if(result < 0){
    return ITEM_FAIL;
  }

  if(result == 0){ /* no bytes available */
    add_fd(ss, ss->s_fd, ADD_READ);
    return ITEM_STAY;
  }

  result = find_read_reply(ss);
#ifdef DEBUG
    fprintf(stderr, "read reply is %d", result);
#endif
  if(result != 0){
    add_fd(ss, ss->s_fd, ADD_READ);
    return ITEM_STAY;
  }

  if(ss->s_address != REGISTER_POWERSTATE){
    log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "logic error, expected to see reply to powerstate read not 0x%x", ss->s_address);
    clear_io_state(ss);
    return ITEM_FAIL;
  }

  value = ss->s_value;

  log_message_katcl(ss->s_up, KATCP_LEVEL_TRACE, NAME, "powerstate value is 0x%04x", value);

  value = value & 0x7;
  if(value & 0x4){
    ss->s_power = POWER_OFF;
    log_message_katcl(ss->s_up, KATCP_LEVEL_INFO, NAME, "roach %s is currently down", ss->s_name);
  } else {
    if(value == 3){
      ss->s_power = POWER_ON;
      log_message_katcl(ss->s_up, KATCP_LEVEL_INFO, NAME, "roach %s is currently on", ss->s_name);
    } else {
      ss->s_power = POWER_STARTING;
      log_message_katcl(ss->s_up, KATCP_LEVEL_INFO, NAME, "roach %s starting up", ss->s_name);
    }
  }

  clear_io_state(ss);

  return ITEM_OK;
}

/*********************************************************************/
/* main and friends **************************************************/

void usage(char *app)
{
  printf("usage: %s [flags] xport\n", app);
  printf("-h                 this help\n");
  printf("-v                 increase verbosity\n");
  printf("-q                 run quietly\n");

  printf("-U                 power up\n");
  printf("-Q                 query power status\n");
  printf("-D                 power down\n");

  printf("return codes:\n");
  printf("0     command completed successfully\n");
  printf("1     command failed\n");
  printf("3     network problems\n");
  printf("2     usage problems\n");
  printf("4     internal errors\n");
}

struct item poweron_table[7] = {
  { setup_network_item,       2, 0, 0, 0 },
  { reset_item,               0, 0, 0, 0 },
  { complete_network_item,    3, 1, 0, 0 }, 
  { request_ping_item,        4, 1, 0, 0 }, 
  { decode_ping_item,         5, 1, 0, 0 }, 
  { request_read_item,        6, 0, 0, REGISTER_POWERSTATE },
  { decode_powerstatus_item, -1, 0, 0, 0 }
};

int main(int argc, char **argv)
{
  struct state *ss;
  char *cmd;
  int i, j, c, mfd, fd, verbose, result, status, len, run, want, power, code, valid;
  unsigned int value, address;
  struct timeval now, target, delta;
  int *(call)(struct state *s, int tag);
  struct item *ix;

  ss = create_state(STDOUT_FILENO);
  if(ss == NULL){
    return 4;
  }

  power = POWER_ON;

#if 0
  char *app, *parm, *cmd, *copy, *ptr, *servers;
  int verbose, result, status, base, info, reply, timeout, pos, flags, show;
  int xmit, code;
  unsigned int len;
  
  info = 1;
  reply = 1;
  i = j = 1;
  app = argv[0];
  base = (-1);
  timeout = 5;
  pos = (-1);
  k = NULL;
  show = 1;
  parm = NULL;
#endif

  verbose = 1;
  i = j = 1;

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(NAME);
          return 0;

        case 'D' :
          power = POWER_OFF;
          j++;
          break;

        case 'U' :
          power = POWER_ON;
          j++;
          break;
          
        case 'Q' :
          power = POWER_NA;
          j++;
          break;
          

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          j++;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;

        default:
          sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unknown option -%c", argv[i][j]);
          return 2;
      }
    } else {

      if(add_roach(ss, argv[i]) < 0){
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "unable to add roach %s", argv[i]);
        return 4;
      }
      i++;
    }
  }

  /* TODO: select different tables */

  load_table(ss, poweron_table, 7);

  if(ss->s_table == NULL){
    sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "need a table to be loaded");
    return 1;
  }

  ss->s_limit = DEFAULT_WAIT;

  gettimeofday(&now, NULL);

  if(ss->s_limit > 0){
    delta.tv_sec = ss->s_limit;
    delta.tv_usec = 0;
  }

  add_time_katcp(&(ss->s_total), &now, &delta);

  run = 1;

  for(run = 1; run > 0; ){

    if(ss->s_index >= ss->s_size){
      log_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "bad state return code %d", code);
      /* break better */
      return 1;
    }

    ix = &(ss->s_table[ss->s_index]);
    code = (*(ix->i_call))(ss, ix->i_tag);

#ifdef DEBUG 
    fprintf(stderr, "run: state=%u, code=%d\n", ss->s_index, code);
#endif

    switch(code){
      case ITEM_STAY : 
        /* do nothing */
        break;
      case ITEM_OK : 
        ss->s_transition = ITEM_STAY;
        ss->s_index = ix->i_ok;
        break;
      case ITEM_FAIL :
        ss->s_transition = ITEM_STAY;
        ss->s_index = ix->i_fail;
        break;
      case ITEM_ALT : 
        ss->s_transition = ITEM_STAY;
        ss->s_index = ix->i_alt;
        break;
      default :
        sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "bad state return code %d", code);
        return 1;
    }

    if(ss->s_index >= ss->s_size){
      log_message_katcl(ss->s_up, KATCP_LEVEL_INFO, NAME, "entered terminal state with code %d", code);
      run = 0;
      continue; /* break out of while loop */
    }

#if 0
    sleep(1);
#endif

    gettimeofday(&now, NULL);
    valid = 0;

    if(ss->s_transition != ITEM_STAY){ /* check if there is a per node timeout */
      if(ss->s_max < 0){
        init_fd(ss);
      }

      if(cmp_time_katcp(&now, &(ss->s_single)) >= 0){
        code = ss->s_transition;
        ss->s_transition = ITEM_STAY;
        ss->s_max = (-1);
        switch(code){
          case ITEM_OK : 
            ss->s_index = ix->i_ok;
            break;
          case ITEM_FAIL : 
            ss->s_index = ix->i_fail;
            break;
          case ITEM_ALT : 
            ss->s_index = ix->i_alt;
            break;
        }
#ifdef DEBUG 
        fprintf(stderr, "timeout: state now %u\n", ss->s_index);
#endif
      } else {
        target.tv_sec = ss->s_single.tv_sec;
        target.tv_usec = ss->s_single.tv_usec;
        valid = 1;
      }
    } 

    if(ss->s_limit > 0){ /* check if overall timeout has been reached */
      if(cmp_time_katcp(&now, &(ss->s_total)) >= 0){
        log_message_katcl(ss->s_up, KATCP_LEVEL_WARN, NAME, "operations timed out after %u seconds", ss->s_limit);
        code = ITEM_FAIL;
        run = 0;
        continue; /* break out of while loop */
      } else {
        if((valid == 0) || (cmp_time_katcp(&(ss->s_total), &target) < 0)){
          target.tv_sec = ss->s_total.tv_sec;
          target.tv_usec = ss->s_total.tv_usec;
          valid = 1;
        }
      }
    }

    if(valid){
      sub_time_katcp(&delta, &target, &now);
    }

    if(ss->s_max >= 0){ /* are we waiting for io (optionally with timeout) ? */
      result = select(ss->s_max + 1, &(ss->s_fsr), &(ss->s_fsw), NULL, valid ? &delta : NULL);
      switch(result){
        case -1 :
          switch(errno){
            case EAGAIN :
            case EINTR  :
              continue; /* WARNING */
            default  :
              sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "select failed: %s", strerror(errno));
              return 4;
          }
          break;
#if 0
        case  0 :
          sync_message_katcl(ss->s_up, KATCP_LEVEL_ERROR, NAME, "requests timed out despite having no timeout");
          /* could terminate cleanly here, but ... */
          return 4;
#endif
      }

      /* this falls into the housekeeping category */
      fd = fileno_katcl(ss->s_up);
      if(FD_ISSET(fd, &(ss->s_fsr))){
        result = read_katcl(ss->s_up);
        if(result > 0){
          code = ITEM_OK;
          run = 0; /* end loop, but not immediately */
        }

        /* discard all upstream requests */
        while(have_katcl(ss->s_up) > 0);
      }

      if(FD_ISSET(fd, &(ss->s_fsw))){
        result = write_katcl(ss->s_up);
      }

      ss->s_max = (-1);
    }

  }

  /* force drain */
  while(write_katcl(ss->s_up) == 0);
  destroy_state(ss);

  if(code == ITEM_OK){
    return 0;
  } else {
    return 1;
  }
}

