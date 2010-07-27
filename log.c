#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/time.h>

#include "katpriv.h"
#include "katcp.h"
#include "katcl.h"

/*************************************************************/

static int vector_sum(int *result, int size)
{
  int i, sum;

  sum = 0;
  for(i = 0; i < size; i++){
    if(result[i] < 0){
      return -1;
    }
    sum += result[i];
  }

  return sum;
}

/*************************************************************/

static char *log_levels_vector[KATCP_MAX_LEVELS] = {
  "trace", 
  "debug", 
  "info", 
  "warn", 
  "error", 
  "fatal",
  "off"
};

int log_to_code(char *name)
{
  int code;

  if(name == NULL){
    return -1;
  }

  for(code = 0; code < KATCP_MAX_LEVELS; code++){
    if(!strncmp(name, log_levels_vector[code], 4)){
      return code;
    }
  }

  /* ugly and pointless all alias */
  if(!strncmp(name, "all", 4)){
    return KATCP_LEVEL_TRACE;
  }

  return -1;
}

char *log_to_string(int code)
{
  if((code < 0) || (code >= KATCP_MAX_LEVELS)){
    return NULL;
  }

  return log_levels_vector[code];
}

int log_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);
  result = vlog_message_katcl(cl, level, name, fmt, args);
  va_end(args);

  return result;
}

int vlog_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, va_list args)
{
  int result[5];
  struct timeval now;
  unsigned int milli;
  char *subsystem, *logstring;

#ifdef DEBUG
  if(level >= KATCP_LEVEL_OFF){
    fprintf(stderr, "log: bad form to a message of level off or worse\n");
    return -1;
  }
#endif

  logstring = log_to_string(level);
  if(logstring == NULL){
#ifdef DEBUG
    fprintf(stderr, "log: bad log level\n");
    abort();
#endif
    return -1;
  }

  subsystem = name ? name : "unknown" ;

  gettimeofday(&now, NULL);
  milli = now.tv_usec / 1000;

  result[0] = append_string_katcl(cl, KATCP_FLAG_FIRST, "#log");
  result[1] = append_string_katcl(cl, 0, logstring);
  result[2] = append_args_katcl(cl, 0, "%lu%03d", now.tv_sec, milli);
  result[3] = append_string_katcl(cl, 0, subsystem);

#if DEBUG > 1
  fprintf(stderr, "log: my fmt string is <%s>, milli=%u\n", fmt, milli);
#endif

  result[4] = append_vargs_katcl(cl, KATCP_FLAG_LAST, fmt, args);

  /* do the right thing, collect error codes */
  return vector_sum(result, 5);
}

int basic_inform_katcl(struct katcl_line *cl, char *name, char *arg)
{
  int result[2];

  if(arg){
    result[0] = append_string_katcl(cl, KATCP_FLAG_FIRST, name);
    result[1] = append_string_katcl(cl, KATCP_FLAG_LAST, arg);
    return vector_sum(result, 2);
  } else {
    return append_string_katcl(cl, KATCP_FLAG_FIRST | KATCP_FLAG_LAST, name);
  }
}

int extra_response_katcl(struct katcl_line *cl, int code, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);
  result = vextra_response_katcl(cl, code, fmt, args);
  va_end(args);

  return result;
}

int vextra_response_katcl(struct katcl_line *cl, int code, char *fmt, va_list args)
{
  int result[3];
  char *name, *status;

  if(code > KATCP_RESULT_OK){
    return -1;
  }

  name = arg_copy_string_katcl(cl, 0);
  if(name == NULL){
    return -1;
  }
  name[0] = KATCP_REPLY;

  status = code_to_name_katcm(code);
  if(status == NULL){
    return -1;
  }

  result[0] = append_string_katcl(cl, KATCP_FLAG_FIRST, name);
  result[1] = append_string_katcl(cl, 0, status);
  result[2] = append_vargs_katcl(cl, KATCP_FLAG_LAST, fmt, args);

  return vector_sum(result, 3);
}

