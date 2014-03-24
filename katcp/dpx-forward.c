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
    if(fs->f_magic != FORWARD_STATE_MAGIC){
      fprintf(stderr, "logic problem: expected %p to be a symbolic state structure, but magic 0x%x\n", fs, fs->f_magic);
      abort();
    }
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

int complete_relay_generic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;

#ifdef KATCP_CONSISTENCY_CHECKS
  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
  }
#endif

#ifdef DEBUG
  fprintf(stderr, "complete relay: got %d parameters\n", argc);
#endif

#if 0
  /* testing nonsense */
  return extra_response_katcp(d, KATCP_RESULT_FAIL, "just because");
#endif

#if 0
  cmd = arg_string_katcp(d, 0);
  if(cmd == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "internal/usage");
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "triggering generic completion logic for %s (%d args)", cmd, argc);
#endif

  px = arg_parse_katcp(d);
  if(px == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve message from relay");
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  dump_parse_katcl(px, "complete-relay", stderr);
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
  if(is_request_parse_katcl(px)){
    fprintf(stderr, "complete-relay: did not expect to see a request message\n");
    abort();
  }
#endif

  if(is_reply_parse_katcl(px)){
    prepend_reply_katcp(d);
  } else {
    prepend_inform_katcp(d);
  }

  /* TODO - what about a px without any parameters ? */

  append_trailing_katcp(d, KATCP_FLAG_LAST, px, 1);

#if 0
  /* TODO relay the response payload */
  append_parse_katcp(d, px);

  if(is_reply_ok_parse_katcl(px)){
    return KATCP_RESULT_OK;
  }
#endif

  return KATCP_RESULT_OWN;
}

