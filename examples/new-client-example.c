/* new client example to issue arbitrary katcp requests (with no parameters,
 * to simplify the example). This uses a simpler api than the client-example
 * code
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include <sys/time.h>

#include "katcl.h"
#include "katcp.h"

int main(int argc, char **argv)
{
  struct katcl_line *l;
  int result, total, i;
  char *ptr;

  if(argc <= 2){
    fprintf(stderr, "usage: %s server-ip[:server-port] command\n", argv[0]);
    fprintf(stderr, "example: %s localhost:7147 ?watchdog\n", argv[0]);
    return 1;
  }

  /* connect to a remote machine, arg is "server:port" where ":port is optional" */
  l = create_name_rpc_katcl(argv[1]);
  if(l == NULL){
    fprintf(stderr, "unable to create client connection to server %s: %s\n", argv[1], strerror(errno));
    return 1;
  }

  /* send the request, with 5000ms timeout. Here we don't pass any parameters */
  result = send_rpc_katcl(l, 5000, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, argv[2], NULL);

  /* result is 0 if the reply returns "ok", 1 if it failed and -1 if things went wrong doing IO or otherwise */
  printf("result of %s request is %d\n", argv[2], result);

  /* you can examine the content of the reply with the following functions */
  total = arg_count_katcl(l);
  printf("have %d arguments in reply\n", total);
  for(i = 0; i < total; i++){
    /* for binary data use the arg_buffer_katcl, string will stop at the first occurrence of a \0 */
    ptr = arg_string_katcl(l, i);
    printf("reply[%d] is <%s>\n", i, ptr);
  }

  destroy_rpc_katcl(l);

  return 0;
}

