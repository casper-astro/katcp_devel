#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

int client_rename_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *from, *to, *group;
  struct katcp_flat *fx;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "usage");
  }

  to = arg_string_katcp(d, 1);
  if(to == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
  }

  if(argc > 2){
    from = arg_string_katcp(d, 2);
    if(from == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire previous name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    }
  } else {
    fx = require_flat_katcp(d);
    if(fx == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "usage");
    }
    from = fx->f_name;
  }

  if(argc > 3){
    group = arg_string_katcp(d, 3);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    } 
  } else {
    group = NULL;
  }

  if(rename_flat_katcp(d, group, from, to) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to rename from %s to %s in within %s group", from, to, group ? group : "any");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "instance previously called %s now is %s", from, to);

  return KATCP_RESULT_OK;
}


#endif
