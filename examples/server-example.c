/* a simple server example which registers a couple of sensors and commands */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <sysexits.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>

#include <katcp.h>
#include <katsensor.h>

/* build string **************************************************************/
/* when compiled with gcc BUILD can be set at compile time with -DBUILD=...  */

#ifndef BUILD
#define BUILD "unknown-0.0"
#endif

/* simple sensor functions ***************************************************/
/* these functions return the value immediately. This approach is acceptable */
/* when it is cheap to query a sensor value                                  */

int simple_integer_check_sensor(struct katcp_dispatch *d, void *local)
{
#if 0
  set_status_sensor_katcp(s, KATCP_STATUS_NOMINAL);
#endif

  return ((int)time(NULL)) / 10;
}

#if 0  /* all sensors have been disabled - for a while, sorry */

/* a simple discrete sensor, returns index into the set of values */

#define DISCRETE_RANGE 3U

int discrete_check_sensor(struct katcp_sensor *s, void *local)
{
  int result;

  result = ((unsigned int)rand()) % DISCRETE_RANGE;

#ifdef DEBUG
  fprintf(stderr, "discrete: result is %d\n", result);
#endif

  return result;
}

/* a simple LRU example sensor */

int lru_check_sensor(struct katcp_sensor *s, void *local)
{
  int result;

  result = ((unsigned int)rand()) % 2;

#ifdef DEBUG
  fprintf(stderr, "lru: result is %d\n", result);
#endif

  return result;
}

/* a simple boolean example sensor */

int bool_check_sensor(struct katcp_sensor *s, void *local)
{
  int result;

  result = ((unsigned int)rand()) % 2;

  /* change the status of the sensor, useful to provide an 
   * opinion if the value is ok or problematic. Available options
   * are KATCP_STATUS_UNKNOWN, KATCP_STATUS_NOMINAL, KATCP_STATUS_WARN
   * KATCP_STATUS_ERROR and KATCP_STATUS_FAILURE
   *
   * if no value is set, sensor defaults to KATCP_STATUS_UNKNOWN
   */

  if(result){
    set_status_sensor_katcp(s, KATCP_STATUS_NOMINAL);
  } else {
    set_status_sensor_katcp(s, KATCP_STATUS_WARN);
  }

#ifdef DEBUG
  fprintf(stderr, "bool: result is %d\n", result);
#endif

  return result;
}

/* more complex sensors ******************************************************/
/* In this example we return a cached value in the *_check_sensor function   */
/* The real value is acquired at most once a second, using a prepare and     */
/* acquire function. The prepare function sets either a timeout or a file    */
/* descriptor which needs to be come active, while the acquire function      */
/* actually retrieves the value. This approach is useful if it is expensive  */
/* to acquire a real value                                                   */

struct cached_sensor_state{
  struct timeval c_when;
  int c_value;
};

int cached_integer_prepare_sensor(struct katcp_sensor *s, void *local, int *max, fd_set *fsr, fd_set *fsw, struct timeval *now, struct timeval *future)
{
  struct cached_sensor_state *css;

  /* in case this function needs to access persistent data, we can retrieve */
  /* what we saved with local_name_sensor_katcp when setting up */

  css = local;

  /* now is the current time, future is the time at which the sensor */
  /* with the closest timeout should be run, future may need to be updated */

  /* helper function which shrinks the value of future as necessary */

  return preparation_time_sensor_katcp(s, now, future);
}

int cached_integer_acquire_sensor(struct katcp_sensor *s, void *local, int *max, fd_set *fsr, fd_set *fsw, struct timeval *now, struct timeval *past)
{
  struct cached_sensor_state *css;

  css = local;

  /* has our saved second field ticked over ? */
  if(now->tv_sec > css->c_when.tv_sec){

    /* then get real data about once a second */
    /* in this example "real" means increment the counter */

    css->c_value++;

    css->c_when.tv_sec = now->tv_sec;
    css->c_when.tv_usec = now->tv_usec;
  }

  return acquisition_time_sensor_katcp(s, now, past);
}

int cached_integer_check_sensor(struct katcp_sensor *s, void *local)
{
  struct cached_sensor_state *css;

  /* simply retrieve the cached value */

  css = local;

  return css->c_value;
}

