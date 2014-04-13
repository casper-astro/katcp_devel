#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#include <sys/time.h>

#include <math.h>

#include "katcl.h"
#include "katcp.h"
#include "katpriv.h"

#if 0
#define FORGIVE_LATE
#endif
#define FORGIVE_LARGE

/* cribbed from math.h, it wants __GNU to be defined and who knows what else that does */
#define OVERPI  0.3183098861837906715377675267450287L
#define BUFFER 64

#define NAME   "k7-delay"

int complete_with_log(struct katcl_line *l, struct timeval *until, struct katcl_line *k)
{
  struct katcl_parse *px;
  int result;
  char *ptr;

  while((result = complete_rpc_katcl(l, 0, until)) == 0){
    px = ready_katcl(l);
    if(px){
      if(is_inform_parse_katcl(px)){
        ptr = get_string_parse_katcl(px, 0);
        if(!strcmp("#log", ptr)){
          append_parse_katcl(k, px);
        }
      }
    }
  }

  return result;
}

int main(int argc, char **argv)
{
  struct katcl_line *l, *k;
  struct timeval start, ready, done, until, delta, request, elapsed, lead, local;
  int result, code;
  char *server, *end, *ptr;
  long double value, tmp;

  gettimeofday(&start, NULL);

  /* preparation */

  k = create_katcl(STDOUT_FILENO);
  if(k == NULL){
    fprintf(stderr, "unable to create katcp message logic\n");
    return 2;
  }

  if(argc <= 6){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, NAME, "expect parameters: antpol loadtime delay delayrate fringe fringerate");
    return 1;
  }

#if 0
  server = getenv("KATCP_SERVER");
#endif
  server = "localhost:1235";

  l = create_name_rpc_katcl(server ? server : "localhost:7147");
  if(l == NULL){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, NAME, "unable to connect to %s", server ? server : "localhost:7147");
    return 1;
  }

  append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?fr-delay-set");

  append_string_katcl(l, KATCP_FLAG_STRING, argv[1]); /* antenna */

  value = strtold(argv[5], &end); /* fringe */
  tmp = value * 180.0 * OVERPI;
#ifdef FORGIVE_LARGE
  tmp = fmodl(tmp, 360.0);
#endif

  append_args_katcl(l, KATCP_FLAG_STRING , "%.16Lf", tmp);
  log_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "fringe offset %srad mapped to %.16Lfdeg", argv[5], tmp);

  value = strtold(argv[6], &end); /* fringe rate */
  tmp = value * 500.0 * OVERPI;
  append_args_katcl(l, KATCP_FLAG_STRING, "%.16Lf", tmp);
  log_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "fringe rate %srads/ms mapped to %.16Lfrotations/s", argv[6], tmp);

  value = strtold(argv[3], &end); /* delay */
  append_args_katcl(l, KATCP_FLAG_STRING , "%.16Lf", value / 1000.0);

  log_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "delay %sms mapped to %.16Lfs", argv[3], value / 1000.0);

  value = strtold(argv[4], &end); /* delay rate */
  append_args_katcl(l, KATCP_FLAG_STRING , "%.16Lf", value);

  log_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "delay rate is %.16Lfs/s or ms/ms", value);

  value = strtold(argv[2], &end) / 1000.0; /* time */
  append_args_katcl(l, KATCP_FLAG_STRING , "%Lf", value);

  log_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "%s load time %sms mapped to %Lfs", argv[1], argv[2], value);

  tmp = truncl(value);
  request.tv_sec = tmp;
  request.tv_usec = (value - tmp) * 1000000;

  append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "now");

  gettimeofday(&ready, NULL);

  if(cmp_time_katcp(&request, &ready) <= 0){
    sub_time_katcp(&delta, &ready, &request);

    sync_message_katcl(k, KATCP_LEVEL_ERROR, NAME, "requested time %lu.%06lus is %lu.%06lus in the past", request.tv_sec, request.tv_usec, delta.tv_sec, delta.tv_usec);
    code = 1;

#ifdef FORGIVE_LATE
  }
#else
  } else {
#endif

    /* the send part */

    delta.tv_sec = 20;
    delta.tv_usec = 0;

    add_time_katcp(&until, &ready, &delta);

    result = complete_with_log(l, &until, k);

    if(result < 0){
      sync_message_katcl(k, KATCP_LEVEL_ERROR, NAME, "unable to complete request");
      code = 2;
    } else {
      ptr = arg_string_katcl(l, 1);
      if(ptr){
        if(strcmp(ptr, KATCP_OK)){
          code = 1;
        } else {
          code = 0;
        }
        append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "?get-log");
        complete_with_log(l, &until, k);

        append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "?clr-log");
        complete_with_log(l, &until, k);
      } else {
        code = 2;
      }
    }
#ifndef FORGIVE_LATE
  }
#endif

  gettimeofday(&done, NULL);

  destroy_rpc_katcl(l);

  sub_time_katcp(&lead, &request, &start);
  sub_time_katcp(&elapsed, &done, &start);
  sub_time_katcp(&local, &ready, &start);

  sync_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "delay stats: lead=%lu.%06lus taken=%lu.%06lus setup=%lu.%06lus code=%d", lead.tv_sec, lead.tv_usec, elapsed.tv_sec, elapsed.tv_usec, local.tv_sec, local.tv_usec, code);

  destroy_rpc_katcl(k);

  return code;
}