int relay_generic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *cmd, *ptr;
  unsigned int len, flags;
  struct katcp_flat *fx, *fy;
  struct katcl_parse *px, *po;
  struct katcp_endpoint *source, *target;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters (%d) to relay logic", argc);
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  cmd = arg_string_katcp(d, 2);

  if((name == NULL) || (cmd == NULL)){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
  }
  source = handler_of_flat_katcp(d, fx);

  fy = find_name_flat_katcp(d, NULL, name);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not look up name %s", name);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }
  if(fy == fx){
    target = remote_of_flat_katcp(d, fx);
  } else {
    target = handler_of_flat_katcp(d, fy);
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sending %s ... from endpoint %p to endpoint %p", cmd, source, target);
#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: sending %s ...(%d) from endpoint %p to endpoint %p at dpx[%p]\n", fx, cmd, argc, source, target, fy);
#endif

  px = create_parse_katcl();
  if(px == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  if(argc > 3){
    flags = KATCP_FLAG_FIRST;
  } else {
    flags = KATCP_FLAG_FIRST | KATCP_FLAG_LAST;
  }

  if(cmd[0] == KATCP_REQUEST){
    add_string_parse_katcl(px, flags | KATCP_FLAG_STRING, cmd);
  } else {
    len = strlen(cmd);
    ptr = malloc(len + 2);
    if(ptr == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
    }

    ptr[0] = KATCP_REQUEST;
    strcpy(ptr + 1, cmd);

    add_string_parse_katcl(px, flags | KATCP_FLAG_STRING, ptr);
    free(ptr);
  }

  if(argc > 3){
    po = arg_parse_katcp(d);
    add_trailing_parse_katcl(px, KATCP_FLAG_LAST, po, 3);
  }

  if(send_message_endpoint_katcp(d, source, target, px, 1) < 0){
    destroy_parse_katcl(px);
    return KATCP_RESULT_FAIL;
  }

  if(cmd[0] == KATCP_REQUEST){
    ptr = cmd + 1;
  } else {
    ptr = cmd;
  }

  /* TODO: use wrappers to access fx members */

  if(callback_flat_katcp(d, fx->f_current_endpoint, fx->f_rx, target, &complete_relay_generic_group_cmd_katcp, ptr, KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unable to register callback for %s", ptr);
    return KATCP_RESULT_FAIL;
  }

  /* WARNING: complications: how do we know we have stalled handling a request ? */

  return KATCP_RESULT_OWN;
}

int perform_forward_symbolic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_cmd_item *ix;
  struct katcp_flat *fx, *fy;
  struct forward_symbolic_state *fs;
  struct katcp_endpoint *target, *source, *origin;
  struct katcl_parse *px, *po;
  char *req;

  req = arg_string_katcp(d, 0);
  if(req == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  ix = this_cmd_katcp(d);
  if(ix == NULL){
    return KATCP_RESULT_FAIL;
  }

  fs = ix->i_data;
  if(fs == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no state associated with %s: possibly incorrect callback", req);
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

  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
  }
  source = handler_of_flat_katcp(d, fx);

  fy = find_name_flat_katcp(d, NULL, fs->f_peer);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no peer matching %s available to process request %s", fs->f_peer, req);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "about to send %s to peer %s", req, fy->f_name);

  if(fy == fx){
    target = remote_of_flat_katcp(d, fx);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "currently not able to handle requests to self");
    return KATCP_RESULT_FAIL;
  } else {
    target = handler_of_flat_katcp(d, fy);
  }

  px = create_parse_katcl();
  if(px == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?relay");
  add_string_parse_katcl(px, KATCP_FLAG_STRING, fy->f_name);

  po = arg_parse_katcp(d);

  if(fs->f_as){
    if(argc <= 1){
      add_string_parse_katcl(px, KATCP_FLAG_STRING | KATCP_FLAG_LAST, fs->f_as);
    } else {
      add_string_parse_katcl(px, KATCP_FLAG_STRING, fs->f_as);
      add_trailing_parse_katcl(px, KATCP_FLAG_LAST, po, 1);
    }
  } else {
    add_trailing_parse_katcl(px, KATCP_FLAG_LAST, po, 0);
  }

  if(send_message_endpoint_katcp(d, source, target, px, 1) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to send relay message to target %s", fy->f_name);
    destroy_parse_katcl(px);
    return KATCP_RESULT_FAIL;
  }


#if 0
  /* TODO: use wrappers to access fx members */
  /* this code path is just added, presumed nodefective, defer testing it for some other time */
  origin = sender_to_flat_katcp(d, fx);

  if(callback_flat_katcp(d, origin, po, target, &complete_relay_generic_group_cmd_katcp, "relay", KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register relay callback for %s", req);
    return KATCP_RESULT_FAIL;
  }
#else

  if(callback_flat_katcp(d, fx->f_current_endpoint, fx->f_rx, target, &complete_relay_generic_group_cmd_katcp, "relay", KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register relay callback for %s", req);
    return KATCP_RESULT_FAIL;
  }
#endif

  return KATCP_RESULT_OWN;
}

int forward_symbolic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *cmd, *ptr;
  struct forward_symbolic_state *fs;
  struct katcp_cmd_map *mx;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "forward logic only available within duplex handlers");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no map associated with current connection, unable to register callback");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters, need a command and remote party");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  cmd = arg_string_katcp(d, 1);
  if(cmd == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null command parameter");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  /* this isn't quite ideal, we could end up handling a request handler within an inform map. Maybe eliminate the ptr code completely, not needed by add_full_cmd_map anyway */
  ptr = default_message_type_katcm(cmd, KATCP_REQUEST);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate %s", cmd);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  fs = malloc(sizeof(struct forward_symbolic_state));
  if(fs == NULL){
    free(ptr);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate symbolic state");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
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
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "about to register request %s to %s", ptr, fs->f_peer);

  if(add_full_cmd_map_katcp(mx, ptr, "user requested relay", 0, &perform_forward_symbolic_group_cmd_katcp, fs, &clear_forward_symbolic_state) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register handler for %s", ptr);
    free(ptr);
    clear_forward_symbolic_state(fs);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "registered forwarding handler for %s to %s", ptr, fs->f_peer);
  free(ptr);

  return KATCP_RESULT_OK;
}

#endif