/* fifo example: a boolean sensor which reads from a fifo, turns true if 
 * the read character a digit */

#define FIFO_MAGIC 0xfa430ad4

struct fifo_sensor_state{
  unsigned int f_magic;
  int f_fd;
  int f_isdigit;
};

void fifo_boolean_destroy_sensor(struct fifo_sensor_state *fss);

int fifo_boolean_prepare_sensor(struct katcp_sensor *s, void *local, int *max, fd_set *fsr, fd_set *fsw, struct timeval *now, struct timeval *future)
{
  struct fifo_sensor_state *fss;

  fss = local;

  if(fss->f_magic != FIFO_MAGIC){
    fprintf(stderr, "fifo: logic problem: bad pointer %p\n", fss);
    abort();
  }

  return prepare_read_sensor_katcp(s, max, fsr, fss->f_fd);
}

int fifo_boolean_acquire_sensor(struct katcp_sensor *s, void *local, int *max, fd_set *fsr, fd_set *fsw, struct timeval *now, struct timeval *future)
{
  struct fifo_sensor_state *fss;
  int result;
  unsigned char byte;

  fss = local;

  if(fss->f_magic != FIFO_MAGIC){
    fprintf(stderr, "fifo: logic problem: bad pointer %p\n", fss);
    abort();
  }

  if(fss->f_fd < 0){
    return -1;
  }

  /* could, should be hidden in a helper function ? */
  if(!(FD_ISSET(fss->f_fd, fsr))){
    return 0;
  }

  result = read(fss->f_fd, &byte, 1);
  if(result < 0){
    switch(errno){
      case EAGAIN :
      case EINTR  :
        return 0;
      default :
        close(fss->f_fd);
        fss->f_fd = (-1);
        return -1;
    }
  }

  if(result == 0){
    close(fss->f_fd);
    fss->f_fd = (-1);
    return -1;
  }

  if(isalnum(byte)){
    fss->f_isdigit = isdigit(byte) ? 1 : 0;
  }
#ifdef DEBUG
  fprintf(stderr, "fifo: isdigit now %d\n", fss->f_isdigit);
#endif

  return 1;
}

int fifo_boolean_check_sensor(struct katcp_sensor *s, void *local)
{
  struct fifo_sensor_state *fss;

  /* simply retrieve the cached value */

  fss = local;

  if(fss->f_magic != FIFO_MAGIC){
    fprintf(stderr, "fifo: logic problem: bad pointer %p\n", fss);
    abort();
  }

#ifdef DEBUG
  fprintf(stderr, "fifo: on retrieve %d\n", fss->f_isdigit);
#endif

  return fss->f_isdigit;
}

struct fifo_sensor_state *fifo_boolean_create_sensor(struct katcp_dispatch *d, char *path, char *name)
{
  struct fifo_sensor_state *fss;
  int fd;

  fd = open(path, O_RDONLY);
  if(fd < 0){
    return NULL;
  }

  fss = malloc(sizeof(struct fifo_sensor_state));
  if(fss == NULL){
    close(fd);
    return NULL;
  }

  fss->f_magic = FIFO_MAGIC;
  fss->f_fd = fd;
  fss->f_isdigit = 0;

  if(register_boolean_sensor_katcp(d, name, "checks input on fifo for digit characters", "truth values", KATCP_STRATEGY_EVENT, &fifo_boolean_check_sensor)){
    fifo_boolean_destroy_sensor(fss);
    return NULL;
  }

  local_name_sensor_katcp(d, name, fss);
  prepare_name_sensor_katcp(d, name, &fifo_boolean_prepare_sensor);
  acquire_name_sensor_katcp(d, name, &fifo_boolean_acquire_sensor);

  return NULL;
}

void fifo_boolean_destroy_sensor(struct fifo_sensor_state *fss)
{
  if(fss == NULL){
    return;
  }

  if(fss->f_magic != FIFO_MAGIC){
    fprintf(stderr, "fifo: logic problem: bad pointer %p\n", fss);
    abort();
  }
  
  if(fss->f_fd >= 0){
    close(fss->f_fd);
    fss->f_fd = (-1);
  }

  free(fss);
}

