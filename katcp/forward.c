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

#ifdef KATCP_CONSISTENCY_CHECKS 
#define FORWARD_STATE_MAGIC 0xfa34df12
#endif

struct forward_symbolic_state
{
#ifdef KATCP_CONSISTENCY_CHECKS 
  unsigned int f_magic;
#endif
  char *f_peer;
  char *f_as;
};

void clear_forward_symbolic_state(void *data)
{
  struct forward_symbolic_state *fs;

  fs = data;
  if(fs){
#ifdef KATCP_CONSISTENCY_CHECKS 
    fs->f_magic = 0;
#endif
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
  struct katcp_cmd_item *ix;
  struct katcp_flat *fy;
  struct forward_symbolic_state *fs;

  ix = this_cmd_katcp(d);
  if(ix == NULL){
    return KATCP_RESULT_FAIL;
  }

  fs = ix->i_data;
  if(fs == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no state associated with %s: possibly incorrect callback", arg_string_katcp(d, 0));
    return KATCP_RESULT_FAIL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS 
  if(fs->f_magic != FORWARD_STATE_MAGIC){
    fprintf(stderr, "major memory corruption: bad forward state magic 0x%x in structure %p, expected 0x%x\n", fs->f_magic, fs, FORWARD_STATE_MAGIC);
    abort();
  }
  if(fs->f_peer == NULL){
    fprintf(stderr, "major memory corruption: expected a peer name, not null in structure %p\n", fs);
    abort();
  }
#endif

  fy = find_name_flat_katcp(d, NULL, fs->f_peer);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no peer matching %s available to process request %s", fs->f_peer, arg_string_katcp(d, 0));
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "about to send %s to peer %s", arg_string_katcp(d, 0), fy->f_name);


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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate symbolic state");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
  }

#ifdef KATCP_CONSISTENCY_CHECKS 
  fs->f_magic = FORWARD_STATE_MAGIC;
#endif

  fs->f_peer = NULL;
  fs->f_as = NULL;

  fs->f_peer = arg_copy_string_katcp(d, 2);
  if(argc > 3){
    fs->f_as = arg_copy_string_katcp(d, 3);
  }
  
  if((fs->f_peer == NULL) || ((argc > 3) && (fs->f_as == NULL))){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to copy parameters, argument count is %d", argc);
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
