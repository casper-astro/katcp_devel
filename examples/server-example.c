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

/* simple sensor functions ***************************************************/
/* these functions return the value immediately. This approach is acceptable */
/* when it is cheap to query a sensor value                                  */

int simple_integer_check_sensor(struct katcp_dispatch *d, struct katcp_acquire *a)
{
#if 0
  set_status_sensor_katcp(s, KATCP_STATUS_NOMINAL);
#endif

  return ((((int)time(NULL)) / 10) % 7) - 3;
}

/* check command 1: generates its own reply, with binary and integer output */

int own_check_cmd(struct katcp_dispatch *d, int argc)
{
  if(argc > 1){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "my first parameter is %s", arg_string_katcp(d, 1));
  }

  send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!check-own", KATCP_FLAG_BUFFER, "\0\n\r ", 4, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, 42UL);

  return KATCP_RESULT_OWN; /* we send our own return codes */
}

/* check command 2: has the infrastructure generate its reply */

int ok_check_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw ok check with %d arguments", argc);

  return KATCP_RESULT_OK; /* have the system send a status message for us */
}

int fail_check_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw fail check with %d arguments", argc);

  return KATCP_RESULT_FAIL; /* have the system send a status message for us */
}

int pause_check_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "ran pause check, will not return");

  /* not useful on its own, but can use resume() to restart */

  return KATCP_RESULT_PAUSE;
}

#ifdef KATCP_SUBPROCESS
int subprocess_check_callback(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "was woken by child process exit");

  /* a callback can not use KATCP_RESULT_* codes, it has to generate its messages by hand */
  send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!check-subprocess", KATCP_FLAG_LAST | KATCP_FLAG_STRING, KATCP_OK);

  /* unpause the client instance, so that it can parse new commands */
  resume_katcp(d);

  return 0;
}

int subprocess_check_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_notice *n;
  struct katcp_job *j;
  char *arguments[3] = { "SLEEP", "10", NULL };

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "starting child process sleep 10");

  /* check if somebody else is busy */
  n = find_notice_katcp(d, "sleep-notice");
  if(n != NULL){ 
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "another instance already active");
    return KATCP_RESULT_FAIL;
  }
  
  /* create a notice, an entity which can invoke the callback when triggered */
  n = register_notice_katcp(d, "sleep-notice", 0, &subprocess_check_callback, NULL);
  if(n == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice object");
    return KATCP_RESULT_FAIL;
  }

  /* create a job, something which isn't a timer or a client issuing commands, give it the notice so that it can trigger it when it needs to */
  j = process_name_create_job_katcp(d, arguments[0], arguments, n, NULL);
  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create a subprocess handling job");
    return KATCP_RESULT_FAIL;
  }

  /* suspend, rely on the call back to resume this task */
  return KATCP_RESULT_PAUSE;
}
#endif

int main(int argc, char **argv)
{
  struct katcp_dispatch *d;
#if 0
  struct cached_sensor_state local_data;
  struct fifo_sensor_state *fss;
#endif
  int status, result;

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
  add_version_katcp(d, "mylabel", 0, "myversion", "mybuildtime");

  /* example sensor */
  if(declare_integer_sensor_katcp(d, 0, "check.integer.simple", "integers where -1,0,1 is nominal, -2,2 is warning and the rest error", "none", &simple_integer_check_sensor, NULL, NULL, -1, 1, -2, 2, NULL)){
    fprintf(stderr, "server: unable to register sensors\n");
    return 1;
  }

  /* register example commands */

  result = 0;

  result += register_katcp(d, "?check-own",   "return self generated code", &own_check_cmd);
  result += register_katcp(d, "?check-ok",    "return ok", &ok_check_cmd);
  result += register_katcp(d, "?check-fail",  "return fail", &fail_check_cmd);
  result += register_katcp(d, "?check-pause", "pauses", &pause_check_cmd);
#ifdef KATCP_SUBPROCESS
  result += register_katcp(d, "?check-subprocess", "runs sleep 10 as a subprocess and waits for completion", &subprocess_check_cmd);
#endif

  if(result < 0){
    fprintf(stderr, "server: unable to register commands\n");
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