#endif

/* command functions *********************************************************/

/* check command 1: generates its own reply, with binary and integer output */

int check1_cmd(struct katcp_dispatch *d, int argc)
{
  send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!cmd-check-1", KATCP_FLAG_BUFFER, "\0\n\r ", 4, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, 42UL);

  return KATCP_RESULT_OWN; /* we send our own return codes */
}

/* check command 2: has the infrastructure generate its reply */

int check2_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw a check 2 message with %d arguments", argc);

  return KATCP_RESULT_OK; /* have the system send a status message for us */
}

int main(int argc, char **argv)
{
  struct katcp_dispatch *d;
#if 0
  struct cached_sensor_state local_data;
  struct fifo_sensor_state *fss;
#endif
  int status;

  if(argc <= 1){
    fprintf(stderr, "usage: %s [bind-ip:]listen-port\n", argv[0]);
    return 1;
  }

  /* create a state handle */
  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "%s: unable to allocate state\n", argv[0]);
    return 1;
  }

  /* load up build and version information */
  version_katcp(d, "exampleserver", 0, 1);
  build_katcp(d, BUILD);

#if 0
  /* register example sensors */
  if(register_lru_sensor_katcp(d, "check.lru", "checks lru", "lru", KATCP_STRATEGY_EVENT, &lru_check_sensor)){
    fprintf(stderr, "server: unable to register lru sensor\n");
    return 1;
  }

  if(register_boolean_sensor_katcp(d, "check.bool", "checks boolean value", "truth values", KATCP_STRATEGY_EVENT, &bool_check_sensor)){
    fprintf(stderr, "server: unable to register bool sensor\n");
    return 1;
  }

  if(register_discrete_sensor_katcp(d, "check.discrete", "random 3 values", "initial greek letters", KATCP_STRATEGY_EVENT, &discrete_check_sensor, "alpha", "beta", "gamma", NULL)){
    fprintf(stderr, "server: unable to register sensors\n");
    return 1;
  }

  if(register_integer_sensor_katcp(d, "check.integer.cached", "arbitrary counter", "counter", KATCP_STRATEGY_EVENT, &cached_integer_check_sensor, 0, INT_MAX)){
    fprintf(stderr, "server: unable to register sensors\n");
    return 1;
  }

  /* set up the cached state */
  local_data.c_value = 42;
  local_data.c_when.tv_sec = 0;

  /* register supporting functions and local state variable */
  prepare_name_sensor_katcp(d, "check.integer.cached", &cached_integer_prepare_sensor);
  acquire_name_sensor_katcp(d, "check.integer.cached", &cached_integer_acquire_sensor);
  local_name_sensor_katcp(d, "check.integer.cached", &local_data);


#if 0
  fss = fifo_boolean_create_sensor(d, "example-fifo", "check.fifo");
#endif
#endif

  /* register example commands */

  if(register_integer_sensor_katcp(d, 0, "check.integer.simple", "unix time in decaseconds", "Ds", &simple_integer_check_sensor, NULL, 0, INT_MAX)){
    fprintf(stderr, "server: unable to register sensors\n");
    return 1;
  }


  if(register_katcp(d, "?cmd-check-1", "test command 1", &check1_cmd)){
    fprintf(stderr, "server: unable to enroll command\n");
    return 1;
  }

  if(register_katcp(d, "?cmd-check-2", "test command 2 with log message", &check2_cmd)){
    fprintf(stderr, "server: unable to enroll command\n");
    return 1;
  }

#if 1
  /* alternative - run with more than one client */
  #define CLIENT_COUNT 3

  if(run_multi_server_katcp(d, CLIENT_COUNT, argv[1], 0) < 0){
    fprintf(stderr, "server: run failed\n");
  }
#else
  if(run_server_katcp(d, argv[1], 0) < 0){
    fprintf(stderr, "server: run failed\n");
  }
#endif

  status = exited_katcp(d);

  shutdown_katcp(d);
#if 0
  fifo_boolean_destroy_sensor(fss);
#endif

  return status;
}
