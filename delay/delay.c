#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <math.h>

#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#include <sys/time.h>

#include "katcl.h"
#include "katcp.h"
#include "katpriv.h"

#define BUFFER 64

#define NAME   "k7-delay"

int main(int argc, char **argv)
{
  struct katcl_line *l, *k;
  struct timeval start, ready, done, until, delta, request, elapsed, lead, local;
  int result, total, i, code;
  char *server, *end, *ptr;
  long double value, tmp;
  char buffer[BUFFER];

  code = 1; /* assume a failure */
  gettimeofday(&start, NULL);

  /* preparation */

  k = create_katcl(STDOUT_FILENO);
  if(k == NULL){
    fprintf(stderr, "unable to create katcp message logic\n");
    return 2;
  }

  if(argc <= 6){
    fprintf(stderr, "usage: %s antpol loadtime delay delayrate fringe fringerate\n", argv[0]);
    return 1;
  }

  server = getenv("KATCP_SERVER");

  l = create_name_rpc_katcl(server ? server : "localhost:7147");
  if(l == NULL){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, NAME, "unable to connect to %s", server ? server : "localhost:7147");
    return 1;
  }

  append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?fr-delay-set");

  append_string_katcl(l, KATCP_FLAG_STRING, argv[1]); /* antenna */

  value = strtold(argv[5], &end); /* fringe */
  append_args_katcl(l, KATCP_FLAG_STRING , "%.8Lf", value * 57.29578);

  value = strtold(argv[6], &end); /* fringe rate */
  append_args_katcl(l, KATCP_FLAG_STRING , "%.8Lf", value * 159.154943091);

  value = strtold(argv[3], &end); /* delay */
  append_args_katcl(l, KATCP_FLAG_STRING , "%.8Lf", value / 1000.0);

  value = strtold(argv[4], &end); /* delay rate */
  append_args_katcl(l, KATCP_FLAG_STRING , "%.8Lf", value);

  value = strtold(argv[2], &end); /* time */
  append_args_katcl(l, KATCP_FLAG_STRING , "%Lf", value / 1000.0);

  tmp = truncl(value);
  request.tv_sec = tmp;
  request.tv_usec = (value - tmp) * 1000000;

  append_string_katcl(l, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "now");


  gettimeofday(&ready, NULL);

  if(cmp_time_katcp(&request, &ready) <= 0){
    sub_time_katcp(&delta, &ready, &request);

    sync_message_katcl(k, KATCP_LEVEL_ERROR, NAME, "requested time %lu.%06lus is %lu.%06lus in the past", request.tv_sec, request.tv_usec, delta.tv_sec, delta.tv_usec);
    return 1;
  }

  /* the send part */

  delta.tv_sec = 20;
  delta.tv_usec = 0;

  add_time_katcp(&until, &ready, &delta);

  while((result = complete_rpc_katcl(l, 0, &until)) == 0){
#ifdef DEBUG
    fprintf(stderr, "got message\n");
#endif
  }

  if(result < 0){
    /* TODO: end connection, request/replies potentially out of sync */
    return 3;
  }

  ptr = arg_string_katcl(l, 1);
  if(ptr == NULL){
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
    code = 0;
  } 

  gettimeofday(&done, NULL);

  destroy_rpc_katcl(l);

  sub_time_katcp(&lead, &request, &start);
  sub_time_katcp(&elapsed, &done, &start);
  sub_time_katcp(&local, &ready, &start);

  sync_message_katcl(k, KATCP_LEVEL_DEBUG, NAME, "delay stats: lead=%lu.%06lus taken=%lu.%06lus setup=%lu.%06lus", lead.tv_sec, lead.tv_usec, elapsed.tv_sec, elapsed.tv_usec, local.tv_sec, local.tv_usec);

  destroy_rpc_katcl(k);

  return code;
}

