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

struct forward_symbolic_state
{
  char *f_peer;
  char *f_as;
};

void clear_forward_symbolic_state(void *data)
{
  struct forward_symbolic_state *fs;

  fs = data;
  if(fs){
    if(fs->f_peer){
      free(fs->f_peer);
      fs->f_peer = NULL;
    }
    if(fs->f_as){
      free(fs->f_as);
      fs->f_as = NULL;
    }
    free(fs);
  }
}

int perform_forward_symbolic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* TODO */
  return KATCP_RESULT_FAIL;
}

int forward_symbolic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  char *cmd, *ptr;
  struct forward_symbolic_state *fs;
  struct katcp_cmd_map *mx;
  struct katcp_flat *fx;

  s = d->d_shared;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "forward logic only available within duplex handlers");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no map associated with current connection, unable to register callback");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "logic");
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters, need a command and remote party");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  cmd = arg_string_katcp(d, 1);
  if(cmd == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null command parameter");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  /* this isn't quite ideal, we could end up handling a request handler within an inform map. Maybe eliminate the ptr code completely, not needed by add_full_cmd_map anyway */
  ptr = default_message_type_katcm(cmd, KATCP_REQUEST);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate %s", cmd);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "allocation");
  }

  fs = malloc(sizeof(struct forward_symbolic_state));
  if(fs == NULL){
    free(ptr);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "allocation failure");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
  }

  fs->f_peer = NULL;
  fs->f_as = NULL;

  fs->f_peer = arg_copy_string_katcp(d, 2);
  if(argc > 2){
    fs->f_as = arg_copy_string_katcp(d, 3);
  }
  
  if((fs->f_peer == NULL) || ((argc > 2) && (fs->f_as == NULL))){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "allocation failure");
    free(ptr);
    clear_forward_symbolic_state(fs);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
  }

  if(add_full_cmd_map(mx, ptr, "user requested relay", 0, &perform_forward_symbolic_group_cmd_katcp, fs, &clear_forward_symbolic_state) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register handler for %s", ptr);
    free(ptr);
    clear_forward_symbolic_state(fs);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "registered forwarding handler for %s to %s", ptr, fs->f_peer);
  free(ptr);

  return KATCP_RESULT_OK;
}

#endif
